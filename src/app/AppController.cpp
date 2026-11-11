#include "AppController.h"

#include "sim/ScenarioRunner.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

using namespace ecusim;

namespace {
constexpr int kTimerMs = 16;
constexpr int kMaxStepsPerTick = 400;

bool sameInputs(const DriverInputs &a, const DriverInputs &b)
{
    return a.ignition == b.ignition && a.throttlePedal == b.throttlePedal && a.brakePedal == b.brakePedal
        && a.selector == b.selector && a.headlamps == b.headlamps && a.highBeam == b.highBeam
        && a.turnLeft == b.turnLeft && a.turnRight == b.turnRight && a.hazard == b.hazard
        && a.doorLock == b.doorLock && a.doorAjar == b.doorAjar;
}

QString circuitText(CircuitState c)
{
    switch (c) {
    case CircuitState::Open: return QStringLiteral("open");
    case CircuitState::ShortGround: return QStringLiteral("short to ground");
    case CircuitState::ShortBattery: return QStringLiteral("short to battery");
    default: return QStringLiteral("ok");
    }
}
} // namespace

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_trace(new TraceModel(this))
    , m_scanner(new ScannerViewModel(this))
{
    createSimulation();

    const QDir dir(QStringLiteral(":/ecusim/scenarios"));
    for (const QString &file : dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
        QFile f(dir.filePath(file));
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        m_scenarios.append(QVariantMap{{QStringLiteral("path"), dir.filePath(file)},
            {QStringLiteral("name"), o.value(QLatin1String("name")).toString(file)},
            {QStringLiteral("description"), o.value(QLatin1String("description")).toString()},
            {QStringLiteral("lesson"), o.value(QLatin1String("lesson")).toString()},
            {QStringLiteral("steps"), o.value(QLatin1String("steps")).toArray().size()}});
    }

    m_timer.setInterval(kTimerMs);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &AppController::onTimer);
    m_clock.start();
    m_timer.start();
}

AppController::~AppController() = default;

void AppController::createSimulation()
{
    m_sim = std::make_unique<Simulation>();
    m_trace->setDatabase(&m_sim->dbc());
    m_sim->bus().addListener([this](const BusFrame &frame) { m_trace->append(frame); });
    m_sim->tester().onResult = [this](const DiagResult &result) { m_scanner->onResult(result); };
    m_sim->scenario().onLog = [this](const ScenarioLogEntry &entry) {
        static const char *kLevels[] = {"info", "action", "pass", "fail"};
        m_scenarioLog.append(QVariantMap{
            {QStringLiteral("time"), QString::number(static_cast<double>(entry.timeMs) / 1000.0, 'f', 2)},
            {QStringLiteral("level"), QString::fromLatin1(kLevels[static_cast<int>(entry.level)])},
            {QStringLiteral("text"), entry.text}});
        if (m_sim->scenario().finished())
            setMessage(QStringLiteral("Lesson finished: %1 - %2").arg(m_sim->scenario().name(), entry.text));
        emit scenarioChanged();
    };
    m_lastFrameCount = 0;
    m_lastRateSimMs = 0;
    m_lastInputs = m_sim->driver();
}

void AppController::reset()
{
    m_scenarioLog.clear();
    m_trace->clear();
    createSimulation();
    m_scanner->simulationReset();
    m_debtMs = 0.0;
    setMessage(QStringLiteral("Simulation reset"));
    emit inputsChanged();
    emit faultsChanged();
    emit scenarioChanged();
    emit frameUpdated();
    emit slowUpdated();
}

void AppController::setMessage(const QString &text)
{
    m_message = text;
    emit messageChanged();
}

// --- time ---------------------------------------------------------------------

void AppController::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    m_debtMs = 0.0;
    emit runningChanged();
}

void AppController::setSpeedFactor(double factor)
{
    factor = std::clamp(factor, 0.1, 20.0);
    if (qFuzzyCompare(factor, m_speed))
        return;
    m_speed = factor;
    emit speedFactorChanged();
}

void AppController::advance(int steps)
{
    for (int i = 0; i < steps; ++i)
        m_sim->step();
}

void AppController::step(int milliseconds)
{
    setRunning(false);
    advance(std::clamp(milliseconds, 1, 10000));
    m_trace->flush();
    emit frameUpdated();
    emit slowUpdated();
}

void AppController::onTimer()
{
    const double elapsed = std::min<double>(static_cast<double>(m_clock.restart()), 100.0);
    if (m_running) {
        m_debtMs += elapsed * m_speed;
        const int steps = std::min(static_cast<int>(m_debtMs), kMaxStepsPerTick);
        m_debtMs -= steps;
        if (m_debtMs > kMaxStepsPerTick)
            m_debtMs = 0.0; // the machine cannot keep up: drop the backlog instead of spiralling
        advance(steps);
    }

    ++m_slowCounter;
    if (m_slowCounter % 2 == 0)
        m_trace->flush();
    emit frameUpdated();

    if (m_slowCounter % 12 == 0) {
        const qint64 nowMs = m_sim->nowMs();
        if (nowMs > m_lastRateSimMs) {
            m_frameRate = static_cast<double>(m_sim->bus().frameCount() - m_lastFrameCount) * 1000.0
                / static_cast<double>(nowMs - m_lastRateSimMs);
            m_lastFrameCount = m_sim->bus().frameCount();
            m_lastRateSimMs = nowMs;
        }
        // Scenarios change inputs and faults behind the UI's back.
        if (!sameInputs(m_lastInputs, m_sim->driver())) {
            m_lastInputs = m_sim->driver();
            emit inputsChanged();
        }
        if (m_lastActiveFaults != m_sim->activeFaults().size()) {
            m_lastActiveFaults = static_cast<int>(m_sim->activeFaults().size());
            emit faultsChanged();
        }
        emit slowUpdated();
    }
    if (m_slowCounter % 30 == 0 && m_running)
        m_scanner->poll();
}

// --- driver inputs ------------------------------------------------------------

void AppController::setIgnition(int on)
{
    m_sim->driver().ignition = on ? Ignition::On : Ignition::Off;
    emit inputsChanged();
}

void AppController::startEngine()
{
    m_sim->requestEngineStart();
    emit inputsChanged();
}

void AppController::setThrottle(double percent)
{
    m_sim->driver().throttlePedal = std::clamp(percent, 0.0, 100.0) / 100.0;
    emit inputsChanged();
}

void AppController::setBrake(double percent)
{
    m_sim->driver().brakePedal = std::clamp(percent, 0.0, 100.0) / 100.0;
    emit inputsChanged();
}

void AppController::setSelector(int position)
{
    m_sim->driver().selector = static_cast<Selector>(std::clamp(position, 0, 3));
    emit inputsChanged();
}

void AppController::setHeadlamps(int mode)
{
    m_sim->driver().headlamps = std::clamp(mode, 0, 2);
    emit inputsChanged();
}

void AppController::setHighBeam(bool on)
{
    m_sim->driver().highBeam = on;
    emit inputsChanged();
}

void AppController::setTurnLeft(bool on)
{
    m_sim->driver().turnLeft = on;
    if (on)
        m_sim->driver().turnRight = false;
    emit inputsChanged();
}

void AppController::setTurnRight(bool on)
{
    m_sim->driver().turnRight = on;
    if (on)
        m_sim->driver().turnLeft = false;
    emit inputsChanged();
}

void AppController::setHazard(bool on)
{
    m_sim->driver().hazard = on;
    emit inputsChanged();
}

void AppController::setDoorLock(bool on)
{
    m_sim->driver().doorLock = on;
    emit inputsChanged();
}

void AppController::setDoor(int index, bool ajar)
{
    if (index < 0 || index > 3)
        return;
    m_sim->driver().doorAjar[static_cast<size_t>(index)] = ajar;
    emit inputsChanged();
}

QVariantList AppController::doors() const
{
    QVariantList list;
    for (bool ajar : m_sim->driver().doorAjar)
        list.append(ajar);
    return list;
}

// --- state for the views --------------------------------------------------------

QVariantMap AppController::cluster() const
{
    const ClusterState &c = m_sim->ipc().state();
    static const char *kSelector[] = {"P", "R", "N", "D"};
    QString gear = QString::fromLatin1(kSelector[std::clamp(c.selector, 0, 3)]);
    if (c.selector == 3 && c.gear >= 1 && c.gear <= 6)
        gear += QString::number(c.gear);
    return {{QStringLiteral("powered"), c.powered},
        {QStringLiteral("speed"), c.speedKph},
        {QStringLiteral("rpm"), c.rpm},
        {QStringLiteral("coolant"), c.coolantTemp},
        {QStringLiteral("voltage"), c.batteryVoltage},
        {QStringLiteral("gear"), c.powered ? gear : QString()},
        {QStringLiteral("mil"), c.mil},
        {QStringLiteral("abs"), c.abs},
        {QStringLiteral("tcs"), c.tcs},
        {QStringLiteral("battery"), c.battery},
        {QStringLiteral("coolantWarn"), c.coolantWarn},
        {QStringLiteral("transFault"), c.transFault},
        {QStringLiteral("turnLeft"), c.turnLeft},
        {QStringLiteral("turnRight"), c.turnRight},
        {QStringLiteral("lowBeam"), c.lowBeam},
        {QStringLiteral("highBeam"), c.highBeam},
        {QStringLiteral("doorAjar"), c.doorAjar},
        {QStringLiteral("lampFault"), c.lampFault}};
}

QVariantMap AppController::vehicle() const
{
    const VehicleModel &v = m_sim->vehicle();
    return {{QStringLiteral("speed"), v.speedKph()},
        {QStringLiteral("rpm"), v.engineRpm()},
        {QStringLiteral("engineRunning"), v.engineRunning()},
        {QStringLiteral("cranking"), v.starterEngaged()},
        {QStringLiteral("coolant"), v.coolantTemp()},
        {QStringLiteral("transTemp"), v.transTemp()},
        {QStringLiteral("voltage"), v.batteryVoltage()},
        {QStringLiteral("throttle"), v.throttlePos() * 100.0},
        {QStringLiteral("gear"), v.engagedGear()},
        {QStringLiteral("lowBeam"), m_sim->bcm().lowBeamOn()},
        {QStringLiteral("locked"), m_sim->bcm().doorsLocked()},
        {QStringLiteral("busFailure"), m_sim->bus().busFailure()}};
}

QVariantList AppController::ecus() const
{
    QVariantList list;
    for (Ecu *e : m_sim->ecus()) {
        int confirmed = 0;
        bool active = false;
        for (const DtcEntry &d : e->dtcs().entries()) {
            confirmed += (d.status & DtcStatus::Confirmed) ? 1 : 0;
            active = active || (d.status & DtcStatus::TestFailed);
        }
        QString state = QStringLiteral("ok");
        if (e->powerFault())
            state = QStringLiteral("nopower");
        else if (!e->awake())
            state = QStringLiteral("sleep");
        else if (e->busOff())
            state = QStringLiteral("busoff");
        else if (active)
            state = QStringLiteral("fault");
        else if (e->dtcs().storedCount() > 0)
            state = QStringLiteral("stored");
        list.append(QVariantMap{{QStringLiteral("name"), e->identity().name},
            {QStringLiteral("description"), e->identity().description},
            {QStringLiteral("state"), state},
            {QStringLiteral("dtcCount"), e->dtcs().storedCount()},
            {QStringLiteral("confirmed"), confirmed},
            {QStringLiteral("hasDiag"), e->identity().requestId != 0}});
    }
    return list;
}

QVariantList AppController::sensors() const
{
    QVariantList list;
    for (Sensor *s : m_sim->sensors().all()) {
        const Sensor::Config &c = s->config();
        list.append(QVariantMap{{QStringLiteral("id"), c.id},
            {QStringLiteral("name"), c.name},
            {QStringLiteral("unit"), c.unit},
            {QStringLiteral("ecu"), c.ecu},
            {QStringLiteral("digital"), c.digital},
            {QStringLiteral("value"), s->value()},
            {QStringLiteral("trueValue"), s->trueValue()},
            {QStringLiteral("voltage"), s->voltage()},
            {QStringLiteral("circuit"), circuitText(s->circuit())},
            {QStringLiteral("faulted"), s->fault().type != FaultType::None},
            {QStringLiteral("fault"), s->fault().type == FaultType::None ? QString() : faultTypeLabel(s->fault().type)}});
    }
    return list;
}

// --- faults -----------------------------------------------------------------------

QVariantList AppController::faultTargets() const
{
    static const char *kKinds[] = {"Sensors", "Components", "Control units", "Network"};
    QVariantList list;
    for (const FaultTarget &t : m_sim->faultTargets()) {
        QVariantList types;
        for (FaultType type : t.faultTypes)
            types.append(QVariantMap{{QStringLiteral("id"), faultTypeName(type)},
                {QStringLiteral("label"), faultTypeLabel(type)}});
        list.append(QVariantMap{{QStringLiteral("id"), t.id},
            {QStringLiteral("name"), t.name},
            {QStringLiteral("ecu"), t.ecu},
            {QStringLiteral("kind"), QString::fromLatin1(kKinds[static_cast<int>(t.kind)])},
            {QStringLiteral("unit"), t.unit},
            {QStringLiteral("types"), types}});
    }
    return list;
}

QVariantList AppController::activeFaults() const
{
    QVariantList list;
    for (const ActiveFault &f : m_sim->activeFaults()) {
        const FaultTarget *t = m_sim->faultTarget(f.targetId);
        QString detail;
        if (f.fault.hasP1)
            detail = QString::number(f.fault.p1);
        list.append(QVariantMap{{QStringLiteral("targetId"), f.targetId},
            {QStringLiteral("target"), t ? t->name : f.targetId},
            {QStringLiteral("type"), faultTypeLabel(f.fault.type)},
            {QStringLiteral("detail"), detail}});
    }
    return list;
}

bool AppController::injectFault(const QString &targetId, const QString &faultType, const QString &p1,
    const QString &p2)
{
    Fault fault;
    fault.type = faultTypeFromName(faultType);
    bool ok1 = false, ok2 = false;
    fault.p1 = p1.trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok1);
    fault.p2 = p2.trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok2);
    fault.hasP1 = ok1;
    if (!ok2)
        fault.p2 = 0.0;
    if (fault.type == FaultType::Intermittent && ok2)
        fault.p2 /= 100.0; // entered in percent
    const bool ok = m_sim->injectFault(targetId, fault);
    if (ok) {
        const FaultTarget *t = m_sim->faultTarget(targetId);
        setMessage(QStringLiteral("Fault injected: %1 - %2").arg(t ? t->name : targetId, faultTypeLabel(fault.type)));
    }
    m_lastActiveFaults = static_cast<int>(m_sim->activeFaults().size());
    emit faultsChanged();
    return ok;
}

void AppController::clearFault(const QString &targetId)
{
    m_sim->clearFault(targetId);
    m_lastActiveFaults = static_cast<int>(m_sim->activeFaults().size());
    emit faultsChanged();
}

void AppController::clearAllFaults()
{
    m_sim->clearAllFaults();
    m_lastActiveFaults = 0;
    emit faultsChanged();
}

QString AppController::faultHint(const QString &targetId, const QString &faultType) const
{
    const FaultTarget *t = m_sim->faultTarget(targetId);
    const QString unit = t ? t->unit : QString();
    switch (faultTypeFromName(faultType)) {
    case FaultType::Stuck: return QStringLiteral("Output frozen. Value in %1 (empty = current value).").arg(unit);
    case FaultType::Offset: return QStringLiteral("Adds a constant error. Offset in %1.").arg(unit);
    case FaultType::Gain: return QStringLiteral("Scales the signal. Factor, e.g. 0.5 (empty = 0.5).");
    case FaultType::Noise: return QStringLiteral("Gaussian noise. Sigma in %1 (empty = 5 % of range).").arg(unit);
    case FaultType::OpenCircuit: return QStringLiteral("Broken wire or unplugged connector.");
    case FaultType::ShortToGround: return QStringLiteral("Signal wire touches chassis ground.");
    case FaultType::ShortToBattery: return QStringLiteral("Signal wire touches a supply voltage.");
    case FaultType::Intermittent:
        return QStringLiteral("Loose contact. Period in ms (empty = 400); second value = open time in % (empty = 50).");
    case FaultType::Erratic: return QStringLiteral("Random values across the whole measuring range.");
    case FaultType::Failure: return QStringLiteral("The component stops working.");
    case FaultType::PowerLoss: return QStringLiteral("Blown fuse: the control unit is dead and silent.");
    case FaultType::BusOff:
        return QStringLiteral("The control unit runs, but its CAN controller is off the bus: it neither sends nor receives.");
    case FaultType::FrozenCounter: return QStringLiteral("The sender task hangs: same alive counter in every frame.");
    case FaultType::BadChecksum: return QStringLiteral("Data corrupted between application and bus: CRC does not match.");
    case FaultType::BusShort: return QStringLiteral("CAN-H shorted to CAN-L: no frame gets through.");
    default: return {};
    }
}

// --- scenarios, trace files -----------------------------------------------------------

QString AppController::scenarioState() const
{
    switch (m_sim->scenario().state()) {
    case ScenarioRunner::State::Running: return QStringLiteral("running");
    case ScenarioRunner::State::Passed: return QStringLiteral("passed");
    case ScenarioRunner::State::Failed: return QStringLiteral("failed");
    default: return QStringLiteral("idle");
    }
}

QString AppController::scenarioName() const
{
    return m_sim->scenario().state() == ScenarioRunner::State::Idle && m_scenarioLog.isEmpty()
        ? QString()
        : m_sim->scenario().name();
}

double AppController::scenarioProgress() const
{
    const ScenarioRunner &s = m_sim->scenario();
    return s.stepCount() > 0 ? static_cast<double>(s.stepIndex()) / s.stepCount() : 0.0;
}

void AppController::runScenario(const QString &path)
{
    // A lesson always starts from a known state.
    reset();
    QString error;
    if (!m_sim->scenario().loadFile(path, &error)) {
        setMessage(QStringLiteral("Scenario: ") + error);
        return;
    }
    m_sim->scenario().start();
    setRunning(true);
    setMessage(QStringLiteral("Lesson running: ") + m_sim->scenario().name());
    emit scenarioChanged();
}

void AppController::runScenarioFile(const QUrl &file)
{
    runScenario(file.isLocalFile() ? file.toLocalFile() : file.toString());
}

void AppController::stopScenario()
{
    m_sim->scenario().stop();
    emit scenarioChanged();
}

void AppController::saveTrace(const QUrl &file)
{
    const QString path = file.toLocalFile();
    QString error;
    const bool asc = path.endsWith(QLatin1String(".asc"), Qt::CaseInsensitive);
    const bool ok = asc ? m_sim->trace().saveAsc(path, QDateTime::currentDateTime(), &error)
                        : m_sim->trace().saveCandump(path, &error);
    setMessage(ok ? QStringLiteral("Trace saved: %1 (%2 frames, %3)")
                        .arg(path)
                        .arg(m_sim->trace().frames().size())
                        .arg(asc ? QLatin1String("Vector ASC") : QLatin1String("candump"))
                  : QStringLiteral("Cannot save trace: ") + error);
}

void AppController::loadReplay(const QUrl &file)
{
    QString error;
    const QList<BusFrame> frames = TraceRecorder::load(file.toLocalFile(), &error);
    if (frames.isEmpty()) {
        setMessage(QStringLiteral("Cannot replay: ") + error);
        return;
    }
    reset();
    m_sim->startReplay(frames);
    setRunning(true);
    const double seconds = static_cast<double>(frames.last().timeUs - frames.first().timeUs) / 1e6;
    setMessage(QStringLiteral("Replaying %1 frames (%2 s). The ECUs are switched off; the cluster shows the log.")
                   .arg(frames.size())
                   .arg(seconds, 0, 'f', 1));
    emit inputsChanged();
}

void AppController::stopReplay()
{
    m_sim->stopReplay();
    setMessage(QStringLiteral("Replay stopped"));
    emit slowUpdated();
}
