#include "AppController.h"
#include "WindowGeometry.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>

#ifdef ECUSIM_HAS_VEHICLE_3D
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#endif

#ifdef ECUSIM_HAS_VELOPACK
#include "Velopack.hpp"
#endif

int main(int argc, char *argv[])
{
#ifdef ECUSIM_HAS_VELOPACK
    // First thing, before any window: Velopack's install / update / uninstall hooks run here and
    // may exit the process, and an update downloaded earlier is installed on this start.
    Velopack::VelopackApp::Build().Run();
#endif
#ifdef ECUSIM_HAS_VEHICLE_3D
    QtWebEngineQuick::initialize();
#endif
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("EcuDiagSim"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("ECU Diagnostics Simulator"));
    QGuiApplication::setApplicationVersion(QStringLiteral(ECUSIM_VERSION));
    QGuiApplication::setOrganizationName(QStringLiteral("EcuDiagSim"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/ecusim/icons/ecudiagsim.png")));
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Virtual vehicle with networked ECUs for learning CAN and diagnostics."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption scenarioOpt(QStringLiteral("scenario"),
        QStringLiteral("Run a scenario at start: a JSON file or the number (1..n) of a bundled lesson."),
        QStringLiteral("file|number"));
    const QCommandLineOption tabOpt(QStringLiteral("tab"),
        QStringLiteral("Panel to show at start: 0 faults, 1 trace, 2 scanner, 3 scenarios."), QStringLiteral("index"));
    const QCommandLineOption speedOpt(QStringLiteral("speed"), QStringLiteral("Simulation speed factor (0.1..20)."),
        QStringLiteral("factor"));
    const QCommandLineOption shotOpt(QStringLiteral("screenshot"),
        QStringLiteral("Save a screenshot of the window to this file and quit."), QStringLiteral("file"));
    const QCommandLineOption afterOpt(QStringLiteral("after"),
        QStringLiteral("Milliseconds to wait before the screenshot (default 3000)."), QStringLiteral("ms"),
        QStringLiteral("3000"));
    const QCommandLineOption settingsOpt(QStringLiteral("settings"), QStringLiteral("Open the settings page at start."));
    const QCommandLineOption udsOpt(QStringLiteral("uds"),
        QStringLiteral("Open the scan tool in UDS mode on this control unit: 0 ECM, 1 TCM, 2 ABS, 3 BCM."),
        QStringLiteral("index"));
    const QCommandLineOption sizeOpt(QStringLiteral("size"),
        QStringLiteral("Window size, e.g. 1366x768 (for screenshots and testing small screens)."),
        QStringLiteral("WxH"));
    const QCommandLineOption vehicle3DOpt(QStringLiteral("vehicle-3d"),
        QStringLiteral("Open the expanded 3D vehicle workspace (requires the 3D build option)."));
    parser.addOptions({scenarioOpt, tabOpt, speedOpt, shotOpt, afterOpt, sizeOpt, settingsOpt, udsOpt, vehicle3DOpt});
    parser.process(app);

    QQmlApplicationEngine engine;
    bool hasVehicle3D = false;
#ifdef ECUSIM_HAS_VEHICLE_3D
    hasVehicle3D = true;
#endif
    engine.setInitialProperties({{QStringLiteral("vehicle3DAvailable"), hasVehicle3D},
        {QStringLiteral("vehicle3DDefault"), hasVehicle3D && qEnvironmentVariable("QT_QUICK_BACKEND") != QStringLiteral("software")}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
        [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("EcuDiagSim", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    QObject *root = engine.rootObjects().constFirst();
    auto *controller = engine.singletonInstance<AppController *>("EcuDiagSim", "App");
    if (controller) {
        if (parser.isSet(speedOpt))
            controller->setSpeedFactor(parser.value(speedOpt).toDouble());
        if (parser.isSet(scenarioOpt)) {
            const QString value = parser.value(scenarioOpt);
            bool isNumber = false;
            const int number = value.toInt(&isNumber);
            const QVariantList bundled = controller->scenarios();
            if (isNumber && number >= 1 && number <= bundled.size())
                controller->runScenario(bundled.at(number - 1).toMap().value(QStringLiteral("path")).toString());
            else
                controller->runScenario(value);
        }
    }
    if (parser.isSet(tabOpt))
        root->setProperty("currentTab", parser.value(tabOpt).toInt());
    if (parser.isSet(udsOpt))
        QMetaObject::invokeMethod(root, "openUds", Qt::QueuedConnection, Q_ARG(QVariant, parser.value(udsOpt).toInt()));
    if (parser.isSet(settingsOpt))
        QMetaObject::invokeMethod(root, "openSettings", Qt::QueuedConnection);
    if (parser.isSet(vehicle3DOpt) && hasVehicle3D)
        QMetaObject::invokeMethod(root, "openVehicle3D", Qt::QueuedConnection);
    if (parser.isSet(sizeOpt)) {
        const QStringList wh = parser.value(sizeOpt).split(QLatin1Char('x'));
        if (wh.size() == 2) {
            root->setProperty("width", wh.at(0).toInt());
            root->setProperty("height", wh.at(1).toInt());
        }
    } else if (auto *window = qobject_cast<QQuickWindow *>(root); window && window->screen()) {
        // Never open larger than the screen the window is on (not the whole virtual desktop),
        // and centre it there. Done once: later resizes are the user's.
        const InitialWindowGeometry fit = fitWindowToScreen(window->screen()->availableGeometry(),
            window->size(), window->minimumSize());
        window->setMinimumSize(fit.minimum);
        window->setGeometry(fit.geometry);
    }

    if (parser.isSet(shotOpt)) {
        QTimer::singleShot(parser.value(afterOpt).toInt(), &app, [&] {
            if (auto *window = qobject_cast<QQuickWindow *>(root))
                window->grabWindow().save(parser.value(shotOpt));
            QCoreApplication::quit();
        });
    }
    return QGuiApplication::exec();
}
