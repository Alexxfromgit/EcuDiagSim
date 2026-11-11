#pragma once

#include "can/VirtualCanBus.h"
#include "dbc/DbcDatabase.h"
#include "ecu/Ecus.h"
#include "sensors/Sensor.h"
#include "tester/DiagnosticTester.h"
#include "trace/TraceRecorder.h"
#include "vehicle/VehicleModel.h"

#include <QHash>
#include <QList>
#include <QString>

#include <memory>

namespace ecusim {

class ScenarioRunner;

/// Something a fault can be injected into.
struct FaultTarget
{
    enum class Kind { Sensor, Component, Ecu, Network };
    QString id;
    QString name;
    QString ecu;
    Kind kind = Kind::Sensor;
    QString unit;
    QList<FaultType> faultTypes;
};

struct ActiveFault
{
    QString targetId;
    Fault fault;
};

/// The whole virtual vehicle: plant, sensors, bus, ECUs, scan tool and trace.
///
/// Time only advances through step(); the simulation never looks at the wall clock,
/// so a run is fully reproducible and tests can run it as fast as the CPU allows.
class Simulation
{
public:
    static constexpr qint64 kStepUs = 1000; ///< fixed step: 1 ms

    Simulation();
    ~Simulation();
    Simulation(const Simulation &) = delete;
    Simulation &operator=(const Simulation &) = delete;

    bool isValid() const { return m_error.isEmpty(); }
    QString errorString() const { return m_error; }

    void step();
    void run(int milliseconds);
    /// Steps until the predicate holds. Returns false on timeout.
    template <typename Predicate>
    bool runUntil(Predicate done, int timeoutMs)
    {
        for (int i = 0; i < timeoutMs; ++i) {
            if (done())
                return true;
            step();
        }
        return done();
    }

    qint64 nowUs() const { return m_timeUs; }
    qint64 nowMs() const { return m_timeUs / 1000; }

    DriverInputs &driver() { return m_driver; }
    VehicleModel &vehicle() { return m_vehicle; }
    SensorSet &sensors() { return m_sensors; }
    VirtualCanBus &bus() { return m_bus; }
    const DbcDatabase &dbc() const { return m_dbc; }
    Ecm &ecm() { return *m_ecm; }
    Tcm &tcm() { return *m_tcm; }
    AbsEcu &abs() { return *m_abs; }
    Bcm &bcm() { return *m_bcm; }
    Ipc &ipc() { return *m_ipc; }
    QList<Ecu *> ecus() const;
    Ecu *ecu(const QString &name) const;
    DiagnosticTester &tester() { return *m_tester; }
    TraceRecorder &trace() { return m_trace; }
    ScenarioRunner &scenario() { return *m_scenario; }

    /// Turns the key to "start" until the engine runs (or 3 s have passed).
    void requestEngineStart();

    /// Latest value of a DBC signal seen on the bus.
    double signal(const QString &name, double fallback = 0.0) const;
    bool hasSignal(const QString &name) const { return m_signals.contains(name); }

    // --- fault injection ---
    const QList<FaultTarget> &faultTargets() const { return m_targets; }
    const FaultTarget *faultTarget(const QString &id) const;
    bool injectFault(const QString &targetId, const Fault &fault);
    void clearFault(const QString &targetId);
    void clearAllFaults();
    const QList<ActiveFault> &activeFaults() const { return m_activeFaults; }

    // --- log replay: the recorded frames replace the ECUs, the cluster shows them ---
    void startReplay(const QList<BusFrame> &frames);
    void stopReplay();
    bool replaying() const { return m_replayer->active(); }
    double replayProgress() const { return m_replayer->progress(); }

private:
    void buildFaultTargets();
    void applyFault(const QString &targetId, const Fault &fault);

    QString m_error;
    qint64 m_timeUs = 0;
    qint64 m_startRequestUntilMs = -1;

    DbcDatabase m_dbc;
    VirtualCanBus m_bus;
    DriverInputs m_driver;
    VehicleModel m_vehicle;
    SensorSet m_sensors;
    TraceRecorder m_trace;
    QHash<QString, double> m_signals;

    std::unique_ptr<SimContext> m_ctx;
    std::unique_ptr<Ecm> m_ecm;
    std::unique_ptr<Tcm> m_tcm;
    std::unique_ptr<AbsEcu> m_abs;
    std::unique_ptr<Bcm> m_bcm;
    std::unique_ptr<Ipc> m_ipc;
    std::unique_ptr<DiagnosticTester> m_tester;
    std::unique_ptr<TraceReplayer> m_replayer;
    std::unique_ptr<ScenarioRunner> m_scenario;

    QList<FaultTarget> m_targets;
    QList<ActiveFault> m_activeFaults;
};

} // namespace ecusim
