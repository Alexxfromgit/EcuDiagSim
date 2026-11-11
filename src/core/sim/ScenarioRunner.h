#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <functional>

namespace ecusim {

class Simulation;
struct DiagResult;

struct ScenarioLogEntry
{
    enum class Level { Info, Action, Pass, Fail };
    qint64 timeMs = 0;
    Level level = Level::Info;
    QString text;
};

/// Runs a scripted lesson: a JSON list of steps that operate the vehicle, inject faults,
/// talk to ECUs with the scan tool and check expectations. Steps run in order; "wait",
/// "waitFor" and "uds" block until done. See docs/scenarios.md for the format.
class ScenarioRunner
{
public:
    enum class State { Idle, Running, Passed, Failed };

    explicit ScenarioRunner(Simulation &sim);

    bool loadFile(const QString &path, QString *error = nullptr);
    bool loadJson(const QByteArray &json, QString *error = nullptr);

    void start();
    void stop();
    void tick(qint64 nowMs);

    State state() const { return m_state; }
    bool finished() const { return m_state == State::Passed || m_state == State::Failed; }
    QString name() const { return m_name; }
    QString description() const { return m_description; }
    int stepIndex() const { return m_index; }
    int stepCount() const { return static_cast<int>(m_steps.size()); }
    int failures() const { return m_failures; }
    const QList<ScenarioLogEntry> &log() const { return m_log; }

    std::function<void(const ScenarioLogEntry &)> onLog;

    static bool compare(double actual, const QString &op, double expected);

private:
    enum class Wait { None, Time, Signal, Diag };

    void execute(const QJsonObject &step);
    void expect(const QJsonObject &step);
    void sendUds(const QJsonObject &step);
    void unlock(const QString &ecuName);
    void checkUds(const QJsonObject &step, const DiagResult &result);
    void add(ScenarioLogEntry::Level level, const QString &text);
    void pass(const QString &text) { add(ScenarioLogEntry::Level::Pass, text); }
    void fail(const QString &text);
    void diagDone();

    Simulation &m_sim;
    QString m_name;
    QString m_description;
    QJsonArray m_steps;
    QList<ScenarioLogEntry> m_log;
    State m_state = State::Idle;
    int m_index = 0;
    int m_failures = 0;
    int m_generation = 0;
    qint64 m_now = 0;

    Wait m_wait = Wait::None;
    qint64 m_waitUntil = 0;
    QString m_waitSignal;
    QString m_waitOp;
    double m_waitValue = 0.0;
};

} // namespace ecusim
