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
};

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
    QCOMPARE(s.all().size(), 8);
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

QTEST_GUILESS_MAIN(tst_Vehicle)
#include "tst_vehicle.moc"
