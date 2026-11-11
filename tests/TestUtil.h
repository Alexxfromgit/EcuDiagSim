#pragma once

#include "sim/Simulation.h"
#include "tester/Decode.h"

#include <QTest>

namespace testutil {

using namespace ecusim;

inline QByteArray hex(const char *text)
{
    return fromHex(QString::fromLatin1(text));
}

/// Sends a diagnostic request and runs the simulation until the exchange is complete.
inline DiagResult exchange(Simulation &sim, quint32 requestId, const QByteArray &request)
{
    DiagResult result;
    bool done = false;
    sim.tester().request(requestId, request, [&](const DiagResult &r) {
        result = r;
        done = true;
    });
    sim.runUntil([&] { return done; }, 10000);
    return result;
}

/// Physical request; returns the response bytes (empty on timeout).
inline QByteArray ask(Simulation &sim, quint32 requestId, const char *requestHex)
{
    const DiagResult r = exchange(sim, requestId, hex(requestHex));
    return r.responses.isEmpty() ? QByteArray() : r.responses.first().data;
}

/// Ignition on, warm engine running, bulb check over.
inline void startEngine(Simulation &sim)
{
    sim.driver().ignition = Ignition::On;
    sim.vehicle().setCoolantTemp(88.0);
    sim.vehicle().setTransTemp(75.0);
    sim.requestEngineStart();
    QVERIFY(sim.runUntil([&] { return sim.ecm().engineRunning(); }, 5000));
    sim.run(3500);
}

/// Accelerates in D until the given speed is reached, then holds a light throttle.
inline void driveTo(Simulation &sim, double kph, double throttle = 0.4)
{
    sim.driver().selector = Selector::D;
    sim.driver().throttlePedal = throttle;
    QVERIFY(sim.runUntil([&] { return sim.vehicle().speedKph() >= kph; }, 60000));
    sim.driver().throttlePedal = 0.15;
}

inline void ignitionCycle(Simulation &sim, int offMs = 500)
{
    sim.driver().ignition = Ignition::Off;
    sim.run(offMs);
    sim.driver().ignition = Ignition::On;
    sim.run(100);
}

} // namespace testutil
