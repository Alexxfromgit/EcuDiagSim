#include "sim/Simulation.h"

#include "sim/ScenarioRunner.h"

#include <algorithm>

namespace ecusim {

Simulation::Simulation(const VehicleConfig &config)
    : m_config(config)
{
    if (!m_dbc.loadFile(QStringLiteral(":/ecusim/dbc/vehicle.dbc")))
        m_error = QStringLiteral("CAN database: ") + m_dbc.errorString();

    m_sensors.fitEngineObd(m_config.has(Equipment::EngineObd));
    m_ctx = std::make_unique<SimContext>(SimContext{m_bus, m_dbc, m_vehicle, m_driver, m_sensors, m_config});
    m_abs = std::make_unique<AbsEcu>(*m_ctx);
    m_ecm = std::make_unique<Ecm>(*m_ctx);
    m_tcm = std::make_unique<Tcm>(*m_ctx);
    m_bcm = std::make_unique<Bcm>(*m_ctx);
    m_ipc = std::make_unique<Ipc>(*m_ctx);
    m_tester = std::make_unique<DiagnosticTester>(m_bus);
    m_replayer = std::make_unique<TraceReplayer>(m_bus);
    m_scenario = std::make_unique<ScenarioRunner>(*this);

    m_bus.addListener([this](const BusFrame &frame) {
        m_trace.record(frame);
        if (DiagId::isDiagnostic(frame.frame.frameId()))
            return;
        const QVariantMap values = m_dbc.decode(frame.frame);
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            m_signals.insert(it.key(), it.value().toDouble());
            m_signalTimeUs.insert(it.key(), frame.timeUs);
        }
    });

    // The scan tool configured a replaced part (coding written, adaptation learned): the
    // "unconfigured" fault is over without a repair.
    for (Ecu *e : ecus())
        e->onConfigured = [this](const QString &targetId) {
            for (qsizetype i = 0; i < m_activeFaults.size(); ++i)
                if (m_activeFaults.at(i).targetId == targetId
                    && m_activeFaults.at(i).fault.type == FaultType::Unconfigured) {
                    m_activeFaults.removeAt(i);
                    return;
                }
        };

    buildFaultTargets();
}

Simulation::~Simulation() = default;

QList<Ecu *> Simulation::ecus() const
{
    return {m_ecm.get(), m_tcm.get(), m_abs.get(), m_bcm.get(), m_ipc.get()};
}

Ecu *Simulation::ecu(const QString &name) const
{
    for (Ecu *e : ecus())
        if (e->identity().name.compare(name, Qt::CaseInsensitive) == 0)
            return e;
    return nullptr;
}

void Simulation::requestEngineStart()
{
    if (m_driver.ignition == Ignition::Off)
        m_driver.ignition = Ignition::On;
    m_startRequestUntilMs = nowMs() + 3000;
}

void Simulation::step()
{
    const qint64 ms = nowMs();

    if (m_startRequestUntilMs >= 0) {
        const bool done = m_vehicle.engineRunning() || ms >= m_startRequestUntilMs
            || m_driver.ignition == Ignition::Off;
        if (done) {
            m_startRequestUntilMs = -1;
            if (m_driver.ignition == Ignition::Start)
                m_driver.ignition = Ignition::On;
        } else {
            m_driver.ignition = Ignition::Start;
        }
    }

    m_vehicle.step(static_cast<double>(kStepUs) / 1e6, m_driver);
    m_sensors.update(m_vehicle, ms);
    for (Ecu *e : ecus())
        e->tick(ms);
    m_tester->tick(ms);
    m_replayer->tick(m_timeUs);
    m_bus.tick(m_timeUs, kStepUs);
    m_scenario->tick(ms);

    m_timeUs += kStepUs;
}

void Simulation::run(int milliseconds)
{
    for (int i = 0; i < milliseconds; ++i)
        step();
}

double Simulation::signal(const QString &name, double fallback) const
{
    return m_signals.value(name, fallback);
}

qint64 Simulation::signalAgeMs(const QString &name) const
{
    auto it = m_signalTimeUs.constFind(name);
    return it == m_signalTimeUs.cend() ? -1 : std::max<qint64>(0, (m_timeUs - it.value()) / 1000);
}

// --- fault injection ----------------------------------------------------------

void Simulation::buildFaultTargets()
{
    const QList<FaultType> analog = {FaultType::OpenCircuit, FaultType::ShortToGround, FaultType::ShortToBattery,
        FaultType::Stuck, FaultType::Offset, FaultType::Gain, FaultType::Noise, FaultType::Intermittent,
        FaultType::Erratic};
    for (Sensor *s : m_sensors.all()) {
        const Sensor::Config &c = s->config();
        m_targets.append({c.id, c.name, c.ecu, FaultTarget::Kind::Sensor, c.unit, analog, c.label});
    }
    const QList<FaultType> failure = {FaultType::Failure};
    m_targets.append({QStringLiteral("alternator"), QStringLiteral("Alternator"), QStringLiteral("ECM"),
        FaultTarget::Kind::Component, {}, failure, QStringLiteral("ALT")});
    m_targets.append({QStringLiteral("cooling"), QStringLiteral("Cooling system (water pump)"),
        QStringLiteral("ECM"), FaultTarget::Kind::Component, {}, failure, QStringLiteral("PUMP")});
    m_targets.append({QStringLiteral("bulb_low_left"), QStringLiteral("Left low beam bulb"), QStringLiteral("BCM"),
        FaultTarget::Kind::Component, {}, failure, QStringLiteral("BULB")});
    m_targets.append({QStringLiteral("oil_pump"), QStringLiteral("Oil pump (low oil pressure)"), QStringLiteral("ECM"),
        FaultTarget::Kind::Component, {}, failure, QStringLiteral("OIL")});
    m_targets.append({QStringLiteral("park_brake_switch"), QStringLiteral("Park brake switch (stuck closed)"),
        QStringLiteral("BCM"), FaultTarget::Kind::Component, {}, failure, QStringLiteral("PB")});
    if (m_config.has(Equipment::EngineObd)) {
        m_targets.append({QStringLiteral("intake_leak"), QStringLiteral("Intake hose (vacuum leak)"), QStringLiteral("ECM"),
            FaultTarget::Kind::Component, {}, failure, QStringLiteral("VAC")});
        m_targets.append({QStringLiteral("ignition_coils"), QStringLiteral("Ignition coils"), QStringLiteral("ECM"),
            FaultTarget::Kind::Component, {}, {FaultType::Misfire}, QStringLiteral("COIL")});
    }
    m_targets.append({QStringLiteral("tyres"), QStringLiteral("Tyres (slow puncture)"), QString(),
        FaultTarget::Kind::Component, {}, {FaultType::Leak}, QStringLiteral("TYRE")});
    if (m_config.has(Equipment::Tpms)) {
        static const char *kWheels[4] = {"fl", "fr", "rl", "rr"};
        for (int i = 0; i < 4; ++i)
            m_targets.append({QStringLiteral("tpms_") + QLatin1String(kWheels[i]),
                QStringLiteral("TPMS sensor ") + QString::fromLatin1(kWheels[i]).toUpper(), QStringLiteral("BCM"),
                FaultTarget::Kind::Component, {}, {FaultType::Failure, FaultType::Unconfigured}, QStringLiteral("TP")});
    }
    m_targets.append({QStringLiteral("throttle_body"), QStringLiteral("Throttle body (electronic throttle)"),
        QStringLiteral("ECM"), FaultTarget::Kind::Component, {}, {FaultType::Unconfigured}, QStringLiteral("THR")});
    for (Ecu *e : ecus()) {
        const QString name = e->identity().name;
        QList<FaultType> ecuFaults = {FaultType::PowerLoss, FaultType::BusOff};
        if (e->hasCoding())
            ecuFaults.append(FaultType::Unconfigured);
        m_targets.append({name.toLower(), name + QStringLiteral(" - ") + e->identity().description, name,
            FaultTarget::Kind::Ecu, {}, ecuFaults, name});
    }
    m_targets.append({QStringLiteral("abs_e2e"), QStringLiteral("ABS wheel speed message protection"),
        QStringLiteral("ABS"), FaultTarget::Kind::Network, {},
        {FaultType::FrozenCounter, FaultType::BadChecksum}, QStringLiteral("E2E")});
    m_targets.append({QStringLiteral("can_bus"), QStringLiteral("CAN bus wiring"), QString(),
        FaultTarget::Kind::Network, {}, {FaultType::BusShort}, QStringLiteral("CAN")});
}

const FaultTarget *Simulation::faultTarget(const QString &id) const
{
    for (const FaultTarget &t : m_targets)
        if (t.id == id)
            return &t;
    return nullptr;
}

void Simulation::applyFault(const QString &id, const Fault &fault)
{
    const bool on = fault.type != FaultType::None;
    if (Sensor *s = m_sensors.byId(id)) {
        s->setFault(fault);
    } else if (id == QLatin1String("alternator")) {
        m_vehicle.alternatorFailed = on;
    } else if (id == QLatin1String("cooling")) {
        m_vehicle.coolingFailed = on;
    } else if (id == QLatin1String("bulb_low_left")) {
        m_vehicle.bulbLowLeftOpen = on;
    } else if (id == QLatin1String("intake_leak")) {
        m_vehicle.vacuumLeakGps = on ? 1.2 : 0.0; // a cracked hose: about half the idle air again
    } else if (id == QLatin1String("ignition_coils")) {
        m_vehicle.misfireCylinder = on ? std::clamp(fault.hasP1 ? static_cast<int>(fault.p1) : 3, 1, 4) : 0;
    } else if (id == QLatin1String("tyres")) {
        if (on) {
            const int wheel = std::clamp(fault.hasP1 ? static_cast<int>(fault.p1) : 1, 1, 4);
            m_vehicle.tyreLeakBarPerS[static_cast<size_t>(wheel - 1)] = 0.01; // 0.6 bar a minute
        } else {
            // Repaired and inflated to the placard again.
            m_vehicle.tyreLeakBarPerS = {};
            for (int i = 0; i < 4; ++i)
                m_vehicle.setTyrePressureCold(i, std::max(m_vehicle.tyrePressureCold(i), VehicleModel::kPlacardBar));
        }
    } else if (id.startsWith(QLatin1String("tpms_"))) {
        const qsizetype wheel = QStringList{QStringLiteral("tpms_fl"), QStringLiteral("tpms_fr"),
            QStringLiteral("tpms_rl"), QStringLiteral("tpms_rr")}.indexOf(id);
        TpmsSensor &sensor = m_sensors.tpms[static_cast<size_t>(std::max<qsizetype>(0, wheel))];
        sensor.dead = fault.type == FaultType::Failure;                       // flat battery
        sensor.id = fault.type == FaultType::Unconfigured ? sensor.factoryId ^ 0x00777700u // a new sensor
                                                         : sensor.factoryId;
    } else if (id == QLatin1String("oil_pump")) {
        m_vehicle.oilPumpFailed = on;
    } else if (id == QLatin1String("park_brake_switch")) {
        m_vehicle.parkBrakeSwitchStuck = on;
    } else if (id == QLatin1String("throttle_body")) {
        // New throttle body: its closed position is unknown. Repair = the old, adapted one is back.
        m_ecm->setThrottleAdapted(!on);
    } else if (id == QLatin1String("abs_e2e")) {
        m_abs->faultFrozenCounter = fault.type == FaultType::FrozenCounter;
        m_abs->faultBadChecksum = fault.type == FaultType::BadChecksum;
    } else if (id == QLatin1String("can_bus")) {
        m_bus.setBusFailure(on);
    } else if (Ecu *e = ecu(id)) {
        e->setPowerFault(fault.type == FaultType::PowerLoss);
        e->setBusOff(fault.type == FaultType::BusOff);
        e->setCodingBlank(fault.type == FaultType::Unconfigured);
    }
}

bool Simulation::injectFault(const QString &targetId, const Fault &fault)
{
    const FaultTarget *target = faultTarget(targetId);
    if (!target || !target->faultTypes.contains(fault.type))
        return false;
    clearFault(targetId);
    applyFault(targetId, fault);
    m_activeFaults.append({targetId, fault});
    return true;
}

void Simulation::clearFault(const QString &targetId)
{
    for (qsizetype i = 0; i < m_activeFaults.size(); ++i) {
        if (m_activeFaults.at(i).targetId == targetId) {
            m_activeFaults.removeAt(i);
            applyFault(targetId, {});
            return;
        }
    }
}

void Simulation::clearAllFaults()
{
    const QList<ActiveFault> faults = m_activeFaults;
    for (const ActiveFault &f : faults)
        clearFault(f.targetId);
}

// --- replay -------------------------------------------------------------------

void Simulation::startReplay(const QList<BusFrame> &frames)
{
    // The log takes the place of the transmitting ECUs; only the cluster keeps listening.
    for (Ecu *e : ecus())
        if (e != m_ipc.get())
            e->setPowerFault(true);
    m_driver.ignition = Ignition::On;
    m_replayer->start(frames, m_timeUs);
}

void Simulation::stopReplay()
{
    if (!m_replayer->active())
        return;
    m_replayer->stop();
    for (Ecu *e : ecus()) {
        bool faulted = false;
        for (const ActiveFault &f : std::as_const(m_activeFaults))
            faulted = faulted || (f.targetId == e->identity().name.toLower() && f.fault.type == FaultType::PowerLoss);
        e->setPowerFault(faulted);
    }
}

} // namespace ecusim
