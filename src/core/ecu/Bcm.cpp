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

    auto &s = diag();
    s.addDid(0x4001, [this] { return encU8(m_ctx.vehicle.batteryVoltage() * 10.0); });
    s.addDid(0x4002, [this] {
        const DriverInputs &in = m_ctx.driver;
        return encU8((m_lowBeam ? 0x01 : 0) | (in.highBeam ? 0x02 : 0) | (in.headlamps >= 1 ? 0x04 : 0)
            | ((in.turnLeft || in.hazard) ? 0x08 : 0) | ((in.turnRight || in.hazard) ? 0x10 : 0));
    });
    s.addDid(0x4003, [this] {
        const auto &doors = m_ctx.driver.doorAjar;
        return encU8((doors[0] ? 0x01 : 0) | (doors[1] ? 0x02 : 0) | (doors[2] ? 0x04 : 0)
            | (doors[3] ? 0x08 : 0) | (m_locked ? 0x80 : 0));
    });
    s.addDid(0xF442, [this] { return encU16(m_ctx.vehicle.batteryVoltage() * 1000.0); });

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

void Bcm::task20ms()
{
    const DriverInputs &in = m_ctx.driver;
    m_lowBeam = m_lowBeamForced >= 0 ? m_lowBeamForced == 1 : in.headlamps == 2;
    m_locked = m_lockForced >= 0 ? m_lockForced == 1 : in.doorLock;
    m_blink = (now() / 400) % 2 == 0;
}

void Bcm::task100ms()
{
    using namespace dtc;
    const DriverInputs &in = m_ctx.driver;
    const VehicleModel &v = m_ctx.vehicle;
    const bool ignition = in.ignition != Ignition::Off;

    if (ignition)
        dtcs().report(B1318, v.batteryVoltage() < 11.0);
    // Lamp outputs are current monitored: commanded on but no current = open circuit.
    if (m_lowBeam)
        dtcs().report(B1601, v.bulbLowLeftOpen);

    send(msg::BcmStatus, {{QStringLiteral("IgnitionState"), static_cast<int>(in.ignition)},
                             {QStringLiteral("Headlamps"), m_lowBeam ? 2 : (in.headlamps >= 1 ? 1 : 0)},
                             {QStringLiteral("HighBeam"), in.highBeam ? 1 : 0},
                             {QStringLiteral("TurnLeft"), (in.turnLeft || in.hazard) && m_blink ? 1 : 0},
                             {QStringLiteral("TurnRight"), (in.turnRight || in.hazard) && m_blink ? 1 : 0},
                             {QStringLiteral("DoorLock"), m_locked ? 1 : 0},
                             {QStringLiteral("DoorAjar_FL"), in.doorAjar[0] ? 1 : 0},
                             {QStringLiteral("DoorAjar_FR"), in.doorAjar[1] ? 1 : 0},
                             {QStringLiteral("DoorAjar_RL"), in.doorAjar[2] ? 1 : 0},
                             {QStringLiteral("DoorAjar_RR"), in.doorAjar[3] ? 1 : 0},
                             {QStringLiteral("LampFault"), dtcs().testFailed(B1601) ? 1 : 0},
                             {QStringLiteral("BatteryVoltage"), v.batteryVoltage()}});
}

} // namespace ecusim
