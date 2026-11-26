#pragma once

#include <QtGlobal>

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
    bool parkBrake = false;     ///< mechanical hand brake on the rear axle
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
    static constexpr double kTankLitres = 55.0;
    static constexpr double kStartFuelLitres = 40.0;
    /// The injectors deliver 3 % less than the ECM's calculation (part tolerance): the closed
    /// loop corrects it, and the long-term trim learns it.
    static constexpr double kInjectorFlow = 0.97;

    // --- actuators (written by ECUs) ---
    double throttleCmd = 0.0; ///< ECM: throttle plate command 0..1
    bool fuelEnable = false;  ///< ECM: injection + ignition enabled
    int gearCmd = 1;          ///< TCM: forward gear 1..6 used while the selector is in D

    // --- plant faults ---
    bool alternatorFailed = false;
    bool coolingFailed = false;
    bool bulbLowLeftOpen = false;
    // --- engine OBD pack: closed-loop fuelling (written by the ECM when fitted) ---
    bool mixtureControl = false; ///< the ECM meters the fuel itself (otherwise lambda stays 1)
    double fuelMassGps = 0.0;    ///< injected fuel, g/s

    bool oilPumpFailed = false;        ///< worn pump / oil level far too low
    double vacuumLeakGps = 0.0;        ///< unmetered air entering behind the air flow meter
    int misfireCylinder = 0;           ///< 1..4: that cylinder does not fire, 0 = none
    bool parkBrakeSwitchStuck = false; ///< switch reports "applied" whatever the lever does

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
    bool parkBrakeApplied() const { return m_parkBrake; }
    double fuelLevel() const { return m_fuel; }          ///< litres in the tank
    double fuelRateLph() const { return m_fuelRate; }    ///< what the injectors deliver, l/h
    double oilPressureBar() const { return m_oilPressure; }
    /// Air through the intake, as the air flow meter sees it (without any leak behind it), g/s.
    double airMassGps() const { return m_air; }
    /// Exhaust lambda at the upstream oxygen sensor (lagged by the gas transport).
    double lambda() const { return m_lambda; }
    /// Narrowband O2 sensor output for the current exhaust: 0.1 V lean ... 0.9 V rich.
    double o2SensorVolts() const;
    /// Combustion events since start, and misfires per cylinder (1..4). The ECM derives these
    /// from the crankshaft tooth timing; the simulator hands over the result.
    quint64 firingEvents() const { return m_firings; }
    quint64 misfires(int cylinder) const;
    /// What the engine draws at this speed and throttle: displacement x volumetric efficiency.
    static double expectedAirGps(double rpm, double throttle01);
    /// Gear the transmission is mechanically in: 0 = no drive, 1..6, -1 = reverse.
    int engagedGear() const { return m_gear; }

    void setCoolantTemp(double t) { m_coolant = t; }
    void setTransTemp(double t) { m_transTemp = t; }
    void setFuelLevel(double litres);

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
    double m_fuel = kStartFuelLitres;
    double m_fuelRate = 0.0;
    double m_oilPressure = 0.0;
    double m_air = 0.0;
    double m_lambda = 1.0;
    /// Exhaust transport: the gas needs 60..150 ms from the valves to the sensor (1 ms samples).
    std::array<double, 400> m_exhaust{};
    size_t m_exhaustIndex = 0;
    double m_revs = 0.0;
    int m_firingIndex = 0;
    quint64 m_firings = 0;
    std::array<quint64, 4> m_misfires{};
    bool m_parkBrake = false;
    double m_voltage = 12.6;
    double m_restVoltage = 12.6;
    double m_crankTime = 0.0;
    int m_gear = 0;
    bool m_running = false;
    bool m_starter = false;
};

} // namespace ecusim
