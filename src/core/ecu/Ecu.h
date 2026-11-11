#pragma once

#include "can/VirtualCanBus.h"
#include "dbc/DbcDatabase.h"
#include "diag/DiagnosticServer.h"
#include "diag/DtcCatalog.h"
#include "diag/DtcManager.h"
#include "diag/IsoTp.h"
#include "sensors/Sensor.h"
#include "vehicle/VehicleModel.h"

#include <QHash>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <vector>

namespace ecusim {

/// What every ECU can reach: the bus, the CAN database and its wiring to the vehicle.
struct SimContext
{
    VirtualCanBus &bus;
    const DbcDatabase &dbc;
    VehicleModel &vehicle;
    DriverInputs &driver;
    SensorSet &sensors;
};

/// Common ECU behaviour: power/ignition handling, a cyclic task scheduler, signal based
/// CAN transmit/receive with timeout supervision, a fault memory and a diagnostic server
/// reachable over ISO-TP.
class Ecu : public CanNode
{
public:
    struct Identity
    {
        QString name;          ///< short name as in the DBC, e.g. "ECM"
        QString description;
        quint32 requestId = 0; ///< physical diagnostic request ID, 0 = no diagnostics
        bool alwaysPowered = false; ///< terminal 30 (true) or switched by ignition, terminal 15
        bool obd = false;      ///< answers the legislated OBD-II services
        QString softwareVersion;
        QString partNumber;
    };

    static constexpr int kStartupGraceMs = 1000; ///< no timeout monitoring right after wake-up
    static constexpr int kResetDurationMs = 300;
    static constexpr const char *kVin = "WSIM0ECU0DIAG2026";

    Ecu(SimContext &ctx, Identity identity);
    ~Ecu() override;

    QString nodeName() const override { return m_id.name; }
    bool isOnline() const override { return m_awake && !m_busOff; }
    void onFrame(const QCanBusFrame &frame) override;

    /// Called once per millisecond of simulation time.
    void tick(qint64 nowMs);

    const Identity &identity() const { return m_id; }
    quint32 responseId() const { return m_id.requestId + DiagId::ResponseOffset; }
    DtcManager &dtcs() { return m_dtcs; }
    const DtcManager &dtcs() const { return m_dtcs; }
    DiagnosticServer &diag() { return m_server; }
    bool awake() const { return m_awake; }

    // fault injection
    void setPowerFault(bool lost) { m_powerFault = lost; }
    bool powerFault() const { return m_powerFault; }
    void setBusOff(bool off) { m_busOff = off; }
    bool busOff() const { return m_busOff; }

protected:
    /// Registers a cyclic task. Tasks run in registration order when due.
    void every(int periodMs, std::function<void()> task);
    /// Subscribes to a message; if dtc != 0 its absence is reported as "lost communication".
    void subscribe(quint32 id, int timeoutMs, quint32 dtc = 0);
    void send(quint32 id, const QVariantMap &signalValues);
    void sendRaw(const QCanBusFrame &frame);
    double rx(quint32 id, const QString &signal, double fallback = 0.0) const;
    bool rxTimedOut(quint32 id) const;
    /// True once the startup grace period after wake-up is over.
    bool monitoringEnabled() const { return m_cycle && m_now - monitorStartMs() >= kStartupGraceMs; }
    qint64 now() const { return m_now; }
    qint64 awakeForMs() const { return m_now - m_wakeTime; }

    void setSnapshotDids(const QList<quint16> &dids) { m_snapshotDids = dids; }

    virtual void onWake() {}
    virtual void onSleep() {}
    /// Raw frame hook, called for subscribed messages before signals are decoded.
    virtual void onMessage(quint32, const QCanBusFrame &) {}

    SimContext &m_ctx;

private:
    struct Task
    {
        int periodMs;
        std::function<void()> run;
    };
    struct Subscription
    {
        int timeoutMs = 0;
        quint32 dtc = 0;
        qint64 lastRxMs = -1;
        QVariantMap values;
    };

    void checkTimeouts();
    qint64 monitorStartMs() const { return m_wakeTime > m_cycleStartMs ? m_wakeTime : m_cycleStartMs; }

    Identity m_id;
    DtcManager m_dtcs;
    DiagnosticServer m_server;
    std::unique_ptr<IsoTpLink> m_isotp;
    std::vector<Task> m_tasks;
    QHash<quint32, Subscription> m_subs;
    QList<quint16> m_snapshotDids;

    qint64 m_now = 0;
    qint64 m_wakeTime = 0;
    qint64 m_cycleStartMs = 0;
    qint64 m_resetAtMs = -1;
    qint64 m_resetUntilMs = -1;
    bool m_awake = false;
    bool m_cycle = false;
    bool m_powerFault = false;
    bool m_busOff = false;
};

/// DTC definition with the text from the catalog. Emission relevant DTCs heal after 3 cycles.
DtcDefinition dtcDef(quint32 code, int failThreshold = 10, bool emissionRelevant = false, int tripsToConfirm = 1);
/// "Lost communication" style DTC: stored, but does not request a warning lamp.
DtcDefinition networkDtc(quint32 code, int failThreshold = 3);

// --- small encoding helpers shared by the ECU implementations ---
QByteArray encU8(double value);
QByteArray encU16(double value);
QByteArray encAscii(const QString &text, int length);

} // namespace ecusim
