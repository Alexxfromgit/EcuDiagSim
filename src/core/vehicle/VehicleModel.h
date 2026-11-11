#pragma once

#include <array>
#include <cmath>

namespace ecusim {

enum class Ignition { Off = 0, On = 1, Start = 2 };
enum class Selector { P = 0, R = 1, N = 2, D = 3 };

/// Everything the driver can touch. Written by the UI or by a scenario.
struct DriverInputs
{
    Ignition ignition = Ignition::Off;
    double throttlePedal = 0.0; ///< 0..1
    double brakePedal = 0.0;    ///< 0..1
    Selector selector = Selector::P;
    int headlamps = 0;          ///< 0 off, 1 parking, 2 low beam
    bool highBeam = false;
    bool turnLeft = false;
    bool turnRight = false;
    bool hazard = false;
    bool doorLock = false;
    std::array<bool, 4> doorAjar{};
};

/// The plant: a simple longitudinal model of a mid-size car with a 6-speed automatic.
///
/// ECUs are the controllers. They read the plant through sensors and act on it through
/// the actuator members below (throttle, fuel, gear). Good enough to produce realistic
/// looking signals; not a drivetrain simulation.
class VehicleModel
{
public:
    static constexpr double kAmbientTemp = 20.0;
    static constexpr double kIdleRpm = 800.0;
    static constexpr int kGearCount = 6;

    // --- actuators (written by ECUs) ---
    double throttleCmd = 0.0; ///< ECM: throttle plate command 0..1
    bool fuelEnable = false;  ///< ECM: injection + ignition enabled
    int gearCmd = 1;          ///< TCM: forward gear 1..6 used while the selector is in D

    // --- plant faults ---
    bool alternatorFailed = false;
    bool coolingFailed = false;
    bool bulbLowLeftOpen = false;

    void step(double dt, const DriverInputs &driver);

    // --- true physical state (sensors sample these) ---
    /// Road speed, always positive (what a speedometer shows).
    double speedKph() const { return std::abs(m_speed) * 3.6; }
    /// Signed velocity: negative while rolling backwards.
    double velocityKph() const { return m_speed * 3.6; }
    /// Passive wheel speed sensors measure the magnitude only, not the direction.
    double wheelSpeedKph(int) const { return std::abs(m_speed) * 3.6; }
    double engineRpm() const { return m_rpm; }
    bool engineRunning() const { return m_running; }
    bool starterEngaged() const { return m_starter; }
    double engineTorque() const { return m_torque; }
    double throttlePos() const { return m_throttle; } ///< 0..1
    double coolantTemp() const { return m_coolant; }
    double transTemp() const { return m_transTemp; }
    double batteryVoltage() const { return m_voltage; }
    double brakePressureBar() const { return m_brake * 150.0; }
    /// Gear the transmission is mechanically in: 0 = no drive, 1..6, -1 = reverse.
    int engagedGear() const { return m_gear; }

    void setCoolantTemp(double t) { m_coolant = t; }
    void setTransTemp(double t) { m_transTemp = t; }

    static double gearRatio(int gear);
    /// Vehicle speed that corresponds to an engine speed in a given gear (locked converter).
    static double speedKphFor(double rpm, int gear);

private:
    double m_speed = 0.0; ///< m/s, signed
    double m_rpm = 0.0;
    double m_torque = 0.0;
    double m_throttle = 0.0;
    double m_brake = 0.0;
    double m_coolant = kAmbientTemp;
    double m_transTemp = kAmbientTemp;
    double m_voltage = 12.6;
    double m_restVoltage = 12.6;
    double m_crankTime = 0.0;
    int m_gear = 0;
    bool m_running = false;
    bool m_starter = false;
};

} // namespace ecusim
