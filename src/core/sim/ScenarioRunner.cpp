#include "sim/ScenarioRunner.h"

#include "sim/Simulation.h"

#include <QFile>
#include <QJsonDocument>

#include <cmath>

namespace ecusim {

ScenarioRunner::ScenarioRunner(Simulation &sim)
    : m_sim(sim)
{
}

bool ScenarioRunner::loadFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Cannot open %1: %2").arg(path, file.errorString());
        return false;
    }
    return loadJson(file.readAll(), error);
}

bool ScenarioRunner::loadJson(const QByteArray &json, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (!doc.isObject() || !doc.object().value(QLatin1String("steps")).isArray()) {
        if (error)
            *error = parseError.error != QJsonParseError::NoError
                ? parseError.errorString()
                : QStringLiteral("scenario must be an object with a \"steps\" array");
        return false;
    }
    stop();
    const QJsonObject root = doc.object();
    m_name = root.value(QLatin1String("name")).toString(QStringLiteral("Unnamed scenario"));
    m_description = root.value(QLatin1String("description")).toString();
    m_steps = root.value(QLatin1String("steps")).toArray();
    m_log.clear();
    m_state = State::Idle;
    return true;
}

void ScenarioRunner::start()
{
    ++m_generation;
    m_log.clear();
    m_index = 0;
    m_failures = 0;
    m_wait = Wait::None;
    m_state = State::Running;
    add(ScenarioLogEntry::Level::Info, QStringLiteral("Scenario started: %1").arg(m_name));
}

void ScenarioRunner::stop()
{
    ++m_generation;
    if (m_state == State::Running) {
        m_state = State::Idle;
        add(ScenarioLogEntry::Level::Info, QStringLiteral("Scenario stopped"));
    }
    m_wait = Wait::None;
}

void ScenarioRunner::add(ScenarioLogEntry::Level level, const QString &text)
{
    const ScenarioLogEntry entry{m_now, level, text};
    m_log.append(entry);
    if (onLog)
        onLog(entry);
}

void ScenarioRunner::fail(const QString &text)
{
    ++m_failures;
    add(ScenarioLogEntry::Level::Fail, text);
}

bool ScenarioRunner::compare(double actual, const QString &op, double expected)
{
    if (op == QLatin1String("==") || op.isEmpty())
        return std::abs(actual - expected) < 1e-6;
    if (op == QLatin1String("!="))
        return std::abs(actual - expected) >= 1e-6;
    if (op == QLatin1String(">"))
        return actual > expected;
    if (op == QLatin1String(">="))
        return actual >= expected;
    if (op == QLatin1String("<"))
        return actual < expected;
    if (op == QLatin1String("<="))
        return actual <= expected;
    return false;
}

void ScenarioRunner::tick(qint64 nowMs)
{
    m_now = nowMs;
    if (m_state != State::Running)
        return;

    while (m_index < m_steps.size()) {
        switch (m_wait) {
        case Wait::Time:
            if (nowMs < m_waitUntil)
                return;
            break;
        case Wait::Signal: {
            const double actual = m_sim.signal(m_waitSignal);
            if (m_sim.hasSignal(m_waitSignal) && compare(actual, m_waitOp, m_waitValue))
                pass(QStringLiteral("%1 %2 %3 reached (%4)").arg(m_waitSignal, m_waitOp).arg(m_waitValue).arg(actual));
            else if (nowMs >= m_waitUntil)
                fail(QStringLiteral("Timeout waiting for %1 %2 %3 (is %4)")
                         .arg(m_waitSignal, m_waitOp)
                         .arg(m_waitValue)
                         .arg(actual));
            else
                return;
            break;
        }
        case Wait::Diag:
            return; // the tester callback moves on
        case Wait::None:
            execute(m_steps.at(m_index).toObject());
            if (m_wait != Wait::None)
                continue; // evaluate the new wait condition right away
            break;
        }
        m_wait = Wait::None;
        ++m_index;
    }

    m_state = m_failures == 0 ? State::Passed : State::Failed;
    add(m_failures == 0 ? ScenarioLogEntry::Level::Pass : ScenarioLogEntry::Level::Fail,
        m_failures == 0 ? QStringLiteral("Scenario passed")
                        : QStringLiteral("Scenario failed: %1 expectation(s) not met").arg(m_failures));
}

void ScenarioRunner::execute(const QJsonObject &step)
{
    const QString action = step.value(QLatin1String("action")).toString();
    const QJsonValue value = step.value(QLatin1String("value"));
    DriverInputs &driver = m_sim.driver();
    const auto act = [this](const QString &text) { add(ScenarioLogEntry::Level::Action, text); };

    if (const QString note = step.value(QLatin1String("note")).toString(); !note.isEmpty())
        add(ScenarioLogEntry::Level::Info, note);

    if (action == QLatin1String("log")) {
        add(ScenarioLogEntry::Level::Info, step.value(QLatin1String("text")).toString());
    } else if (action == QLatin1String("ignition")) {
        const bool on = value.toString() == QLatin1String("on") || value.toBool();
        driver.ignition = on ? Ignition::On : Ignition::Off;
        act(QStringLiteral("Ignition %1").arg(on ? QLatin1String("on") : QLatin1String("off")));
    } else if (action == QLatin1String("startEngine")) {
        m_sim.requestEngineStart();
        act(QStringLiteral("Start engine"));
    } else if (action == QLatin1String("warmUp")) {
        m_sim.vehicle().setCoolantTemp(88.0);
        m_sim.vehicle().setTransTemp(75.0);
        act(QStringLiteral("Engine and transmission at operating temperature"));
    } else if (action == QLatin1String("selector")) {
        const int index = static_cast<int>(QStringLiteral("PRND").indexOf(value.toString().toUpper()));
        if (index < 0) {
            fail(QStringLiteral("Unknown selector position '%1'").arg(value.toString()));
        } else {
            driver.selector = static_cast<Selector>(index);
            act(QStringLiteral("Selector %1").arg(value.toString().toUpper()));
        }
    } else if (action == QLatin1String("throttle")) {
        driver.throttlePedal = value.toDouble() / 100.0;
        act(QStringLiteral("Accelerator pedal %1 %").arg(value.toDouble()));
    } else if (action == QLatin1String("brake")) {
        driver.brakePedal = value.toDouble() / 100.0;
        act(QStringLiteral("Brake pedal %1 %").arg(value.toDouble()));
    } else if (action == QLatin1String("headlamps")) {
        const QString v = value.toString();
        driver.headlamps = v == QLatin1String("low") ? 2 : v == QLatin1String("park") ? 1 : 0;
        act(QStringLiteral("Headlamps %1").arg(v));
    } else if (action == QLatin1String("input")) {
        const QString name = step.value(QLatin1String("name")).toString();
        const bool on = value.toBool();
        if (name == QLatin1String("highBeam"))
            driver.highBeam = on;
        else if (name == QLatin1String("turnLeft"))
            driver.turnLeft = on;
        else if (name == QLatin1String("turnRight"))
            driver.turnRight = on;
        else if (name == QLatin1String("hazard"))
            driver.hazard = on;
        else if (name == QLatin1String("doorLock"))
            driver.doorLock = on;
        else {
            fail(QStringLiteral("Unknown input '%1'").arg(name));
            return;
        }
        act(QStringLiteral("%1 %2").arg(name, on ? QLatin1String("on") : QLatin1String("off")));
    } else if (action == QLatin1String("wait")) {
        m_wait = Wait::Time;
        m_waitUntil = m_now + step.value(QLatin1String("ms")).toInt();
    } else if (action == QLatin1String("waitFor")) {
        m_wait = Wait::Signal;
        m_waitSignal = step.value(QLatin1String("signal")).toString();
        m_waitOp = step.value(QLatin1String("op")).toString(QStringLiteral("=="));
        m_waitValue = value.toDouble();
        m_waitUntil = m_now + step.value(QLatin1String("timeout")).toInt(10000);
    } else if (action == QLatin1String("inject")) {
        const QString target = step.value(QLatin1String("target")).toString();
        Fault fault;
        fault.type = faultTypeFromName(step.value(QLatin1String("fault")).toString());
        fault.hasP1 = step.contains(QLatin1String("p1"));
        fault.p1 = step.value(QLatin1String("p1")).toDouble();
        fault.p2 = step.value(QLatin1String("p2")).toDouble();
        if (m_sim.injectFault(target, fault))
            act(QStringLiteral("Fault injected: %1 - %2").arg(target, faultTypeLabel(fault.type)));
        else
            fail(QStringLiteral("Cannot inject '%1' into '%2'")
                     .arg(step.value(QLatin1String("fault")).toString(), target));
    } else if (action == QLatin1String("clearFault")) {
        const QString target = step.value(QLatin1String("target")).toString(QStringLiteral("all"));
        if (target == QLatin1String("all"))
            m_sim.clearAllFaults();
        else
            m_sim.clearFault(target);
        act(QStringLiteral("Fault removed: %1").arg(target));
    } else if (action == QLatin1String("expect")) {
        expect(step);
    } else if (action == QLatin1String("uds")) {
        sendUds(step);
    } else if (action == QLatin1String("unlock")) {
        unlock(step.value(QLatin1String("ecu")).toString());
    } else {
        fail(QStringLiteral("Unknown action '%1'").arg(action));
    }
}

void ScenarioRunner::expect(const QJsonObject &step)
{
    if (step.contains(QLatin1String("signal"))) {
        const QString name = step.value(QLatin1String("signal")).toString();
        const QString op = step.value(QLatin1String("op")).toString(QStringLiteral("=="));
        const double expected = step.value(QLatin1String("value")).toDouble();
        const double actual = m_sim.signal(name);
        const QString text = QStringLiteral("%1 %2 %3 (is %4)").arg(name, op).arg(expected).arg(actual);
        if (m_sim.hasSignal(name) && compare(actual, op, expected))
            pass(text);
        else
            fail(text);
    } else if (step.contains(QLatin1String("dtc"))) {
        const QString ecuName = step.value(QLatin1String("ecu")).toString();
        const QString codeText = step.value(QLatin1String("dtc")).toString();
        const QString status = step.value(QLatin1String("status")).toString(QStringLiteral("confirmed"));
        const Ecu *ecu = m_sim.ecu(ecuName);
        const quint32 code = dtcFromString(codeText);
        if (!ecu || code == 0xFFFFFFFF) {
            fail(QStringLiteral("Bad DTC expectation: %1 in %2").arg(codeText, ecuName));
            return;
        }
        const bool exact = QString(codeText).remove(QLatin1Char('-')).size() == 7;
        quint8 combined = 0;
        for (const DtcEntry &e : ecu->dtcs().entries()) {
            if (exact ? e.def.code == code : (e.def.code >> 8) == (code >> 8))
                combined |= e.status;
        }
        bool ok = false;
        if (status == QLatin1String("confirmed"))
            ok = combined & DtcStatus::Confirmed;
        else if (status == QLatin1String("pending"))
            ok = combined & DtcStatus::Pending;
        else if (status == QLatin1String("testFailed"))
            ok = combined & DtcStatus::TestFailed;
        else if (status == QLatin1String("warning"))
            ok = combined & DtcStatus::WarningIndicatorRequested;
        else if (status == QLatin1String("stored"))
            ok = combined & (DtcStatus::Pending | DtcStatus::Confirmed);
        else if (status == QLatin1String("absent"))
            ok = !(combined & (DtcStatus::Pending | DtcStatus::Confirmed));
        const QString text = QStringLiteral("%1 %2 is %3 (status 0x%4)")
                                 .arg(ecuName, codeText, status)
                                 .arg(combined, 2, 16, QLatin1Char('0'));
        if (ok)
            pass(text);
        else
            fail(text);
    } else if (step.contains(QLatin1String("lamp"))) {
        const QString lamp = step.value(QLatin1String("lamp")).toString();
        const bool expected = step.value(QLatin1String("value")).toBool(true);
        const ClusterState &c = m_sim.ipc().state();
        bool actual = false;
        if (lamp == QLatin1String("mil"))
            actual = c.mil;
        else if (lamp == QLatin1String("abs"))
            actual = c.abs;
        else if (lamp == QLatin1String("tcs"))
            actual = c.tcs;
        else if (lamp == QLatin1String("battery"))
            actual = c.battery;
        else if (lamp == QLatin1String("coolant"))
            actual = c.coolantWarn;
        else if (lamp == QLatin1String("trans"))
            actual = c.transFault;
        else if (lamp == QLatin1String("lampFault"))
            actual = c.lampFault;
        else {
            fail(QStringLiteral("Unknown lamp '%1'").arg(lamp));
            return;
        }
        const QString text = QStringLiteral("Cluster lamp '%1' is %2")
                                 .arg(lamp, actual ? QLatin1String("on") : QLatin1String("off"));
        if (actual == expected)
            pass(text);
        else
            fail(text + QStringLiteral(" - expected ") + (expected ? QLatin1String("on") : QLatin1String("off")));
    } else {
        fail(QStringLiteral("expect needs \"signal\", \"dtc\" or \"lamp\""));
    }
}

void ScenarioRunner::diagDone()
{
    m_wait = Wait::None;
    ++m_index;
}

void ScenarioRunner::sendUds(const QJsonObject &step)
{
    const QString ecuName = step.value(QLatin1String("ecu")).toString();
    const quint32 id = DiagnosticTester::requestIdForEcu(ecuName);
    const QByteArray request = fromHex(step.value(QLatin1String("request")).toString());
    if (id == 0 || request.isEmpty()) {
        fail(QStringLiteral("Bad uds step: ecu '%1', request '%2'")
                 .arg(ecuName, step.value(QLatin1String("request")).toString()));
        return;
    }
    add(ScenarioLogEntry::Level::Action, QStringLiteral("%1 <- %2").arg(ecuName, toHex(request)));
    m_wait = Wait::Diag;
    m_sim.tester().request(id, request, [this, step, generation = m_generation](const DiagResult &result) {
        if (generation != m_generation)
            return; // the scenario was stopped or restarted meanwhile
        checkUds(step, result);
        diagDone();
    });
}

void ScenarioRunner::checkUds(const QJsonObject &step, const DiagResult &result)
{
    if (result.responses.isEmpty()) {
        if (step.value(QLatin1String("expectNoResponse")).toBool())
            pass(QStringLiteral("No response, as expected"));
        else
            fail(QStringLiteral("No response (timeout)"));
        return;
    }
    QStringList all;
    for (const DiagResponse &r : result.responses)
        all << QStringLiteral("%1 -> %2").arg(r.ecu, toHex(r.data));
    const QString received = all.join(QStringLiteral(" | "));

    const QString prefix = toHex(fromHex(step.value(QLatin1String("expect")).toString()));
    const QString contains = toHex(fromHex(step.value(QLatin1String("expectContains")).toString()));
    const QString notContains = toHex(fromHex(step.value(QLatin1String("expectNotContains")).toString()));
    bool ok = true;
    bool anyPrefix = prefix.isEmpty();
    bool anyContains = contains.isEmpty();
    for (const DiagResponse &r : result.responses) {
        const QString hex = toHex(r.data);
        anyPrefix = anyPrefix || hex.startsWith(prefix);
        anyContains = anyContains || hex.contains(contains);
        if (!notContains.isEmpty() && hex.contains(notContains))
            ok = false;
    }
    ok = ok && anyPrefix && anyContains;
    if (step.value(QLatin1String("expectNoResponse")).toBool())
        ok = false;
    if (ok)
        pass(received);
    else
        fail(QStringLiteral("Unexpected response: %1").arg(received));
}

void ScenarioRunner::unlock(const QString &ecuName)
{
    const quint32 id = DiagnosticTester::requestIdForEcu(ecuName);
    if (id == 0 || id == DiagId::Functional) {
        fail(QStringLiteral("unlock: unknown ECU '%1'").arg(ecuName));
        return;
    }
    add(ScenarioLogEntry::Level::Action,
        QStringLiteral("%1: extended session + security access").arg(ecuName));
    m_wait = Wait::Diag;
    const int generation = m_generation;
    DiagnosticTester &tester = m_sim.tester();
    tester.request(id, fromHex(QStringLiteral("1003")));
    tester.request(id, fromHex(QStringLiteral("2701")), [this, id, generation, ecuName](const DiagResult &seedResult) {
        if (generation != m_generation)
            return;
        const DiagResponse *r = seedResult.positive();
        if (!r || r->data.size() < 6) {
            fail(QStringLiteral("%1: no seed received").arg(ecuName));
            diagDone();
            return;
        }
        quint32 seed = 0;
        for (int i = 2; i < 6; ++i)
            seed = (seed << 8) | static_cast<quint8>(r->data[i]);
        const quint32 key = securityKeyFromSeed(seed);
        QByteArray keyRequest = fromHex(QStringLiteral("2702"));
        for (int shift = 24; shift >= 0; shift -= 8)
            keyRequest.append(static_cast<char>((key >> shift) & 0xFF));
        m_sim.tester().request(id, keyRequest, [this, generation, ecuName](const DiagResult &keyResult) {
            if (generation != m_generation)
                return;
            if (keyResult.positive())
                pass(QStringLiteral("%1 unlocked").arg(ecuName));
            else
                fail(QStringLiteral("%1: security access failed").arg(ecuName));
            diagDone();
        });
    });
}

} // namespace ecusim
