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

    auto &s = diag();
    // Variant coding: which body functions this car has (see Bcm::Coding).
    s.addDid(0x0100, [this] { return encU8(m_codingBlank ? 0xFF : m_coding); });
    s.addWritableDid(0x0100, 1, [this](const QByteArray &data) -> quint8 {
        const quint8 coding = static_cast<quint8>(data[0]);
        if (coding & ~(LampMonitoring | AutoLock | DaytimeLights))
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

    every(20, [this] { task20ms(); });
    every(100, [this] { task100ms(); });
}

void Bcm::setCodingBlank(bool blank)
{
    if (blank) {
        m_codingBlank = true;
    } else if (m_codingBlank) {
        m_codingBlank = false;
        m_coding = kDefaultCoding;
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
