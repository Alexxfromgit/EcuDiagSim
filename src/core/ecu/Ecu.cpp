#include "ecu/Ecu.h"

#include <algorithm>
#include <cmath>

namespace ecusim {

DtcDefinition dtcDef(quint32 code, int failThreshold, bool emissionRelevant, int tripsToConfirm)
{
    DtcDefinition d;
    d.code = code;
    d.text = dtcDescription(code);
    d.failThreshold = failThreshold;
    d.passThreshold = failThreshold;
    d.emissionRelevant = emissionRelevant;
    d.tripsToConfirm = tripsToConfirm;
    d.healingCycles = emissionRelevant ? 3 : 1;
    return d;
}

DtcDefinition networkDtc(quint32 code, int failThreshold)
{
    DtcDefinition d = dtcDef(code, failThreshold);
    d.warningIndicator = false;
    return d;
}

QByteArray encU8(double value)
{
    return QByteArray(1, static_cast<char>(static_cast<quint8>(std::clamp(std::round(value), 0.0, 255.0))));
}

QByteArray encU16(double value)
{
    const auto v = static_cast<quint16>(std::clamp(std::round(value), 0.0, 65535.0));
    QByteArray a;
    a.append(static_cast<char>(v >> 8));
    a.append(static_cast<char>(v & 0xFF));
    return a;
}

QByteArray encAscii(const QString &text, int length)
{
    return text.leftJustified(length, QLatin1Char(' '), true).toLatin1();
}

Ecu::Ecu(SimContext &ctx, Identity identity)
    : m_ctx(ctx)
    , m_id(std::move(identity))
    , m_server(m_dtcs, static_cast<quint32>(qHash(m_id.name)) | 1u)
{
    m_ctx.bus.attach(this);

    if (m_id.requestId != 0) {
        IsoTpLink::Config cfg;
        cfg.txId = responseId();
        cfg.rxId = m_id.requestId;
        cfg.functionalRxId = DiagId::Functional;
        cfg.padByte = 0xCC;
        m_isotp = std::make_unique<IsoTpLink>(cfg);
        m_isotp->sendFrame = [this](const QCanBusFrame &frame) { sendRaw(frame); };
        m_isotp->onMessage = [this](const QByteArray &request, bool functional) {
            const QByteArray response = m_server.handleRequest(request, functional);
            // A request that arrives while the previous response is still being segmented:
            // hold the new response back until the link is free instead of dropping it.
            if (!response.isEmpty() && !m_isotp->send(response))
                m_pendingResponse = response;
        };
    }

    m_server.enableObd(m_id.obd);
    m_server.onReset = [this] { m_resetAtMs = m_now + 20; }; // let the positive response out first
    m_server.addDid(0xF190, [] { return QByteArray(kVin); });
    m_server.addDid(0xF187, [this] { return encAscii(m_id.partNumber, 12); });
    m_server.addDid(0xF195, [this] { return encAscii(m_id.softwareVersion, 12); });
    m_server.addDid(0xF18C, [this] { return encAscii(m_id.name + QStringLiteral("-SN000042"), 12); });

    m_dtcs.setSnapshotProvider([this] {
        QList<SnapshotRecord> records;
        for (quint16 did : std::as_const(m_snapshotDids))
            records.append({did, m_server.readDid(did)});
        return records;
    });

    every(100, [this] { checkTimeouts(); });
}

void Ecu::setSnapshotDids(const QList<quint16> &dids)
{
    m_snapshotDids = dids;
    QList<quint8> pids;
    for (quint16 did : dids)
        if ((did & 0xFF00) == 0xF400)
            pids.append(static_cast<quint8>(did & 0xFF));
    m_server.setFreezeFramePids(pids);
}

Ecu::~Ecu()
{
    m_ctx.bus.detach(this);
}

void Ecu::every(int periodMs, std::function<void()> task)
{
    m_tasks.push_back({periodMs, std::move(task)});
}

void Ecu::subscribe(quint32 id, int timeoutMs, quint32 dtc)
{
    Subscription s;
    s.timeoutMs = timeoutMs;
    s.dtc = dtc;
    m_subs.insert(id, s);
}

void Ecu::sendRaw(const QCanBusFrame &frame)
{
    if (isOnline())
        m_ctx.bus.send(this, frame);
}

void Ecu::send(quint32 id, const QVariantMap &signalValues)
{
    sendRaw(m_ctx.dbc.encode(id, signalValues));
}

double Ecu::rx(quint32 id, const QString &signal, double fallback) const
{
    auto it = m_subs.constFind(id);
    if (it == m_subs.cend())
        return fallback;
    auto v = it->values.constFind(signal);
    return v == it->values.cend() ? fallback : v->toDouble();
}

bool Ecu::rxTimedOut(quint32 id) const
{
    auto it = m_subs.constFind(id);
    if (it == m_subs.cend())
        return true;
    const qint64 reference = std::max(it->lastRxMs, monitorStartMs());
    return m_now - reference > it->timeoutMs;
}

void Ecu::checkTimeouts()
{
    if (!monitoringEnabled())
        return;
    for (auto it = m_subs.cbegin(); it != m_subs.cend(); ++it) {
        if (it->dtc != 0)
            m_dtcs.report(it->dtc, rxTimedOut(it.key()));
    }
}

void Ecu::onFrame(const QCanBusFrame &frame)
{
    if (!m_awake)
        return;
    if (m_isotp && m_isotp->handleFrame(frame))
        return;
    auto it = m_subs.find(frame.frameId());
    if (it == m_subs.end())
        return;
    it->lastRxMs = m_now;
    onMessage(frame.frameId(), frame);
    it->values = m_ctx.dbc.decode(frame);
}

void Ecu::tick(qint64 nowMs)
{
    m_now = nowMs;
    m_dtcs.setTime(nowMs);
    const bool ignition = m_ctx.driver.ignition != Ignition::Off;

    // Operation cycle = ignition cycle (as long as the ECU has supply).
    const bool cycle = !m_powerFault && ignition;
    if (cycle != m_cycle) {
        m_cycle = cycle;
        if (cycle) {
            m_cycleStartMs = nowMs;
            m_dtcs.startOperationCycle();
        } else {
            m_dtcs.endOperationCycle();
        }
    }

    if (m_resetAtMs >= 0 && nowMs >= m_resetAtMs) {
        m_resetAtMs = -1;
        m_resetUntilMs = nowMs + kResetDurationMs;
    }

    const bool awake = !m_powerFault && (m_id.alwaysPowered || ignition) && nowMs >= m_resetUntilMs;
    if (awake != m_awake) {
        m_awake = awake;
        if (awake) {
            m_wakeTime = nowMs;
            for (Subscription &s : m_subs) {
                s.lastRxMs = -1;
                s.values.clear();
            }
            m_server.powerOnReset();
            if (m_isotp)
                m_isotp->reset();
            m_pendingResponse.clear();
            m_dtcs.resetDebounce();
            onWake();
        } else {
            onSleep();
        }
    }
    if (!m_awake)
        return;

    if (m_isotp) {
        m_isotp->tick(nowMs);
        if (!m_pendingResponse.isEmpty() && !m_isotp->isSending() && m_isotp->send(m_pendingResponse))
            m_pendingResponse.clear();
    }
    m_server.tick(nowMs);

    const qint64 elapsed = nowMs - m_wakeTime;
    for (const Task &task : m_tasks) {
        if (elapsed % task.periodMs == 0)
            task.run();
    }
}

} // namespace ecusim
