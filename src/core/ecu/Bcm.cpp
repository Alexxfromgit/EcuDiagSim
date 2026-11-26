#include "ecu/Ecus.h"

namespace ecusim {

Bcm::Bcm(SimContext &ctx)
    : Ecu(ctx, {QStringLiteral("BCM"), QStringLiteral("Body control module"), DiagId::BcmRequest, true, false,
               QStringLiteral("BCM_5.0.3"), QStringLiteral("0263B40021")})
{
    using namespace dtc;
    subscribe(msg::EcmStatus, 1000, U0100);
    subscribe(msg::TcmStatus, 500, U0101);
    subscribe(msg::AbsStatus, 500, U0121);

    auto &d = dtcs();
    d.define(dtcDef(B1318, 30));
    d.define(dtcDef(B1601, 5));
    d.define(networkDtc(U0100));
    d.define(networkDtc(U0101));
    d.define(networkDtc(U0121));
    d.define(dtcDef(U3000NotConfigured, 5));
    subscribe(msg::EcmEngine, 500);
    m_tpms = ctx.config.has(Equipment::Tpms);
    m_coding = defaultCoding();

    auto &s = diag();
    // Variant coding: which body functions this car has (see Bcm::Coding).
    s.addDid(0x0100, [this] { return encU8(m_codingBlank ? 0xFF : m_coding); });
    s.addWritableDid(0x0100, 1, [this](const QByteArray &data) -> quint8 {
        const quint8 coding = static_cast<quint8>(data[0]);
        if (coding & ~(LampMonitoring | AutoLock | DaytimeLights | (m_tpms ? Tpms : 0)))
            return Nrc::RequestOutOfRange;
        m_coding = coding;
        if (m_codingBlank) {
            m_codingBlank = false;
            if (onConfigured)
                onConfigured(QStringLiteral("bcm"));
        }
        return 0;
    });
    s.addDid(0x4001, [this] { return encU8(m_ctx.vehicle.batteryVoltage() * 10.0); });
    // Lamp outputs as the BCM drives them; turn signals as "flashing", independent of the blink phase.
    s.addDid(0x4002, [this] {
        const DriverInputs &in = m_ctx.driver;
        return encU8((m_lowBeam ? 0x01 : 0) | (m_highBeam ? 0x02 : 0) | (m_park ? 0x04 : 0)
            | ((in.turnLeft || in.hazard) ? 0x08 : 0) | ((in.turnRight || in.hazard) ? 0x10 : 0));
    });
    s.addDid(0x4003, [this] {
        const auto &doors = m_ctx.driver.doorAjar;
        return encU8((doors[0] ? 0x01 : 0) | (doors[1] ? 0x02 : 0) | (doors[2] ? 0x04 : 0)
            | (doors[3] ? 0x08 : 0) | (m_locked ? 0x80 : 0));
    });
    s.addDid(0xF442, [this] { return encU16(m_ctx.vehicle.batteryVoltage() * 1000.0); });
    // Switch inputs as the BCM reads them: bit0 park brake.
    s.addDid(0x4004, [this] { return encU8(parkBrakeSwitch() ? 0x01 : 0x00); });

    auto forced = [](int &target) {
        return [&target](bool take, const QByteArray &state) {
            if (!take) {
                target = -1;
                return true;
            }
            if (state.size() != 1 || static_cast<quint8>(state[0]) > 1)
                return false;
            target = state[0];
            return true;
        };
    };
    s.addIoControl(0x4010, forced(m_lowBeamForced), [this] { return encU8(m_lowBeam ? 1 : 0); });
    s.addIoControl(0x4011, forced(m_lockForced), [this] { return encU8(m_locked ? 1 : 0); });
    setSnapshotDids({0x4001, 0x4002});

    // --- tyre pressure monitoring: radio receiver, learned IDs, relearn routine ---
    if (m_tpms) {
        for (int i = 0; i < 4; ++i) {
            const auto idx = static_cast<size_t>(i);
            m_tpmsIds[idx] = ctx.sensors.tpms[idx].factoryId;
            d.define(dtcDef(tpmsSensor(i), 1));
            s.addDid(static_cast<quint16>(0x4020 + i), [this, idx] { return encU8(m_tpmsBar[idx] / 0.02); });
            const auto id = static_cast<quint16>(0x0310 + i);
            s.addDid(id, [this, idx] {
                QByteArray a;
                for (int shift = 24; shift >= 0; shift -= 8)
                    a.append(static_cast<char>((m_tpmsIds[idx] >> shift) & 0xFF));
                return a;
            });
            // Manual entry of the ID printed on a new sensor.
            s.addWritableDid(id, 4, [this, idx](const QByteArray &data) -> quint8 {
                quint32 v = 0;
                for (char c : data)
                    v = (v << 8) | static_cast<quint8>(c);
                if (v == 0 || v == 0xFFFFFFFF)
                    return Nrc::RequestOutOfRange;
                m_tpmsIds[idx] = v;
                m_tpmsUnknown.removeAll(v);
                return 0;
            });
        }
        s.addDid(0x4024, [this] {
            QByteArray a;
            for (double t : m_tpmsTemp)
                a.append(encU8(t + 40.0));
            return a;
        });
        // IDs received that belong to no wheel: what a new sensor sends before it is learned.
        s.addDid(0x0314, [this] {
            QByteArray a(1, static_cast<char>(m_tpmsUnknown.size()));
            for (quint32 v : std::as_const(m_tpmsUnknown))
                for (int shift = 24; shift >= 0; shift -= 8)
                    a.append(static_cast<char>((v >> shift) & 0xFF));
            return a;
        });
        DiagnosticServer::Routine relearn;
        relearn.start = [this](const QByteArray &option, QByteArray &status) -> quint8 {
            if (!option.isEmpty())
                return Nrc::IncorrectMessageLength;
            const bool moving = !rxTimedOut(msg::AbsStatus) && rx(msg::AbsStatus, QStringLiteral("VehicleSpeed")) > 3.0;
            if (m_ctx.driver.ignition == Ignition::Off || moving || !tpmsActive())
                return Nrc::ConditionsNotCorrect;
            m_tpmsLearn = Learn::Running;
            m_tpmsLearnStart = now();
            m_tpmsLearnedMask = 0;
            status = tpmsLearnStatus();
            return 0;
        };
        relearn.stop = [this](QByteArray &status) -> quint8 {
            if (m_tpmsLearn != Learn::Running)
                return Nrc::RequestSequenceError;
            m_tpmsLearn = Learn::Aborted;
            status = tpmsLearnStatus();
            return 0;
        };
        relearn.results = [this] { return tpmsLearnStatus(); };
        s.addRoutine(kTpmsRelearn, relearn);
    }

    every(20, [this] { task20ms(); });
    every(100, [this] { task100ms(); });
}

bool Bcm::tpmsActive() const
{
    return m_tpms && !m_codingBlank && (m_coding & Tpms);
}

QByteArray Bcm::tpmsLearnStatus() const
{
    QByteArray a = encU8(static_cast<int>(m_tpmsLearn));
    a.append(encU8(m_tpmsLearnedMask)); // bit n: wheel n (FL, FR, RL, RR) learned
    return a;
}

void Bcm::tpms100ms(bool ignition, double speed)
{
    Q_UNUSED(speed);
    // The receiver works with the ignition on; at ignition on every sensor gets a fresh timeout.
    if (ignition && !m_lastIgnition) {
        m_tpmsLastRx.fill(now());
        m_tpmsFresh.fill(false);
    }
    m_lastIgnition = ignition;
    if (!ignition || !tpmsActive()) {
        m_tpmsLamp = 0;
        return;
    }

    // Each sensor transmits once a second, the four of them staggered.
    for (int i = 0; i < 4; ++i) {
        const auto idx = static_cast<size_t>(i);
        const TpmsSensor &sensor = m_ctx.sensors.tpms[idx];
        if (sensor.dead || (now() + i * 250) % 1000 >= 100)
            continue;
        if (m_tpmsLearn == Learn::Running) {
            // The scan tool triggers the sensors wheel by wheel: the position is known.
            m_tpmsIds[idx] = sensor.id;
            m_tpmsLearnedMask |= static_cast<quint8>(1u << i);
            m_tpmsUnknown.removeAll(sensor.id);
        }
        int wheel = -1;
        for (int k = 0; k < 4; ++k)
            if (m_tpmsIds[static_cast<size_t>(k)] == sensor.id)
                wheel = k;
        if (wheel < 0) {
            if (!m_tpmsUnknown.contains(sensor.id) && m_tpmsUnknown.size() < 4)
                m_tpmsUnknown.append(sensor.id);
            continue;
        }
        const auto w = static_cast<size_t>(wheel);
        m_tpmsLastRx[w] = now();
        m_tpmsFresh[w] = true;
        m_tpmsBar[w] = m_ctx.vehicle.tyrePressure(i);
        m_tpmsTemp[w] = m_ctx.vehicle.tyreTemp(i);
    }
    // A replaced sensor whose ID is now known: the "not configured" fault is over.
    static const char *kTargets[4] = {"tpms_fl", "tpms_fr", "tpms_rl", "tpms_rr"};
    for (int i = 0; i < 4; ++i) {
        TpmsSensor &sensor = m_ctx.sensors.tpms[static_cast<size_t>(i)];
        if (sensor.id != sensor.installedId && m_tpmsIds[static_cast<size_t>(i)] == sensor.id) {
            sensor.installedId = sensor.id; // the new sensor is the configured one from now on
            if (onConfigured)
                onConfigured(QString::fromLatin1(kTargets[i]));
        }
    }
    if (m_tpmsLearn == Learn::Running) {
        if (m_tpmsLearnedMask == 0x0F)
            m_tpmsLearn = Learn::Completed;
        else if (now() - m_tpmsLearnStart > kTpmsLearnMs)
            m_tpmsLearn = Learn::Failed;
    }

    bool systemFault = false;
    bool low = false;
    for (int i = 0; i < 4; ++i) {
        const auto idx = static_cast<size_t>(i);
        const bool missing = now() - m_tpmsLastRx[idx] > kTpmsTimeoutMs;
        // Pass only on a real message: until the timeout runs out there is simply no information,
        // so a sensor that was missing last cycle stays failed and the lamp flashes from the start.
        if (missing)
            dtcs().report(dtc::tpmsSensor(i), true);
        else if (m_tpmsFresh[idx])
            dtcs().report(dtc::tpmsSensor(i), false);
        if (missing)
            m_tpmsWasMissing[idx] = true;
        else if (m_tpmsFresh[idx])
            m_tpmsWasMissing[idx] = false;
        systemFault = systemFault || m_tpmsWasMissing[idx];
        // Compared at 20 degC, as the placard pressure is a cold value.
        const double absolute = m_tpmsBar[idx] + 1.013;
        const double cold = absolute * (VehicleModel::kAmbientTemp + 273.15) / (m_tpmsTemp[idx] + 273.15) - 1.013;
        low = low || (m_tpmsFresh[idx] && !missing && cold < kTpmsWarnBar);
    }
    m_tpmsLamp = systemFault ? 2 : (low ? 1 : 0);
}

void Bcm::setCodingBlank(bool blank)
{
    if (blank) {
        m_codingBlank = true;
    } else if (m_codingBlank) {
        m_codingBlank = false;
        m_coding = defaultCoding();
    }
}

void Bcm::onSleep()
{
    // No supply, no lamp current: every exterior lamp the BCM drives goes dark.
    m_lowBeam = m_highBeam = m_park = m_turnLeftOut = m_turnRightOut = false;
}

void Bcm::task20ms()
{
    const DriverInputs &in = m_ctx.driver;
    m_lowBeam = m_lowBeamForced >= 0 ? m_lowBeamForced == 1 : in.headlamps == 2;
    m_highBeam = in.highBeam;
    const auto coded = [this](quint8 function) { return !m_codingBlank && (m_coding & function); };
    // Daytime running lights: the position lamps while the engine runs.
    const bool engineRunning = !rxTimedOut(msg::EcmEngine) && rx(msg::EcmEngine, QStringLiteral("EngineRPM")) > 400.0;
    const bool drl = coded(DaytimeLights) && in.ignition != Ignition::Off && engineRunning;
    m_park = in.headlamps >= 1 || m_lowBeam || drl;
    m_locked = m_lockForced >= 0 ? m_lockForced == 1 : (in.doorLock || m_autoLocked);
    m_blink = (now() / 400) % 2 == 0;
    m_turnLeftOut = (in.turnLeft || in.hazard) && m_blink;
    m_turnRightOut = (in.turnRight || in.hazard) && m_blink;
}

void Bcm::task100ms()
{
    using namespace dtc;
    const DriverInputs &in = m_ctx.driver;
    const VehicleModel &v = m_ctx.vehicle;
    const bool ignition = in.ignition != Ignition::Off;

    const auto coded = [this](quint8 function) { return !m_codingBlank && (m_coding & function); };
    dtcs().report(U3000NotConfigured, m_codingBlank);

    if (ignition)
        dtcs().report(B1318, v.batteryVoltage() < 11.0);
    // Lamp outputs are current monitored: commanded on but no current = open circuit.
    if (m_lowBeam && coded(LampMonitoring))
        dtcs().report(B1601, v.bulbLowLeftOpen);

    // Auto-lock above 15 km/h with all doors shut; ends with ignition off, an opened door or
    // a press on the central locking switch. It locks once per trip: a driver who unlocks while
    // driving stays unlocked until a door was opened or the ignition switched off.
    const bool anyDoorOpen = in.doorAjar[0] || in.doorAjar[1] || in.doorAjar[2] || in.doorAjar[3];
    const double speed = rxTimedOut(msg::AbsStatus) ? 0.0 : rx(msg::AbsStatus, QStringLiteral("VehicleSpeed"));
    if (!ignition || anyDoorOpen)
        m_autoLockArmed = true;
    if (!ignition || anyDoorOpen || in.doorLock != m_lastLockSwitch || !coded(AutoLock)) {
        m_autoLocked = false;
    } else if (m_autoLockArmed && speed > 15.0 && !in.doorLock) {
        m_autoLocked = true;
        m_autoLockArmed = false;
    }
    m_lastLockSwitch = in.doorLock;

    if (m_tpms) {
        tpms100ms(ignition, speed);
        if (++m_tpmsSendCycle % 5 == 0) {
            int valid = 0;
            for (int i = 0; i < 4; ++i)
                valid |= m_tpmsFresh[static_cast<size_t>(i)]
                        && now() - m_tpmsLastRx[static_cast<size_t>(i)] <= kTpmsTimeoutMs ? (1 << i) : 0;
            send(msg::BcmTpms, {{QStringLiteral("TirePressure_FL"), m_tpmsBar[0]},
                                   {QStringLiteral("TirePressure_FR"), m_tpmsBar[1]},
                                   {QStringLiteral("TirePressure_RL"), m_tpmsBar[2]},
                                   {QStringLiteral("TirePressure_RR"), m_tpmsBar[3]},
                                   {QStringLiteral("TpmsLampReq"), m_tpmsLamp},
                                   {QStringLiteral("TpmsSensorValid"), tpmsActive() && ignition ? valid : 0},
                                   {QStringLiteral("TpmsLearnActive"), m_tpmsLearn == Learn::Running ? 1 : 0}});
        }
    }

    // Park brake: the switch drives the red BRAKE tell-tale, a warning when driving off with it.
    const bool parkBrake = parkBrakeSwitch();
    const bool parkBrakeWarn = parkBrake && speed > 10.0;

    send(msg::BcmStatus, {{QStringLiteral("IgnitionState"), static_cast<int>(in.ignition)},
                             {QStringLiteral("Headlamps"), m_lowBeam ? 2 : (in.headlamps >= 1 ? 1 : 0)},
                             {QStringLiteral("HighBeam"), m_highBeam ? 1 : 0},
                             {QStringLiteral("TurnLeft"), m_turnLeftOut ? 1 : 0},
                             {QStringLiteral("TurnRight"), m_turnRightOut ? 1 : 0},
                             {QStringLiteral("DoorLock"), m_locked ? 1 : 0},
                             {QStringLiteral("DoorAjar_FL"), in.doorAjar[0] ? 1 : 0},
                             {QStringLiteral("DoorAjar_FR"), in.doorAjar[1] ? 1 : 0},
                             {QStringLiteral("DoorAjar_RL"), in.doorAjar[2] ? 1 : 0},
                             {QStringLiteral("DoorAjar_RR"), in.doorAjar[3] ? 1 : 0},
                             {QStringLiteral("LampFault"), dtcs().testFailed(B1601) ? 1 : 0},
                             {QStringLiteral("ParkBrake"), parkBrake ? 1 : 0},
                             {QStringLiteral("ParkBrakeWarn"), parkBrakeWarn ? 1 : 0},
                             {QStringLiteral("BatteryVoltage"), v.batteryVoltage()}});
}

} // namespace ecusim
