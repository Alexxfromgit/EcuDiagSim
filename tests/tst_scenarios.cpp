// End-to-end tests: the complete chain from an injected fault to the scan tool,
// run headless and much faster than real time.
#include "TestUtil.h"

#include "sim/ScenarioRunner.h"

#include <QDir>

using namespace ecusim;
using namespace testutil;

class tst_Scenarios : public QObject
{
    Q_OBJECT

private slots:
    void simulationIsValid();
    void wheelSpeedSensorChain();
    void bundledScenarios_data();
    void bundledScenarios();
    void failingExpectationFailsScenario();
    void endToEndProtection();
    void busShortSilencesEverything();
    void busOffNodeIsReportedByOthers();
    void ecuResetOverUds();
    void ioControlNeedsSessionAndSecurity();
    void faultTargetsAreConsistent();
};

void tst_Scenarios::simulationIsValid()
{
    Simulation sim;
    QVERIFY2(sim.isValid(), qPrintable(sim.errorString()));
    QCOMPARE(sim.dbc().messages().size(), 6);
}

// The verification flow from the project plan, step by step.
void tst_Scenarios::wheelSpeedSensorChain()
{
    Simulation sim;
    startEngine(sim);
    driveTo(sim, 50.0);
    QVERIFY(!sim.ipc().state().abs);
    QCOMPARE(sim.signal(QStringLiteral("WheelSpeed_FL_Valid")), 1.0);

    // Inject: front-left sensor unplugged.
    QVERIFY(sim.injectFault(QStringLiteral("wss_fl"), {FaultType::OpenCircuit}));
    const qint64 injectedAt = sim.nowMs();
    const quint32 code = dtc::wheel(0, dtc::FtbOpen);
    QVERIFY(sim.runUntil([&] { return sim.abs().dtcs().status(code) & DtcStatus::Confirmed; }, 1000));
    const qint64 detectionMs = sim.nowMs() - injectedAt;
    QVERIFY2(detectionMs >= 190 && detectionMs <= 260, qPrintable(QString::number(detectionMs)));

    sim.run(200);
    // 0x0B0 shows FL = 0 and invalid, the other wheels keep reporting.
    QCOMPARE(sim.signal(QStringLiteral("WheelSpeed_FL")), 0.0);
    QCOMPARE(sim.signal(QStringLiteral("WheelSpeed_FL_Valid")), 0.0);
    QCOMPARE(sim.signal(QStringLiteral("WheelSpeed_FR_Valid")), 1.0);
    QVERIFY(sim.signal(QStringLiteral("WheelSpeed_FR")) > 40.0);
    QCOMPARE(sim.signal(QStringLiteral("VehicleSpeedValid")), 1.0);
    // Lamp request on 0x0B1 and the lamp in the cluster.
    QCOMPARE(sim.signal(QStringLiteral("AbsLampReq")), 1.0);
    QVERIFY(sim.ipc().state().abs);
    QVERIFY(sim.ipc().state().tcs);

    // Scan tool: 19 02 lists the DTC with status 0xAF.
    QByteArray rsp = ask(sim, DiagId::AbsRequest, "19 02 FF");
    QVERIFY(rsp.startsWith(hex("59 02 FF")));
    QVERIFY2(rsp.contains(hex("40 35 13 AF")), qPrintable(toHex(rsp)));
    QList<DtcInfo> dtcs = parseUdsDtcList(ask(sim, DiagId::AbsRequest, "19 02 08"));
    QCOMPARE(dtcs.size(), 1);
    QCOMPARE(dtcToString(dtcs.first().code), QStringLiteral("C0035-13"));

    // 19 04: snapshot with the speeds at the moment of detection (FL already 0).
    rsp = ask(sim, DiagId::AbsRequest, "19 04 40 35 13 01");
    const QList<DecodedValue> snapshot = parseSnapshot(rsp);
    QCOMPARE(snapshot.size(), 6);
    QCOMPARE(snapshot.at(0).did, quint16(0xF40D));
    QVERIFY(snapshot.at(0).number > 40.0);
    QCOMPARE(snapshot.at(1).did, quint16(0x3001));
    QCOMPARE(snapshot.at(1).number, 0.0);
    QVERIFY(snapshot.at(2).number > 40.0);

    // 14 FF FF FF clears, but the fault is still there: the DTC returns after the debounce time.
    QCOMPARE(ask(sim, DiagId::AbsRequest, "14 FF FF FF"), hex("54"));
    QVERIFY(!(sim.abs().dtcs().status(code) & DtcStatus::Confirmed));
    QVERIFY(sim.runUntil([&] { return sim.abs().dtcs().status(code) & DtcStatus::Confirmed; }, 400));

    // Repair, then clear: the DTC stays away and the lamp goes out.
    sim.clearFault(QStringLiteral("wss_fl"));
    sim.run(1000);
    QVERIFY(!sim.abs().dtcs().testFailed(code));
    QVERIFY(sim.abs().dtcs().status(code) & DtcStatus::Confirmed);
    QCOMPARE(ask(sim, DiagId::AbsRequest, "14 FF FF FF"), hex("54"));
    sim.run(2000);
    QCOMPARE(sim.abs().dtcs().storedCount(), 0);
    QCOMPARE(ask(sim, DiagId::AbsRequest, "19 02 08"), hex("59 02 FF"));
    QVERIFY(!sim.ipc().state().abs);
    QCOMPARE(sim.signal(QStringLiteral("WheelSpeed_FL_Valid")), 1.0);
}

void tst_Scenarios::bundledScenarios_data()
{
    QTest::addColumn<QString>("path");
    const QDir dir(QStringLiteral(":/ecusim/scenarios"));
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    QVERIFY(files.size() >= 4);
    for (const QString &file : files)
        QTest::newRow(qPrintable(file)) << dir.filePath(file);
}

void tst_Scenarios::bundledScenarios()
{
    QFETCH(QString, path);
    Simulation sim;
    QString error;
    QVERIFY2(sim.scenario().loadFile(path, &error), qPrintable(error));
    sim.scenario().start();
    QVERIFY(sim.runUntil([&] { return sim.scenario().finished(); }, 300'000));

    QStringList failures;
    for (const ScenarioLogEntry &e : sim.scenario().log())
        if (e.level == ScenarioLogEntry::Level::Fail)
            failures << QStringLiteral("[%1 ms] %2").arg(e.timeMs).arg(e.text);
    QVERIFY2(sim.scenario().state() == ScenarioRunner::State::Passed, qPrintable(failures.join(u'\n')));
}

void tst_Scenarios::failingExpectationFailsScenario()
{
    Simulation sim;
    const QByteArray json = R"({ "name": "t", "steps": [
        { "action": "ignition", "value": "on" },
        { "action": "wait", "ms": 4000 },
        { "action": "expect", "lamp": "abs", "value": true },
        { "action": "expect", "signal": "IgnitionState", "op": "==", "value": 1 },
        { "action": "waitFor", "signal": "VehicleSpeed", "op": ">", "value": 10, "timeout": 500 },
        { "action": "bogus" } ] })";
    QVERIFY(sim.scenario().loadJson(json));
    sim.scenario().start();
    QVERIFY(sim.runUntil([&] { return sim.scenario().finished(); }, 20000));
    QCOMPARE(sim.scenario().state(), ScenarioRunner::State::Failed);
    QCOMPARE(sim.scenario().failures(), 3); // lamp, waitFor timeout, unknown action

    QString error;
    QVERIFY(!sim.scenario().loadJson("{ \"steps\": 5 }", &error));
    QVERIFY(!error.isEmpty());
}

void tst_Scenarios::endToEndProtection()
{
    Simulation sim;
    startEngine(sim);
    QCOMPARE(sim.ecm().dtcs().storedCount(), 0);

    QVERIFY(sim.injectFault(QStringLiteral("abs_e2e"), {FaultType::BadChecksum}));
    sim.run(500);
    QVERIFY(sim.ecm().dtcs().status(dtc::U0415Checksum) & DtcStatus::Confirmed);
    QVERIFY(!(sim.ecm().dtcs().status(dtc::U0415Counter) & DtcStatus::Confirmed));

    QVERIFY(sim.injectFault(QStringLiteral("abs_e2e"), {FaultType::FrozenCounter}));
    sim.run(500);
    QVERIFY(sim.ecm().dtcs().status(dtc::U0415Counter) & DtcStatus::Confirmed);
    QVERIFY(!sim.ecm().dtcs().testFailed(dtc::U0415Checksum));

    // A network DTC is stored but must not light the MIL.
    QVERIFY(!sim.ipc().state().mil);
    sim.clearFault(QStringLiteral("abs_e2e"));
    sim.run(500);
    QVERIFY(!sim.ecm().dtcs().testFailed(dtc::U0415Counter));
}

void tst_Scenarios::busShortSilencesEverything()
{
    Simulation sim;
    startEngine(sim);
    const quint64 before = sim.bus().frameCount();
    QVERIFY(sim.injectFault(QStringLiteral("can_bus"), {FaultType::BusShort}));
    sim.run(2500);
    QCOMPARE(sim.bus().frameCount(), before);
    QVERIFY(sim.ecm().dtcs().status(dtc::U0121) & DtcStatus::Confirmed);
    QVERIFY(sim.ecm().dtcs().status(dtc::U0101) & DtcStatus::Confirmed);
    QVERIFY(sim.ecm().dtcs().status(dtc::U0140) & DtcStatus::Confirmed);
    QVERIFY(sim.tcm().dtcs().status(dtc::U0100) & DtcStatus::Confirmed);
    QVERIFY(sim.abs().dtcs().status(dtc::U0100) & DtcStatus::Confirmed);
    QVERIFY(sim.bcm().dtcs().status(dtc::U0100) & DtcStatus::Confirmed);
    QVERIFY(sim.tcm().limp());
    // The cluster falls back to fail-safe lamps, the scan tool gets no answer.
    QVERIFY(sim.ipc().state().mil && sim.ipc().state().abs && sim.ipc().state().transFault);
    QVERIFY(exchange(sim, DiagId::EcmRequest, hex("3E 00")).timedOut());

    sim.clearFault(QStringLiteral("can_bus"));
    sim.run(2000);
    QVERIFY(!sim.ecm().dtcs().testFailed(dtc::U0121));
    QVERIFY(!sim.tcm().limp());
    QCOMPARE(ask(sim, DiagId::EcmRequest, "3E 00"), hex("7E 00"));
}

void tst_Scenarios::busOffNodeIsReportedByOthers()
{
    Simulation sim;
    startEngine(sim);
    QVERIFY(sim.injectFault(QStringLiteral("tcm"), {FaultType::BusOff}));
    sim.run(2000);
    // Others miss the TCM; the TCM, still powered, misses everybody else.
    QVERIFY(sim.ecm().dtcs().status(dtc::U0101) & DtcStatus::Confirmed);
    QVERIFY(sim.bcm().dtcs().status(dtc::U0101) & DtcStatus::Confirmed);
    QVERIFY(sim.tcm().dtcs().status(dtc::U0100) & DtcStatus::Confirmed);
    QVERIFY(sim.tcm().dtcs().status(dtc::U0121) & DtcStatus::Confirmed);
    QVERIFY(sim.ipc().state().transFault);
    QVERIFY(sim.tcm().awake());
    QVERIFY(!sim.tcm().isOnline());
}

void tst_Scenarios::ecuResetOverUds()
{
    Simulation sim;
    startEngine(sim);
    QCOMPARE(ask(sim, DiagId::AbsRequest, "10 03"), hex("50 03 00 32 01 F4"));
    QCOMPARE(ask(sim, DiagId::AbsRequest, "11 01"), hex("51 01"));
    sim.run(100);
    QVERIFY(!sim.abs().awake());
    QVERIFY(exchange(sim, DiagId::AbsRequest, hex("3E 00")).timedOut());
    sim.run(400);
    QVERIFY(sim.abs().awake());
    // After the reset the ECU is back in the default session.
    QCOMPARE(ask(sim, DiagId::AbsRequest, "22 F1 86"), hex("62 F1 86 01"));
    sim.run(2000);
    // A short reset must not leave lost-communication DTCs behind.
    QCOMPARE(sim.ecm().dtcs().storedCount(), 0);
}

void tst_Scenarios::ioControlNeedsSessionAndSecurity()
{
    Simulation sim;
    const QByteArray json = R"({ "name": "io", "steps": [
        { "action": "ignition", "value": "on" },
        { "action": "wait", "ms": 3500 },
        { "action": "expect", "lamp": "abs", "value": false },
        { "action": "uds", "ecu": "ABS", "request": "2F 30 10 03 01", "expect": "7F 2F 7F" },
        { "action": "uds", "ecu": "ABS", "request": "10 03", "expect": "50 03" },
        { "action": "uds", "ecu": "ABS", "request": "2F 30 10 03 01", "expect": "7F 2F 33" },
        { "action": "unlock", "ecu": "ABS" },
        { "action": "uds", "ecu": "ABS", "request": "2F 30 10 03 01", "expect": "6F 30 10 03" },
        { "action": "wait", "ms": 200 },
        { "action": "expect", "lamp": "abs", "value": true },
        { "action": "uds", "ecu": "ABS", "request": "2F 30 10 03 07", "expect": "7F 2F 31" },
        { "action": "uds", "ecu": "ABS", "request": "2F 30 10 00", "expect": "6F 30 10 00" },
        { "action": "wait", "ms": 200 },
        { "action": "expect", "lamp": "abs", "value": false },
        { "action": "uds", "ecu": "ABS", "request": "2F 30 10 03 01", "expect": "6F 30 10 03" },
        { "action": "wait", "ms": 6000, "note": "S3 timeout: back to the default session, control returns to the ECU" },
        { "action": "expect", "lamp": "abs", "value": false },
        { "action": "uds", "ecu": "ABS", "request": "22 F1 86", "expect": "62 F1 86 01" } ] })";
    QVERIFY(sim.scenario().loadJson(json));
    sim.scenario().start();
    QVERIFY(sim.runUntil([&] { return sim.scenario().finished(); }, 60000));
    QStringList failures;
    for (const ScenarioLogEntry &e : sim.scenario().log())
        if (e.level == ScenarioLogEntry::Level::Fail)
            failures << e.text;
    QVERIFY2(sim.scenario().state() == ScenarioRunner::State::Passed, qPrintable(failures.join(u'\n')));
}

void tst_Scenarios::faultTargetsAreConsistent()
{
    Simulation sim;
    QVERIFY(sim.faultTargets().size() >= 15);
    for (const FaultTarget &t : sim.faultTargets()) {
        QVERIFY(!t.faultTypes.isEmpty());
        for (FaultType type : t.faultTypes) {
            QCOMPARE(faultTypeFromName(faultTypeName(type)), type);
            QVERIFY2(sim.injectFault(t.id, {type}), qPrintable(t.id));
            QCOMPARE(sim.activeFaults().size(), 1);
            sim.clearFault(t.id);
        }
    }
    QVERIFY(sim.activeFaults().isEmpty());
    QVERIFY(!sim.injectFault(QStringLiteral("nope"), {FaultType::OpenCircuit}));
    QVERIFY(!sim.injectFault(QStringLiteral("alternator"), {FaultType::OpenCircuit}));
}

QTEST_GUILESS_MAIN(tst_Scenarios)
#include "tst_scenarios.moc"
