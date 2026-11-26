#include "vehicle/VehicleModel.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ecusim {

namespace {
constexpr double kMass = 1500.0;        // kg
constexpr double kWheelRadius = 0.31;   // m
constexpr double kFinalDrive = 3.2;
constexpr double kDrivelineEff = 0.9;
constexpr double kCdA = 0.65;           // m^2
constexpr double kRollCoeff = 0.012;
constexpr double kAirDensity = 1.2;
constexpr double kGravity = 9.81;
constexpr double kMaxTraction = 0.55 * kMass * kGravity; // front axle grip
constexpr double kRedline = 6200.0;
constexpr double kCrankRpm = 250.0;
constexpr std::array<double, 7> kRatios{0.0, 4.17, 2.34, 1.52, 1.14, 0.87, 0.69};
constexpr double kReverseRatio = 3.40;

double approach(double value, double target, double dt, double tau)
{
    return value + (target - value) * std::min(1.0, dt / tau);
}

double maxTorque(double rpm)
{
    const double x = (rpm - 3800.0) / 5200.0;
    return std::max(0.0, 260.0 * (1.0 - x * x));
}
} // namespace

double VehicleModel::gearRatio(int gear)
{
    if (gear < 0)
        return kReverseRatio;
    if (gear >= 1 && gear <= kGearCount)
        return kRatios[static_cast<size_t>(gear)];
    return 0.0;
}

double VehicleModel::speedKphFor(double rpm, int gear)
{
    const double ratio = gearRatio(gear);
    if (ratio <= 0.0)
        return 0.0;
    const double wheelRps = rpm / 60.0 / (ratio * kFinalDrive);
    return wheelRps * 2.0 * std::numbers::pi * kWheelRadius * 3.6;
}

void VehicleModel::setFuelLevel(double litres)
{
    m_fuel = std::clamp(litres, 0.0, kTankLitres);
}

void VehicleModel::step(double dt, const DriverInputs &driver)
{
    const bool ignitionOn = driver.ignition != Ignition::Off;

    // The manual valve selects P/R/N/D hydraulically; the TCM only picks the forward gear.
    switch (driver.selector) {
    case Selector::D:
        m_gear = std::clamp(gearCmd, 1, kGearCount);
        break;
    case Selector::R:
        m_gear = -1;
        break;
    default:
        m_gear = 0;
        break;
    }

    // --- engine ---------------------------------------------------------
    m_starter = driver.ignition == Ignition::Start && !m_running
        && (driver.selector == Selector::P || driver.selector == Selector::N) && m_voltage > 9.0;
    // An empty tank starves the engine like a closed injector: it stops, and cranks without starting.
    if (m_running && (!fuelEnable || !ignitionOn || m_fuel <= 0.0))
        m_running = false;
    if (m_starter) {
        m_crankTime += dt;
        if (fuelEnable && m_fuel > 0.0 && m_crankTime > 0.6) {
            m_running = true;
            m_rpm = std::max(m_rpm, 500.0);
        }
    } else {
        m_crankTime = 0.0;
    }

    // Electronic throttle: the ECM's motor positions the plate whenever it has supply, engine
    // running or not (that is what the adaptation routine relies on).
    const double throttleTarget = ignitionOn ? std::clamp(throttleCmd, 0.0, 1.0) : 0.0;
    m_throttle = approach(m_throttle, throttleTarget, dt, 0.05);

    const double ratio = gearRatio(m_gear);
    // m_speed is signed (negative = rolling backwards). When the car rolls against the
    // engaged direction the converter slips and the engine is not dragged along.
    const double travel = m_gear < 0 ? -m_speed : m_speed;
    const double wheelRps = travel / (2.0 * std::numbers::pi * kWheelRadius);
    const double coupledRpm = std::max(0.0, wheelRps * 60.0 * ratio * kFinalDrive);

    if (m_running) {
        double target;
        if (ratio <= 0.0) {
            target = kIdleRpm + m_throttle * (kRedline - kIdleRpm - 400.0);
        } else {
            // Torque converter: the engine can rev above the coupled speed at low road speed.
            const double stallRpm = kIdleRpm + m_throttle * 1700.0;
            target = std::max(coupledRpm, stallRpm);
        }
        m_rpm = approach(m_rpm, std::min(target, kRedline + 200.0), dt, 0.25);

        const double effThrottle = m_rpm > kRedline ? 0.0 : m_throttle; // rev limiter
        m_torque = effThrottle * maxTorque(m_rpm);
        if (m_rpm < 1100.0)
            m_torque += 30.0 * (1.0 - effThrottle);          // idle controller holds the engine up
        else
            m_torque -= 25.0 * (1.0 - effThrottle);          // engine braking
    } else {
        m_rpm = approach(m_rpm, m_starter ? kCrankRpm : 0.0, dt, m_starter ? 0.1 : 0.3);
        if (m_rpm < 1.0)
            m_rpm = 0.0;
        m_torque = 0.0;
    }

    // --- fuel ----------------------------------------------------------------
    // Friction and pumping losses grow with engine speed; the useful work costs about 0.3 l/kWh.
    // On the overrun (closed throttle above 1300 rpm, in gear) the injectors are cut off.
    if (m_running) {
        const bool overrun = m_throttle < 0.02 && m_rpm > 1300.0 && ratio > 0.0;
        const double powerKw = std::max(0.0, m_torque) * m_rpm * 2.0 * std::numbers::pi / 60.0 / 1000.0;
        m_fuelRate = overrun ? 0.0 : 0.25 * m_rpm / kIdleRpm + 0.30 * powerKw;
        m_fuel = std::max(0.0, m_fuel - m_fuelRate / 3600.0 * dt);
    } else {
        m_fuelRate = 0.0;
    }

    // --- oil pressure ----------------------------------------------------------
    // The pump delivers with engine speed; hot oil is thinner and holds less pressure.
    double oilTarget = 0.0;
    if (m_running) {
        const double viscosity = std::clamp(1.3 - 0.004 * (m_coolant - 20.0), 0.9, 1.4);
        oilTarget = std::min(5.0, (0.8 + 0.75 * m_rpm / 1000.0) * viscosity);
        if (oilPumpFailed)
            oilTarget *= 0.08;
    }
    m_oilPressure = approach(m_oilPressure, oilTarget, dt, 0.2);

    // --- longitudinal dynamics -------------------------------------------
    m_brake = std::clamp(driver.brakePedal, 0.0, 1.0);
    m_parkBrake = driver.parkBrake;
    double drive = ratio > 0.0 ? m_torque * ratio * kFinalDrive * kDrivelineEff / kWheelRadius : 0.0;
    drive = std::clamp(drive, -kMaxTraction, kMaxTraction);
    if (m_gear < 0)
        drive = -drive; // reverse gear pushes the car backwards
    const double drag = 0.5 * kAirDensity * kCdA * m_speed * std::abs(m_speed);
    double v = m_speed + (drive - drag) / kMass * dt;
    // Rolling resistance and brakes always act against the motion and can only stop the
    // car, never push it the other way; at standstill they hold it against a weak drive.
    // The hand brake acts on the rear axle only: about a quarter of g.
    const double resistDv = (kRollCoeff * kMass * kGravity + m_brake * kMass * kGravity
                                + (m_parkBrake ? 0.25 * kMass * kGravity : 0.0))
        / kMass * dt;
    if (v > 0.0)
        v = std::max(0.0, v - resistDv);
    else if (v < 0.0)
        v = std::min(0.0, v + resistDv);
    m_speed = v;
    if (driver.selector == Selector::P && std::abs(m_speed) < 1.0)
        m_speed = 0.0; // parking pawl

    // --- thermal ---------------------------------------------------------
    const double load = m_running ? m_throttle : 0.0;
    if (m_running) {
        const double target = coolingFailed ? 140.0 : 90.0 + 4.0 * load;
        m_coolant = approach(m_coolant, target, dt, coolingFailed ? 60.0 : 45.0);
        m_transTemp = approach(m_transTemp, 80.0 + 10.0 * load, dt, 90.0);
    } else {
        m_coolant = approach(m_coolant, kAmbientTemp, dt, 600.0);
        m_transTemp = approach(m_transTemp, kAmbientTemp, dt, 900.0);
    }

    // --- electrical ------------------------------------------------------
    const bool charging = m_running && !alternatorFailed;
    if (charging)
        m_restVoltage = std::min(12.6, m_restVoltage + 0.05 * dt);
    else if (ignitionOn)
        m_restVoltage = std::max(9.0, m_restVoltage - (alternatorFailed ? 0.06 : 0.0005) * dt);
    double target = charging ? 14.2 : m_restVoltage;
    if (m_starter)
        target -= 1.8;
    m_voltage = approach(m_voltage, target, dt, 0.15);
}

} // namespace ecusim
