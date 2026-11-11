#include "diag/DiagnosticServer.h"

#include <algorithm>

namespace ecusim {

namespace {
quint8 u8(const QByteArray &a, qsizetype i)
{
    return static_cast<quint8>(a.at(i));
}
void put16(QByteArray &a, quint32 v)
{
    a.append(static_cast<char>((v >> 8) & 0xFF));
    a.append(static_cast<char>(v & 0xFF));
}
void put24(QByteArray &a, quint32 v)
{
    a.append(static_cast<char>((v >> 16) & 0xFF));
    put16(a, v);
}
QByteArray bytes(std::initializer_list<int> list)
{
    QByteArray a;
    for (int v : list)
        a.append(static_cast<char>(v));
    return a;
}
} // namespace

DiagnosticServer::DiagnosticServer(DtcManager &dtcs, quint32 seedInit)
    : m_dtcs(dtcs)
    , m_seedState(seedInit)
{
    addDid(kDidActiveSession, [this] { return QByteArray(1, static_cast<char>(m_session)); });
}

void DiagnosticServer::addDid(quint16 did, ReadFn read)
{
    m_dids.insert(did, std::move(read));
}

void DiagnosticServer::addIoControl(quint16 did, IoApplyFn apply, ReadFn status)
{
    m_ioControls.insert(did, {std::move(apply), std::move(status)});
}

void DiagnosticServer::addObdPid(quint8 pid, ReadFn read)
{
    m_obdPids.insert(pid, read);
    addDid(static_cast<quint16>(0xF400 | pid), std::move(read)); // ISO 27145 / 14229 OBD DID range
}

void DiagnosticServer::addObdInfoType(quint8 infoType, ReadFn read)
{
    m_obdInfo.insert(infoType, std::move(read));
}

QByteArray DiagnosticServer::readDid(quint16 did) const
{
    auto it = m_dids.constFind(did);
    return it == m_dids.cend() ? QByteArray() : it.value()();
}

QByteArray DiagnosticServer::negative(quint8 sid, quint8 nrc)
{
    return bytes({Sid::NegativeResponse, sid, nrc});
}

void DiagnosticServer::releaseIoControls()
{
    for (quint16 did : std::as_const(m_activeIo))
        m_ioControls[did].apply(false, {});
    m_activeIo.clear();
}

void DiagnosticServer::enterSession(Session session)
{
    if (session == Session::Default)
        releaseIoControls();
    m_session = session;
    m_unlocked = false; // every session change re-locks the ECU
    m_seedIssued = false;
}

void DiagnosticServer::powerOnReset()
{
    // The failed-attempt counter and a running delay survive a reset (they live in
    // non-volatile memory on a real ECU); otherwise resetting would defeat the lockout.
    enterSession(Session::Default);
}

void DiagnosticServer::tick(qint64 nowMs)
{
    m_nowMs = nowMs;
    // S3 server timer: without any request the ECU falls back to the default session.
    if (m_session != Session::Default && nowMs - m_lastRequestMs > kS3TimeoutMs)
        enterSession(Session::Default);
}

QByteArray DiagnosticServer::handleRequest(const QByteArray &request, bool functional)
{
    if (request.isEmpty())
        return {};
    m_lastRequestMs = m_nowMs;

    bool suppress = false;
    QByteArray response = dispatch(request, suppress);
    if (response.isEmpty())
        return {};
    if (response.size() > kMaxResponseLength && u8(response, 0) != Sid::NegativeResponse)
        response = negative(u8(request, 0), Nrc::ResponseTooLong);

    if (u8(response, 0) == Sid::NegativeResponse) {
        // With functional addressing "not supported" style answers are not sent,
        // otherwise every ECU on the bus would reject a request meant for one of them.
        const quint8 nrc = response.size() > 2 ? u8(response, 2) : 0;
        if (functional
            && (nrc == Nrc::ServiceNotSupported || nrc == Nrc::SubFunctionNotSupported
                || nrc == Nrc::RequestOutOfRange || nrc == Nrc::SubFunctionNotSupportedInActiveSession
                || nrc == Nrc::ServiceNotSupportedInActiveSession))
            return {};
        return response;
    }
    return suppress ? QByteArray() : response;
}

QByteArray DiagnosticServer::dispatch(const QByteArray &request, bool &suppress)
{
    const quint8 sid = u8(request, 0);

    if (sid <= 0x0A)
        return m_obd ? obd(request) : negative(sid, Nrc::ServiceNotSupported);

    // Services with a sub-function carry the suppressPosRspMsgIndicationBit in bit 7.
    QByteArray req = request;
    const bool hasSubFunction = sid == Sid::DiagnosticSessionControl || sid == Sid::EcuReset
        || sid == Sid::ReadDtcInformation || sid == Sid::SecurityAccess || sid == Sid::TesterPresent;
    if (hasSubFunction && req.size() >= 2) {
        suppress = u8(req, 1) & 0x80;
        req[1] = static_cast<char>(u8(req, 1) & 0x7F);
    }

    switch (sid) {
    case Sid::DiagnosticSessionControl: return sessionControl(req);
    case Sid::EcuReset: return ecuReset(req);
    case Sid::ClearDiagnosticInformation: return clearDtcs(req);
    case Sid::ReadDtcInformation: return readDtcInfo(req);
    case Sid::ReadDataByIdentifier: return readDataById(req);
    case Sid::SecurityAccess: return securityAccess(req);
    case Sid::InputOutputControlByIdentifier: return ioControl(req);
    case Sid::TesterPresent: return testerPresent(req);
    default: return negative(sid, Nrc::ServiceNotSupported);
    }
}

// --- 0x10 -----------------------------------------------------------------
QByteArray DiagnosticServer::sessionControl(const QByteArray &req)
{
    if (req.size() != 2)
        return negative(Sid::DiagnosticSessionControl, Nrc::IncorrectMessageLength);
    const quint8 type = u8(req, 1);
    if (type != 0x01 && type != 0x03)
        return negative(Sid::DiagnosticSessionControl, Nrc::SubFunctionNotSupported);
    enterSession(static_cast<Session>(type));
    // P2server_max = 50 ms, P2*server_max = 5000 ms (in 10 ms units)
    return bytes({0x50, type, 0x00, 0x32, 0x01, 0xF4});
}

// --- 0x11 -----------------------------------------------------------------
QByteArray DiagnosticServer::ecuReset(const QByteArray &req)
{
    if (req.size() != 2)
        return negative(Sid::EcuReset, Nrc::IncorrectMessageLength);
    const quint8 type = u8(req, 1);
    if (type != 0x01 && type != 0x03) // hardReset, softReset
        return negative(Sid::EcuReset, Nrc::SubFunctionNotSupported);
    if (onReset)
        onReset();
    return bytes({0x51, type});
}

// --- 0x14 -----------------------------------------------------------------
QByteArray DiagnosticServer::clearDtcs(const QByteArray &req)
{
    if (req.size() != 4)
        return negative(Sid::ClearDiagnosticInformation, Nrc::IncorrectMessageLength);
    const quint32 group = (quint32{u8(req, 1)} << 16) | (quint32{u8(req, 2)} << 8) | u8(req, 3);
    if (!m_dtcs.clear(group))
        return negative(Sid::ClearDiagnosticInformation, Nrc::RequestOutOfRange);
    return bytes({0x54});
}

// --- 0x19 -----------------------------------------------------------------
QByteArray DiagnosticServer::readDtcInfo(const QByteArray &req)
{
    const quint8 sid = Sid::ReadDtcInformation;
    if (req.size() < 2)
        return negative(sid, Nrc::IncorrectMessageLength);
    const quint8 sub = u8(req, 1);
    QByteArray rsp = bytes({0x59, sub});

    switch (sub) {
    case 0x01: { // reportNumberOfDTCByStatusMask
        if (req.size() != 3)
            return negative(sid, Nrc::IncorrectMessageLength);
        rsp.append(static_cast<char>(0xFF)); // DTCStatusAvailabilityMask
        rsp.append(static_cast<char>(0x01)); // DTCFormatIdentifier: ISO 14229-1
        put16(rsp, static_cast<quint32>(m_dtcs.matching(u8(req, 2)).size()));
        return rsp;
    }
    case 0x02: { // reportDTCByStatusMask
        if (req.size() != 3)
            return negative(sid, Nrc::IncorrectMessageLength);
        rsp.append(static_cast<char>(0xFF));
        for (const DtcEntry *e : m_dtcs.matching(u8(req, 2))) {
            put24(rsp, e->def.code);
            rsp.append(static_cast<char>(e->status));
        }
        return rsp;
    }
    case 0x0A: { // reportSupportedDTC
        if (req.size() != 2)
            return negative(sid, Nrc::IncorrectMessageLength);
        rsp.append(static_cast<char>(0xFF));
        for (const DtcEntry &e : m_dtcs.entries()) {
            put24(rsp, e.def.code);
            rsp.append(static_cast<char>(e.status));
        }
        return rsp;
    }
    case 0x04:   // reportDTCSnapshotRecordByDTCNumber
    case 0x06: { // reportDTCExtDataRecordByDTCNumber
        if (req.size() != 6)
            return negative(sid, Nrc::IncorrectMessageLength);
        const quint32 code = (quint32{u8(req, 2)} << 16) | (quint32{u8(req, 3)} << 8) | u8(req, 4);
        const quint8 record = u8(req, 5);
        const DtcEntry *e = m_dtcs.find(code);
        if (!e || (record != 0x01 && record != 0xFF))
            return negative(sid, Nrc::RequestOutOfRange);
        put24(rsp, code);
        rsp.append(static_cast<char>(e->status));
        if (sub == 0x04) {
            if (!e->snapshot.isEmpty()) {
                rsp.append(static_cast<char>(0x01)); // record number
                rsp.append(static_cast<char>(e->snapshot.size()));
                for (const SnapshotRecord &r : e->snapshot) {
                    put16(rsp, r.did);
                    rsp.append(r.data);
                }
            }
        } else if (e->status & (DtcStatus::Pending | DtcStatus::Confirmed)) {
            rsp.append(static_cast<char>(0x01)); // record number
            rsp.append(static_cast<char>(qMin(e->occurrenceCounter, 255)));
            rsp.append(static_cast<char>(qMin(e->agingCounter, 255)));
        }
        return rsp;
    }
    default:
        return negative(sid, Nrc::SubFunctionNotSupported);
    }
}

// --- 0x22 -----------------------------------------------------------------
QByteArray DiagnosticServer::readDataById(const QByteArray &req)
{
    if (req.size() < 3 || (req.size() - 1) % 2 != 0)
        return negative(Sid::ReadDataByIdentifier, Nrc::IncorrectMessageLength);
    // Unsupported identifiers are left out of the response; only if none of the
    // requested identifiers is supported does the server answer requestOutOfRange.
    QByteArray rsp = bytes({0x62});
    for (qsizetype i = 1; i + 1 < req.size(); i += 2) {
        const quint16 did = static_cast<quint16>((u8(req, i) << 8) | u8(req, i + 1));
        if (!m_dids.contains(did))
            continue;
        put16(rsp, did);
        rsp.append(readDid(did));
    }
    if (rsp.size() == 1)
        return negative(Sid::ReadDataByIdentifier, Nrc::RequestOutOfRange);
    return rsp;
}

// --- 0x27 -----------------------------------------------------------------
QByteArray DiagnosticServer::securityAccess(const QByteArray &req)
{
    const quint8 sid = Sid::SecurityAccess;
    if (m_session == Session::Default)
        return negative(sid, Nrc::ServiceNotSupportedInActiveSession);
    if (req.size() < 2)
        return negative(sid, Nrc::IncorrectMessageLength);
    const quint8 sub = u8(req, 1);

    if (sub == 0x01) { // requestSeed
        if (req.size() != 2)
            return negative(sid, Nrc::IncorrectMessageLength);
        if (m_nowMs < m_lockedUntilMs)
            return negative(sid, Nrc::RequiredTimeDelayNotExpired);
        QByteArray rsp = bytes({0x67, 0x01});
        if (m_unlocked) {
            rsp.append(4, '\0'); // already unlocked: seed of all zeros
            return rsp;
        }
        do {
            m_seedState = m_seedState * 1664525u + 1013904223u;
            m_seed = m_seedState;
        } while (m_seed == 0);
        m_seedIssued = true;
        put16(rsp, m_seed >> 16);
        put16(rsp, m_seed);
        return rsp;
    }
    if (sub == 0x02) { // sendKey
        if (req.size() != 6)
            return negative(sid, Nrc::IncorrectMessageLength);
        if (!m_seedIssued)
            return negative(sid, Nrc::RequestSequenceError);
        m_seedIssued = false;
        const quint32 key = (quint32{u8(req, 2)} << 24) | (quint32{u8(req, 3)} << 16)
            | (quint32{u8(req, 4)} << 8) | u8(req, 5);
        if (key != securityKeyFromSeed(m_seed)) {
            if (++m_failedAttempts >= kMaxKeyAttempts) {
                m_failedAttempts = 0;
                m_lockedUntilMs = m_nowMs + kLockoutMs;
                return negative(sid, Nrc::ExceededNumberOfAttempts);
            }
            return negative(sid, Nrc::InvalidKey);
        }
        m_failedAttempts = 0;
        m_unlocked = true;
        return bytes({0x67, 0x02});
    }
    return negative(sid, Nrc::SubFunctionNotSupported);
}

// --- 0x2F -----------------------------------------------------------------
QByteArray DiagnosticServer::ioControl(const QByteArray &req)
{
    const quint8 sid = Sid::InputOutputControlByIdentifier;
    if (m_session == Session::Default)
        return negative(sid, Nrc::ServiceNotSupportedInActiveSession);
    if (req.size() < 4)
        return negative(sid, Nrc::IncorrectMessageLength);
    const quint16 did = static_cast<quint16>((u8(req, 1) << 8) | u8(req, 2));
    auto it = m_ioControls.find(did);
    if (it == m_ioControls.end())
        return negative(sid, Nrc::RequestOutOfRange);
    if (!m_unlocked)
        return negative(sid, Nrc::SecurityAccessDenied);

    const quint8 param = u8(req, 3);
    if (param == 0x00) { // returnControlToECU
        it->apply(false, {});
        m_activeIo.remove(did);
    } else if (param == 0x03) { // shortTermAdjustment
        if (req.size() < 5)
            return negative(sid, Nrc::IncorrectMessageLength);
        if (!it->apply(true, req.mid(4)))
            return negative(sid, Nrc::RequestOutOfRange);
        m_activeIo.insert(did);
    } else {
        return negative(sid, Nrc::RequestOutOfRange);
    }
    QByteArray rsp = bytes({0x6F});
    put16(rsp, did);
    rsp.append(static_cast<char>(param));
    rsp.append(it->status());
    return rsp;
}

// --- 0x3E -----------------------------------------------------------------
QByteArray DiagnosticServer::testerPresent(const QByteArray &req)
{
    if (req.size() != 2)
        return negative(Sid::TesterPresent, Nrc::IncorrectMessageLength);
    if (u8(req, 1) != 0x00)
        return negative(Sid::TesterPresent, Nrc::SubFunctionNotSupported);
    return bytes({0x7E, 0x00});
}

// --- OBD-II (SAE J1979) -----------------------------------------------------
QByteArray DiagnosticServer::obdSupportMask(const QList<quint8> &pids, quint8 base) const
{
    // Bit 31 = PID base+1 ... bit 0 = PID base+0x20 ("next range supported").
    quint32 mask = 0;
    for (quint8 pid : pids) {
        if (pid > base && pid <= base + 0x20)
            mask |= 1u << (0x20 - (pid - base));
        else if (pid > base + 0x20)
            mask |= 1u;
    }
    QByteArray a;
    put16(a, mask >> 16);
    put16(a, mask);
    return a;
}

QByteArray DiagnosticServer::obdDtcList(quint8 responseSid, quint8 statusMask) const
{
    QByteArray list;
    int count = 0;
    QSet<quint32> seen; // OBD has no failure type byte: report each base code once
    for (const DtcEntry *e : m_dtcs.matching(statusMask)) {
        const quint32 base = e->def.code >> 8;
        if (!e->def.emissionRelevant || seen.contains(base))
            continue;
        seen.insert(base);
        put16(list, base);
        ++count;
    }
    QByteArray rsp = bytes({responseSid, count});
    rsp.append(list);
    return rsp;
}

QByteArray DiagnosticServer::obd(const QByteArray &req)
{
    const quint8 mode = u8(req, 0);
    switch (mode) {
    case Sid::ObdCurrentData: {
        if (req.size() < 2 || req.size() > 7)
            return {};
        QByteArray rsp = bytes({0x41});
        for (qsizetype i = 1; i < req.size(); ++i) {
            const quint8 pid = u8(req, i);
            if (pid % 0x20 == 0) {
                // Only answer for ranges that exist, so a scan tool stops asking.
                if (pid != 0 && (m_obdPids.isEmpty() || m_obdPids.lastKey() <= pid))
                    continue;
                rsp.append(static_cast<char>(pid));
                rsp.append(obdSupportMask(m_obdPids.keys(), pid));
            } else if (m_obdPids.contains(pid)) {
                rsp.append(static_cast<char>(pid));
                rsp.append(m_obdPids[pid]());
            }
        }
        return rsp.size() > 1 ? rsp : QByteArray();
    }
    case Sid::ObdFreezeFrame: {
        if (req.size() != 3 || u8(req, 2) != 0x00)
            return {};
        const quint8 pid = u8(req, 1);
        const DtcEntry *ff = m_dtcs.freezeFrameDtc();
        QByteArray rsp = bytes({0x42, pid, 0x00});
        if (pid % 0x20 == 0) {
            // Only the PIDs that are actually stored in a freeze frame (plus PID 02).
            QList<quint8> pids = m_freezeFramePids;
            pids.append(0x02);
            std::sort(pids.begin(), pids.end());
            if (pid != 0 && pids.last() <= pid)
                return {};
            rsp.append(obdSupportMask(pids, pid));
        } else if (pid == 0x02) {
            put16(rsp, ff ? ff->def.code >> 8 : 0); // DTC that caused the freeze frame
        } else {
            if (!ff)
                return {};
            for (const SnapshotRecord &r : ff->snapshot) {
                if (r.did == (0xF400 | pid)) {
                    rsp.append(r.data);
                    return rsp;
                }
            }
            return {};
        }
        return rsp;
    }
    case Sid::ObdStoredDtcs:
        return obdDtcList(0x43, DtcStatus::Confirmed);
    case Sid::ObdPendingDtcs:
        return obdDtcList(0x47, DtcStatus::Pending);
    case Sid::ObdClearDtcs:
        m_dtcs.clear();
        return bytes({0x44});
    case Sid::ObdVehicleInfo: {
        if (req.size() != 2)
            return {};
        const quint8 type = u8(req, 1);
        QByteArray rsp = bytes({0x49, type});
        if (type == 0x00) {
            rsp.append(obdSupportMask(m_obdInfo.keys(), 0));
        } else if (m_obdInfo.contains(type)) {
            rsp.append(static_cast<char>(0x01)); // number of data items
            rsp.append(m_obdInfo[type]());
        } else {
            return {};
        }
        return rsp;
    }
    default:
        return {}; // unsupported OBD modes are simply not answered
    }
}

} // namespace ecusim
