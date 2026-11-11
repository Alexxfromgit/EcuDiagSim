#include "ecu/Ecus.h"

#include <algorithm>
#include <cmath>

namespace ecusim {

namespace {
// Upshift speeds in km/h at zero pedal, indexed by the current gear.
constexpr double kUpshift[7] = {0.0, 15.0, 30.0, 45.0, 60.0, 80.0, 1e9};
constexpr int kShiftTimeMs = 300;
constexpr int kLimpGear = 3;
} // namespace

Tcm::Tcm(SimContext &ctx)
    : Ecu(ctx, {QStringLiteral("TCM"), QStringLiteral("Transmission control module"), DiagId::TcmRequest, false,
               true, QStringLiteral("TCM_1.9.0"), QStringLiteral("0260T20017")})
{
    using namespace dtc;
    subscribe(msg::EcmEngine, 200, U0100);
    subscribe(msg::EcmStatus, 1000);
    subscribe(msg::AbsStatus, 200, U0121);

    auto &d = dtcs();
    d.define(dtcDef(P0500, 25, true));
    d.define(dtcDef(P0711, 3, true, 2));
    d.define(dtcDef(P0712, 5, true));
    d.define(dtcDef(P0713, 5, true));
    d.define(dtcDef(P0218, 30));
    d.define(networkDtc(U0100));
    d.define(networkDtc(U0121));

    auto &s = diag();
    s.addObdPid(0x01, [this] {
        int count = 0;
        for (const DtcEntry *e : dtcs().matching(DtcStatus::Confirmed))
            count += e->def.emissionRelevant ? 1 : 0;
        QByteArray a;
        a.append(static_cast<char>(std::min(count, 127))); // the TCM does not drive the MIL itself
        a.append(static_cast<char>(0x04));
        a.append(2, '\0');
        return a;
    });
    s.addObdPid(0x0D, [this] { return encU8(m_speed); });
    s.addObdPid(0x42, [this] { return encU16(m_ctx.vehicle.batteryVoltage() * 1000.0); });
    s.addObdInfoType(0x04, [] { return QByteArray("ECUSIM-TCM-0190").leftJustified(16, '\0'); });
    s.addObdInfoType(0x0A, [] { return QByteArray("TCM-Transmission").leftJustified(20, '\0'); });

    s.addDid(0x2001, [this] { return encU8(m_ctx.driver.selector == Selector::D ? m_gear : 0); });
    s.addDid(0x2002, [this] { return encU8(m_temp + 40.0); });
    s.addDid(0x2003, [this] { return encU16(m_ctx.sensors.tft.voltage() * 1000.0); });
    setSnapshotDids({0xF40D, 0x2001, 0x2002, 0xF442});

    every(20, [this] { task20ms(); });
    every(100, [this] { task100ms(); });
}

void Tcm::onWake()
{
    m_gear = 1;
    m_target = 1;
    m_shifting = false;
    m_haveLastTft = false;
}

void Tcm::onSleep()
{
    // Unpowered solenoids: the hydraulics fall back to a fixed gear.
    m_ctx.vehicle.gearCmd = kLimpGear;
}

void Tcm::task20ms()
{
    using namespace dtc;
    const DriverInputs &driver = m_ctx.driver;

    const bool ecmLost = rxTimedOut(msg::EcmEngine);
    const bool absLost = rxTimedOut(msg::AbsStatus);
    const double rpm = ecmLost ? 0.0 : rx(msg::EcmEngine, QStringLiteral("EngineRPM"));
    const double pedal = ecmLost ? 0.0 : rx(msg::EcmEngine, QStringLiteral("PedalPos")) / 100.0;

    const bool speedValid = !absLost && rx(msg::AbsStatus, QStringLiteral("VehicleSpeedValid")) > 0.5;
    if (monitoringEnabled())
        dtcs().report(P0500, !speedValid);
    m_speedFallback = !speedValid;
    // Substitute value: estimate the road speed from engine speed and the engaged gear.
    m_speed = speedValid ? rx(msg::AbsStatus, QStringLiteral("VehicleSpeed"))
                         : VehicleModel::speedKphFor(rpm, m_gear);

    m_limp = monitoringEnabled() && (ecmLost || dtcs().testFailed(P0218));

    int reported = 0;
    if (driver.selector == Selector::D) {
        int target = m_gear;
        if (m_limp) {
            target = kLimpGear;
        } else {
            const double factor = 1.0 + 2.0 * pedal; // more pedal = later upshift, earlier kick-down
            if (m_gear < VehicleModel::kGearCount && m_speed > kUpshift[m_gear] * factor)
                target = m_gear + 1;
            else if (m_gear > 1 && m_speed < kUpshift[m_gear - 1] * factor * 0.7)
                target = m_gear - 1;
        }
        if (!m_shifting && target != m_gear) {
            m_shifting = true;
            m_target = m_gear + (target > m_gear ? 1 : -1);
            m_shiftEndMs = now() + kShiftTimeMs;
        }
        if (m_shifting && now() >= m_shiftEndMs) {
            m_gear = m_target;
            m_shifting = false;
        }
        reported = m_gear;
    } else {
        m_gear = 1;
        m_target = 1;
        m_shifting = false;
        reported = driver.selector == Selector::R ? 7 : 0;
    }
    m_ctx.vehicle.gearCmd = m_gear;

    send(msg::TcmStatus, {{QStringLiteral("CurrentGear"), reported},
                             {QStringLiteral("TargetGear"), m_shifting ? m_target : reported},
                             {QStringLiteral("GearSelector"), static_cast<int>(driver.selector)},
                             {QStringLiteral("ShiftInProgress"), m_shifting ? 1 : 0},
                             {QStringLiteral("LimpMode"), m_limp ? 1 : 0},
                             {QStringLiteral("TcmMilReq"), dtcs().milRequested() ? 1 : 0},
                             {QStringLiteral("TransTemp"), m_temp}});
}

void Tcm::task100ms()
{
    using namespace dtc;
    Sensor &tft = m_ctx.sensors.tft;
    const double volts = tft.voltage();
    const bool high = volts > 4.8;
    const bool low = volts < 0.2;
    dtcs().report(P0713, high);
    dtcs().report(P0712, low);
    const double raw = tft.value();
    if (!high && !low) {
        dtcs().report(P0711, m_haveLastTft && std::abs(raw - m_lastTft) > 10.0);
        m_lastTft = raw;
        m_haveLastTft = true;
    } else {
        m_haveLastTft = false;
    }
    const bool fault = high || low || dtcs().testFailed(P0711) || dtcs().testFailed(P0712)
        || dtcs().testFailed(P0713);
    m_temp = fault ? 80.0 : raw;
    dtcs().report(P0218, m_temp > 135.0);
}

} // namespace ecusim
