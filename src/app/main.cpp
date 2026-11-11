#include "AppController.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("EcuDiagSim"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("ECU Diagnostics Simulator"));
    QGuiApplication::setApplicationVersion(QStringLiteral(ECUSIM_VERSION));
    QGuiApplication::setOrganizationName(QStringLiteral("EcuDiagSim"));
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
    const QCommandLineOption sizeOpt(QStringLiteral("size"),
        QStringLiteral("Window size, e.g. 1366x768 (for screenshots and testing small screens)."),
        QStringLiteral("WxH"));
    parser.addOptions({scenarioOpt, tabOpt, speedOpt, shotOpt, afterOpt, sizeOpt});
    parser.process(app);

    QQmlApplicationEngine engine;
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
    if (parser.isSet(sizeOpt)) {
        const QStringList wh = parser.value(sizeOpt).split(QLatin1Char('x'));
        if (wh.size() == 2) {
            root->setProperty("width", wh.at(0).toInt());
            root->setProperty("height", wh.at(1).toInt());
        }
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
