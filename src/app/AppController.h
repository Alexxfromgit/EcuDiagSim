#pragma once

#include "ScannerViewModel.h"
#include "TraceModel.h"
#include "UpdateService.h"
#include "sim/Simulation.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

/// The application's single QML entry point ("App"): owns the simulation, drives it in
/// real time and exposes driver inputs, cluster state, faults, scenarios and the
/// trace/scanner view models.
class AppController : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(App)
    QML_SINGLETON

    // --- simulation control ---
    Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged)
    Q_PROPERTY(double speedFactor READ speedFactor WRITE setSpeedFactor NOTIFY speedFactorChanged)
    Q_PROPERTY(double simTime READ simTime NOTIFY frameUpdated)
    Q_PROPERTY(double busLoad READ busLoad NOTIFY slowUpdated)
    Q_PROPERTY(double frameRate READ frameRate NOTIFY slowUpdated)
    Q_PROPERTY(QString errorText READ errorText CONSTANT)

    // --- driver inputs ---
    Q_PROPERTY(int ignition READ ignition WRITE setIgnition NOTIFY inputsChanged)
    Q_PROPERTY(double throttle READ throttle WRITE setThrottle NOTIFY inputsChanged)
    Q_PROPERTY(double brake READ brake WRITE setBrake NOTIFY inputsChanged)
    Q_PROPERTY(int selector READ selector WRITE setSelector NOTIFY inputsChanged)
    Q_PROPERTY(int headlamps READ headlamps WRITE setHeadlamps NOTIFY inputsChanged)
    Q_PROPERTY(bool highBeam READ highBeam WRITE setHighBeam NOTIFY inputsChanged)
    Q_PROPERTY(bool turnLeft READ turnLeft WRITE setTurnLeft NOTIFY inputsChanged)
    Q_PROPERTY(bool turnRight READ turnRight WRITE setTurnRight NOTIFY inputsChanged)
    Q_PROPERTY(bool hazard READ hazard WRITE setHazard NOTIFY inputsChanged)
    Q_PROPERTY(bool doorLock READ doorLock WRITE setDoorLock NOTIFY inputsChanged)
    Q_PROPERTY(QVariantList doors READ doors NOTIFY inputsChanged)

    // --- vehicle state for the views ---
    Q_PROPERTY(QVariantMap cluster READ cluster NOTIFY frameUpdated)
    Q_PROPERTY(QVariantMap vehicle READ vehicle NOTIFY frameUpdated)
    Q_PROPERTY(QVariantList ecus READ ecus NOTIFY slowUpdated)
    Q_PROPERTY(QVariantList sensors READ sensors NOTIFY slowUpdated)

    // --- faults ---
    Q_PROPERTY(QVariantList faultTargets READ faultTargets CONSTANT)
    Q_PROPERTY(QVariantList activeFaults READ activeFaults NOTIFY faultsChanged)

    // --- scenarios and replay ---
    Q_PROPERTY(QVariantList scenarios READ scenarios CONSTANT)
    Q_PROPERTY(QVariantList scenarioLog READ scenarioLog NOTIFY scenarioChanged)
    Q_PROPERTY(QString scenarioState READ scenarioState NOTIFY scenarioChanged)
    Q_PROPERTY(QString scenarioName READ scenarioName NOTIFY scenarioChanged)
    Q_PROPERTY(double scenarioProgress READ scenarioProgress NOTIFY slowUpdated)
    Q_PROPERTY(bool replaying READ replaying NOTIFY slowUpdated)
    Q_PROPERTY(double replayProgress READ replayProgress NOTIFY slowUpdated)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)

    Q_PROPERTY(TraceModel *trace READ trace CONSTANT)
    Q_PROPERTY(ScannerViewModel *scanner READ scanner CONSTANT)
    Q_PROPERTY(UpdateService *updates READ updates CONSTANT)

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

    ecusim::Simulation &sim() { return *m_sim; }

    bool running() const { return m_running; }
    void setRunning(bool running);
    double speedFactor() const { return m_speed; }
    void setSpeedFactor(double factor);
    double simTime() const { return static_cast<double>(m_sim->nowMs()) / 1000.0; }
    double busLoad() const { return m_sim->bus().busLoad(); }
    double frameRate() const { return m_frameRate; }
    QString errorText() const { return m_sim->errorString(); }

    int ignition() const { return m_sim->driver().ignition == ecusim::Ignition::Off ? 0 : 1; }
    void setIgnition(int on);
    double throttle() const { return m_sim->driver().throttlePedal * 100.0; }
    void setThrottle(double percent);
    double brake() const { return m_sim->driver().brakePedal * 100.0; }
    void setBrake(double percent);
    int selector() const { return static_cast<int>(m_sim->driver().selector); }
    void setSelector(int position);
    int headlamps() const { return m_sim->driver().headlamps; }
    void setHeadlamps(int mode);
    bool highBeam() const { return m_sim->driver().highBeam; }
    void setHighBeam(bool on);
    bool turnLeft() const { return m_sim->driver().turnLeft; }
    void setTurnLeft(bool on);
    bool turnRight() const { return m_sim->driver().turnRight; }
    void setTurnRight(bool on);
    bool hazard() const { return m_sim->driver().hazard; }
    void setHazard(bool on);
    bool doorLock() const { return m_sim->driver().doorLock; }
    void setDoorLock(bool on);
    QVariantList doors() const;

    QVariantMap cluster() const;
    QVariantMap vehicle() const;
    QVariantList ecus() const;
    QVariantList sensors() const;
    QVariantList faultTargets() const;
    QVariantList activeFaults() const;

    QVariantList scenarios() const { return m_scenarios; }
    QVariantList scenarioLog() const { return m_scenarioLog; }
    QString scenarioState() const;
    QString scenarioName() const;
    double scenarioProgress() const;
    bool replaying() const { return m_sim->replaying(); }
    double replayProgress() const { return m_sim->replayProgress(); }
    QString message() const { return m_message; }

    TraceModel *trace() { return m_trace; }
    ScannerViewModel *scanner() { return m_scanner; }
    UpdateService *updates() { return m_updates; }

    Q_INVOKABLE void step(int milliseconds);
    Q_INVOKABLE void reset();
    Q_INVOKABLE void startEngine();
    Q_INVOKABLE void setDoor(int index, bool ajar);

    Q_INVOKABLE bool injectFault(const QString &targetId, const QString &faultType, const QString &p1,
        const QString &p2);
    Q_INVOKABLE void clearFault(const QString &targetId);
    Q_INVOKABLE void clearAllFaults();
    Q_INVOKABLE QString faultHint(const QString &targetId, const QString &faultType) const;

    Q_INVOKABLE void runScenario(const QString &path);
    Q_INVOKABLE void runScenarioFile(const QUrl &file);
    Q_INVOKABLE void stopScenario();

    Q_INVOKABLE void saveTrace(const QUrl &file);
    Q_INVOKABLE void loadReplay(const QUrl &file);
    Q_INVOKABLE void stopReplay();

signals:
    void runningChanged();
    void speedFactorChanged();
    void frameUpdated(); ///< after every batch of simulation steps (display rate)
    void slowUpdated();  ///< a few times per second
    void inputsChanged();
    void faultsChanged();
    void scenarioChanged();
    void messageChanged();

private:
    void createSimulation();
    void onTimer();
    void advance(int steps);
    void setMessage(const QString &text);

    std::unique_ptr<ecusim::Simulation> m_sim;
    TraceModel *m_trace;
    ScannerViewModel *m_scanner;
    UpdateService *m_updates;
    QTimer m_timer;
    QElapsedTimer m_clock;
    double m_debtMs = 0.0;
    bool m_running = true;
    double m_speed = 1.0;
    int m_slowCounter = 0;
    quint64 m_lastFrameCount = 0;
    qint64 m_lastRateSimMs = 0;
    double m_frameRate = 0.0;
    int m_lastActiveFaults = 0;
    ecusim::DriverInputs m_lastInputs;
    QVariantList m_scenarios;
    QVariantList m_scenarioLog;
    QString m_message;
};
