#include "ecu/Ecus.h"

namespace ecusim {

Ipc::Ipc(SimContext &ctx)
    : Ecu(ctx, {QStringLiteral("IPC"), QStringLiteral("Instrument panel cluster"), 0, false, false,
               QStringLiteral("IPC_1.2.0"), QStringLiteral("0263C50003")})
{
    subscribe(msg::AbsStatus, 200);
    subscribe(msg::EcmEngine, 200);
    subscribe(msg::EcmStatus, 1000);
    subscribe(msg::EcmFuel, 1000);
    subscribe(msg::BcmTpms, 2000);
    subscribe(msg::SrsStatus, 500);
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
    const bool fuelLost = !grace && rxTimedOut(msg::EcmFuel);

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
    s.milFlash = !ecmLost && flag(msg::EcmStatus, "MilFlash");
    const bool milBlinkOff = s.milFlash && (awakeForMs() / 500) % 2 == 1;
    s.mil = bulbCheck || ecmLost || ((flag(msg::EcmStatus, "MilReq") || s.milFlash) && !milBlinkOff);
    // The last received values of a silent sender are kept by the receive buffer; a lamp that
    // depends on them must not freeze in its last state (e.g. a turn signal stuck on).
    s.coolantWarn = bulbCheck || (!ecmLost && flag(msg::EcmStatus, "CoolantWarnReq"));
    s.abs = bulbCheck || absLost || flag(msg::AbsStatus, "AbsLampReq");
    s.tcs = bulbCheck || absLost || flag(msg::AbsStatus, "TcsLampReq");
    s.battery = bulbCheck || !engineRunning || (!bcmLost && s.batteryVoltage < 12.0);
    s.transFault = bulbCheck || tcmLost || flag(msg::TcmStatus, "LimpMode");
    if (!bcmLost) {
        s.turnLeft = flag(msg::BcmStatus, "TurnLeft");
        s.turnRight = flag(msg::BcmStatus, "TurnRight");
        s.lowBeam = sig(msg::BcmStatus, "Headlamps") > 1.5;
        s.highBeam = flag(msg::BcmStatus, "HighBeam");
        s.doorAjar = flag(msg::BcmStatus, "DoorAjar_FL") || flag(msg::BcmStatus, "DoorAjar_FR")
            || flag(msg::BcmStatus, "DoorAjar_RL") || flag(msg::BcmStatus, "DoorAjar_RR");
        s.lampFault = flag(msg::BcmStatus, "LampFault");
        s.parkBrakeWarn = flag(msg::BcmStatus, "ParkBrakeWarn");
        // Flashes while the car is driven with the park brake applied.
        const bool blinkOff = s.parkBrakeWarn && (awakeForMs() / 400) % 2 == 1;
        s.parkBrake = flag(msg::BcmStatus, "ParkBrake") && !blinkOff;
    }
    s.parkBrake = s.parkBrake || bulbCheck;

    // --- fuel and oil ---------------------------------------------------------------
    const bool fuelKnown = !ecmLost && !fuelLost;
    s.oilWarn = bulbCheck || !fuelKnown || flag(msg::EcmFuel, "OilPressureWarn");
    s.fuelValid = fuelKnown && flag(msg::EcmFuel, "FuelLevelValid");
    s.fuelLevel = s.fuelValid ? sig(msg::EcmFuel, "FuelLevel") : 0.0; // a faulty sender drops the gauge to empty
    s.lowFuel = bulbCheck || !s.fuelValid || s.fuelLevel < kLowFuelLitres;

    // --- trip computer ---------------------------------------------------------------
    s.fuelRate = fuelKnown ? sig(msg::EcmFuel, "FuelRate") : 0.0;
    m_tripFuel += s.fuelRate * 0.02 / 3600.0;
    m_tripKm += s.speedKph * 0.02 / 3600.0;
    s.consumption = s.speedKph > 5.0 ? s.fuelRate / s.speedKph * 100.0 : -1.0;
    s.averageConsumption = m_tripKm > 0.2 ? m_tripFuel / m_tripKm * 100.0 : -1.0;
    const double perHundred = s.averageConsumption > 0.0 ? s.averageConsumption : kDefaultConsumption;
    s.rangeKm = s.fuelLevel / perHundred * 100.0;

    // --- tyre pressure monitoring (FMVSS 138 lamp behaviour) -----------------------------
    // --- airbag system: own 6 s lamp check; a silent module lights the lamp -------------------
    if (m_ctx.config.has(Equipment::Srs)) {
        const bool srsLost = !grace && rxTimedOut(msg::SrsStatus);
        s.airbag = awakeForMs() < Srs::kLampCheckMs || srsLost || flag(msg::SrsStatus, "AirbagLampReq");
        const bool chime = !srsLost && flag(msg::SrsStatus, "BeltChime");
        const bool beltBlinkOff = chime && (awakeForMs() / 300) % 2 == 1;
        s.seatBelt = bulbCheck || (!srsLost && flag(msg::SrsStatus, "BeltWarn") && !beltBlinkOff);
    }

    if (m_ctx.config.has(Equipment::Tpms)) {
        const bool tpmsLost = !grace && rxTimedOut(msg::BcmTpms);
        const int request = tpmsLost ? 2 : static_cast<int>(sig(msg::BcmTpms, "TpmsLampReq"));
        s.tpmsFlash = request == 2 && awakeForMs() < 60000 + kBulbCheckMs;
        const bool blinkOff = s.tpmsFlash && (awakeForMs() / 500) % 2 == 1;
        s.tpms = bulbCheck || (request > 0 && !blinkOff);
    }
    m_state = s;
}

} // namespace ecusim
