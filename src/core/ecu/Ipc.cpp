#include "ecu/Ecus.h"

namespace ecusim {

Ipc::Ipc(SimContext &ctx)
    : Ecu(ctx, {QStringLiteral("IPC"), QStringLiteral("Instrument panel cluster"), 0, false, false,
               QStringLiteral("IPC_1.2.0"), QStringLiteral("0263C50003")})
{
    subscribe(msg::AbsStatus, 200);
    subscribe(msg::EcmEngine, 200);
    subscribe(msg::EcmStatus, 1000);
    subscribe(msg::TcmStatus, 200);
    subscribe(msg::BcmStatus, 500);
    every(20, [this] { task20ms(); });
}

void Ipc::task20ms()
{
    auto sig = [this](quint32 id, const char *name) { return rx(id, QString::fromLatin1(name)); };
    auto flag = [&sig](quint32 id, const char *name) { return sig(id, name) > 0.5; };

    const bool bulbCheck = awakeForMs() < kBulbCheckMs;
    const bool grace = awakeForMs() < kStartupGraceMs;
    const bool absLost = !grace && rxTimedOut(msg::AbsStatus);
    const bool ecmLost = !grace && (rxTimedOut(msg::EcmEngine) || rxTimedOut(msg::EcmStatus));
    const bool tcmLost = !grace && rxTimedOut(msg::TcmStatus);
    const bool bcmLost = !grace && rxTimedOut(msg::BcmStatus);

    ClusterState s;
    s.powered = true;
    s.speedKph = absLost ? 0.0 : sig(msg::AbsStatus, "VehicleSpeed");
    s.rpm = ecmLost ? 0.0 : sig(msg::EcmEngine, "EngineRPM");
    s.coolantTemp = ecmLost ? 0.0 : sig(msg::EcmStatus, "CoolantTemp");
    s.batteryVoltage = bcmLost ? 0.0 : sig(msg::BcmStatus, "BatteryVoltage");
    s.gear = tcmLost ? 0 : static_cast<int>(sig(msg::TcmStatus, "CurrentGear"));
    s.selector = tcmLost ? 0 : static_cast<int>(sig(msg::TcmStatus, "GearSelector"));

    // Lamps: requested over CAN, forced on during the bulb check and when the sender is silent.
    const bool engineRunning = !ecmLost && flag(msg::EcmStatus, "EngineRunning");
    s.mil = bulbCheck || ecmLost || flag(msg::EcmStatus, "MilReq");
    s.coolantWarn = bulbCheck || flag(msg::EcmStatus, "CoolantWarnReq");
    s.abs = bulbCheck || absLost || flag(msg::AbsStatus, "AbsLampReq");
    s.tcs = bulbCheck || absLost || flag(msg::AbsStatus, "TcsLampReq");
    s.battery = bulbCheck || !engineRunning || (!bcmLost && s.batteryVoltage < 12.0);
    s.transFault = bulbCheck || tcmLost || flag(msg::TcmStatus, "LimpMode");
    s.turnLeft = flag(msg::BcmStatus, "TurnLeft");
    s.turnRight = flag(msg::BcmStatus, "TurnRight");
    s.lowBeam = sig(msg::BcmStatus, "Headlamps") > 1.5;
    s.highBeam = flag(msg::BcmStatus, "HighBeam");
    s.doorAjar = flag(msg::BcmStatus, "DoorAjar_FL") || flag(msg::BcmStatus, "DoorAjar_FR")
        || flag(msg::BcmStatus, "DoorAjar_RL") || flag(msg::BcmStatus, "DoorAjar_RR");
    s.lampFault = flag(msg::BcmStatus, "LampFault");
    m_state = s;
}

} // namespace ecusim
