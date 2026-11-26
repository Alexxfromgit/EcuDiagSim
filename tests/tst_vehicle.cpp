// Plant, sensors, bus and ECU control behaviour.
#include "TestUtil.h"

using namespace ecusim;
using namespace testutil;

class tst_Vehicle : public QObject
{
    Q_OBJECT

private slots:
    void sensorCircuitModel();
    void sensorFaults();
    void busArbitrationAndLoad();
    void offlineNodesNeitherSendNorReceive();
    void ignitionOffIsQuiet();
    void engineStartAndIdle();
    void accelerateShiftAndBrake();
    void warmUp();
    void noStartWithoutCrankSignal();
    void stallWhenCrankSignalIsLost();
    void throttleLimpHome();
    void transmissionTempSensor();
    void overheating();
    void plausibilityCatchesWrongWheelSpeed();
    void bulbCheckAndBodyFunctions();
    void lampCircuitFault();
    void reverseWhileRollingForwardBrakes();
    void deterministic();
    void vehicleConfig();
    void fuelConsumptionAndOverrunCut();
    void runningOutOfFuelSetsNoDtc();
    void fuelSenderFaults();
    void oilPressure();
    void parkBrake();
    void engineObdPackClosedLoop();
    void vacuumLeakLeansOutAndLearns();
    void misfireFlashesTheMil();
    void oxygenAndAirFlowSensorFaults();
    void tyrePhysicsWithoutTpms();
    void tpmsLowPressureWarning();
    void tpmsReplacedSensorAndRelearn();
    void tpmsDeadSensorAndManualId();
};

namespace {
VehicleConfig obdPack()
{
    VehicleConfig c;
    c.fit(Equipment::EngineObd);
    return c;
}

VehicleConfig withTpms()
{
    VehicleConfig c;
    c.fit(Equipment::Tpms);
    return c;
}

QByteArray unlockAndAsk(Simulation &sim, quint32 id, const char *request)
{
    if (ask(sim, id, "10 03").left(2) != hex("50 03"))
        return {};
    const QByteArray seed = ask(sim, id, "27 01");
    if (seed.size() != 6)
        return {};
    quint32 s = 0;
    for (int i = 2; i < 6; ++i)
        s = (s << 8) | static_cast<quint8>(seed[i]);
    const quint32 key = securityKeyFromSeed(s);
    QByteArray keyRequest = hex("27 02");
    for (int shift = 24; shift >= 0; shift -= 8)
        keyRequest.append(static_cast<char>((key >> shift) & 0xFF));
    exchange(sim, id, keyRequest);
    return ask(sim, id, request);
}
} // namespace

void tst_Vehicle::sensorCircuitModel()
{
    SensorSet s;
    // NTC against a pull-up: open = 5 V = "very cold", short to ground = 0 V = "very hot".
    s.ect.update(90.0, 0);
    QVERIFY(qAbs(s.ect.value() - 90.0) < 1e-9);
    QVERIFY(s.ect.voltage() > 0.5 && s.ect.voltage() < 4.5);
    s.ect.setFault({FaultType::OpenCircuit});
    s.ect.update(90.0, 1);
    QCOMPARE(s.ect.voltage(), 5.0);
    QCOMPARE(s.ect.value(), -40.0);
    s.ect.setFault({FaultType::ShortToGround});
    s.ect.update(90.0, 2);
    QCOMPARE(s.ect.voltage(), 0.0);
    QCOMPARE(s.ect.value(), 150.0);
    s.ect.clearFault();
    s.ect.update(20.0, 3);
    QVERIFY(qAbs(s.ect.value() - 20.0) < 1e-9);

    // The throttle potentiometer has a pull-down: unplugged reads 0 V.
    s.tps.setFault({FaultType::OpenCircuit});
    s.tps.update(50.0, 0);
    QCOMPARE(s.tps.voltage(), 0.0);
    s.tps.setFault({FaultType::ShortToBattery});
    s.tps.update(50.0, 0);
    QCOMPARE(s.tps.voltage(), 5.0);

    // Digital sensors deliver no pulses with any circuit fault.
    s.wss[0].setFault({FaultType::ShortToBattery});
    s.wss[0].update(80.0, 0);
    QCOMPARE(s.wss[0].value(), 0.0);
    QCOMPARE(s.wss[0].circuit(), CircuitState::ShortBattery);
    QCOMPARE(s.all().size(), 10);
    QVERIFY(s.byId(QStringLiteral("ckp")) == &s.ckp);
    QVERIFY(!s.byId(QStringLiteral("nope")));
}

void tst_Vehicle::sensorFaults()
{
    SensorSet s;
    Sensor &w = s.wss[1];
    w.setFault({FaultType::Stuck});
    w.update(42.0, 0);
    w.update(80.0, 1);
    QCOMPARE(w.value(), 42.0); // frozen at the value it had when the fault appeared
    w.setFault({FaultType::Stuck, 10.0, 0.0, true});
    w.update(80.0, 2);
    QCOMPARE(w.value(), 10.0);
    w.setFault({FaultType::Offset, 5.0, 0.0, true});
    w.update(80.0, 3);
    QCOMPARE(w.value(), 85.0);
    w.setFault({FaultType::Gain, 0.5, 0.0, true});
    w.update(80.0, 4);
    QCOMPARE(w.value(), 40.0);
    QCOMPARE(w.trueValue(), 80.0);

    // Intermittent: open for half of every 400 ms.
    w.setFault({FaultType::Intermittent});
    int open = 0;
    for (int t = 0; t < 4000; ++t) {
        w.update(80.0, t);
        open += w.circuit() == CircuitState::Open ? 1 : 0;
    }
    QCOMPARE(open, 2000);

    // Noise: zero mean, the requested spread.
    w.setFault({FaultType::Noise, 2.0, 0.0, true});
    double sum = 0, sq = 0;
    const int n = 20000;
    for (int t = 0; t < n; ++t) {
        w.update(80.0, t);
        sum += w.value() - 80.0;
        sq += (w.value() - 80.0) * (w.value() - 80.0);
    }
    QVERIFY(qAbs(sum / n) < 0.1);
    QVERIFY(qAbs(std::sqrt(sq / n) - 2.0) < 0.1);

    w.setFault({FaultType::Erratic});
    double lo = 1e9, hi = -1e9;
    for (int t = 0; t < 5000; ++t) {
        w.update(80.0, t);
        lo = qMin(lo, w.value());
        hi = qMax(hi, w.value());
    }
    QVERIFY(lo < 100.0 && hi > 300.0);
}

namespace {
struct TestNode : CanNode
{
    QString name;
    bool online = true;
    QList<quint32> received;
    QString nodeName() const override { return name; }
    void onFrame(const QCanBusFrame &f) override { received << f.frameId(); }
    bool isOnline() const override { return online; }
};
} // namespace

void tst_Vehicle::busArbitrationAndLoad()
{
    VirtualCanBus bus(500000);
    TestNode a, b;
    a.name = QStringLiteral("A");
    b.name = QStringLiteral("B");
    bus.attach(&a);
    bus.attach(&b);
    QList<BusFrame> seen;
    bus.addListener([&](const BusFrame &f) { seen << f; });

    // Queued in the "wrong" order: the lowest identifier wins arbitration.
    const QByteArray data(8, '\x55');
    bus.send(&a, QCanBusFrame(0x300, data));
    bus.send(&a, QCanBusFrame(0x100, data));
    bus.send(&a, QCanBusFrame(0x0B0, data));
    bus.send(&a, QCanBusFrame(0x100, QByteArray(8, '\x01')));
    bus.tick(0, 1000);
    QCOMPARE(b.received, QList<quint32>({0x0B0, 0x100, 0x100, 0x300}));
    QVERIFY(a.received.isEmpty()); // no self reception
    QCOMPARE(seen.at(1).frame.payload(), data); // FIFO among equal identifiers
    QCOMPARE(seen.first().sender, QStringLiteral("A"));
    // An 8 byte frame takes roughly a quarter of a millisecond at 500 kbit/s.
    QVERIFY(seen.at(0).timeUs > 200 && seen.at(0).timeUs < 280);
    QVERIFY(seen.at(1).timeUs > seen.at(0).timeUs);
    QCOMPARE(VirtualCanBus::frameBits(QCanBusFrame(0x100, data)), 47 + 64 + 12);

    // More frames than fit into one tick stay queued and go out later.
    for (int i = 0; i < 20; ++i)
        bus.send(&b, QCanBusFrame(0x200, data));
    bus.tick(1000, 1000);
    QVERIFY(bus.pendingCount() > 10);
    for (int t = 2; t < 10; ++t)
        bus.tick(t * 1000, 1000);
    QCOMPARE(bus.pendingCount(), 0);
    QCOMPARE(a.received.size(), 20);
    QCOMPARE(bus.frameCount(), quint64(24));

    // Saturate the bus for a second: the load approaches 100 %.
    for (int t = 10; t < 1100; ++t) {
        for (int i = 0; i < 5; ++i)
            bus.send(&a, QCanBusFrame(0x200, data));
        bus.tick(t * 1000, 1000);
    }
    QVERIFY2(bus.busLoad() > 0.95 && bus.busLoad() < 1.05, qPrintable(QString::number(bus.busLoad())));

    bus.setBusFailure(true);
    QCOMPARE(bus.pendingCount(), 0);
    const quint64 count = bus.frameCount();
    bus.send(&a, QCanBusFrame(0x100, data));
    bus.tick(2'000'000, 1000);
    QCOMPARE(bus.frameCount(), count);
}

void tst_Vehicle::offlineNodesNeitherSendNorReceive()
{
    VirtualCanBus bus;
    TestNode a, b;
    bus.attach(&a);
    bus.attach(&b);
    b.online = false;
    QVERIFY(!bus.send(&b, QCanBusFrame(0x100, QByteArray(1, 0))));
    QVERIFY(bus.send(&a, QCanBusFrame(0x100, QByteArray(1, 0))));
    bus.tick(0, 1000);
    QVERIFY(b.received.isEmpty());
    bus.detach(&b);
    bus.send(&a, QCanBusFrame(0x100, QByteArray(1, 0)));
    bus.tick(1000, 1000);
    QCOMPARE(bus.frameCount(), quint64(2));
}

void tst_Vehicle::ignitionOffIsQuiet()
{
    Simulation sim;
    sim.run(1000);
    // Only the BCM (terminal 30) talks: one message every 100 ms.
    QCOMPARE(sim.bus().frameCount(), quint64(10));
    QVERIFY(sim.bcm().awake());
    QVERIFY(!sim.ecm().awake() && !sim.tcm().awake() && !sim.abs().awake() && !sim.ipc().awake());
    QVERIFY(!sim.ipc().state().powered);
    QCOMPARE(sim.signal(QStringLiteral("IgnitionState")), 0.0);
    QVERIFY(sim.bus().busLoad() < 0.01);

    sim.driver().ignition = Ignition::On;
    sim.run(2000);
    // 0x0B0 + 0x100 at 10 ms, 0x0B1 + 0x1A0 at 20 ms, 0x300 + 0x350 at 100 ms = 320 frames/s
    const double load = sim.bus().busLoad();
    QVERIFY2(load > 0.06 && load < 0.10, qPrintable(QString::number(load)));
    QCOMPARE(sim.signal(QStringLiteral("IgnitionState")), 1.0);
    for (Ecu *e : sim.ecus())
        QCOMPARE(e->dtcs().storedCount(), 0);
}

void tst_Vehicle::engineStartAndIdle()
{
    Simulation sim;
    sim.driver().ignition = Ignition::On;
    sim.run(500);
    QVERIFY(!sim.vehicle().engineRunning());
    QCOMPARE(sim.signal(QStringLiteral("EngineRunning")), 0.0);
    QVERIFY(sim.ipc().state().battery); // charge lamp with ignition on, engine off

    sim.requestEngineStart();
    QVERIFY(sim.runUntil([&] { return sim.vehicle().engineRunning(); }, 2000));
    sim.run(4000);
    QCOMPARE(sim.driver().ignition, Ignition::On); // the key springs back from "start"
    QVERIFY(qAbs(sim.vehicle().engineRpm() - VehicleModel::kIdleRpm) < 30.0);
    QVERIFY(qAbs(sim.signal(QStringLiteral("EngineRPM")) - 800.0) < 30.0);
    QCOMPARE(sim.signal(QStringLiteral("EngineRunning")), 1.0);
    QVERIFY(sim.vehicle().batteryVoltage() > 14.0);
    const ClusterState &c = sim.ipc().state();
    QVERIFY(c.powered && !c.mil && !c.abs && !c.battery && !c.coolantWarn && !c.transFault);
    QCOMPARE(sim.vehicle().speedKph(), 0.0); // selector in P

    // Starting needs P or N.
    sim.driver().ignition = Ignition::Off;
    sim.run(1000);
    QVERIFY(!sim.vehicle().engineRunning());
    sim.driver().ignition = Ignition::On;
    sim.driver().selector = Selector::D;
    sim.requestEngineStart();
    sim.run(3500);
    QVERIFY(!sim.vehicle().engineRunning());
    // The starter interlock is not a crankshaft sensor fault.
    QCOMPARE(sim.ecm().dtcs().storedCount(), 0);
    QVERIFY(!sim.ipc().state().mil);
}

void tst_Vehicle::accelerateShiftAndBrake()
{
    Simulation sim;
    startEngine(sim);
    sim.driver().selector = Selector::D;
    sim.run(6000);
    // Creep at idle in first gear.
    QVERIFY(sim.vehicle().speedKph() > 3.0 && sim.vehicle().speedKph() < 14.0);
    QCOMPARE(sim.signal(QStringLiteral("GearSelector")), 3.0);

    sim.driver().throttlePedal = 0.5;
    int maxGear = 0;
    double maxRpm = 0;
    for (int i = 0; i < 40000; ++i) {
        sim.step();
        maxGear = qMax(maxGear, sim.tcm().forwardGear());
        maxRpm = qMax(maxRpm, sim.vehicle().engineRpm());
    }
    QVERIFY2(sim.vehicle().speedKph() > 120.0, qPrintable(QString::number(sim.vehicle().speedKph())));
    QVERIFY(maxGear >= 5);
    QVERIFY(maxRpm < 6500.0);
    QVERIFY(qAbs(sim.signal(QStringLiteral("VehicleSpeed")) - sim.vehicle().speedKph()) < 1.0);
    QCOMPARE(sim.ipc().state().gear, sim.tcm().forwardGear());

    // Kick-down: full throttle makes the TCM shift down.
    sim.driver().throttlePedal = 0.1;
    sim.run(8000);
    const int cruisingGear = sim.tcm().forwardGear();
    sim.driver().throttlePedal = 1.0;
    sim.run(1500);
    QVERIFY(sim.tcm().forwardGear() < cruisingGear);

    sim.driver().throttlePedal = 0.0;
    sim.driver().brakePedal = 0.6;
    QVERIFY(sim.runUntil([&] { return sim.vehicle().speedKph() < 0.1; }, 15000));
    sim.run(1000);
    QCOMPARE(sim.tcm().forwardGear(), 1);
    QVERIFY(sim.vehicle().engineRunning()); // the torque converter keeps the engine alive
    sim.driver().selector = Selector::R;
    sim.driver().brakePedal = 0.0;
    sim.run(3000);
    QCOMPARE(sim.signal(QStringLiteral("CurrentGear")), 7.0);
    QVERIFY(sim.vehicle().velocityKph() < -1.0); // backwards
    QCOMPARE(sim.vehicle().speedKph(), -sim.vehicle().velocityKph());
    QVERIFY(qAbs(sim.signal(QStringLiteral("VehicleSpeed")) - sim.vehicle().speedKph()) < 1.0);
    for (Ecu *e : sim.ecus())
        QCOMPARE(e->dtcs().storedCount(), 0);
}

void tst_Vehicle::warmUp()
{
    Simulation sim;
    sim.driver().ignition = Ignition::On;
    sim.requestEngineStart();
    sim.run(3000);
    QVERIFY(sim.signal(QStringLiteral("CoolantTemp")) < 30.0);
    sim.run(150'000);
    QVERIFY(sim.signal(QStringLiteral("CoolantTemp")) > 80.0);
    QVERIFY(sim.signal(QStringLiteral("TransTemp")) > 50.0);
    QVERIFY(!sim.ipc().state().coolantWarn);
    QCOMPARE(sim.ecm().dtcs().storedCount(), 0); // a healthy sensor moves during warm-up
}

void tst_Vehicle::noStartWithoutCrankSignal()
{
    Simulation sim;
    sim.driver().ignition = Ignition::On;
    QVERIFY(sim.injectFault(QStringLiteral("ckp"), {FaultType::OpenCircuit}));
    sim.run(500);
    QCOMPARE(sim.ecm().dtcs().storedCount(), 0); // nothing to detect while the engine stands still
    sim.requestEngineStart();
    sim.run(4000);
    // The starter turns the engine, but the ECM sees no speed signal and does not inject.
    QVERIFY(!sim.vehicle().engineRunning());
    QVERIFY(sim.ecm().dtcs().status(dtc::P0335) & DtcStatus::Confirmed);
    QVERIFY(sim.ipc().state().mil);

    sim.clearFault(QStringLiteral("ckp"));
    sim.requestEngineStart();
    QVERIFY(sim.runUntil([&] { return sim.vehicle().engineRunning(); }, 3000));
}

void tst_Vehicle::stallWhenCrankSignalIsLost()
{
    Simulation sim;
    startEngine(sim);
    driveTo(sim, 40.0);
    QVERIFY(sim.injectFault(QStringLiteral("ckp"), {FaultType::OpenCircuit}));
    sim.run(1000);
    QVERIFY(!sim.vehicle().engineRunning());
    QVERIFY(!sim.ecm().engineRunning());
    QVERIFY(sim.ecm().dtcs().status(dtc::P0335) & DtcStatus::Confirmed);
    QCOMPARE(sim.signal(QStringLiteral("EngineRunning")), 0.0);
    QVERIFY(sim.ipc().state().battery); // alternator no longer turning
}

void tst_Vehicle::throttleLimpHome()
{
    Simulation sim;
    startEngine(sim);
    sim.driver().selector = Selector::D;
    sim.driver().throttlePedal = 0.8;
    sim.run(3000);
    QVERIFY(sim.vehicle().throttlePos() > 0.75);

    QVERIFY(sim.injectFault(QStringLiteral("tps"), {FaultType::OpenCircuit}));
    sim.run(1000);
    QVERIFY(sim.ecm().dtcs().status(dtc::P0122) & DtcStatus::Confirmed);
    QVERIFY(sim.ecm().limp());
    QVERIFY(sim.vehicle().throttlePos() < 0.26); // opening limited although the pedal is at 80 %
    QCOMPARE(sim.signal(QStringLiteral("EngineLimp")), 1.0);
    QVERIFY(qAbs(sim.signal(QStringLiteral("PedalPos")) - 80.0) < 0.5);
    QVERIFY(qAbs(sim.signal(QStringLiteral("ThrottlePos")) - 25.0) < 1.0); // substitute value

    sim.clearFault(QStringLiteral("tps"));
    QVERIFY(sim.injectFault(QStringLiteral("tps"), {FaultType::ShortToBattery}));
    sim.run(1000);
    QVERIFY(sim.ecm().dtcs().status(dtc::P0123) & DtcStatus::Confirmed);

    // A sensor stuck in range needs the plausibility monitor (two trips -> only pending).
    sim.clearFault(QStringLiteral("tps"));
    sim.run(1000);
    QVERIFY(!sim.ecm().limp());
    QVERIFY(sim.injectFault(QStringLiteral("tps"), {FaultType::Stuck, 5.0, 0.0, true}));
    sim.run(2000);
    QCOMPARE(sim.ecm().dtcs().status(dtc::P0121) & (DtcStatus::Pending | DtcStatus::Confirmed),
        int(DtcStatus::Pending));
}

void tst_Vehicle::transmissionTempSensor()
{
    Simulation sim;
    startEngine(sim);
    QVERIFY(qAbs(sim.signal(QStringLiteral("TransTemp")) - 75.0) < 3.0);
    QVERIFY(sim.injectFault(QStringLiteral("tft"), {FaultType::ShortToGround}));
    sim.run(1500);
    QVERIFY(sim.tcm().dtcs().status(dtc::P0712) & DtcStatus::Confirmed);
    QCOMPARE(sim.signal(QStringLiteral("TransTemp")), 80.0); // substitute value, not 150 degC
    QVERIFY(!sim.tcm().limp());
    // The TCM cannot light the MIL itself: it asks the ECM, which stores P0700.
    QCOMPARE(sim.signal(QStringLiteral("TcmMilReq")), 1.0);
    QVERIFY(sim.ecm().dtcs().status(dtc::P0700) & DtcStatus::Confirmed);
    QVERIFY(sim.ipc().state().mil);

    sim.clearFault(QStringLiteral("tft"));
    QVERIFY(sim.injectFault(QStringLiteral("tft"), {FaultType::OpenCircuit}));
    sim.run(1500);
    QVERIFY(sim.tcm().dtcs().status(dtc::P0713) & DtcStatus::Confirmed);
}

void tst_Vehicle::overheating()
{
    Simulation sim;
    startEngine(sim);
    QVERIFY(sim.injectFault(QStringLiteral("cooling"), {FaultType::Failure}));
    QVERIFY(sim.runUntil([&] { return sim.ipc().state().coolantWarn; }, 120'000));
    QVERIFY(sim.signal(QStringLiteral("CoolantTemp")) >= 112.0);
    QVERIFY(sim.runUntil([&] { return sim.ecm().dtcs().status(dtc::P0217) & DtcStatus::Confirmed; }, 60'000));
}

void tst_Vehicle::plausibilityCatchesWrongWheelSpeed()
{
    Simulation sim;
    startEngine(sim);
    driveTo(sim, 60.0);
    // Electrically fine, but reads half the real speed (e.g. wrong tone ring).
    QVERIFY(sim.injectFault(QStringLiteral("wss_rr"), {FaultType::Gain, 0.5, 0.0, true}));
    sim.run(1500);
    const quint32 code = dtc::wheel(3, dtc::FtbSignalInvalid);
    QVERIFY(sim.abs().dtcs().status(code) & DtcStatus::Confirmed);
    QVERIFY(!(sim.abs().dtcs().status(dtc::wheel(3, dtc::FtbOpen)) & DtcStatus::Pending));
    QVERIFY(!sim.abs().wheelValid(3));
    QVERIFY(sim.abs().wheelValid(0));
    // The implausible wheel is excluded from the vehicle speed.
    QVERIFY(qAbs(sim.signal(QStringLiteral("VehicleSpeed")) - sim.vehicle().speedKph()) < 1.0);
    QVERIFY(sim.ipc().state().abs);

    // Three failed sensors leave too little for a valid speed: the TCM switches to its estimate.
    QVERIFY(sim.injectFault(QStringLiteral("wss_fl"), {FaultType::OpenCircuit}));
    QVERIFY(sim.injectFault(QStringLiteral("wss_fr"), {FaultType::ShortToGround}));
    sim.run(1500);
    QCOMPARE(sim.signal(QStringLiteral("VehicleSpeedValid")), 0.0);
    QVERIFY(sim.tcm().usingSpeedFallback());
    QVERIFY(sim.tcm().dtcs().status(dtc::P0500) & DtcStatus::Confirmed);
    QVERIFY(sim.abs().dtcs().status(dtc::wheel(1, dtc::FtbShortGround)) & DtcStatus::Confirmed);
    QCOMPARE(sim.activeFaults().size(), 3);
    sim.clearAllFaults();
    QVERIFY(sim.activeFaults().isEmpty());
}

void tst_Vehicle::bulbCheckAndBodyFunctions()
{
    Simulation sim;
    sim.driver().ignition = Ignition::On;
    sim.run(1500);
    const ClusterState &c = sim.ipc().state();
    QVERIFY(c.mil && c.abs && c.tcs && c.battery && c.coolantWarn && c.transFault); // bulb check
    sim.run(Ipc::kBulbCheckMs);
    QVERIFY(!c.mil && !c.abs && !c.tcs && !c.coolantWarn && !c.transFault);

    sim.driver().headlamps = 2;
    sim.driver().highBeam = true;
    sim.driver().doorAjar[2] = true;
    sim.driver().doorLock = true;
    sim.driver().hazard = true;
    bool leftOn = false, leftOff = false, both = true;
    for (int i = 0; i < 2000; ++i) {
        sim.step();
        leftOn = leftOn || c.turnLeft;
        leftOff = leftOff || !c.turnLeft;
        both = both && c.turnLeft == c.turnRight;
    }
    QVERIFY(leftOn && leftOff && both); // hazard lights blink on both sides
    QVERIFY(c.lowBeam && c.highBeam && c.doorAjar);
    QVERIFY(sim.bcm().doorsLocked());
    QCOMPARE(sim.signal(QStringLiteral("DoorAjar_RL")), 1.0);
    QCOMPARE(sim.signal(QStringLiteral("DoorAjar_FL")), 0.0);
    // Lamp and door states as a UDS data identifier: low + high + park + left + right
    QCOMPARE(ask(sim, DiagId::BcmRequest, "22 40 02"), hex("62 40 02 1F"));
    QCOMPARE(ask(sim, DiagId::BcmRequest, "22 40 03"), hex("62 40 03 84"));
}

void tst_Vehicle::lampCircuitFault()
{
    Simulation sim;
    sim.driver().ignition = Ignition::On;
    QVERIFY(sim.injectFault(QStringLiteral("bulb_low_left"), {FaultType::Failure}));
    sim.run(4000);
    // The BCM can only see the open circuit while it drives the lamp.
    QCOMPARE(sim.bcm().dtcs().storedCount(), 0);
    sim.driver().headlamps = 2;
    sim.run(1500);
    QVERIFY(sim.bcm().dtcs().status(dtc::B1601) & DtcStatus::Confirmed);
    QVERIFY(sim.ipc().state().lampFault);
}

void tst_Vehicle::reverseWhileRollingForwardBrakes()
{
    Simulation sim;
    startEngine(sim);
    driveTo(sim, 60.0);
    sim.driver().throttlePedal = 0.5;
    sim.driver().selector = Selector::R; // abuse: reverse at speed
    const double before = sim.vehicle().velocityKph();
    sim.run(2000);
    QVERIFY2(sim.vehicle().velocityKph() < before - 5.0, qPrintable(QString::number(sim.vehicle().velocityKph())));
    // The converter slips: the engine is not dragged up to the limiter by the wheels.
    QVERIFY2(sim.vehicle().engineRpm() < 4000.0, qPrintable(QString::number(sim.vehicle().engineRpm())));
}

void tst_Vehicle::deterministic()
{
    // Same inputs, same trace - byte for byte.
    auto runOnce = [] {
        Simulation sim;
        sim.driver().ignition = Ignition::On;
        sim.requestEngineStart();
        sim.run(3000);
        sim.driver().selector = Selector::D;
        sim.driver().throttlePedal = 0.6;
        sim.injectFault(QStringLiteral("wss_fl"), {FaultType::Noise, 3.0, 0.0, true});
        sim.tester().request(DiagId::Functional, hex("09 02"));
        // Several keep-alive targets: their order must not depend on hashing.
        for (quint32 id : {DiagId::BcmRequest, DiagId::EcmRequest, DiagId::AbsRequest, DiagId::TcmRequest})
            sim.tester().setKeepAlive(id, true);
        sim.run(5000);
        QStringList lines;
        for (const BusFrame &f : sim.trace().frames())
            lines << TraceRecorder::candumpLine(f);
        return lines;
    };
    const QStringList first = runOnce();
    const QStringList second = runOnce();
    QVERIFY(first.size() > 2000);
    QCOMPARE(first, second);
    // TesterPresent goes out in ascending ID order.
    QStringList testerPresent;
    for (const QString &line : first)
        if (line.contains(QStringLiteral("#023E80")))
            testerPresent << line.section(u' ', 2).left(3);
    QVERIFY(testerPresent.size() >= 4);
    QCOMPARE(testerPresent.mid(0, 4), QStringList({"7E0", "7E1", "7E2", "7E3"}));
}

// Idle burns about a litre an hour, load much more; closed throttle in gear cuts the injectors.
void tst_Vehicle::fuelConsumptionAndOverrunCut()
{
    Simulation sim;
    QCOMPARE(sim.vehicle().fuelLevel(), VehicleModel::kStartFuelLitres);
    startEngine(sim);
    sim.run(2000);
    const double idle = sim.vehicle().fuelRateLph();
    QVERIFY2(idle > 0.7 && idle < 1.5, qPrintable(QString::number(idle)));

    sim.driver().selector = Selector::D;
    sim.driver().throttlePedal = 1.0;
    sim.run(3000);
    const double full = sim.vehicle().fuelRateLph();
    QVERIFY2(full > 15.0, qPrintable(QString::number(full)));

    driveTo(sim, 90.0, 0.8);
    sim.driver().throttlePedal = 0.0;
    sim.run(500);
    QCOMPARE(sim.vehicle().fuelRateLph(), 0.0); // overrun fuel cut-off

    // The ECM forwards it; the cluster turns it into l/100 km and a range.
    sim.driver().throttlePedal = 0.2;
    sim.run(3000);
    QVERIFY(std::abs(sim.signal(QStringLiteral("FuelRate")) - sim.vehicle().fuelRateLph()) < 0.5);
    const ClusterState &c = sim.ipc().state();
    QVERIFY(c.consumption > 2.0 && c.consumption < 30.0);
    QVERIFY(c.rangeKm > 100.0);
    QVERIFY(std::abs(c.fuelLevel - sim.vehicle().fuelLevel()) < 0.5);
    QVERIFY(!c.lowFuel);
    QVERIFY(sim.vehicle().fuelLevel() < VehicleModel::kStartFuelLitres);
}

// An empty tank stops the engine. It cranks, but does not start - and no DTC explains why.
void tst_Vehicle::runningOutOfFuelSetsNoDtc()
{
    Simulation sim;
    startEngine(sim);
    sim.vehicle().setFuelLevel(0.02);
    sim.driver().selector = Selector::D;
    sim.driver().throttlePedal = 1.0;
    QVERIFY(sim.runUntil([&] { return !sim.vehicle().engineRunning(); }, 20000));
    sim.driver().throttlePedal = 0.0;
    sim.driver().selector = Selector::P;
    sim.run(2000);
    QVERIFY(!sim.ecm().engineRunning());
    QCOMPARE(sim.ecm().dtcs().storedCount(), 0); // a stall is not a crankshaft sensor fault

    sim.requestEngineStart();
    QVERIFY(sim.runUntil([&] { return sim.vehicle().starterEngaged(); }, 500));
    sim.run(3500);
    QVERIFY(!sim.vehicle().engineRunning());
    QCOMPARE(sim.ecm().dtcs().storedCount(), 0);
    QVERIFY(sim.ipc().state().lowFuel);

    sim.vehicle().setFuelLevel(20.0);
    sim.requestEngineStart();
    QVERIFY(sim.runUntil([&] { return sim.vehicle().engineRunning(); }, 5000));
}

// Open circuit reads empty (P0463), short to ground reads full (P0462); the gauge drops to empty.
void tst_Vehicle::fuelSenderFaults()
{
    Simulation sim;
    startEngine(sim);
    QVERIFY(sim.ecm().fuelLevelValid());
    QVERIFY(std::abs(sim.ecm().fuelLevel() - VehicleModel::kStartFuelLitres) < 0.5);

    QVERIFY(sim.injectFault(QStringLiteral("fls"), {FaultType::OpenCircuit}));
    sim.run(2500);
    QVERIFY(sim.ecm().dtcs().status(dtc::P0463) & DtcStatus::Confirmed);
    QVERIFY(!sim.ecm().fuelLevelValid());
    QCOMPARE(sim.signal(QStringLiteral("FuelLevelValid")), 0.0);
    QVERIFY(sim.ipc().state().lowFuel);
    QCOMPARE(sim.ipc().state().fuelLevel, 0.0);

    sim.clearFault(QStringLiteral("fls"));
    QVERIFY(sim.injectFault(QStringLiteral("fls"), {FaultType::ShortToGround}));
    sim.run(2500);
    QVERIFY(sim.ecm().dtcs().status(dtc::P0462) & DtcStatus::Confirmed);
    QVERIFY(sim.ecm().milOn()); // emission relevant: the level is needed for the EVAP monitor
}

// Pressure rises with engine speed; a failing pump sets P0524, a dead sensor P0522 - both light OIL.
void tst_Vehicle::oilPressure()
{
    Simulation sim;
    sim.driver().ignition = Ignition::On;
    sim.run(3500);
    QVERIFY(sim.ipc().state().oilWarn); // engine stopped: no pressure, lamp on
    startEngine(sim);
    const double idle = sim.vehicle().oilPressureBar();
    QVERIFY2(idle > 1.0 && idle < 2.0, qPrintable(QString::number(idle)));
    QVERIFY(!sim.ipc().state().oilWarn);
    sim.driver().throttlePedal = 0.6;
    sim.run(2000);
    QVERIFY(sim.vehicle().oilPressureBar() > idle + 1.0);
    sim.driver().throttlePedal = 0.0;
    sim.run(2000);

    QVERIFY(sim.injectFault(QStringLiteral("oil_pump"), {FaultType::Failure}));
    sim.run(3000);
    QVERIFY(sim.vehicle().oilPressureBar() < 0.3);
    QVERIFY(sim.ecm().dtcs().status(dtc::P0524) & DtcStatus::Confirmed);
    QVERIFY(sim.ipc().state().oilWarn);
    QVERIFY(!sim.ecm().milOn()); // a warning lamp, not an emission fault

    sim.clearFault(QStringLiteral("oil_pump"));
    sim.run(3000);
    QVERIFY(!sim.ipc().state().oilWarn);
    QVERIFY(sim.injectFault(QStringLiteral("eop"), {FaultType::OpenCircuit}));
    sim.run(2500);
    QVERIFY(sim.ecm().dtcs().status(dtc::P0522) & DtcStatus::Confirmed);
    QVERIFY(sim.ipc().state().oilWarn);                  // fail-safe
    QVERIFY(sim.vehicle().oilPressureBar() > 1.0);       // although the pressure is fine
}

void tst_Vehicle::parkBrake()
{
    Simulation sim;
    startEngine(sim);
    QVERIFY(!sim.ipc().state().parkBrake);
    sim.driver().parkBrake = true;
    sim.run(300);
    QCOMPARE(sim.signal(QStringLiteral("ParkBrake")), 1.0);
    QVERIFY(sim.ipc().state().parkBrake);

    // It holds the car against the creep of the converter and a light throttle ...
    sim.driver().selector = Selector::D;
    sim.driver().throttlePedal = 0.25;
    sim.run(3000);
    QCOMPARE(sim.vehicle().speedKph(), 0.0);
    // ... but not against a lot of throttle: the car drags itself off, slowly, with a warning.
    sim.driver().throttlePedal = 0.7;
    const qint64 braked = sim.nowMs();
    QVERIFY(sim.runUntil([&] { return sim.vehicle().speedKph() > 12.0; }, 20000));
    const qint64 brakedMs = sim.nowMs() - braked;
    sim.run(300);
    QCOMPARE(sim.signal(QStringLiteral("ParkBrakeWarn")), 1.0);

    // Released: much quicker.
    Simulation free;
    startEngine(free);
    free.driver().selector = Selector::D;
    free.driver().throttlePedal = 0.7;
    const qint64 released = free.nowMs();
    QVERIFY(free.runUntil([&] { return free.vehicle().speedKph() > 12.0; }, 20000));
    QVERIFY2((free.nowMs() - released) * 2 < brakedMs,
        qPrintable(QStringLiteral("%1 ms vs %2 ms").arg(free.nowMs() - released).arg(brakedMs)));

    // A stuck switch keeps the lamp on after the lever is released; the BCM input shows it.
    sim.driver().parkBrake = false;
    QVERIFY(sim.injectFault(QStringLiteral("park_brake_switch"), {FaultType::Failure}));
    sim.run(300);
    QVERIFY(sim.ipc().state().parkBrake || sim.ipc().state().parkBrakeWarn);
    QVERIFY(!sim.vehicle().parkBrakeApplied());
    QCOMPARE(ask(sim, DiagId::BcmRequest, "22 40 04"), hex("62 40 04 01"));
    QCOMPARE(sim.bcm().dtcs().storedCount(), 0);
}

// Without the pack nothing changes; with it the ECM meters the fuel and holds lambda 1 by switching.
void tst_Vehicle::engineObdPackClosedLoop()
{
    Simulation base;
    QVERIFY(!base.sensors().byId(QStringLiteral("maf")));
    QVERIFY(!base.faultTarget(QStringLiteral("ignition_coils")));
    QVERIFY(!base.ecm().hasObdPack());
    startEngine(base);
    QVERIFY(!base.hasSignal(QStringLiteral("AirMassFlow")));
    QVERIFY(!base.vehicle().mixtureControl);

    Simulation sim(obdPack());
    QVERIFY(sim.sensors().byId(QStringLiteral("o2")));
    QVERIFY(sim.faultTarget(QStringLiteral("intake_leak")));
    startEngine(sim);
    sim.run(6000);
    QVERIFY(sim.ecm().closedLoop());
    QVERIFY(std::abs(sim.vehicle().lambda() - 1.0) < 0.05);
    double lo = 1.0, hi = 0.0;
    for (int i = 0; i < 3000; ++i) {
        sim.step();
        lo = std::min(lo, sim.sensors().o2.voltage());
        hi = std::max(hi, sim.sensors().o2.voltage());
    }
    QVERIFY2(lo < 0.3 && hi > 0.6, qPrintable(QStringLiteral("%1..%2 V").arg(lo).arg(hi)));
    QVERIFY(std::abs(sim.ecm().shortTermTrim() + sim.ecm().longTermTrim()) < 0.06);
    QVERIFY(std::abs(sim.signal(QStringLiteral("AirMassFlow")) - sim.vehicle().airMassGps()) < 0.3);
    QCOMPARE(sim.ecm().dtcs().storedCount(), 0);

    // PIDs 06/07/10/14 are announced and answered.
    const QByteArray mask = ask(sim, DiagId::EcmRequest, "01 00");
    QCOMPARE(mask.size(), 6);
    QVERIFY(static_cast<quint8>(mask[2]) & 0x06); // PID 06 and 07: bits 2 and 1 of byte A
    QVERIFY(ask(sim, DiagId::EcmRequest, "01 10").startsWith(hex("41 10")));
    QVERIFY(ask(sim, DiagId::EcmRequest, "01 14").startsWith(hex("41 14")));
}

// Unmetered air: the trims go positive, the long-term value learns it, P0171 after two trips.
void tst_Vehicle::vacuumLeakLeansOutAndLearns()
{
    Simulation sim(obdPack());
    startEngine(sim);
    sim.run(6000);
    QVERIFY(sim.injectFault(QStringLiteral("intake_leak"), {FaultType::Failure}));
    sim.run(30000);
    const double total = sim.ecm().shortTermTrim() + sim.ecm().longTermTrim();
    QVERIFY2(total > 0.23, qPrintable(QString::number(total)));
    QVERIFY(sim.ecm().longTermTrim() > 0.1);
    QVERIFY(sim.ecm().dtcs().status(dtc::P0171) & DtcStatus::Pending);
    QVERIFY(!(sim.ecm().dtcs().status(dtc::P0171) & DtcStatus::Confirmed));

    ignitionCycle(sim);
    startEngine(sim);
    sim.run(15000);
    QVERIFY(sim.ecm().dtcs().status(dtc::P0171) & DtcStatus::Confirmed);
    QVERIFY(sim.ecm().milOn());

    // Fixed hose, but the learned value now makes the mixture rich until it is reset.
    sim.clearFault(QStringLiteral("intake_leak"));
    sim.run(3000);
    QVERIFY(sim.ecm().shortTermTrim() < 0.0);
    QCOMPARE(ask(sim, DiagId::EcmRequest, "10 03").left(2), hex("50 03"));
    QCOMPARE(ask(sim, DiagId::EcmRequest, "31 01 02 02"), hex("71 01 02 02 02"));
    QCOMPARE(sim.ecm().longTermTrim(), 0.0);
    sim.run(5000);
    QVERIFY(std::abs(sim.ecm().shortTermTrim() + sim.ecm().longTermTrim()) < 0.06);

    // Without the pack the routine does not exist.
    Simulation base;
    base.driver().ignition = Ignition::On;
    base.run(1500);
    ask(base, DiagId::EcmRequest, "10 03");
    QCOMPARE(ask(base, DiagId::EcmRequest, "31 01 02 02"), hex("7F 31 31"));
}

void tst_Vehicle::misfireFlashesTheMil()
{
    Simulation sim(obdPack());
    startEngine(sim);
    Fault misfire{FaultType::Misfire, 2.0, 0.0, true};
    QVERIFY(sim.injectFault(QStringLiteral("ignition_coils"), misfire));
    QCOMPARE(sim.vehicle().misfireCylinder, 2);
    sim.driver().throttlePedal = 0.3; // in P: revs up, the 200-revolution window fills quickly
    sim.run(10000);
    QVERIFY(sim.ecm().dtcs().status(dtc::misfireDtc(2)) & DtcStatus::Confirmed);
    QVERIFY(!(sim.ecm().dtcs().status(dtc::misfireDtc(3)) & DtcStatus::TestFailed));
    QVERIFY(!(sim.ecm().dtcs().status(dtc::P0300) & DtcStatus::TestFailed));
    QVERIFY(sim.ecm().milFlashing());
    QCOMPARE(sim.signal(QStringLiteral("MilFlash")), 1.0);
    QCOMPARE(sim.signal(QStringLiteral("MisfireCyl")), 2.0); // bit 1 = cylinder 2
    QVERIFY(sim.ipc().state().milFlash);
    const QByteArray counters = ask(sim, DiagId::EcmRequest, "22 10 10");
    QCOMPARE(counters.size(), 7);
    QVERIFY(static_cast<quint8>(counters[4]) > 50 && counters[3] == 0);

    // Freeze frame: Mode 02 PID 02 names the DTC that stored it.
    QVERIFY(ask(sim, DiagId::EcmRequest, "02 02 00").contains(hex("03 02")));

    sim.clearFault(QStringLiteral("ignition_coils"));
    sim.run(10000);
    QVERIFY(!sim.ecm().milFlashing());
    QVERIFY(sim.ecm().milOn()); // confirmed: steady until cleared
}

void tst_Vehicle::oxygenAndAirFlowSensorFaults()
{
    Simulation sim(obdPack());
    startEngine(sim);
    sim.run(5000);

    // Unplugged O2 sensor: the input sits at the bias voltage, never switches.
    QVERIFY(sim.injectFault(QStringLiteral("o2"), {FaultType::OpenCircuit}));
    QVERIFY(sim.runUntil([&] { return sim.ecm().dtcs().testFailed(dtc::P0134); }, 25000));
    QVERIFY(!sim.ecm().closedLoop());
    QCOMPARE(sim.ecm().shortTermTrim(), 0.0);
    QVERIFY(std::abs(sim.ecm().longTermTrim()) < 0.05); // the trims did not run away meanwhile
    sim.clearFault(QStringLiteral("o2"));

    // Shorted to ground: circuit low.
    QVERIFY(sim.injectFault(QStringLiteral("o2"), {FaultType::ShortToGround}));
    QVERIFY(sim.runUntil([&] { return sim.ecm().dtcs().testFailed(dtc::P0131); }, 10000));
    sim.clearFault(QStringLiteral("o2"));

    // A dirty air flow meter reads 40 % low: the speed-density check catches it.
    Fault dirty{FaultType::Gain, 0.6, 0.0, true};
    QVERIFY(sim.injectFault(QStringLiteral("maf"), dirty));
    QVERIFY(sim.runUntil([&] { return sim.ecm().dtcs().testFailed(dtc::P0101); }, 10000));
    // ... and the ECM switches to the model, so the mixture is right again.
    sim.run(8000);
    QVERIFY(std::abs(sim.vehicle().lambda() - 1.0) < 0.06);

    // Unplugged: circuit low within a second.
    QVERIFY(sim.injectFault(QStringLiteral("maf"), {FaultType::OpenCircuit}));
    QVERIFY(sim.runUntil([&] { return sim.ecm().dtcs().testFailed(dtc::P0102); }, 2000));
    QVERIFY(sim.vehicle().engineRunning());
}

// Tyres exist on every car: warm tyres read higher, and a soft one turns faster - nobody tells the
// driver without TPMS.
void tst_Vehicle::tyrePhysicsWithoutTpms()
{
    Simulation sim;
    QVERIFY(!sim.faultTarget(QStringLiteral("tpms_fl")));
    QVERIFY(sim.faultTarget(QStringLiteral("tyres")));
    startEngine(sim);
    QCOMPARE(sim.vehicle().tyrePressure(0), VehicleModel::kPlacardBar);
    driveTo(sim, 100.0, 0.8);
    sim.run(60000);
    QVERIFY(sim.vehicle().tyreTemp(0) > 21.0); // flexing warms them (slowly: minutes)
    QVERIFY(sim.vehicle().tyrePressure(0) > VehicleModel::kPlacardBar); // warm
    QCOMPARE(sim.vehicle().tyrePressureCold(0), VehicleModel::kPlacardBar);

    Fault leak{FaultType::Leak, 2.0, 0.0, true}; // front right
    QVERIFY(sim.injectFault(QStringLiteral("tyres"), leak));
    sim.run(80000);
    QVERIFY(sim.vehicle().tyrePressureCold(1) < 1.7);
    QCOMPARE(sim.vehicle().tyrePressureCold(0), VehicleModel::kPlacardBar);
    // Indirect: the soft tyre's wheel turns faster - well inside the ABS plausibility window.
    QVERIFY(sim.vehicle().wheelSpeedKph(1) > sim.vehicle().wheelSpeedKph(0) * 1.005);
    QCOMPARE(sim.abs().dtcs().storedCount(), 0);
    QVERIFY(!sim.ipc().state().tpms);
    QVERIFY(!sim.hasSignal(QStringLiteral("TpmsLampReq")));

    sim.clearFault(QStringLiteral("tyres"));
    QCOMPARE(sim.vehicle().tyrePressureCold(1), VehicleModel::kPlacardBar);
}

void tst_Vehicle::tpmsLowPressureWarning()
{
    Simulation sim(withTpms());
    QCOMPARE(sim.bcm().coding(), quint8(Bcm::LampMonitoring | Bcm::Tpms));
    startEngine(sim);
    sim.run(2000);
    QCOMPARE(sim.signal(QStringLiteral("TpmsSensorValid")), 15.0);
    QCOMPARE(sim.signal(QStringLiteral("TpmsLampReq")), 0.0);
    QVERIFY(!sim.ipc().state().tpms);
    QVERIFY(std::abs(sim.signal(QStringLiteral("TirePressure_RL")) - 2.4) < 0.05);
    QCOMPARE(ask(sim, DiagId::BcmRequest, "22 40 22"), hex("62 40 22 78")); // 120 x 0.02 bar

    sim.vehicle().setTyrePressureCold(2, 1.7); // a night with a nail in the tyre
    sim.run(2000);
    QCOMPARE(sim.signal(QStringLiteral("TpmsLampReq")), 1.0);
    QVERIFY(sim.ipc().state().tpms);
    QVERIFY(!sim.ipc().state().tpmsFlash); // low pressure: steady
    QCOMPARE(sim.bcm().dtcs().storedCount(), 0); // low pressure is a warning, not a fault

    // Switched off by coding: no lamp, no monitoring.
    QCOMPARE(unlockAndAsk(sim, DiagId::BcmRequest, "2E 01 00 01"), hex("6E 01 00"));
    sim.run(1000);
    QCOMPARE(sim.signal(QStringLiteral("TpmsLampReq")), 0.0);
    // ... and a car without TPMS refuses the bit.
    Simulation base;
    base.driver().ignition = Ignition::On;
    base.run(1500);
    QCOMPARE(unlockAndAsk(base, DiagId::BcmRequest, "2E 01 00 09"), hex("7F 2E 31"));
}

void tst_Vehicle::tpmsReplacedSensorAndRelearn()
{
    Simulation sim(withTpms());
    startEngine(sim);
    QVERIFY(sim.injectFault(QStringLiteral("tpms_fr"), {FaultType::Unconfigured}));
    sim.run(11000);
    QVERIFY(sim.bcm().dtcs().testFailed(dtc::tpmsSensor(1)));
    QCOMPARE(sim.signal(QStringLiteral("TpmsLampReq")), 2.0);
    // The new sensor is heard, but belongs to no wheel.
    const QByteArray unknown = ask(sim, DiagId::BcmRequest, "22 03 14");
    QCOMPARE(unknown.left(4), hex("62 03 14 01"));
    QCOMPARE(unknown.mid(4), hex("1A 5C 4B 22"));

    // FMVSS 138: flashing for a minute after ignition on, then steady.
    ignitionCycle(sim);
    sim.run(5000);
    QVERIFY(sim.ipc().state().tpmsFlash);
    sim.run(60000);
    QVERIFY(!sim.ipc().state().tpmsFlash);
    QVERIFY(sim.ipc().state().tpms);

    QCOMPARE(ask(sim, DiagId::BcmRequest, "10 03").left(2), hex("50 03"));
    QCOMPARE(ask(sim, DiagId::BcmRequest, "31 01 03 01"), hex("71 01 03 01 01 00"));
    sim.run(2000);
    QCOMPARE(ask(sim, DiagId::BcmRequest, "31 03 03 01"), hex("71 03 03 01 02 0F"));
    QCOMPARE(sim.bcm().tpmsLearnedId(1), 0x1A5C4B22u);
    QVERIFY(sim.activeFaults().isEmpty()); // the new sensor is configured
    sim.run(1000);
    QCOMPARE(sim.signal(QStringLiteral("TpmsSensorValid")), 15.0);
    QVERIFY(!sim.bcm().dtcs().testFailed(dtc::tpmsSensor(1)));

    // Not while driving.
    startEngine(sim); // the ignition cycle above stopped it
    driveTo(sim, 20.0);
    QCOMPARE(ask(sim, DiagId::BcmRequest, "10 03").left(2), hex("50 03"));
    QCOMPARE(ask(sim, DiagId::BcmRequest, "31 01 03 01"), hex("7F 31 22"));
}

void tst_Vehicle::tpmsDeadSensorAndManualId()
{
    Simulation sim(withTpms());
    startEngine(sim);
    QVERIFY(sim.injectFault(QStringLiteral("tpms_rl"), {FaultType::Failure}));
    sim.run(11000);
    QVERIFY(sim.bcm().dtcs().testFailed(dtc::tpmsSensor(2)));
    QCOMPARE(ask(sim, DiagId::BcmRequest, "22 03 14"), hex("62 03 14 00")); // nothing unknown: silent

    // A relearn cannot find the silent sensor: failed, FL FR RR learned.
    QCOMPARE(ask(sim, DiagId::BcmRequest, "10 03").left(2), hex("50 03"));
    QCOMPARE(ask(sim, DiagId::BcmRequest, "31 01 03 01").left(5), hex("71 01 03 01 01"));
    sim.run(Bcm::kTpmsLearnMs + 1000);
    QCOMPARE(ask(sim, DiagId::BcmRequest, "10 03").left(2), hex("50 03")); // the session timed out meanwhile
    QCOMPARE(ask(sim, DiagId::BcmRequest, "31 03 03 01"), hex("71 03 03 01 04 0B"));

    // New sensor fitted; its ID typed in from the box instead of a relearn.
    sim.clearFault(QStringLiteral("tpms_rl"));
    QVERIFY(sim.injectFault(QStringLiteral("tpms_rl"), {FaultType::Unconfigured}));
    QCOMPARE(unlockAndAsk(sim, DiagId::BcmRequest, "2E 03 12 00 00 00 00"), hex("7F 2E 31"));
    QCOMPARE(ask(sim, DiagId::BcmRequest, "2E 03 12 1A 5C 4B 33"), hex("6E 03 12"));
    sim.run(2000);
    QVERIFY(sim.activeFaults().isEmpty());
    QCOMPARE(sim.signal(QStringLiteral("TpmsSensorValid")), 15.0);

    // Review finding: repairing a later fault on that wheel put the factory sensor back, which the
    // BCM no longer knows. The repair restores the sensor that is configured now.
    QVERIFY(sim.injectFault(QStringLiteral("tpms_rl"), {FaultType::Failure}));
    sim.run(500);
    sim.clearFault(QStringLiteral("tpms_rl"));
    sim.run(2000);
    QCOMPARE(sim.signal(QStringLiteral("TpmsSensorValid")), 15.0);
    QCOMPARE(sim.sensors().tpms[2].id, 0x1A5C4B33u);
}

// The equipment list: identifiers, set operations and the lesson-file form.
void tst_Vehicle::vehicleConfig()
{
    const QList<EquipmentInfo> &catalog = VehicleConfig::catalog();
    QCOMPARE(catalog.size(), 4);
    QStringList ids;
    for (const EquipmentInfo &e : catalog) {
        QVERIFY(!e.name.isEmpty() && !e.description.isEmpty() && !e.adds.isEmpty());
        QCOMPARE(VehicleConfig::fromId(e.id), std::optional<Equipment>(e.item));
        QCOMPARE(VehicleConfig::info(e.item)->id, e.id);
        ids << e.id;
    }
    QCOMPARE(ids, QStringList({"engine_obd", "tpms", "srs", "esc"}));
    QVERIFY(!VehicleConfig::fromId(QStringLiteral("turbo")).has_value());
    QCOMPARE(VehicleConfig::fromId(QStringLiteral("TPMS")), std::optional<Equipment>(Equipment::Tpms));

    VehicleConfig a;
    QVERIFY(a.isEmpty());
    a.fit(Equipment::Esc);
    a.fit(Equipment::Tpms);
    QVERIFY(a.has(Equipment::Tpms) && a.has(Equipment::Esc) && !a.has(Equipment::Srs));
    QCOMPARE(a.ids(), QStringList({"tpms", "esc"})); // catalog order, not insertion order
    VehicleConfig b;
    b.fit(Equipment::Srs);
    const VehicleConfig both = a.united(b);
    QCOMPARE(both.ids(), QStringList({"tpms", "srs", "esc"}));
    a.fit(Equipment::Esc, false);
    QCOMPARE(a.ids(), QStringList({"tpms"}));
    QVERIFY(a != both);

    // Only implemented items can be fitted from settings or lesson files; unknown ids are ignored.
    const VehicleConfig loaded = VehicleConfig::fromIds({QStringLiteral("turbo"), QStringLiteral("tpms")});
    QCOMPARE(loaded.has(Equipment::Tpms), VehicleConfig::info(Equipment::Tpms)->implemented);
    QVERIFY(!loaded.ids().contains(QStringLiteral("turbo")));
    QCOMPARE(VehicleConfig::fromJson(QJsonArray{QStringLiteral("tpms")}), loaded);

    // The simulation keeps what it was built with.
    Simulation sim(both);
    QCOMPARE(sim.config(), both);
    QVERIFY(Simulation().config().isEmpty());
}

QTEST_GUILESS_MAIN(tst_Vehicle)
#include "tst_vehicle.moc"
