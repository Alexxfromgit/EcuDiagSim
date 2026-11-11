// OBD-II (SAE J1979) over the complete stack: tester -> ISO-TP -> CAN -> ECU and back.
#include "TestUtil.h"

using namespace ecusim;
using namespace testutil;

namespace {
const DiagResponse *from(const DiagResult &r, quint32 canId)
{
    for (const DiagResponse &rsp : r.responses)
        if (rsp.canId == canId)
            return &rsp;
    return nullptr;
}
quint8 u8(const QByteArray &a, int i)
{
    return static_cast<quint8>(a.at(i));
}
} // namespace

class tst_Obd : public QObject
{
    Q_OBJECT

private slots:
    void onlyEmissionEcusAnswer();
    void supportedPids();
    void pidEncodings();
    void multiplePidsInOneRequest();
    void vehicleInformation();
    void storedCodesAndFreezeFrame();
    void runTimeCountsFromEngineStart();
    void pendingBeforeConfirmed();
    void noAnswerWithIgnitionOff();
    void decodeHelpers();
};

void tst_Obd::onlyEmissionEcusAnswer()
{
    Simulation sim;
    sim.driver().ignition = Ignition::On;
    sim.run(200);
    const DiagResult r = exchange(sim, DiagId::Functional, hex("01 00"));
    QCOMPARE(r.responses.size(), 2);
    QVERIFY(from(r, 0x7E8)); // ECM
    QVERIFY(from(r, 0x7E9)); // TCM
    QCOMPARE(from(r, 0x7E8)->ecu, QStringLiteral("ECM"));

    // ABS and BCM reject OBD services when addressed physically.
    QCOMPARE(ask(sim, DiagId::AbsRequest, "01 00"), hex("7F 01 11"));
    QCOMPARE(ask(sim, DiagId::BcmRequest, "03"), hex("7F 03 11"));
}

void tst_Obd::supportedPids()
{
    Simulation sim;
    sim.driver().ignition = Ignition::On;
    sim.run(200);
    // ECM: 01 04 05 0C 0D 11 1F + "more in the next range"
    QCOMPARE(ask(sim, DiagId::EcmRequest, "01 00"), hex("41 00 98 18 80 03"));
    // 21..40: nothing but the continuation bit; 41..60: PID 42
    QCOMPARE(ask(sim, DiagId::EcmRequest, "01 20"), hex("41 20 00 00 00 01"));
    QCOMPARE(ask(sim, DiagId::EcmRequest, "01 40"), hex("41 40 40 00 00 00"));
    QVERIFY(exchange(sim, DiagId::Functional, hex("01 60")).timedOut());
    QVERIFY(exchange(sim, DiagId::Functional, hex("01 0F")).timedOut()); // unsupported PID
}

void tst_Obd::pidEncodings()
{
    Simulation sim;
    startEngine(sim);
    driveTo(sim, 60.0);
    sim.run(500);

    QByteArray rsp = ask(sim, DiagId::EcmRequest, "01 0C");
    QCOMPARE(rsp.left(2), hex("41 0C"));
    const double rpm = ((u8(rsp, 2) << 8) | u8(rsp, 3)) / 4.0;
    QVERIFY2(qAbs(rpm - sim.vehicle().engineRpm()) < 30.0, qPrintable(QString::number(rpm)));
    QVERIFY(rpm > 900.0);

    rsp = ask(sim, DiagId::EcmRequest, "01 0D");
    QVERIFY(qAbs(u8(rsp, 2) - sim.vehicle().speedKph()) < 2.0);

    rsp = ask(sim, DiagId::EcmRequest, "01 05");
    QVERIFY(qAbs((u8(rsp, 2) - 40) - sim.vehicle().coolantTemp()) < 1.5);

    rsp = ask(sim, DiagId::EcmRequest, "01 11");
    QVERIFY(qAbs(u8(rsp, 2) * 100.0 / 255.0 - sim.vehicle().throttlePos() * 100.0) < 2.0);

    rsp = ask(sim, DiagId::EcmRequest, "01 42");
    QVERIFY(qAbs(((u8(rsp, 2) << 8) | u8(rsp, 3)) / 1000.0 - 14.2) < 0.2);

    rsp = ask(sim, DiagId::EcmRequest, "01 1F");
    const int runtime = (u8(rsp, 2) << 8) | u8(rsp, 3);
    QVERIFY(runtime >= 4 && runtime < 120);

    // Monitor status: MIL off, no DTCs
    QCOMPARE(ask(sim, DiagId::EcmRequest, "01 01"), hex("41 01 00 07 00 00"));
    // The TCM reports the same road speed.
    rsp = ask(sim, DiagId::TcmRequest, "01 0D");
    QVERIFY(qAbs(u8(rsp, 2) - sim.vehicle().speedKph()) < 2.0);
}

void tst_Obd::multiplePidsInOneRequest()
{
    Simulation sim;
    startEngine(sim);
    const QByteArray rsp = ask(sim, DiagId::EcmRequest, "01 0C 0D 05 0F");
    // 0F is unsupported and simply left out; the answer needs two CAN frames.
    QCOMPARE(rsp.size(), 1 + 3 + 2 + 2);
    const QList<DecodedValue> values = parseDataResponse(rsp);
    QCOMPARE(values.size(), 3);
    QCOMPARE(values.at(0).name, QStringLiteral("Engine speed"));
    QVERIFY(values.at(0).number > 600.0 && values.at(0).number < 1000.0);
    QCOMPARE(values.at(1).number, 0.0);
    QCOMPARE(values.at(2).unit, QStringLiteral("°C"));
}

void tst_Obd::vehicleInformation()
{
    Simulation sim;
    sim.driver().ignition = Ignition::On;
    sim.run(200);
    // The VIN does not fit into one frame: first frame, flow control, consecutive frames.
    const quint64 framesBefore = sim.bus().frameCount();
    const DiagResult r = exchange(sim, DiagId::Functional, hex("09 02"));
    QCOMPARE(r.responses.size(), 1);
    QCOMPARE(r.responses.first().data, hex("49 02 01") + QByteArray(Ecu::kVin));
    QVERIFY(sim.bus().frameCount() - framesBefore >= 5);

    QCOMPARE(ask(sim, DiagId::EcmRequest, "09 00"), hex("49 00 50 40 00 00")); // 02, 04, 0A
    const DiagResult calid = exchange(sim, DiagId::Functional, hex("09 04"));
    QCOMPARE(calid.responses.size(), 2);
    QVERIFY(from(calid, 0x7E9)->data.mid(3).startsWith("ECUSIM-TCM"));
    // The VIN is also available through UDS on every ECU.
    QCOMPARE(ask(sim, DiagId::BcmRequest, "22 F1 90"), hex("62 F1 90") + QByteArray(Ecu::kVin));
}

void tst_Obd::storedCodesAndFreezeFrame()
{
    Simulation sim;
    startEngine(sim);
    QCOMPARE(ask(sim, DiagId::EcmRequest, "03"), hex("43 00"));
    QCOMPARE(ask(sim, DiagId::EcmRequest, "02 02 00"), hex("42 02 00 00 00"));
    // Freeze frame support masks list exactly the stored PIDs (02 04 05 0C 0D 11 | 42).
    QCOMPARE(ask(sim, DiagId::EcmRequest, "02 00 00"), hex("42 00 00 58 18 80 01"));
    QCOMPARE(ask(sim, DiagId::EcmRequest, "02 20 00"), hex("42 20 00 00 00 00 01"));
    QCOMPARE(ask(sim, DiagId::EcmRequest, "02 40 00"), hex("42 40 00 40 00 00 00"));
    QVERIFY(exchange(sim, DiagId::EcmRequest, hex("02 60 00")).timedOut());
    QVERIFY(exchange(sim, DiagId::EcmRequest, hex("02 01 00")).timedOut()); // PID 01 is not a freeze frame PID
    QVERIFY(exchange(sim, DiagId::EcmRequest, hex("02 0C 00")).timedOut()); // no freeze frame yet

    sim.driver().throttlePedal = 0.3; // rev the engine in P so the freeze frame is recognisable
    sim.run(2000);
    QVERIFY(sim.injectFault(QStringLiteral("ect"), {FaultType::ShortToGround}));
    sim.run(1000);
    sim.driver().throttlePedal = 0.0;
    sim.run(3000);

    QCOMPARE(ask(sim, DiagId::EcmRequest, "03"), hex("43 01 01 17"));
    QCOMPARE(ask(sim, DiagId::EcmRequest, "07"), hex("47 01 01 17"));
    QCOMPARE(ask(sim, DiagId::EcmRequest, "01 01"), hex("41 01 81 07 00 00")); // MIL on, 1 DTC
    QVERIFY(sim.ipc().state().mil);
    QCOMPARE(ask(sim, DiagId::EcmRequest, "02 02 00"), hex("42 02 00 01 17"));

    // Freeze frame = engine speed when the fault was detected, not the current idle speed.
    QByteArray rsp = ask(sim, DiagId::EcmRequest, "02 0C 00");
    QCOMPARE(rsp.left(3), hex("42 0C 00"));
    const double frozenRpm = ((u8(rsp, 3) << 8) | u8(rsp, 4)) / 4.0;
    QVERIFY2(frozenRpm > 1800.0, qPrintable(QString::number(frozenRpm)));
    QVERIFY(sim.vehicle().engineRpm() < 1000.0);
    // The same record through UDS.
    rsp = ask(sim, DiagId::EcmRequest, "19 04 01 17 00 01");
    const QList<DecodedValue> snapshot = parseSnapshot(rsp);
    QCOMPARE(snapshot.size(), 6);
    QCOMPARE(snapshot.at(2).did, quint16(0xF40C));
    QCOMPARE(snapshot.at(2).number, frozenRpm);

    // Mode 04 with the fault still present: codes come back.
    QCOMPARE(exchange(sim, DiagId::Functional, hex("04")).responses.size(), 2);
    sim.run(1500);
    QCOMPARE(ask(sim, DiagId::EcmRequest, "03"), hex("43 01 01 17"));
    sim.clearFault(QStringLiteral("ect"));
    sim.run(1500);
    QCOMPARE(ask(sim, DiagId::EcmRequest, "04"), hex("44"));
    sim.run(1500);
    QCOMPARE(ask(sim, DiagId::EcmRequest, "03"), hex("43 00"));
    QVERIFY(!sim.ipc().state().mil);
}

void tst_Obd::runTimeCountsFromEngineStart()
{
    Simulation sim;
    startEngine(sim);
    sim.run(20000);
    // Stall by switching off, then restart.
    sim.driver().ignition = Ignition::Off;
    sim.run(300);
    startEngine(sim); // includes 3.5 s of running
    const QByteArray rsp = ask(sim, DiagId::EcmRequest, "01 1F");
    const int runtime = (u8(rsp, 2) << 8) | u8(rsp, 3);
    QVERIFY2(runtime >= 3 && runtime <= 5, qPrintable(QString::number(runtime)));
}

void tst_Obd::pendingBeforeConfirmed()
{
    // P0116 is a two-trip DTC: pending (Mode 07) after the first trip, confirmed with the
    // MIL (Mode 03) only when the fault shows up again in the next ignition cycle.
    Simulation sim;
    startEngine(sim);
    QVERIFY(sim.injectFault(QStringLiteral("ect"), {FaultType::Erratic}));
    sim.run(3000);
    QCOMPARE(ask(sim, DiagId::EcmRequest, "07"), hex("47 01 01 16"));
    QCOMPARE(ask(sim, DiagId::EcmRequest, "03"), hex("43 00"));
    QVERIFY(!sim.ipc().state().mil);

    ignitionCycle(sim);
    sim.run(5000);
    QCOMPARE(ask(sim, DiagId::EcmRequest, "03"), hex("43 01 01 16"));
    QVERIFY(sim.ipc().state().mil);

    // The MIL needs three fault-free trips to go out again; the DTC stays stored.
    sim.clearFault(QStringLiteral("ect"));
    ignitionCycle(sim); // ends the trip in which the fault was still seen
    for (int trip = 1; trip <= 3; ++trip) {
        QVERIFY2(sim.ecm().dtcs().milRequested(), qPrintable(QString::number(trip)));
        sim.run(2000);      // a trip without the fault ...
        ignitionCycle(sim); // ... counts when it ends
    }
    sim.run(4000);
    QVERIFY(!sim.ecm().dtcs().milRequested());
    QVERIFY(!sim.ipc().state().mil);
    QCOMPARE(ask(sim, DiagId::EcmRequest, "03"), hex("43 01 01 16"));
}

void tst_Obd::noAnswerWithIgnitionOff()
{
    Simulation sim;
    sim.run(100);
    QVERIFY(exchange(sim, DiagId::Functional, hex("01 00")).timedOut());
    // The BCM is permanently powered and still talks UDS.
    QCOMPARE(ask(sim, DiagId::BcmRequest, "3E 00"), hex("7E 00"));
}

void tst_Obd::decodeHelpers()
{
    QCOMPARE(decodeDid(0xF40C, hex("1A F8")).number, 1726.0);
    QCOMPARE(decodeDid(0xF405, hex("82")).value, QStringLiteral("90"));
    QCOMPARE(decodeDid(0xF442, hex("37 78")).value, QStringLiteral("14.20"));
    QCOMPARE(decodeDid(0xF401, hex("81 07 00 00")).value, QStringLiteral("MIL ON, 1 confirmed DTC(s)"));
    QCOMPARE(decodeDid(0xF190, QByteArray("ABC  ")).value, QStringLiteral("ABC"));
    QCOMPARE(decodeDid(0x9999, hex("01 02")).value, QStringLiteral("01 02"));

    const QList<DtcInfo> obd = parseObdDtcList(hex("43 02 01 18 C1 21"));
    QCOMPARE(obd.size(), 2);
    QCOMPARE(dtcToString(obd.at(0).code, false), QStringLiteral("P0118"));
    QCOMPARE(dtcToString(obd.at(1).code, false), QStringLiteral("U0121"));
    QVERIFY(!obd.at(1).text.isEmpty());

    const QList<DtcInfo> uds = parseUdsDtcList(hex("59 02 FF 40 35 13 AF 01 18 00 2F"));
    QCOMPARE(uds.size(), 2);
    QCOMPARE(uds.at(0).status, quint8(0xAF));
    QCOMPARE(uds.at(1).text, QStringLiteral("Engine coolant temperature sensor circuit high"));

    QVERIFY(explainResponse(hex("22 99 99"), hex("7F 22 31")).contains(QStringLiteral("requestOutOfRange")));
    QVERIFY(explainResponse(hex("10 03"), hex("50 03 00 32 01 F4")).contains(QStringLiteral("extended")));
    QCOMPARE(explainResponse(hex("3E 00"), {}), QStringLiteral("no response"));
}

QTEST_GUILESS_MAIN(tst_Obd)
#include "tst_obd.moc"
