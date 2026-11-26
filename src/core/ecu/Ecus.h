#pragma once

#include "ecu/Ecu.h"

#include <array>

namespace ecusim {

/// CRC-8 SAE J1850 (poly 0x1D, init 0xFF, final XOR 0xFF) used for end-to-end protection.
quint8 crc8J1850(const QByteArray &data);

/// Engine control module: sensors ECT/TPS/CKP, throttle and fuel control, MIL.
class Ecm : public Ecu
{
public:
    static constexpr quint16 kThrottleAdaptation = 0x0201; ///< routine id
    static constexpr int kAdaptationMs = 3000;

    explicit Ecm(SimContext &ctx);
    bool engineRunning() const { return m_running; }
    bool milOn() const { return m_mil; }
    bool limp() const { return m_limp; }
    double coolantUsed() const { return m_coolant; }
    /// The throttle body's closed position has been learned (false after it was replaced).
    bool throttleAdapted() const { return m_throttleAdapted; }
    void setThrottleAdapted(bool adapted) { m_throttleAdapted = adapted; }
    bool adaptationRunning() const { return m_learn == Learn::Running; }
    /// Damped tank level from the fuel sender, litres (0 while the sender is faulty).
    double fuelLevel() const { return m_fuelValid ? m_fuelL : 0.0; }
    bool fuelLevelValid() const { return m_fuelValid; }
    double oilPressure() const { return m_oilBar; }
    bool oilWarning() const { return m_oilWarn; }

protected:
    void onWake() override;
    void onSleep() override;
    void onMessage(quint32 id, const QCanBusFrame &frame) override;

private:
    enum class Learn : quint8 { Idle = 0, Running = 1, Completed = 2, Aborted = 3, Failed = 4 };

    void task10ms();
    void task100ms();
    /// Drives the throttle plate through its range and learns the closed position.
    void runAdaptation(double pedal);
    void abortAdaptation();
    QByteArray adaptationStatus() const;

    Learn m_learn = Learn::Idle;
    qint64 m_learnStartMs = 0;
    double m_learnMaxError = 0.0;
    double m_learnedClosedVolts = 0.5;
    bool m_throttleAdapted = true; ///< non-volatile: survives ignition cycles
    double m_fuelL = 0.0;
    bool m_haveFuel = false;
    bool m_fuelValid = true;
    double m_oilBar = 0.0;
    bool m_oilWarn = true;
    double m_prevRpm = 0.0; ///< last engine speed with a crank signal (stall versus sensor loss)
    double m_rpm = 0.0;
    double m_coolant = 20.0;
    double m_throttlePct = 0.0;
    double m_lastEct = 0.0;
    double m_startEct = 0.0;
    bool m_haveLastEct = false;
    bool m_running = false;
    bool m_mil = false;
    bool m_limp = false;
    bool m_coolantWarn = false;
    int m_crankMs = 0;
    int m_noSignalMs = 1000;
    qint64 m_runMs = 0;
    int m_lastCounter = -1;
};

/// Transmission control module: shift strategy, fluid temperature sensor, limp mode.
class Tcm : public Ecu
{
public:
    explicit Tcm(SimContext &ctx);
    int forwardGear() const { return m_gear; }
    bool limp() const { return m_limp; }
    bool usingSpeedFallback() const { return m_speedFallback; }

protected:
    void onWake() override;
    void onSleep() override;

private:
    void task20ms();
    void task100ms();

    int m_gear = 1;
    int m_target = 1;
    bool m_shifting = false;
    qint64 m_shiftEndMs = 0;
    bool m_limp = false;
    bool m_speedFallback = false;
    double m_speed = 0.0;
    double m_temp = 20.0;
    double m_lastTft = 0.0;
    bool m_haveLastTft = false;
};

/// ABS/ESC module: wheel speed sensors, vehicle speed, ABS and traction control lamps.
class AbsEcu : public Ecu
{
public:
    static constexpr int kDefaultCircumferenceMm = 1948; ///< 2 pi x 0.31 m, the fitted tyres
    static constexpr quint8 kDefaultFlags = 0x00;

    explicit AbsEcu(SimContext &ctx);
    bool hasCoding() const override { return true; }
    void setCodingBlank(bool blank) override;
    bool codingBlank() const { return m_codingBlank; }
    int circumferenceMm() const { return m_circumferenceMm; }
    bool lampOn() const { return m_lamp; }
    double vehicleSpeed() const { return m_speed; }
    bool wheelValid(int i) const { return m_valid[static_cast<size_t>(i)]; }

    // fault injection: end-to-end protection errors on ABS_WheelSpeeds
    bool faultFrozenCounter = false;
    bool faultBadChecksum = false;

protected:
    void onWake() override;

private:
    void task10ms();

    // Variant coding, DID 0x0100: flags (bit0 stability control fitted), rolling circumference in mm.
    quint8 m_codingFlags = kDefaultFlags;
    int m_circumferenceMm = kDefaultCircumferenceMm;
    bool m_codingBlank = false;
    std::array<double, 4> m_wheel{};
    std::array<bool, 4> m_valid{true, true, true, true};
    double m_speed = 0.0;
    bool m_speedValid = true;
    bool m_lamp = false;
    int m_lampForced = -1; ///< IO control: -1 = ECU in control, 0/1 = forced state
    int m_counter = 0;
    int m_txCycle = 0;
};

/// Body control module: ignition state, exterior lights, doors, battery voltage.
class Bcm : public Ecu
{
public:
    /// Variant coding, DID 0x0100.
    enum Coding : quint8 {
        LampMonitoring = 0x01, ///< current monitoring of the low beams (B1601, BULB lamp)
        AutoLock = 0x02,       ///< locks the doors above 15 km/h
        DaytimeLights = 0x04,  ///< position lamps on while the engine runs
    };
    static constexpr quint8 kDefaultCoding = LampMonitoring;

    explicit Bcm(SimContext &ctx);
    bool hasCoding() const override { return true; }
    void setCodingBlank(bool blank) override;
    bool codingBlank() const { return m_codingBlank; }
    quint8 coding() const { return m_coding; }
    // Lamp driver outputs: what the exterior lamps really do (all dark without BCM supply).
    bool lowBeamOn() const { return m_lowBeam; }
    bool highBeamOn() const { return m_highBeam; }
    bool parkLampsOn() const { return m_park; }
    bool turnLeftLampOn() const { return m_turnLeftOut; }
    bool turnRightLampOn() const { return m_turnRightOut; }
    bool doorsLocked() const { return m_locked; }
    /// The park brake switch as the BCM reads it (a stuck switch reads "applied").
    bool parkBrakeSwitch() const { return m_ctx.driver.parkBrake || m_ctx.vehicle.parkBrakeSwitchStuck; }

protected:
    void onSleep() override;

private:
    void task20ms();
    void task100ms();

    bool m_lowBeam = false;
    bool m_highBeam = false;
    bool m_park = false;
    bool m_turnLeftOut = false;
    bool m_turnRightOut = false;
    bool m_locked = false;
    bool m_autoLocked = false;
    bool m_autoLockArmed = true; ///< auto-lock may fire (once per trip)
    bool m_lastLockSwitch = false;
    bool m_blink = false;
    quint8 m_coding = kDefaultCoding;
    bool m_codingBlank = false;
    int m_lowBeamForced = -1;
    int m_lockForced = -1;
};

/// What the instrument cluster currently shows.
struct ClusterState
{
    bool powered = false;
    double speedKph = 0.0;
    double rpm = 0.0;
    double coolantTemp = 0.0;
    double batteryVoltage = 0.0;
    int gear = 0;     ///< 0 none, 1..6, 7 reverse
    int selector = 0; ///< 0 P, 1 R, 2 N, 3 D
    bool mil = false;
    bool abs = false;
    bool tcs = false;
    bool battery = false;
    bool coolantWarn = false;
    bool transFault = false;
    bool turnLeft = false;
    bool turnRight = false;
    bool lowBeam = false;
    bool highBeam = false;
    bool doorAjar = false;
    bool lampFault = false;
    bool oilWarn = false;
    bool lowFuel = false;
    bool parkBrake = false;      ///< red BRAKE tell-tale (flashes while driving with it applied)
    bool parkBrakeWarn = false;
    double fuelLevel = 0.0;      ///< litres, as received
    bool fuelValid = true;
    double fuelRate = 0.0;       ///< l/h
    double consumption = -1.0;   ///< l/100 km right now, -1 below walking pace
    double averageConsumption = -1.0; ///< since the trip was reset, -1 for the first 200 m
    double rangeKm = 0.0;
};

/// Instrument panel cluster: a pure consumer that turns CAN signals into gauges and lamps.
/// Missing messages light the corresponding lamp (fail-safe), just like a real cluster.
class Ipc : public Ecu
{
public:
    static constexpr int kBulbCheckMs = 3000;

    static constexpr double kLowFuelLitres = 7.0;
    static constexpr double kDefaultConsumption = 7.5; ///< l/100 km for the range before a trip is known

    explicit Ipc(SimContext &ctx);
    const ClusterState &state() const { return m_state; }
    void resetTrip() { m_tripFuel = m_tripKm = 0.0; }

protected:
    void onSleep() override { m_state = {}; }

private:
    void task20ms();
    ClusterState m_state;
    double m_tripFuel = 0.0; ///< litres since the trip reset
    double m_tripKm = 0.0;
};

} // namespace ecusim
