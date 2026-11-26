// End-to-end tests: the complete chain from an injected fault to the scan tool,
// run headless and much faster than real time.
#include "TestUtil.h"

#include "sim/ScenarioRunner.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
    void staleSignalsAndBadHexFail();
    void endToEndProtection();
    void busShortSilencesEverything();
    void busOffNodeIsReportedByOthers();
    void ecuResetOverUds();
    void ioControlNeedsSessionAndSecurity();
    void faultTargetsAreConsistent();
    void silentBcmDoesNotFreezeClusterLamps();
    void silentEcmClearsCoolantWarning();
    void runnerInputsAndLamps();
    void dtcExpectationNeedsAKnownDtc();
    void replacedPartsAndCoding();
    void throttleAdaptationAbortsAndFails();
    void autoLockOncePerTrip();
};

void tst_Scenarios::simulationIsValid()
{
    Simulation sim;
    QVERIFY2(sim.isValid(), qPrintable(sim.errorString()));
    QCOMPARE(sim.dbc().messages().size(), 7);
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

    // The Lessons tab filters by level and every lesson links to its documentation.
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject lesson = QJsonDocument::fromJson(file.readAll()).object();
    const QString level = lesson.value(QLatin1String("level")).toString();
    QVERIFY2(QStringList({QStringLiteral("Beginner"), QStringLiteral("Intermediate"), QStringLiteral("Advanced")})
                 .contains(level),
        qPrintable(QStringLiteral("level '%1'").arg(level)));
    QVERIFY(!lesson.value(QLatin1String("topics")).toArray().isEmpty());
    const QString doc = lesson.value(QLatin1String("lesson")).toString();
    QVERIFY2(QFile::exists(QStringLiteral(ECUSIM_SOURCE_DIR "/") + doc), qPrintable(doc));
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

void tst_Scenarios::staleSignalsAndBadHexFail()
{
    Simulation sim;
    const QByteArray json = R"({ "name": "t", "steps": [
        { "action": "ignition", "value": "on" },
        { "action": "wait", "ms": 1500 },
        { "action": "expect", "signal": "VehicleSpeedValid", "op": "==", "value": 1 },
        { "action": "inject", "target": "abs", "fault": "power_loss" },
        { "action": "wait", "ms": 1500 },
        { "action": "expect", "signal": "VehicleSpeedValid", "op": "==", "value": 1 },
        { "action": "expect", "signal": "VehicleSpeedValid", "op": "==", "value": 1, "maxAgeMs": 5000 },
        { "action": "uds", "ecu": "BCM", "request": "3E 00", "expect": "7E GG" } ] })";
    QVERIFY(sim.scenario().loadJson(json));
    sim.scenario().start();
    QVERIFY(sim.runUntil([&] { return sim.scenario().finished(); }, 20000));
    QStringList failures;
    for (const ScenarioLogEntry &e : sim.scenario().log())
        if (e.level == ScenarioLogEntry::Level::Fail && !e.text.startsWith(QStringLiteral("Scenario failed")))
            failures << e.text;
    // The cached value from before the ABS went silent must not count, nor a broken hex string.
    QCOMPARE(failures.size(), 2);
    QVERIFY2(failures.at(0).contains(QStringLiteral("stale")), qPrintable(failures.join(u'\n')));
    QVERIFY2(failures.at(1).contains(QStringLiteral("Invalid hex")), qPrintable(failures.join(u'\n')));
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
        QVERIFY2(!t.label.isEmpty(), qPrintable(t.id)); // tag on the vehicle drawing
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

namespace {
/// Extended session and security access, as the scan tool does it.
void unlockEcu(Simulation &sim, quint32 requestId)
{
    QCOMPARE(ask(sim, requestId, "10 03").left(2), hex("50 03"));
    const QByteArray seed = ask(sim, requestId, "27 01");
    QCOMPARE(seed.size(), 6);
    quint32 s = 0;
    for (int i = 2; i < 6; ++i)
        s = (s << 8) | static_cast<quint8>(seed[i]);
    const quint32 key = securityKeyFromSeed(s);
    QByteArray request = hex("27 02");
    for (int shift = 24; shift >= 0; shift -= 8)
        request.append(static_cast<char>((key >> shift) & 0xFF));
    QCOMPARE(exchange(sim, requestId, request).responses.value(0).data, hex("67 02"));
}
} // namespace

// A replaced module loses its coding; writing it ends the fault, Repair restores the old module,
// and a custom coding is not touched by unrelated faults on the same control unit.
void tst_Scenarios::replacedPartsAndCoding()
{
    Simulation sim;
    startEngine(sim);
    QVERIFY(sim.faultTarget(QStringLiteral("abs"))->faultTypes.contains(FaultType::Unconfigured));
    QVERIFY(!sim.faultTarget(QStringLiteral("tcm"))->faultTypes.contains(FaultType::Unconfigured));

    // BCM: blank coding switches every coded function off.
    QVERIFY(sim.injectFault(QStringLiteral("bcm"), {FaultType::Unconfigured}));
    sim.run(700); // debounced in the 100 ms task
    QVERIFY(sim.bcm().codingBlank());
    QCOMPARE(ask(sim, DiagId::BcmRequest, "22 01 00"), hex("62 01 00 FF"));
    QVERIFY(sim.bcm().dtcs().testFailed(dtc::U3000NotConfigured));
    unlockEcu(sim, DiagId::BcmRequest);
    QCOMPARE(ask(sim, DiagId::BcmRequest, "2E 01 00 04"), hex("6E 01 00"));
    QVERIFY(!sim.bcm().codingBlank());
    QVERIFY(sim.activeFaults().isEmpty()); // configured by the scan tool, not repaired
    sim.run(700);
    QVERIFY(!sim.bcm().dtcs().testFailed(dtc::U3000NotConfigured));
    QVERIFY(sim.bcm().parkLampsOn()); // daytime running lights coded, engine running

    // Power loss and its repair leave the custom coding alone.
    QVERIFY(sim.injectFault(QStringLiteral("bcm"), {FaultType::PowerLoss}));
    sim.run(200);
    sim.clearFault(QStringLiteral("bcm"));
    sim.run(500);
    QCOMPARE(sim.bcm().coding(), quint8(Bcm::DaytimeLights));

    // Repair of a blank module = the old one is back with the factory coding.
    QVERIFY(sim.injectFault(QStringLiteral("abs"), {FaultType::Unconfigured}));
    sim.run(200);
    QVERIFY(!sim.abs().wheelValid(0));
    sim.clearFault(QStringLiteral("abs"));
    sim.run(200);
    QVERIFY(!sim.abs().codingBlank());
    QCOMPARE(sim.abs().circumferenceMm(), AbsEcu::kDefaultCircumferenceMm);
    QVERIFY(sim.abs().wheelValid(0));

    // A wrong tyre size: no DTC, but every wheel speed is off by the same factor.
    driveTo(sim, 50.0);
    unlockEcu(sim, DiagId::AbsRequest);
    QCOMPARE(ask(sim, DiagId::AbsRequest, "2E 01 00 00 06 D6"), hex("6E 01 00")); // 1750 mm
    sim.run(200);
    const double ratio = sim.signal(QStringLiteral("WheelSpeed_FL")) / sim.vehicle().speedKph();
    QVERIFY2(std::abs(ratio - 1750.0 / 1948.0) < 0.02, qPrintable(QString::number(ratio)));
    QCOMPARE(sim.signal(QStringLiteral("VehicleSpeedValid")), 1.0);
}

// Review finding: above 15 km/h the auto-lock locked the doors again 100 ms after the driver
// unlocked them. It locks once per trip.
void tst_Scenarios::autoLockOncePerTrip()
{
    Simulation sim;
    startEngine(sim);
    unlockEcu(sim, DiagId::BcmRequest);
    QCOMPARE(ask(sim, DiagId::BcmRequest, "2E 01 00 03"), hex("6E 01 00")); // + auto-lock
    driveTo(sim, 30.0);
    sim.run(300);
    QVERIFY(sim.bcm().doorsLocked());
    // The driver locks and unlocks with the switch while driving: it stays unlocked.
    sim.driver().doorLock = true;
    sim.run(300);
    sim.driver().doorLock = false;
    sim.run(1000);
    QVERIFY(!sim.bcm().doorsLocked());
    QVERIFY(sim.vehicle().speedKph() > 15.0);
}

void tst_Scenarios::throttleAdaptationAbortsAndFails()
{
    Simulation sim;
    sim.driver().ignition = Ignition::On;
    sim.run(1500);
    QVERIFY(sim.ecm().throttleAdapted());
    QCOMPARE(ask(sim, DiagId::EcmRequest, "10 03").left(2), hex("50 03"));

    // The pedal pressed during the routine aborts it.
    QVERIFY(ask(sim, DiagId::EcmRequest, "31 01 02 01").startsWith(hex("71 01 02 01 01")));
    QVERIFY(sim.ecm().adaptationRunning());
    QCOMPARE(ask(sim, DiagId::EcmRequest, "31 01 02 01"), hex("7F 31 24")); // already running
    sim.run(300);
    QVERIFY(sim.vehicle().throttlePos() > 0.1); // the motor moves the plate with the engine off
    sim.driver().throttlePedal = 0.5;
    sim.run(50);
    QVERIFY(!sim.ecm().adaptationRunning());
    QVERIFY(ask(sim, DiagId::EcmRequest, "31 03 02 01").startsWith(hex("71 03 02 01 03")));
    sim.driver().throttlePedal = 0.0;
    sim.run(300);
    QVERIFY(sim.vehicle().throttlePos() < 0.05);

    // A new throttle body with an unplugged sensor cannot be learned.
    QVERIFY(sim.injectFault(QStringLiteral("throttle_body"), {FaultType::Unconfigured}));
    QVERIFY(sim.injectFault(QStringLiteral("tps"), {FaultType::OpenCircuit}));
    QVERIFY(!sim.ecm().throttleAdapted());
    QVERIFY(ask(sim, DiagId::EcmRequest, "31 01 02 01").startsWith(hex("71 01 02 01 01")));
    sim.run(Ecm::kAdaptationMs + 100);
    QVERIFY(ask(sim, DiagId::EcmRequest, "31 03 02 01").startsWith(hex("71 03 02 01 04")));
    QVERIFY(!sim.ecm().throttleAdapted());
    QCOMPARE(sim.activeFaults().size(), 2);

    // With the sensor repaired it works, and the "replaced" fault is over.
    sim.clearFault(QStringLiteral("tps"));
    QVERIFY(ask(sim, DiagId::EcmRequest, "31 01 02 01").startsWith(hex("71 01 02 01 01")));
    sim.run(Ecm::kAdaptationMs + 100);
    QVERIFY(ask(sim, DiagId::EcmRequest, "31 03 02 01").startsWith(hex("71 03 02 01 02")));
    QVERIFY(sim.ecm().throttleAdapted());
    QVERIFY(sim.activeFaults().isEmpty());
    QVERIFY(ask(sim, DiagId::EcmRequest, "22 10 04").startsWith(hex("62 10 04 01")));
}

// A blinking turn signal and an open door must not stay lit in the cluster when the BCM
// goes silent, and the exterior lamps go dark without BCM supply.
void tst_Scenarios::silentBcmDoesNotFreezeClusterLamps()
{
    Simulation sim;
    startEngine(sim);
    sim.driver().headlamps = 2;
    sim.driver().highBeam = true;
    sim.driver().doorAjar[0] = true;
    sim.driver().turnLeft = true;
    QVERIFY(sim.runUntil([&] { return sim.ipc().state().turnLeft; }, 1000));
    QVERIFY(sim.ipc().state().doorAjar);
    QVERIFY(sim.ipc().state().lowBeam);
    QVERIFY(sim.bcm().lowBeamOn() && sim.bcm().highBeamOn() && sim.bcm().parkLampsOn());

    QVERIFY(sim.injectFault(QStringLiteral("bcm"), {FaultType::PowerLoss}));
    sim.run(1500);
    const ClusterState &c = sim.ipc().state();
    QVERIFY(!c.turnLeft);
    QVERIFY(!c.doorAjar);
    QVERIFY(!c.lowBeam);
    QVERIFY(!c.highBeam);
    QVERIFY(!sim.bcm().lowBeamOn() && !sim.bcm().highBeamOn() && !sim.bcm().parkLampsOn());
    QVERIFY(!sim.bcm().turnLeftLampOn());

    // The fuse is back: lamps and tell-tales return.
    sim.clearFault(QStringLiteral("bcm"));
    QVERIFY(sim.runUntil([&] { return sim.ipc().state().turnLeft; }, 2000));
    QVERIFY(sim.ipc().state().doorAjar);
    QVERIFY(sim.bcm().lowBeamOn());
}

// The cluster's temperature lamp follows the ECM's request, and goes out with a silent ECM
// instead of keeping the last received request.
void tst_Scenarios::silentEcmClearsCoolantWarning()
{
    Simulation sim;
    startEngine(sim);
    sim.vehicle().setCoolantTemp(125.0);
    QVERIFY(sim.runUntil([&] { return sim.ipc().state().coolantWarn; }, 1000));

    QVERIFY(sim.injectFault(QStringLiteral("ecm"), {FaultType::PowerLoss}));
    sim.run(1500);
    QVERIFY(!sim.ipc().state().coolantWarn);
    QVERIFY(sim.ipc().state().mil); // the silent ECM itself is signalled by the MIL
}

void tst_Scenarios::runnerInputsAndLamps()
{
    Simulation sim;
    const QByteArray json = R"({ "name": "t", "steps": [
        { "action": "ignition", "value": "on" },
        { "action": "wait", "ms": 3500 },
        { "action": "expect", "lamp": "door", "value": false },
        { "action": "input", "name": "doorRL", "value": true },
        { "action": "input", "name": "highBeam", "value": true },
        { "action": "headlamps", "value": "low" },
        { "action": "wait", "ms": 300 },
        { "action": "expect", "lamp": "door", "value": true },
        { "action": "expect", "lamp": "highBeam", "value": true },
        { "action": "expect", "lamp": "lowBeam", "value": true },
        { "action": "input", "name": "doorRL", "value": false },
        { "action": "input", "name": "turnLeft", "value": true },
        { "action": "input", "name": "turnRight", "value": true },
        { "action": "wait", "ms": 300 },
        { "action": "expect", "lamp": "door", "value": false } ] })";
    QVERIFY(sim.scenario().loadJson(json));
    sim.scenario().start();
    QVERIFY(sim.runUntil([&] { return sim.scenario().finished(); }, 10000));
    QStringList failures;
    for (const ScenarioLogEntry &e : sim.scenario().log())
        if (e.level == ScenarioLogEntry::Level::Fail)
            failures << e.text;
    QVERIFY2(sim.scenario().state() == ScenarioRunner::State::Passed, qPrintable(failures.join(u'\n')));
    // One stalk: switching right cancels left.
    QVERIFY(sim.driver().turnRight);
    QVERIFY(!sim.driver().turnLeft);
    QVERIFY(!sim.driver().doorAjar[2]);

    // Switching the active side off leaves the other side alone.
    QVERIFY(sim.scenario().loadJson(R"({ "name": "t", "steps": [
        { "action": "input", "name": "turnLeft", "value": false } ] })"));
    sim.scenario().start();
    sim.run(10);
    QVERIFY(sim.driver().turnRight);
}

// "absent" and "notConfirmed" must not pass for a DTC the ECU does not monitor (typo, wrong ECU).
void tst_Scenarios::dtcExpectationNeedsAKnownDtc()
{
    Simulation sim;
    const QByteArray json = R"({ "name": "t", "steps": [
        { "action": "ignition", "value": "on" },
        { "action": "wait", "ms": 100 },
        { "action": "expect", "dtc": "P0121", "ecu": "TCM", "status": "absent" },
        { "action": "expect", "dtc": "P1021", "ecu": "ECM", "status": "notConfirmed" },
        { "action": "expect", "dtc": "P0121", "ecu": "ECM", "status": "absent" },
        { "action": "expect", "dtc": "P0121", "ecu": "ECM", "status": "notConfirmed" } ] })";
    QVERIFY(sim.scenario().loadJson(json));
    sim.scenario().start();
    QVERIFY(sim.runUntil([&] { return sim.scenario().finished(); }, 1000));
    QCOMPARE(sim.scenario().failures(), 2);
}

QTEST_GUILESS_MAIN(tst_Scenarios)
#include "tst_scenarios.moc"
