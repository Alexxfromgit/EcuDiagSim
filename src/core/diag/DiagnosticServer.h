#pragma once

#include "diag/DtcManager.h"
#include "diag/UdsDefs.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>

#include <functional>

namespace ecusim {

/// The diagnostic application layer of one ECU: UDS (ISO 14229-1) and, for the
/// emission relevant ECUs, the legislated OBD-II services (SAE J1979 / ISO 15031-5).
///
/// It works on complete request messages (ISO-TP is handled by the caller) and
/// returns the complete response, or an empty array if no response must be sent.
class DiagnosticServer
{
public:
    enum class Session : quint8 { Default = 0x01, Programming = 0x02, Extended = 0x03 };

    using ReadFn = std::function<QByteArray()>;
    /// takeControl = false means "return control to the ECU"; returns false if the state is invalid.
    using IoApplyFn = std::function<bool(bool takeControl, const QByteArray &state)>;

    static constexpr int kS3TimeoutMs = 5000;
    static constexpr int kMaxKeyAttempts = 3;
    static constexpr int kLockoutMs = 10000;
    static constexpr quint16 kDidActiveSession = 0xF186;
    static constexpr int kMaxResponseLength = 4095; ///< largest ISO-TP message on classic CAN

    explicit DiagnosticServer(DtcManager &dtcs, quint32 seedInit = 0x1234567);

    // --- configuration (done once by the ECU) ---
    void addDid(quint16 did, ReadFn read);
    void addIoControl(quint16 did, IoApplyFn apply, ReadFn status);
    void enableObd(bool enabled) { m_obd = enabled; }
    void addObdPid(quint8 pid, ReadFn read);           ///< Mode 01; also readable as DID 0xF400 + pid
    void addObdInfoType(quint8 infoType, ReadFn read); ///< Mode 09
    /// PIDs that the freeze frame (DTC snapshot) contains; announced by Mode 02 PID 00.
    void setFreezeFramePids(const QList<quint8> &pids) { m_freezeFramePids = pids; }
    std::function<void()> onReset;

    // --- runtime ---
    QByteArray handleRequest(const QByteArray &request, bool functional);
    void tick(qint64 nowMs);
    /// Back to the power-on state: default session, locked, IO control released.
    void powerOnReset();

    Session session() const { return m_session; }
    bool unlocked() const { return m_unlocked; }
    bool hasDid(quint16 did) const { return m_dids.contains(did); }
    QByteArray readDid(quint16 did) const;
    QList<quint16> dids() const { return m_dids.keys(); }
    bool obdEnabled() const { return m_obd; }

private:
    struct IoControl
    {
        IoApplyFn apply;
        ReadFn status;
    };

    QByteArray dispatch(const QByteArray &req, bool &suppress);
    QByteArray sessionControl(const QByteArray &req);
    QByteArray ecuReset(const QByteArray &req);
    QByteArray clearDtcs(const QByteArray &req);
    QByteArray readDtcInfo(const QByteArray &req);
    QByteArray readDataById(const QByteArray &req);
    QByteArray securityAccess(const QByteArray &req);
    QByteArray ioControl(const QByteArray &req);
    QByteArray testerPresent(const QByteArray &req);
    QByteArray obd(const QByteArray &req);
    QByteArray obdSupportMask(const QList<quint8> &pids, quint8 base) const;
    QByteArray obdDtcList(quint8 responseSid, quint8 statusMask) const;

    void enterSession(Session session);
    void releaseIoControls();
    static QByteArray negative(quint8 sid, quint8 nrc);

    DtcManager &m_dtcs;
    QMap<quint16, ReadFn> m_dids;
    QMap<quint16, IoControl> m_ioControls;
    QSet<quint16> m_activeIo;
    QMap<quint8, ReadFn> m_obdPids;
    QMap<quint8, ReadFn> m_obdInfo;
    QList<quint8> m_freezeFramePids;
    bool m_obd = false;

    Session m_session = Session::Default;
    bool m_unlocked = false;
    bool m_seedIssued = false;
    quint32 m_seed = 0;
    quint32 m_seedState;
    int m_failedAttempts = 0;
    qint64 m_lockedUntilMs = 0;
    qint64 m_nowMs = 0;
    qint64 m_lastRequestMs = 0;
};

} // namespace ecusim
