#include "ScannerViewModel.h"

#include "AppController.h"
#include "diag/DtcCatalog.h"
#include "diag/DtcManager.h"
#include "tester/Decode.h"

using namespace ecusim;

namespace {
constexpr int kMaxLog = 400;

QByteArray hex(const char *text)
{
    return fromHex(QString::fromLatin1(text));
}

QVariantMap dtcRow(const DtcInfo &d, const QString &ecu, bool obd)
{
    const quint8 ftb = d.code & 0xFF;
    QVariantMap m;
    m[QStringLiteral("ecu")] = ecu;
    m[QStringLiteral("code")] = dtcToString(d.code, false);
    m[QStringLiteral("fullCode")] = dtcToString(d.code, true);
    m[QStringLiteral("ftb")] = obd ? QString() : QStringLiteral("%1").arg(ftb, 2, 16, QLatin1Char('0')).toUpper();
    m[QStringLiteral("ftbText")] = obd ? QString() : failureTypeText(ftb);
    m[QStringLiteral("text")] = d.text.isEmpty() ? QStringLiteral("(unknown DTC)") : d.text;
    m[QStringLiteral("status")] = d.status;
    m[QStringLiteral("statusHex")] = QStringLiteral("0x") + QStringLiteral("%1").arg(d.status, 2, 16, QLatin1Char('0')).toUpper();
    m[QStringLiteral("statusNames")] = dtcStatusNames(d.status);
    m[QStringLiteral("confirmed")] = bool(d.status & DtcStatus::Confirmed);
    m[QStringLiteral("pending")] = bool(d.status & DtcStatus::Pending);
    m[QStringLiteral("active")] = bool(d.status & DtcStatus::TestFailed);
    m[QStringLiteral("lamp")] = bool(d.status & DtcStatus::WarningIndicatorRequested);
    m[QStringLiteral("obd")] = obd;
    return m;
}

/// Live data identifiers per ECU for the "read data" button.
QList<quint16> liveDids(int target)
{
    switch (target) {
    case 0: return {0xF40C, 0xF40D, 0xF405, 0xF411, 0xF404, 0xF442, 0x1001, 0x1002, 0x1003};
    case 1: return {0x2001, 0x2002, 0x2003, 0xF40D, 0xF442};
    case 2: return {0x3001, 0x3002, 0x3003, 0x3004, 0x3005, 0xF40D, 0xF442};
    default: return {0x4001, 0x4002, 0x4003, 0xF442};
    }
}
} // namespace

ScannerViewModel::ScannerViewModel(AppController *app)
    : QObject(app)
    , m_app(app)
{
}

DiagnosticTester &ScannerViewModel::tester() const
{
    return m_app->sim().tester();
}

QStringList ScannerViewModel::ecuNames() const
{
    return {QStringLiteral("ECM  (0x7E0)"), QStringLiteral("TCM  (0x7E1)"), QStringLiteral("ABS  (0x7E2)"),
        QStringLiteral("BCM  (0x7E3)")};
}

void ScannerViewModel::setTarget(int target)
{
    target = qBound(0, target, 3);
    if (target == m_target)
        return;
    m_target = target;
    emit targetChanged();
    emit stateChanged();
}

QVariantList ScannerViewModel::ioControls() const
{
    auto item = [](int did, const QString &name) {
        return QVariantMap{{QStringLiteral("did"), did}, {QStringLiteral("name"), name}};
    };
    switch (m_target) {
    case 2: return {item(0x3010, QStringLiteral("ABS warning lamp"))};
    case 3: return {item(0x4010, QStringLiteral("Low beam")), item(0x4011, QStringLiteral("Door lock"))};
    default: return {};
    }
}

QString ScannerViewModel::sessionText() const
{
    return m_session[m_target] == 3 ? QStringLiteral("Extended session") : QStringLiteral("Default session");
}

bool ScannerViewModel::keepAlive() const
{
    return tester().keepAlive(targetId());
}

void ScannerViewModel::setKeepAlive(bool on)
{
    tester().setKeepAlive(targetId(), on);
    emit stateChanged();
}

void ScannerViewModel::setPollMode(int mode)
{
    if (mode == m_pollMode)
        return;
    m_pollMode = mode;
    emit pollModeChanged();
}

void ScannerViewModel::simulationReset()
{
    for (int i = 0; i < 4; ++i) {
        m_session[i] = 1;
        m_unlocked[i] = false;
    }
    setDtcs({}, {});
    setValues({}, {});
    emit stateChanged();
}

void ScannerViewModel::addLog(const QString &kind, const QString &text)
{
    m_log.append(QVariantMap{{QStringLiteral("kind"), kind},
        {QStringLiteral("time"), QString::number(static_cast<double>(m_app->sim().nowMs()) / 1000.0, 'f', 3)},
        {QStringLiteral("text"), text}});
    while (m_log.size() > kMaxLog)
        m_log.removeFirst();
    emit logChanged();
}

void ScannerViewModel::clearLog()
{
    m_log.clear();
    emit logChanged();
}

void ScannerViewModel::setDtcs(const QString &title, const QVariantList &list)
{
    m_dtcTitle = title;
    m_dtcs = list;
    emit dtcsChanged();
}

void ScannerViewModel::setValues(const QString &title, const QVariantList &list)
{
    m_valuesTitle = title;
    m_values = list;
    emit valuesChanged();
}

QVariantMap ScannerViewModel::valueRow(const QString &ecu, const QString &name, const QString &value,
    const QString &unit)
{
    return {{QStringLiteral("ecu"), ecu}, {QStringLiteral("name"), name}, {QStringLiteral("value"), value},
        {QStringLiteral("unit"), unit}};
}

void ScannerViewModel::send(quint32 id, const QByteArray &request, DiagnosticTester::Callback callback)
{
    tester().request(id, request, std::move(callback));
}

/// Every finished exchange passes through here: log it and track session/security state.
void ScannerViewModel::onResult(const DiagResult &result)
{
    if (!result.responseExpected)
        return; // background TesterPresent
    // Requests queued by poll() come first in the FIFO; they are not written to the log.
    const bool quiet = m_quietJobs > 0;
    if (quiet)
        --m_quietJobs;
    if (!quiet) {
        addLog(QStringLiteral("tx"),
            QStringLiteral("%1  %2   %3")
                .arg(QString::number(result.requestId, 16).toUpper(), toHex(result.request),
                    serviceName(static_cast<quint8>(result.request.at(0)))));
        if (result.responses.isEmpty()) {
            addLog(QStringLiteral("error"),
                result.transportError.isEmpty()
                    ? QStringLiteral("No response (P2 timeout). Ignition on? ECU powered and on the bus?")
                    : QStringLiteral("Transport error: ") + result.transportError);
        }
    }
    for (const DiagResponse &r : result.responses) {
        if (!quiet) {
            addLog(r.isNegative() ? QStringLiteral("neg") : QStringLiteral("rx"),
                QStringLiteral("%1  %2").arg(QString::number(r.canId, 16).toUpper(), toHex(r.data)));
            addLog(QStringLiteral("info"), QStringLiteral("%1: %2").arg(r.ecu, explainResponse(result.request, r.data)));
        }
        const int index = static_cast<int>(r.canId) - 0x7E8;
        if (index < 0 || index > 3 || r.isNegative() || r.data.isEmpty())
            continue;
        const quint8 sid = static_cast<quint8>(r.data[0]);
        if (sid == 0x50 && r.data.size() >= 2) {
            m_session[index] = static_cast<quint8>(r.data[1]);
            m_unlocked[index] = false;
        } else if (sid == 0x67 && r.data.size() >= 2 && static_cast<quint8>(r.data[1]) == 0x02) {
            m_unlocked[index] = true;
        } else if (sid == 0x51) {
            m_session[index] = 1;
            m_unlocked[index] = false;
        } else if (sid == 0x62 && r.data.size() == 4 && static_cast<quint8>(r.data[1]) == 0xF1
            && static_cast<quint8>(r.data[2]) == 0x86) {
            // Active session DID: lets us notice an S3 timeout.
            if (m_session[index] != static_cast<quint8>(r.data[3])) {
                m_session[index] = static_cast<quint8>(r.data[3]);
                m_unlocked[index] = false;
            }
        }
    }
    emit stateChanged();
}

// --- OBD-II -----------------------------------------------------------------

void ScannerViewModel::obdLiveData()
{
    m_collect.clear();
    auto collect = [this](const DiagResult &r) {
        for (const DiagResponse &rsp : r.responses)
            for (const DecodedValue &v : parseDataResponse(rsp.data))
                m_collect.append(valueRow(rsp.ecu, v.name, v.value, v.unit));
    };
    send(DiagId::Functional, hex("01 01 04 05 0C 0D 11"), collect);
    send(DiagId::Functional, hex("01 1F 42"), [this, collect](const DiagResult &r) {
        collect(r);
        setValues(QStringLiteral("OBD-II live data (Mode 01)"), m_collect);
    });
}

void ScannerViewModel::obdReadCodes()
{
    m_collect.clear();
    send(DiagId::Functional, hex("03"), [this](const DiagResult &r) {
        for (const DiagResponse &rsp : r.responses)
            for (const DtcInfo &d : parseObdDtcList(rsp.data))
                m_collect.append(dtcRow(d, rsp.ecu, true));
    });
    send(DiagId::Functional, hex("07"), [this](const DiagResult &r) {
        for (const DiagResponse &rsp : r.responses) {
            for (const DtcInfo &d : parseObdDtcList(rsp.data)) {
                // A confirmed DTC is usually pending as well; list it once.
                bool known = false;
                for (qsizetype i = 0; i < m_collect.size(); ++i) {
                    QVariantMap row = m_collect.at(i).toMap();
                    if (row.value(QStringLiteral("code")) == dtcToString(d.code, false)
                        && row.value(QStringLiteral("ecu")) == rsp.ecu) {
                        row[QStringLiteral("pending")] = true;
                        m_collect[i] = row;
                        known = true;
                    }
                }
                if (!known)
                    m_collect.append(dtcRow(d, rsp.ecu, true));
            }
        }
        setDtcs(QStringLiteral("OBD-II: stored (Mode 03) and pending (Mode 07) codes"), m_collect);
    });
}

void ScannerViewModel::obdClearCodes()
{
    send(DiagId::Functional, hex("04"), [this](const DiagResult &) { obdReadCodes(); });
}

void ScannerViewModel::obdFreezeFrame()
{
    m_collect.clear();
    send(DiagId::Functional, hex("02 02 00"), [this](const DiagResult &r) {
        for (const DiagResponse &rsp : r.responses) {
            if (rsp.data.size() < 5)
                continue;
            const quint32 code = ((quint32{static_cast<quint8>(rsp.data[3])} << 8) | static_cast<quint8>(rsp.data[4])) << 8;
            m_collect.append(valueRow(rsp.ecu, QStringLiteral("DTC that stored the freeze frame"),
                code ? dtcToString(code, false) + QStringLiteral("  ") + dtcDescription(code)
                     : QStringLiteral("none"),
                {}));
        }
    });
    static const quint8 kPids[] = {0x04, 0x05, 0x0C, 0x0D, 0x11, 0x42};
    for (quint8 pid : kPids) {
        QByteArray request = hex("02 00 00");
        request[1] = static_cast<char>(pid);
        const bool last = pid == 0x42;
        send(DiagId::Functional, request, [this, pid, last](const DiagResult &r) {
            for (const DiagResponse &rsp : r.responses) {
                if (rsp.data.size() < 4)
                    continue;
                const DecodedValue v = decodeDid(static_cast<quint16>(0xF400 | pid), rsp.data.mid(3));
                m_collect.append(valueRow(rsp.ecu, v.name, v.value, v.unit));
            }
            if (last)
                setValues(QStringLiteral("OBD-II freeze frame (Mode 02)"), m_collect);
        });
    }
}

void ScannerViewModel::obdVehicleInfo()
{
    m_collect.clear();
    struct Item
    {
        const char *request;
        const char *name;
    };
    static const Item kItems[] = {{"09 02", "VIN"}, {"09 04", "Calibration ID"}, {"09 0A", "ECU name"}};
    for (const Item &item : kItems) {
        const QString name = QString::fromLatin1(item.name);
        const bool last = &item == &kItems[2];
        send(DiagId::Functional, hex(item.request), [this, name, last](const DiagResult &r) {
            for (const DiagResponse &rsp : r.responses) {
                QByteArray text = rsp.data.mid(3);
                text.replace('\0', ' ');
                m_collect.append(valueRow(rsp.ecu, name, QString::fromLatin1(text).trimmed(), {}));
            }
            if (last)
                setValues(QStringLiteral("OBD-II vehicle information (Mode 09)"), m_collect);
        });
    }
}

// --- UDS ----------------------------------------------------------------------

void ScannerViewModel::udsSession(int type)
{
    QByteArray request = hex("10 01");
    request[1] = static_cast<char>(type);
    send(targetId(), request);
}

void ScannerViewModel::udsUnlock()
{
    const quint32 id = targetId();
    send(id, hex("27 01"), [this, id](const DiagResult &r) {
        const DiagResponse *rsp = r.positive();
        if (!rsp || rsp->data.size() < 6)
            return;
        quint32 seed = 0;
        for (int i = 2; i < 6; ++i)
            seed = (seed << 8) | static_cast<quint8>(rsp->data[i]);
        if (seed == 0) {
            addLog(QStringLiteral("info"), QStringLiteral("Seed is zero: the ECU is already unlocked"));
            return;
        }
        const quint32 key = securityKeyFromSeed(seed);
        addLog(QStringLiteral("info"),
            QStringLiteral("Key = rotl32(seed ^ 0x5EC0DE5A, 5) + 0x1234ABCD = 0x%1").arg(key, 8, 16, QLatin1Char('0')));
        QByteArray request = hex("27 02");
        for (int shift = 24; shift >= 0; shift -= 8)
            request.append(static_cast<char>((key >> shift) & 0xFF));
        send(id, request);
    });
}

void ScannerViewModel::udsReset()
{
    send(targetId(), hex("11 01"));
}

void ScannerViewModel::udsReadDtcs(int statusMask)
{
    QByteArray request = hex("19 02 00");
    request[2] = static_cast<char>(statusMask);
    const QString ecu = DiagnosticTester::ecuName(targetId());
    send(targetId(), request, [this, ecu, statusMask](const DiagResult &r) {
        QVariantList rows;
        if (const DiagResponse *rsp = r.positive())
            for (const DtcInfo &d : parseUdsDtcList(rsp->data))
                rows.append(dtcRow(d, ecu, false));
        setDtcs(QStringLiteral("%1: DTCs matching status mask 0x%2 (UDS 19 02)")
                    .arg(ecu)
                    .arg(statusMask, 2, 16, QLatin1Char('0')),
            rows);
    });
}

void ScannerViewModel::udsSupportedDtcs()
{
    const QString ecu = DiagnosticTester::ecuName(targetId());
    send(targetId(), hex("19 0A"), [this, ecu](const DiagResult &r) {
        QVariantList rows;
        if (const DiagResponse *rsp = r.positive())
            for (const DtcInfo &d : parseUdsDtcList(rsp->data))
                rows.append(dtcRow(d, ecu, false));
        setDtcs(QStringLiteral("%1: all supported DTCs (UDS 19 0A)").arg(ecu), rows);
    });
}

void ScannerViewModel::udsClearDtcs()
{
    send(targetId(), hex("14 FF FF FF"), [this](const DiagResult &) { udsReadDtcs(0x0D); });
}

void ScannerViewModel::udsSnapshot(const QString &dtcText)
{
    const quint32 code = dtcFromString(dtcText);
    if (code == 0xFFFFFFFF)
        return;
    QByteArray dtcBytes;
    dtcBytes.append(static_cast<char>(code >> 16));
    dtcBytes.append(static_cast<char>(code >> 8));
    dtcBytes.append(static_cast<char>(code));
    const QString ecu = DiagnosticTester::ecuName(targetId());
    m_collect.clear();
    send(targetId(), hex("19 04") + dtcBytes + hex("01"), [this, ecu](const DiagResult &r) {
        if (const DiagResponse *rsp = r.positive())
            for (const DecodedValue &v : parseSnapshot(rsp->data))
                m_collect.append(valueRow(ecu, v.name, v.value, v.unit));
    });
    send(targetId(), hex("19 06") + dtcBytes + hex("01"), [this, ecu, dtcText](const DiagResult &r) {
        const DiagResponse *rsp = r.positive();
        if (rsp && rsp->data.size() >= 9) {
            m_collect.append(valueRow(ecu, QStringLiteral("Occurrence counter"),
                QString::number(static_cast<quint8>(rsp->data[7])), {}));
            m_collect.append(valueRow(ecu, QStringLiteral("Aging counter (fault-free cycles)"),
                QString::number(static_cast<quint8>(rsp->data[8])), {}));
        }
        setValues(QStringLiteral("%1 %2: snapshot and extended data (UDS 19 04 / 19 06)").arg(ecu, dtcText),
            m_collect);
    });
}

void ScannerViewModel::udsIdentification()
{
    const QString ecu = DiagnosticTester::ecuName(targetId());
    send(targetId(), hex("22 F1 90 F1 87 F1 95 F1 8C F1 86"), [this, ecu](const DiagResult &r) {
        QVariantList rows;
        if (const DiagResponse *rsp = r.positive())
            for (const DecodedValue &v : parseDataResponse(rsp->data))
                rows.append(valueRow(ecu, v.name, v.value, v.unit));
        setValues(QStringLiteral("%1: identification (UDS 22)").arg(ecu), rows);
    });
}

void ScannerViewModel::udsLiveData()
{
    QByteArray request = hex("22");
    for (quint16 did : liveDids(m_target)) {
        request.append(static_cast<char>(did >> 8));
        request.append(static_cast<char>(did & 0xFF));
    }
    request.append(hex("F1 86"));
    const QString ecu = DiagnosticTester::ecuName(targetId());
    send(targetId(), request, [this, ecu](const DiagResult &r) {
        QVariantList rows;
        if (const DiagResponse *rsp = r.positive())
            for (const DecodedValue &v : parseDataResponse(rsp->data))
                if (v.did != DiagnosticServer::kDidActiveSession)
                    rows.append(valueRow(ecu, v.name, v.value, v.unit));
        setValues(QStringLiteral("%1: live data (UDS 22)").arg(ecu), rows);
    });
}

void ScannerViewModel::udsIoControl(int did, int state)
{
    QByteArray request = hex("2F");
    request.append(static_cast<char>(did >> 8));
    request.append(static_cast<char>(did & 0xFF));
    if (state < 0) {
        request.append('\0'); // returnControlToECU
    } else {
        request.append(static_cast<char>(0x03)); // shortTermAdjustment
        request.append(static_cast<char>(state ? 1 : 0));
    }
    send(targetId(), request);
}

void ScannerViewModel::sendRaw(const QString &hexText, bool functional)
{
    const QByteArray request = fromHex(hexText);
    if (request.isEmpty()) {
        addLog(QStringLiteral("error"), QStringLiteral("Not a valid hex string: '%1'").arg(hexText));
        return;
    }
    send(functional ? DiagId::Functional : targetId(), request);
}

void ScannerViewModel::poll()
{
    if (m_pollMode == 0 || tester().busy())
        return;
    if (m_pollMode == 1) {
        m_quietJobs = 2;
        obdLiveData();
    } else {
        m_quietJobs = 1;
        udsLiveData();
    }
}
