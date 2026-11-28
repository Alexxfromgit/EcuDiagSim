// Native QML regression checks. The original 2D view is real; the 3D renderer is a
// test double so lifecycle/navigation checks run on CI without WebEngine or a GPU.
#include "AppController.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <functional>

class tst_VehicleWorkspace : public QObject
{
    Q_OBJECT
private slots:
    void diagramAndLifecycle();
};

void tst_VehicleWorkspace::diagramAndLifecycle()
{
    QTemporaryDir settings, qml;
    QVERIFY(settings.isValid() && qml.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("EcuDiagSimWorkspaceTest"));
    QCoreApplication::setApplicationName(QStringLiteral("tst_vehicle_workspace"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QQuickStyle::setStyle(QStringLiteral("Fusion"));
    AppController app;
    app.setRunning(false);
    app.setReducedMotion(true);
    qmlRegisterSingletonInstance("EcuDiagSim", 1, 0, "App", &app);
    const QDir sources(QStringLiteral(ECUSIM_QML_SOURCE_DIR));
    for (const QString &file : sources.entryList({QStringLiteral("*.qml")}, QDir::Files))
        if (file != QLatin1String("Vehicle3D.qml")) QVERIFY(QFile::copy(sources.filePath(file), qml.filePath(file)));
    QFile directory(qml.filePath(QStringLiteral("qmldir")));
    QVERIFY(directory.open(QIODevice::WriteOnly));
    directory.write("singleton Theme 1.0 Theme.qml\n"); directory.close();
    QFile stub(qml.filePath(QStringLiteral("Vehicle3D.qml")));
    QVERIFY(stub.open(QIODevice::WriteOnly));
    stub.write("import QtQuick\nItem { objectName: 'testVehicle3D'; signal targetPicked(string id); signal ecuPicked(int index); signal fallbackRequested() }\n");
    stub.close();
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl::fromLocalFile(qml.filePath(QStringLiteral("VehicleWorkspace.qml"))));
    std::unique_ptr<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto *workspace = qobject_cast<QQuickItem *>(object.get());
    QVERIFY(workspace);
    QQuickWindow window;
    window.resize(450, 800); workspace->setParentItem(window.contentItem());
    workspace->setSize(QSizeF(450, 800)); window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *loader = workspace->findChild<QObject *>(QStringLiteral("vehicleViewLoader"));
    auto *expanded = workspace->findChild<QObject *>(QStringLiteral("vehicleExpandedDialog"));
    QVERIFY(loader && expanded);
    auto view = [&] { return qvariant_cast<QQuickItem *>(loader->property("item")); };
    QTRY_VERIFY(view());
    auto *diagram = view();
    QCOMPARE(diagram->property("title").toString(), QStringLiteral("Vehicle"));
    QVERIFY(diagram->property("waiting").toBool());
    QVERIFY(!workspace->findChild<QObject *>(QStringLiteral("vehicle3DButton"))->property("enabled").toBool());
    QSignalSpy targets(workspace, SIGNAL(targetPicked(QString))), ecus(workspace, SIGNAL(ecuPicked(int)));
    // Repeater delegates belong to the visual tree, not necessarily the QObject ownership tree.
    std::function<QQuickItem *(QQuickItem *, const QString &)> findVisual = [&](QQuickItem *parent, const QString &name) -> QQuickItem * {
        if (parent->objectName() == name) return parent;
        for (auto *child : parent->childItems()) if (auto *found = findVisual(child, name)) return found;
        return nullptr;
    };
    auto click = [&](QObject *item) {
        auto *visual = qobject_cast<QQuickItem *>(item);
        QVERIFY(visual && visual->isVisible());
        QTest::mouseClick(&window, Qt::LeftButton, {}, visual->mapToScene(QPointF(visual->width() / 2, visual->height() / 2)).toPoint());
    };
    click(findVisual(diagram, QStringLiteral("vehiclePausedPromptDismiss")));
    QTRY_VERIFY(!findVisual(diagram, QStringLiteral("vehiclePausedPrompt"))->property("shown").toBool());
    click(findVisual(diagram, QStringLiteral("vehicleTarget:wss_fl")));
    QTRY_COMPARE(targets.count(), 1); QCOMPARE(targets.last().at(0).toString(), QStringLiteral("wss_fl"));
    click(findVisual(diagram, QStringLiteral("vehicleEcu:ABS")));
    QTRY_COMPARE(ecus.count(), 1); QCOMPARE(ecus.last().at(0).toInt(), 2);
    app.setIgnition(1); app.setHeadlamps(2); app.setHighBeam(true); app.setBrake(60); app.setSelector(1); app.step(1500);
    QTRY_VERIFY(diagram->property("lowLeft").toBool());
    QVERIFY(diagram->property("lowRight").toBool() && diagram->property("highBeam").toBool());
    QVERIFY(diagram->property("braking").toBool() && diagram->property("reversing").toBool());
    QVERIFY(app.injectFault(QStringLiteral("bulb_low_left"), QStringLiteral("failure"), {}, {})); app.step(40);
    QTRY_VERIFY(!diagram->property("lowLeft").toBool());
    QVERIFY(diagram->property("lowRight").toBool());
    QVERIFY(app.injectFault(QStringLiteral("bcm"), QStringLiteral("power_loss"), {}, {})); app.step(100);
    QTRY_VERIFY(!diagram->property("lowRight").toBool());
    QVERIFY(!diagram->property("highBeam").toBool());
    QVERIFY(diagram->property("braking").toBool() && diagram->property("reversing").toBool());
    app.clearAllFaults(); app.step(100);
    const QStringList packs{QStringLiteral("engine_obd"), QStringLiteral("tpms"), QStringLiteral("srs"), QStringLiteral("esc")};
    for (int mask = 0; mask < 16; ++mask) {
        QStringList fitted;
        for (int bit = 0; bit < packs.size(); ++bit) if (mask & (1 << bit)) fitted.append(packs.at(bit));
        app.setEquipment(fitted);
        const auto spots = qvariant_cast<QJSValue>(diagram->property("spots")).toVariant().toList();
        QStringList ids;
        for (const QVariant &spot : spots) ids.append(spot.toMap().value(QStringLiteral("id")).toString());
        QCOMPARE(ids.contains(QStringLiteral("maf")), fitted.contains(QStringLiteral("engine_obd")));
        QCOMPARE(ids.contains(QStringLiteral("tpms_fl")), fitted.contains(QStringLiteral("tpms")));
        QCOMPARE(ids.contains(QStringLiteral("airbag_driver")), fitted.contains(QStringLiteral("srs")));
        QCOMPARE(ids.contains(QStringLiteral("yaw")), fitted.contains(QStringLiteral("esc")));
    }
    const QVariantMap state = app.vehicle();
    const double clock = app.simTime();
    workspace->setProperty("threeDAvailable", true);
    click(workspace->findChild<QObject *>(QStringLiteral("vehicle3DButton")));
    QTRY_VERIFY(view() && view()->objectName() == QLatin1String("testVehicle3D"));
    workspace->setVisible(false); QVERIFY(!view()->isVisible());
    workspace->setVisible(true); QVERIFY(view()->isVisible());
    click(workspace->findChild<QObject *>(QStringLiteral("vehicleDiagramButton")));
    QTRY_VERIFY(view() && view()->property("title").toString() == QLatin1String("Vehicle"));
    click(workspace->findChild<QObject *>(QStringLiteral("vehicle3DButton")));
    QTRY_VERIFY(view() && view()->objectName() == QLatin1String("testVehicle3D"));
    auto *threeD = view();
    click(workspace->findChild<QObject *>(QStringLiteral("vehicleExpandButton")));
    QTRY_VERIFY(expanded->property("visible").toBool());
    QCOMPARE(view(), threeD);
    QVERIFY(QMetaObject::invokeMethod(threeD, "targetPicked", Q_ARG(QString, QStringLiteral("ect"))));
    QTRY_VERIFY(!expanded->property("visible").toBool());
    QCOMPARE(view(), threeD); QCOMPARE(targets.last().at(0).toString(), QStringLiteral("ect"));
    QVERIFY(QMetaObject::invokeMethod(workspace, "open3D"));
    QTRY_VERIFY(expanded->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(threeD, "ecuPicked", Q_ARG(int, 0)));
    QTRY_VERIFY(!expanded->property("visible").toBool()); QCOMPARE(ecus.last().at(0).toInt(), 0);
    QVERIFY(QMetaObject::invokeMethod(threeD, "fallbackRequested"));
    QTRY_VERIFY(view() && view()->property("title").toString() == QLatin1String("Vehicle"));
    QVERIFY(!workspace->property("show3D").toBool());
    click(findVisual(view(), QStringLiteral("vehiclePausedPromptDismiss")));
    QTRY_VERIFY(!findVisual(view(), QStringLiteral("vehiclePausedPrompt"))->property("shown").toBool());
    const int picked = targets.count();
    click(findVisual(view(), QStringLiteral("vehicleTarget:wss_fl")));
    QTRY_COMPARE(targets.count(), picked + 1);
    QCOMPARE(app.simTime(), clock); QCOMPARE(app.vehicle(), state);
    app.setEquipment({});
}

QTEST_MAIN(tst_VehicleWorkspace)
#include "tst_vehicle_workspace.moc"
