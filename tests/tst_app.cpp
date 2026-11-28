// View models behind the QML UI, driven headless: AppController, ScannerViewModel, TraceModel.
#include "AppController.h"
#include "WindowGeometry.h"

#include <QAbstractItemModelTester>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace ecusim;

namespace {
/// A controller whose simulation only moves when the test steps it.
struct Rig
{
    AppController app;
    ScannerViewModel *scanner = app.scanner();

    Rig()
    {
        app.setRunning(false);
        app.setIgnition(1);
        app.step(1500);
    }

    QStringList logLines(const QString &kind) const
    {
        QStringList lines;
        for (const QVariant &v : scanner->log()) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("kind")).toString() == kind)
                lines << m.value(QStringLiteral("text")).toString();
        }
        return lines;
    }
};
} // namespace

class tst_App : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void opensPaused();
    void settingsArePersisted();
    void equipmentCatalogAndChoice();
    void vehicleViewerEquipmentSnapshots();
    void vehicleViewerControlSnapshots();
    void legacyBehaviorSnapshots();
    void viewModelsDescribeTheVehicle();
    void codingReadFillsTheField();
    void pollingDoesNotLeakIntoDtcTable();
    void overlappingRequestsKeepTheirOwnRows();
    void userRequestsAreLoggedAfterResetDuringPoll();
    void pollingStaysOutOfTheLog();
    void sessionStateFollowsTheEcu();
    void traceModelIsConsistent();
    void updatesInADevelopmentBuild();
    void swappedFaultRefreshesViews();
    void windowFitsTheScreen();
    void lessonsPassThroughTheApp_data();
    void lessonsPassThroughTheApp();
};

// Settings go to an INI file in a temporary folder, never to the user's real settings.
void tst_App::initTestCase()
{
    static QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("EcuDiagSimTest"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_app"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
    QSettings().clear();
}

// Opening the app must not start the clock: nothing on the bus until the user presses Run.
void tst_App::opensPaused()
{
    AppController app;
    QVERIFY(!app.startOnLaunch());
    QVERIFY(!app.running());
    QTest::qWait(300); // the display timer runs, the simulation does not
    QCOMPARE(app.simTime(), 0.0);
    QCOMPARE(app.sim().bus().frameCount(), quint64(0));

    // Refuelling while paused shows the new level at once (the view does not wait for a step).
    QSignalSpy frames(&app, &AppController::frameUpdated);
    app.refuel(12.0);
    QCOMPARE(frames.count(), 1);
    QCOMPARE(app.vehicle().value(QStringLiteral("fuel")).toDouble(), 12.0);
    QCOMPARE(app.simTime(), 0.0);

    // A lesson starts by itself.
    app.runScenario(app.scenarios().first().toMap().value(QStringLiteral("path")).toString());
    QVERIFY(app.running());
    app.stopScenario();
}

void tst_App::settingsArePersisted()
{
    {
        AppController app;
        QSignalSpy changed(&app, &AppController::settingsChanged);
        app.setStartOnLaunch(true);
        app.setDefaultSpeed(2.0);
        app.setReducedMotion(true);
        QCOMPARE(changed.size(), 3);
        app.setStartOnLaunch(true); // unchanged: no signal
        QCOMPARE(changed.size(), 3);
        app.setDefaultSpeed(99.0); // clamped
        QCOMPARE(app.defaultSpeed(), 20.0);
        app.setDefaultSpeed(2.0);
    }
    {
        AppController app;
        QVERIFY(app.running());
        QCOMPARE(app.speedFactor(), 2.0);
        QVERIFY(app.reducedMotion());
        app.setStartOnLaunch(false);
        app.setDefaultSpeed(1.0);
        app.setReducedMotion(false);
    }
    AppController app;
    QVERIFY(!app.running());
    QCOMPARE(app.speedFactor(), 1.0);
}

void tst_App::equipmentCatalogAndChoice()
{
    AppController app;
    const QVariantList catalog = app.equipmentCatalog();
    QCOMPARE(catalog.size(), VehicleConfig::catalog().size());
    for (const QVariant &v : catalog) {
        const QVariantMap m = v.toMap();
        QVERIFY(!m.value(QStringLiteral("name")).toString().isEmpty());
        QVERIFY(m.contains(QStringLiteral("implemented")) && m.contains(QStringLiteral("lessons")));
    }
    QVERIFY(app.equipment().isEmpty());
    QVERIFY(app.fitted().isEmpty());

    // Only known, implemented items survive; the choice is saved for the next start.
    QStringList implemented;
    for (const EquipmentInfo &e : VehicleConfig::catalog())
        if (e.implemented)
            implemented << e.id;
    app.setEquipment(QStringList{QStringLiteral("turbo")} + implemented);
    QCOMPARE(app.equipment(), implemented);
    QCOMPARE(app.fitted(), implemented);
    QCOMPARE(QSettings().value(QLatin1String(AppController::kEquipmentKey)).toStringList(), implemented);
    app.setEquipment({});
    QVERIFY(app.equipment().isEmpty());

    // The scan tool offers what the vehicle has: the fuel trim reset only with the engine OBD pack.
    ScannerViewModel *scanner = app.scanner();
    scanner->setTarget(0);
    QCOMPARE(scanner->routines().size(), 1);
    QSignalSpy targetSpy(scanner, &ScannerViewModel::targetChanged);
    app.setEquipment({QStringLiteral("engine_obd")});
    QVERIFY(!targetSpy.isEmpty()); // the views re-read the lists
    QCOMPARE(scanner->routines().size(), 2);

    // A fifth control unit appears in the scan tool with its own address.
    QCOMPARE(scanner->ecuNames().size(), 4);
    app.setEquipment({QStringLiteral("srs")});
    QCOMPARE(scanner->ecuNames().size(), 5);
    QCOMPARE(scanner->ecuNames().at(4), QStringLiteral("SRS  (0x7E4)"));
    scanner->setTarget(4);
    QCOMPARE(scanner->target(), 4);
    QCOMPARE(scanner->codings().size(), 1);
    app.setEquipment({});
    QCOMPARE(scanner->target(), 0); // the module is gone

    // The ABS offers the steering angle calibration only with stability control.
    scanner->setTarget(2);
    QCOMPARE(scanner->routines().size(), 0);
    QCOMPARE(scanner->codings().value(0).toMap().value(QStringLiteral("example")).toString(), QStringLiteral("00 07 9C"));
    app.setEquipment({QStringLiteral("esc")});
    QCOMPARE(scanner->routines().size(), 1);
    // The coding example keeps the ESC flag: writing it must not code the stability control out.
    QCOMPARE(scanner->codings().value(0).toMap().value(QStringLiteral("example")).toString(), QStringLiteral("01 07 9C"));
    app.setEquipment({});
}

void tst_App::vehicleViewerEquipmentSnapshots()
{
    AppController app;
    app.setRunning(false);
    const QStringList packs{QStringLiteral("engine_obd"), QStringLiteral("tpms"),
        QStringLiteral("srs"), QStringLiteral("esc")};
    QJsonArray snapshots;
    for (int mask = 0; mask < 16; ++mask) {
        QStringList fitted;
        for (int bit = 0; bit < packs.size(); ++bit)
            if ((mask & (1 << bit)) != 0)
                fitted.append(packs.at(bit));
        app.setEquipment(fitted);
        app.setIgnition(1);
        app.step(1500);
        QCOMPARE(app.fitted(), fitted);
        QStringList ids;
        for (const QVariant &target : app.faultTargets())
            ids.append(target.toMap().value(QStringLiteral("id")).toString());
        QCOMPARE(ids.contains(QStringLiteral("maf")), fitted.contains(QStringLiteral("engine_obd")));
        QCOMPARE(ids.contains(QStringLiteral("tpms_fl")), fitted.contains(QStringLiteral("tpms")));
        QCOMPARE(ids.contains(QStringLiteral("srs")), fitted.contains(QStringLiteral("srs")));
        QCOMPARE(ids.contains(QStringLiteral("yaw")), fitted.contains(QStringLiteral("esc")));
        snapshots.append(QJsonObject{{QStringLiteral("fitted"), QJsonArray::fromStringList(app.fitted())},
            {QStringLiteral("targets"), QJsonArray::fromVariantList(app.faultTargets())},
            {QStringLiteral("sensors"), QJsonArray::fromVariantList(app.sensors())},
            {QStringLiteral("ecus"), QJsonArray::fromVariantList(app.ecus())},
            {QStringLiteral("faults"), QJsonArray::fromVariantList(app.activeFaults())},
            {QStringLiteral("vehicle"), QJsonObject::fromVariantMap(app.vehicle())},
            {QStringLiteral("running"), app.running()}, {QStringLiteral("reducedMotion"), true}});
    }
    // Optional fixtures for real-browser checks use the same snapshots the native viewer receives.
    const QString exportPath = qEnvironmentVariable("ECUSIM_VIEWER_FIXTURES");
    if (!exportPath.isEmpty()) {
        QFile file(exportPath);
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
        const QByteArray json = QJsonDocument(snapshots).toJson();
        QCOMPARE(file.write(json), static_cast<qint64>(json.size()));
    }
    app.setEquipment({});
}

void tst_App::vehicleViewerControlSnapshots()
{
    Rig r;
    auto &app = r.app;
    app.setEquipment({QStringLiteral("engine_obd"), QStringLiteral("tpms"), QStringLiteral("srs"), QStringLiteral("esc")});
    app.setIgnition(1);
    app.step(1500);
    QJsonArray snapshots;
    auto capture = [&](const QString &name, const QStringList &lights) {
        snapshots.append(QJsonObject{{QStringLiteral("name"), name},
            {QStringLiteral("expectedLights"), QJsonArray::fromStringList(lights)},
            {QStringLiteral("fitted"), QJsonArray::fromStringList(app.fitted())},
            {QStringLiteral("targets"), QJsonArray::fromVariantList(app.faultTargets())},
            {QStringLiteral("sensors"), QJsonArray::fromVariantList(app.sensors())},
            {QStringLiteral("ecus"), QJsonArray::fromVariantList(app.ecus())},
            {QStringLiteral("faults"), QJsonArray::fromVariantList(app.activeFaults())},
            {QStringLiteral("vehicle"), QJsonObject::fromVariantMap(app.vehicle())},
            {QStringLiteral("running"), false}, {QStringLiteral("reducedMotion"), true}});
    };
    const QStringList park{QStringLiteral("park:l"), QStringLiteral("park:r"), QStringLiteral("tail:l"), QStringLiteral("tail:r")};
    const QStringList low = park + QStringList{QStringLiteral("low:l"), QStringLiteral("low:r")};
    const QStringList high = low + QStringList{QStringLiteral("high:l"), QStringLiteral("high:r")};
    const QStringList left{QStringLiteral("turn:l"), QStringLiteral("mirror:l")};
    const QStringList right{QStringLiteral("turn:r"), QStringLiteral("mirror:r")};
    capture(QStringLiteral("lights-off"), {});
    app.setHeadlamps(1); app.step(40);
    QVERIFY(app.vehicle().value(QStringLiteral("parkLamps")).toBool());
    QVERIFY(!app.vehicle().value(QStringLiteral("lowBeam")).toBool());
    capture(QStringLiteral("park"), park);
    app.setHeadlamps(2); app.step(40);
    QVERIFY(app.vehicle().value(QStringLiteral("lowBeam")).toBool());
    capture(QStringLiteral("low"), low);
    app.setHighBeam(true); app.step(40);
    QVERIFY(app.vehicle().value(QStringLiteral("highBeam")).toBool());
    capture(QStringLiteral("high"), high);
    QVERIFY(app.injectFault(QStringLiteral("bulb_low_left"), QStringLiteral("failure"), {}, {}));
    app.step(40);
    QStringList failed = high; failed.removeAll(QStringLiteral("low:l"));
    capture(QStringLiteral("failed-left-low-high-still-works"), failed);
    app.clearAllFaults(); app.setHeadlamps(0); app.setHighBeam(false); app.step(40);
    auto blinkPhase = [&](bool on) {
        for (int i = 0; i < 50; ++i) {
            app.step(20);
            if (app.vehicle().value(QStringLiteral("turnLeftLamp")).toBool() == on
                && app.vehicle().value(QStringLiteral("turnRightLamp")).toBool() == on)
                return true;
        }
        return false;
    };
    app.setTurnLeft(true);
    for (int i = 0; i < 50 && !app.vehicle().value(QStringLiteral("turnLeftLamp")).toBool(); ++i) app.step(20);
    QVERIFY(app.vehicle().value(QStringLiteral("turnLeftLamp")).toBool());
    QVERIFY(!app.vehicle().value(QStringLiteral("turnRightLamp")).toBool());
    capture(QStringLiteral("left-turn"), left);
    app.setTurnRight(true);
    for (int i = 0; i < 50; ++i) {
        app.step(20);
        if (app.vehicle().value(QStringLiteral("turnRightLamp")).toBool()) break;
    }
    QVERIFY(!app.vehicle().value(QStringLiteral("turnLeftLamp")).toBool());
    QVERIFY(app.vehicle().value(QStringLiteral("turnRightLamp")).toBool());
    capture(QStringLiteral("right-turn"), right);
    app.setTurnRight(false); app.setHazard(true);
    QVERIFY(blinkPhase(true)); capture(QStringLiteral("hazards-on"), left + right);
    QVERIFY(blinkPhase(false)); capture(QStringLiteral("hazards-off-phase"), {});
    app.setIgnition(0);
    QVERIFY(blinkPhase(true)); capture(QStringLiteral("hazards-ignition-off"), left + right);
    app.setHazard(false); app.setIgnition(1); app.setBrake(65); app.setSelector(1); app.step(40);
    QVERIFY(app.vehicle().value(QStringLiteral("braking")).toBool());
    QVERIFY(app.vehicle().value(QStringLiteral("reversing")).toBool());
    const QStringList stopReverse{QStringLiteral("tail:l"), QStringLiteral("tail:r"), QStringLiteral("stop:center"), QStringLiteral("reverse:l"), QStringLiteral("reverse:r")};
    capture(QStringLiteral("brake-and-reverse"), stopReverse);
    app.setHeadlamps(2); app.setHighBeam(true); app.setHazard(true); app.step(40);
    QVERIFY(app.injectFault(QStringLiteral("bcm"), QStringLiteral("power_loss"), {}, {})); app.step(100);
    QVERIFY(!app.vehicle().value(QStringLiteral("lowBeam")).toBool());
    capture(QStringLiteral("bcm-power-loss-hardwired-brake-reverse"), stopReverse);
    app.clearAllFaults(); app.setHeadlamps(0); app.setHighBeam(false); app.setHazard(false); app.setBrake(0); app.setSelector(0); app.step(40);
    const QStringList components{QStringLiteral("airbag_driver"), QStringLiteral("airbag_passenger"),
        QStringLiteral("belt_switch"), QStringLiteral("bulb_low_left")};
    for (const QString &id : components) {
        const QString fault = id == QLatin1String("bulb_low_left") ? QStringLiteral("failure") : QStringLiteral("open");
        QVERIFY(app.injectFault(id, fault, {}, {})); app.step(60);
        QCOMPARE(app.vehicle().value(QStringLiteral("componentCircuits")).toMap().value(id).toString(), QStringLiteral("open"));
        capture(QStringLiteral("component-%1-open").arg(id), {});
        if (id.startsWith(QLatin1String("airbag_"))) {
            QVERIFY(app.injectFault(id, QStringLiteral("short_gnd"), {}, {})); app.step(60);
            QCOMPARE(app.vehicle().value(QStringLiteral("componentCircuits")).toMap().value(id).toString(), QStringLiteral("short_gnd"));
            capture(QStringLiteral("component-%1-short").arg(id), {});
        }
        app.clearAllFaults(); app.step(60);
        QCOMPARE(app.vehicle().value(QStringLiteral("componentCircuits")).toMap().value(id).toString(), QStringLiteral("ok"));
        capture(QStringLiteral("component-%1-repaired").arg(id), {});
    }
    for (int i = 0; i < 4; ++i) {
        app.setDoor(i, true); app.step(40);
        QCOMPARE(app.vehicle().value(QStringLiteral("doors")).toList().at(i).toBool(), true);
        capture(QStringLiteral("door-%1-open").arg(i), {});
        app.setDoor(i, false);
    }
    app.setDoorLock(true); app.setSeatBelt(true); app.setParkBrake(true); app.setSteering(360); app.refuel(12); app.setSurface(2); app.step(100);
    QVERIFY(app.vehicle().value(QStringLiteral("doorsLocked")).toBool());
    QVERIFY(app.vehicle().value(QStringLiteral("seatBelt")).toBool());
    QVERIFY(app.vehicle().value(QStringLiteral("parkBrake")).toBool());
    QCOMPARE(app.vehicle().value(QStringLiteral("steering")).toDouble(), 360.0);
    QCOMPARE(app.vehicle().value(QStringLiteral("fuel")).toDouble(), 12.0);
    capture(QStringLiteral("locked-belt-steering-fuel-snow"), {});
    app.setSeatBelt(false); app.setDoorLock(false); app.setSteering(-360); app.setSurface(1); app.step(100);
    capture(QStringLiteral("unlocked-unbuckled-wet"), {});
    app.setSurface(3); app.step(40); capture(QStringLiteral("ice"), {});
    app.setSurface(0); app.setSteering(0); app.setParkBrake(false); app.startEngine(); app.step(2500);
    QVERIFY(app.vehicle().value(QStringLiteral("engineRunning")).toBool());
    app.setSelector(3); app.setThrottle(80); app.step(12000);
    QVERIFY(app.vehicle().value(QStringLiteral("speed")).toDouble() > 20);
    capture(QStringLiteral("driving"), {});
    const auto count = [&] { return app.vehicle().value(QStringLiteral("transmittedFrames")).toMap().value(QStringLiteral("ECM")).toULongLong(); };
    const quint64 before = count();
    app.trace()->setPaused(true); app.trace()->setFilter(QStringLiteral("ABS")); app.step(200);
    QVERIFY(count() > before); // vehicle animation must not depend on trace filtering/pause
    app.crash(); app.step(500);
    QVERIFY(app.vehicle().value(QStringLiteral("airbagFired")).toList().first().toBool());
    QVERIFY(blinkPhase(true)); capture(QStringLiteral("crash-airbags-hazards"), left + right);
    app.replaceAirbags(); app.step(100);
    QVERIFY(!app.vehicle().value(QStringLiteral("airbagFired")).toList().first().toBool());
    // The crash record still commands hazards until ignition is switched off.
    QVERIFY(blinkPhase(true)); capture(QStringLiteral("replaced-airbags"), left + right);
    const QString exportPath = qEnvironmentVariable("ECUSIM_VIEWER_CONTROL_FIXTURES");
    if (!exportPath.isEmpty()) {
        QFile file(exportPath);
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
        const QByteArray json = QJsonDocument(snapshots).toJson();
        QCOMPARE(file.write(json), static_cast<qint64>(json.size()));
    }
    app.setEquipment({});
}

// This same test can be compiled against a pre-3D checkout and its export compared with
// the current app. Repeated display reads must never advance or otherwise alter the vehicle.
void tst_App::legacyBehaviorSnapshots()
{
    AppController app;
    app.setRunning(false);
    QJsonArray checkpoints;
    auto display = [&] {
        QVariantMap vehicle = app.vehicle();
        for (const QString &key : {QStringLiteral("wheelLocked"), QStringLiteral("seatBelt"), QStringLiteral("steering"),
                 QStringLiteral("braking"), QStringLiteral("reversing"), QStringLiteral("transmittedFrames"),
                 QStringLiteral("doorsLocked"), QStringLiteral("componentCircuits")})
            vehicle.remove(key); // additive 3D metadata; retain every original field, including locked
        QJsonArray trace;
        QList<int> roles = app.trace()->roleNames().keys(); std::sort(roles.begin(), roles.end());
        for (int row = 0; row < app.trace()->rowCount(); ++row) {
            QJsonArray values;
            for (int role : roles) values.append(QJsonValue::fromVariant(app.trace()->data(app.trace()->index(row), role)));
            trace.append(values);
        }
        const QByteArray digest = QCryptographicHash::hash(QJsonDocument(trace).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
        return QJsonObject{{QStringLiteral("time"), app.simTime()}, {QStringLiteral("vehicle"), QJsonObject::fromVariantMap(vehicle)},
            {QStringLiteral("cluster"), QJsonObject::fromVariantMap(app.cluster())},
            {QStringLiteral("sensors"), QJsonArray::fromVariantList(app.sensors())},
            {QStringLiteral("ecus"), QJsonArray::fromVariantList(app.ecus())},
            {QStringLiteral("faults"), QJsonArray::fromVariantList(app.activeFaults())},
            {QStringLiteral("targets"), QJsonArray::fromVariantList(app.faultTargets())},
            {QStringLiteral("fitted"), QJsonArray::fromStringList(app.fitted())},
            {QStringLiteral("traceRows"), app.trace()->rowCount()}, {QStringLiteral("traceSha256"), QString::fromLatin1(digest)},
            {QStringLiteral("scannerValues"), QJsonArray::fromVariantList(app.scanner()->values())},
            {QStringLiteral("scannerDtcs"), QJsonArray::fromVariantList(app.scanner()->dtcs())},
            {QStringLiteral("scannerLog"), QJsonArray::fromVariantList(app.scanner()->log())},
            {QStringLiteral("session"), app.scanner()->sessionText()}};
    };
    auto capture = [&](const QString &name) {
        const QVariantMap rawVehicle = app.vehicle();
        const auto first = display();
        for (int i = 0; i < 3; ++i) QCOMPARE(display(), first);
        QCOMPARE(app.vehicle(), rawVehicle);
        checkpoints.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("state"), first}});
    };
    capture(QStringLiteral("fresh-paused"));
    const QStringList packs{QStringLiteral("engine_obd"), QStringLiteral("tpms"), QStringLiteral("srs"), QStringLiteral("esc")};
    for (int mask = 0; mask < 16; ++mask) {
        QStringList fitted;
        for (int bit = 0; bit < packs.size(); ++bit) if (mask & (1 << bit)) fitted.append(packs.at(bit));
        app.setEquipment(fitted); app.setIgnition(1); app.step(1500);
        capture(QStringLiteral("equipment-%1").arg(mask));
    }
    app.setHeadlamps(1); app.step(100); capture(QStringLiteral("park-lamps"));
    app.setHeadlamps(2); app.setHighBeam(true); app.setHazard(true); app.step(100); capture(QStringLiteral("lights-and-hazards"));
    for (int door = 0; door < 4; ++door) app.setDoor(door, true);
    app.setSteering(360); app.setSeatBelt(true); app.setDoorLock(true); app.setSurface(2);
    app.setBrake(25); app.setParkBrake(true); app.setSelector(1); app.refuel(20); app.step(100);
    capture(QStringLiteral("doors-locks-restraints-reverse-brake-snow"));
    for (int door = 0; door < 4; ++door) app.setDoor(door, false);
    app.setSteering(-180); app.setSurface(1); app.setHeadlamps(0); app.setHighBeam(false); app.setHazard(false);
    app.setBrake(0); app.setParkBrake(false); app.setSelector(0); app.startEngine(); app.step(2500);
    QVERIFY(app.vehicle().value(QStringLiteral("engineRunning")).toBool());
    capture(QStringLiteral("engine-running"));
    app.setSelector(3); app.setThrottle(60); app.step(6000); capture(QStringLiteral("driving"));
    QVERIFY(app.injectFault(QStringLiteral("wss_fl"), QStringLiteral("open"), {}, {}));
    QVERIFY(app.injectFault(QStringLiteral("ect"), QStringLiteral("short_gnd"), {}, {}));
    app.step(1500); capture(QStringLiteral("sensor-faults"));
    app.scanner()->obdReadCodes(); app.step(1000);
    app.scanner()->setTarget(2); app.scanner()->udsReadDtcs(0xFF); app.step(500);
    capture(QStringLiteral("obd-and-uds-dtcs"));
    app.scanner()->setTarget(3); app.scanner()->udsSession(3); app.step(200);
    app.scanner()->udsIoControl(0x4010, 1); app.step(200); capture(QStringLiteral("lamp-actuator-test"));
    app.scanner()->udsIoControl(0x4010, -1); app.step(200);
    app.trace()->setFixedMode(true); app.trace()->setFilter(QStringLiteral("ABS")); app.step(200);
    capture(QStringLiteral("filtered-fixed-trace"));
    app.trace()->setPaused(true); app.step(200); capture(QStringLiteral("paused-trace"));
    app.clearAllFaults(); app.step(100); capture(QStringLiteral("repaired"));
    app.reset(); capture(QStringLiteral("reset"));
    const QString exportPath = qEnvironmentVariable("ECUSIM_LEGACY_SNAPSHOTS");
    if (!exportPath.isEmpty()) {
        QFile file(exportPath);
        QVERIFY2(file.open(QIODevice::WriteOnly), qPrintable(file.errorString()));
        const QByteArray json = QJsonDocument(checkpoints).toJson();
        QCOMPARE(file.write(json), static_cast<qint64>(json.size()));
    }
    app.setEquipment({});
}

void tst_App::viewModelsDescribeTheVehicle()
{
    AppController app;
    // Scan tool target index of every diagnosable control unit; the cluster has none.
    QHash<QString, int> index;
    for (const QVariant &v : app.ecus()) {
        const QVariantMap m = v.toMap();
        index.insert(m.value(QStringLiteral("name")).toString(), m.value(QStringLiteral("diagIndex")).toInt());
    }
    QCOMPARE(index.value(QStringLiteral("ECM")), 0);
    QCOMPARE(index.value(QStringLiteral("TCM")), 1);
    QCOMPARE(index.value(QStringLiteral("ABS")), 2);
    QCOMPARE(index.value(QStringLiteral("BCM")), 3);
    QCOMPARE(index.value(QStringLiteral("IPC")), -1);
    for (int i = 0; i < 4; ++i)
        QVERIFY(app.scanner()->ecuNames().at(i).startsWith(index.key(i)));

    for (const QVariant &v : app.sensors()) {
        const QVariantMap m = v.toMap();
        QVERIFY(!m.value(QStringLiteral("shortName")).toString().isEmpty());
        QVERIFY(!m.value(QStringLiteral("label")).toString().isEmpty());
    }
    QVERIFY(app.vehicle().contains(QStringLiteral("velocity")));

    // The sensor table's model only changes with the equipment, not on every refresh.
    QStringList ids;
    for (const QVariant &v : app.sensors())
        ids << v.toMap().value(QStringLiteral("id")).toString();
    QCOMPARE(app.sensorIds(), ids);
    QSignalSpy equipment(&app, &AppController::equipmentChanged);
    app.step(500);
    QVERIFY(equipment.isEmpty());
}

// Review finding: with "Refresh live data" on, Read codes showed live-data rows as DTC cards.
// Review finding: "Read" showed the coding in the values table only; the field to edit kept the example.
void tst_App::codingReadFillsTheField()
{
    Rig r;
    r.scanner->setTarget(3); // BCM
    QSignalSpy read(r.scanner, &ScannerViewModel::didRead);
    r.scanner->udsReadDid(0x0100);
    r.app.step(200);
    QCOMPARE(read.size(), 1);
    QCOMPARE(read.at(0).at(0).toInt(), 0x0100);
    QCOMPARE(read.at(0).at(1).toString(), QStringLiteral("01"));
}

void tst_App::pollingDoesNotLeakIntoDtcTable()
{
    Rig r;
    QVERIFY(r.app.injectFault(QStringLiteral("ect"), QStringLiteral("open"), {}, {}));
    r.app.step(1500); // P0118 qualifies

    r.scanner->setPollMode(1);
    r.scanner->poll();          // queues two background Mode 01 requests
    r.scanner->obdReadCodes();  // queued behind them
    r.app.step(3000);

    const QVariantList dtcs = r.scanner->dtcs();
    QCOMPARE(dtcs.size(), 1);
    const QVariantMap row = dtcs.first().toMap();
    QCOMPARE(row.value(QStringLiteral("code")).toString(), QStringLiteral("P0118"));
    QCOMPARE(row.value(QStringLiteral("ecu")).toString(), QStringLiteral("ECM"));
    QVERIFY(row.value(QStringLiteral("confirmed")).toBool());
    // The poll still produced its own table.
    QVERIFY(r.scanner->valuesTitle().contains(QStringLiteral("Mode 01")));
    for (const QVariant &v : r.scanner->values())
        QVERIFY(v.toMap().contains(QStringLiteral("name")));
}

void tst_App::overlappingRequestsKeepTheirOwnRows()
{
    Rig r;
    r.app.startEngine();
    r.app.step(4000);
    // Two clicks less than a second apart: each table must be complete and unmixed.
    r.scanner->obdVehicleInfo();
    r.scanner->obdLiveData();
    r.app.step(3000);
    QVERIFY(r.scanner->valuesTitle().contains(QStringLiteral("Mode 01")));
    for (const QVariant &v : r.scanner->values()) {
        const QString name = v.toMap().value(QStringLiteral("name")).toString();
        QVERIFY2(name != QStringLiteral("VIN") && name != QStringLiteral("ECU name"), qPrintable(name));
    }
    // ECM answers 8 values, TCM 4 (monitor status, speed, voltage are shared PIDs).
    QVERIFY(r.scanner->values().size() >= 8);
}

// Review finding: a poll in flight during Reset swallowed the next user requests from the log.
void tst_App::userRequestsAreLoggedAfterResetDuringPoll()
{
    Rig r;
    r.scanner->setPollMode(2);
    r.scanner->poll();
    r.app.step(1); // request on its way
    r.app.reset();
    r.app.setRunning(false);
    r.scanner->setPollMode(0);
    r.app.setIgnition(1);
    r.app.step(1500);

    r.scanner->udsReadDtcs(0x0D);
    r.scanner->udsIdentification();
    r.app.step(1000);
    const QStringList tx = r.logLines(QStringLiteral("tx"));
    QCOMPARE(tx.size(), 2);
    QVERIFY(tx.at(0).contains(QStringLiteral("19 02 0D")));
    QVERIFY(tx.at(1).contains(QStringLiteral("22 F1 90")));
    QCOMPARE(r.logLines(QStringLiteral("rx")).size(), 2);
}

void tst_App::pollingStaysOutOfTheLog()
{
    Rig r;
    r.scanner->setPollMode(2);
    for (int i = 0; i < 5; ++i) {
        r.scanner->poll();
        r.app.step(500);
    }
    QVERIFY(r.scanner->valuesTitle().contains(QStringLiteral("live data")));
    QVERIFY(r.scanner->log().isEmpty());
}

// Review finding: the session / unlock chips kept showing a state the ECU had already left.
void tst_App::sessionStateFollowsTheEcu()
{
    Rig r;
    r.scanner->setTarget(2); // ABS
    r.scanner->udsSession(3);
    r.scanner->udsUnlock();
    r.app.step(500);
    QCOMPARE(r.scanner->sessionText(), QStringLiteral("Extended session"));
    QVERIFY(r.scanner->unlocked());

    // S3 timeout: no requests for more than 5 s.
    r.app.step(DiagnosticServer::kS3TimeoutMs + 500);
    r.scanner->updateSessionState();
    QCOMPARE(r.scanner->sessionText(), QStringLiteral("Default session"));
    QVERIFY(!r.scanner->unlocked());
    // ... and the ECU agrees.
    r.scanner->udsIoControl(0x3010, 1);
    r.app.step(300);
    QVERIFY(r.logLines(QStringLiteral("neg")).last().contains(QStringLiteral("7F 2F 7F")));

    // With tester present the session survives.
    r.scanner->setKeepAlive(true);
    r.scanner->udsSession(3);
    r.app.step(DiagnosticServer::kS3TimeoutMs + 3000);
    r.scanner->updateSessionState();
    QCOMPARE(r.scanner->sessionText(), QStringLiteral("Extended session"));
    QCOMPARE(r.app.sim().abs().diag().session(), DiagnosticServer::Session::Extended);

    // A power cycle ends it.
    QVERIFY(r.app.injectFault(QStringLiteral("abs"), QStringLiteral("power_loss"), {}, {}));
    r.app.step(100);
    r.scanner->updateSessionState();
    QCOMPARE(r.scanner->sessionText(), QStringLiteral("Default session"));
    r.scanner->setKeepAlive(false);
}

void tst_App::traceModelIsConsistent()
{
    Rig r;
    TraceModel *trace = r.app.trace();
    QAbstractItemModelTester tester(trace, QAbstractItemModelTester::FailureReportingMode::QtTest);

    // Chronological mode is capped.
    r.app.step(10000); // 330 frames per second
    QCOMPARE(trace->rowCount(), TraceModel::kMaxRows);

    // By ID: one sorted row per identifier, updated in place.
    trace->setFixedMode(true);
    r.app.step(50);
    qRegisterMetaType<QList<int>>();
    QSignalSpy changed(trace, &QAbstractItemModel::dataChanged);
    r.app.step(500);
    QStringList ids;
    for (int i = 0; i < trace->rowCount(); ++i)
        ids << trace->data(trace->index(i), TraceModel::IdRole).toString();
    QCOMPARE(ids, QStringList({"0B0", "0B1", "100", "1A0", "300", "310", "350"}));
    QVERIFY(!changed.isEmpty());
    const QString cycle = trace->data(trace->index(0), TraceModel::CycleRole).toString();
    QCOMPARE(cycle, QStringLiteral("10.0"));

    // A new identifier appearing in the same batch as updates (diagnostic traffic).
    r.scanner->udsIdentification();
    r.app.step(500);
    ids.clear();
    for (int i = 0; i < trace->rowCount(); ++i)
        ids << trace->data(trace->index(i), TraceModel::IdRole).toString();
    QVERIFY(ids.contains(QStringLiteral("7E0")) && ids.contains(QStringLiteral("7E8")));
    QVERIFY(std::is_sorted(ids.begin(), ids.end()));

    trace->setFilter(QStringLiteral("ABS"));
    r.app.step(200);
    QCOMPARE(trace->rowCount(), 2);
}

// A test binary is not installed by Velopack, exactly like a development build of the app.
// A scenario that swaps one fault for another (same count) or changes only a parameter must still
// refresh the views, which poll the simulation for changes made behind their back.
void tst_App::swappedFaultRefreshesViews()
{
    Rig r;
    QSignalSpy spy(&r.app, &AppController::faultsChanged);
    Simulation &sim = r.app.sim();
    QVERIFY(sim.injectFault(QStringLiteral("ect"), {FaultType::OpenCircuit}));
    QVERIFY(spy.wait(2000));

    spy.clear();
    sim.clearFault(QStringLiteral("ect"));
    QVERIFY(sim.injectFault(QStringLiteral("tps"), {FaultType::ShortToGround}));
    QVERIFY(spy.wait(2000));
    QCOMPARE(r.app.activeFaults().size(), 1);
    QCOMPARE(r.app.activeFaults().first().toMap().value(QStringLiteral("targetId")).toString(), QStringLiteral("tps"));

    Fault loose{FaultType::Intermittent, 400.0, 0.3, true};
    QVERIFY(sim.injectFault(QStringLiteral("tps"), loose));
    QVERIFY(spy.wait(2000));
    spy.clear();
    loose.p2 = 0.6;
    QVERIFY(sim.injectFault(QStringLiteral("tps"), loose));
    QVERIFY(spy.wait(2000));

    // Nothing changed: no refresh.
    spy.clear();
    QTest::qWait(600);
    QCOMPARE(spy.count(), 0);
}

// The main window never opens past the work area of its screen, even on screens smaller than
// its minimum size (the minimum gives way).
void tst_App::windowFitsTheScreen()
{
    const QSize preferred(1600, 940);
    const QSize minimum(1024, 600);

    // 1024x600 display with a 60 px taskbar: 540 px of work area.
    InitialWindowGeometry fit = fitWindowToScreen(QRect(0, 0, 1024, 540), preferred, minimum);
    QCOMPARE(fit.geometry, QRect(0, 0, 1024, 540));
    QCOMPARE(fit.minimum, QSize(1024, 540));

    // Narrower than the minimum width, work area not at the origin (taskbar on the left).
    fit = fitWindowToScreen(QRect(48, 0, 1000, 700), preferred, minimum);
    QCOMPARE(fit.geometry, QRect(48, 30, 1000, 640));
    QCOMPARE(fit.minimum, QSize(1000, 600));

    // 1366x768 laptop with a 40 px taskbar: shrinks with a margin, centred.
    fit = fitWindowToScreen(QRect(0, 0, 1366, 728), preferred, minimum);
    QCOMPARE(fit.geometry.size(), QSize(1326, 668));
    QCOMPARE(fit.geometry.topLeft(), QPoint(20, 30));
    QCOMPARE(fit.minimum, minimum);

    // Second monitor at x = 1920, large enough: the preferred size, centred on that screen.
    fit = fitWindowToScreen(QRect(1920, 0, 2560, 1400), preferred, minimum);
    QCOMPARE(fit.geometry, QRect(1920 + 480, 230, 1600, 940));

    // Whatever the screen, the window stays inside the work area.
    for (const QRect &avail : {QRect(0, 0, 800, 480), QRect(0, 0, 1024, 600), QRect(100, 50, 1280, 670)}) {
        fit = fitWindowToScreen(avail, preferred, minimum);
        QVERIFY2(avail.contains(fit.geometry), qPrintable(QStringLiteral("%1x%2").arg(avail.width()).arg(avail.height())));
        QVERIFY(fit.geometry.width() >= fit.minimum.width() && fit.geometry.height() >= fit.minimum.height());
    }
}

// Lessons run on the user's equipment plus what the lesson needs, so every lesson must also pass
// when all optional systems are fitted, and must pass through the app (not only the bare simulator).
void tst_App::lessonsPassThroughTheApp_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QStringList>("equipment");
    AppController app;
    QStringList all;
    for (const QVariant &item : app.equipmentCatalog())
        if (item.toMap().value(QStringLiteral("implemented")).toBool())
            all << item.toMap().value(QStringLiteral("id")).toString();
    QVERIFY(all.size() >= 4);
    for (const QVariant &lesson : app.scenarios()) {
        const QVariantMap map = lesson.toMap();
        const QString path = map.value(QStringLiteral("path")).toString();
        const QString name = QFileInfo(path).fileName();
        QTest::newRow(qPrintable(name + QStringLiteral(" / lesson equipment"))) << path << QStringList();
        QTest::newRow(qPrintable(name + QStringLiteral(" / everything fitted"))) << path << all;
    }
}

void tst_App::lessonsPassThroughTheApp()
{
    QFETCH(QString, path);
    QFETCH(QStringList, equipment);
    AppController app;
    app.setEquipment(equipment);
    app.runScenario(path);
    QCOMPARE(app.scenarioState(), QStringLiteral("running"));
    for (int ms = 0; ms < 300'000 && app.scenarioState() == QStringLiteral("running"); ms += 500)
        app.step(500);
    QStringList failures;
    for (const QVariant &entry : app.scenarioLog())
        if (entry.toMap().value(QStringLiteral("level")).toString() == QStringLiteral("fail"))
            failures << entry.toMap().value(QStringLiteral("text")).toString();
    QVERIFY2(app.scenarioState() == QStringLiteral("passed"), qPrintable(app.scenarioState() + QStringLiteral(": ") + failures.join(u'\n')));
    app.setEquipment({});
}

void tst_App::updatesInADevelopmentBuild()
{
    UpdateService updates;
    QCOMPARE(updates.currentVersion(), QStringLiteral(ECUSIM_VERSION));
    QVERIFY(!updates.canUpdate());
    QVERIFY(!updates.autoCheck()); // shown unticked where nothing would check
    updates.setAutoCheck(true);
    QVERIFY(!updates.autoCheck());

    QSignalSpy changed(&updates, &UpdateService::stateChanged);
    updates.check();
    QCOMPARE(changed.size(), 1);
    QVERIFY(updates.statusText().contains(QStringLiteral("installed app")));
    QVERIFY(!updates.busy());
    QVERIFY(!updates.updateReady());
    updates.checkOnStartup(); // does nothing, schedules nothing
    updates.restartToUpdate();
    updates.dismiss();
    QVERIFY(updates.statusText().isEmpty());

    QCOMPARE(updates.releaseNotesUrl(),
        QStringLiteral("https://github.com/Alexxfromgit/EcuDiagSim/releases/tag/v" ECUSIM_VERSION));
}

QTEST_GUILESS_MAIN(tst_App)
#include "tst_app.moc"
