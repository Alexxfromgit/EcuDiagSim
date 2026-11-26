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
    static constexpr quint16 kResetFuelTrims = 0x0202;     ///< routine id (engine OBD pack)
    static constexpr double kTrimLimit = 0.25;
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
    // --- engine OBD pack ---
    bool hasObdPack() const { return m_obdPack; }
    double shortTermTrim() const { return m_stft; } ///< -0.25..+0.25
    double longTermTrim() const { return m_ltft; }  ///< learned, non-volatile
    bool closedLoop() const { return m_closedLoop; }
    bool milFlashing() const { return m_milFlash; }

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
    void mixtureControl10ms();
    void obdMonitors100ms();

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
    bool m_crashCut = false; ///< airbags fired: fuel pump off until the ignition is switched off
    double m_prevRpm = 0.0; ///< last engine speed with a crank signal (stall versus sensor loss)
    // engine OBD pack
    bool m_obdPack = false;
    double m_stft = 0.0;
    double m_ltft = 0.0; ///< non-volatile adaptation
    bool m_closedLoop = false;
    double m_mafGps = 0.0;
    bool m_mafOk = true;
    bool m_o2Ok = true;
    double m_o2Min = 1.0;
    double m_o2Max = 0.0;
    int m_o2WindowMs = 0;
    bool m_milFlash = false;
    quint64 m_lastFirings = 0;
    std::array<quint64, 4> m_lastMisfires{};
    quint64 m_windowFirings = 0;
    std::array<quint64, 4> m_windowMisfires{};
    quint8 m_misfireBits = 0;
    std::array<quint64, 4> m_reportedMisfires{}; ///< per cylinder, last completed window
    int m_o2Side = 0; ///< +1 rich, -1 lean, 0 not yet known
    qint64 m_o2ActiveMs = 0; ///< last time the O2 signal left the band around its bias
    bool m_o2Dead = false;   ///< no switching although in closed loop
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
        Tpms = 0x08,           ///< tyre pressure monitoring (only with the TPMS equipment)
    };
    static constexpr quint16 kTpmsRelearn = 0x0301; ///< routine id
    static constexpr int kTpmsTimeoutMs = 10000;    ///< a sensor silent this long is missing
    static constexpr int kTpmsLearnMs = 30000;
    static constexpr double kTpmsWarnBar = 0.75 * VehicleModel::kPlacardBar; ///< FMVSS 138: 25 % below placard

    explicit Bcm(SimContext &ctx);
    bool hasCoding() const override { return true; }
    void setCodingBlank(bool blank) override;
    bool codingBlank() const { return m_codingBlank; }
    quint8 coding() const { return m_coding; }
    quint8 defaultCoding() const { return LampMonitoring | (m_tpms ? Tpms : 0); }
    // --- tyre pressure monitoring ---
    bool tpmsFitted() const { return m_tpms; }
    bool tpmsActive() const;
    double tpmsPressure(int wheel) const { return m_tpmsBar[static_cast<size_t>(wheel)]; }
    quint32 tpmsLearnedId(int wheel) const { return m_tpmsIds[static_cast<size_t>(wheel)]; }
    int tpmsLampRequest() const { return m_tpmsLamp; }
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
    void tpms100ms(bool ignition, double speed);
    QByteArray tpmsLearnStatus() const;

    bool m_tpms = false;
    std::array<quint32, 4> m_tpmsIds{}; ///< learned sensor IDs (non-volatile)
    std::array<qint64, 4> m_tpmsLastRx{};
    std::array<bool, 4> m_tpmsFresh{}; ///< received since ignition on
    std::array<bool, 4> m_tpmsWasMissing{}; ///< missing when last checked, kept over ignition cycles
    std::array<double, 4> m_tpmsBar{};
    std::array<double, 4> m_tpmsTemp{};
    QList<quint32> m_tpmsUnknown;       ///< heard, but not learned
    enum class Learn : quint8 { Idle = 0, Running = 1, Completed = 2, Aborted = 3, Failed = 4 };
    Learn m_tpmsLearn = Learn::Idle;
    qint64 m_tpmsLearnStart = 0;
    quint8 m_tpmsLearnedMask = 0;
    int m_tpmsLamp = 0;
    bool m_lastIgnition = false;
    int m_tpmsSendCycle = 0;

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
    quint8 m_coding = LampMonitoring;
    bool m_codingBlank = false;
    int m_lowBeamForced = -1;
    int m_lockForced = -1;
    bool m_crashMode = false; ///< after an airbag deployment: hazards on, doors unlocked
    qint64 m_ignitionOnMs = 0;
};

/// Airbag control module (optional equipment): squib circuits, seat belt buckle switch, crash
/// detection with deployment and a crash record that no scan tool can erase.
class Srs : public Ecu
{
public:
    enum Coding : quint8 {
        PassengerAirbag = 0x01,
        BeltReminder = 0x02,
    };
    static constexpr quint8 kDefaultCoding = PassengerAirbag | BeltReminder;
    static constexpr quint16 kCrashRecordDid = 0x5010;
    static constexpr double kDeployDeltaV = 20.0; ///< km/h
    static constexpr int kLampCheckMs = 6000;    ///< airbag lamp self test at ignition on

    explicit Srs(SimContext &ctx);
    bool hasCoding() const override { return true; }
    void setCodingBlank(bool blank) override;
    bool codingBlank() const { return m_codingBlank; }
    bool crashRecorded() const { return m_crashRecord; }
    bool lampRequest() const { return m_lamp; }
    bool buckled() const;

protected:
    void onWake() override;

private:
    void task10ms();
    void task100ms();
    bool coded(quint8 function) const { return !m_codingBlank && (m_coding & function); }

    quint8 m_coding = kDefaultCoding;
    bool m_codingBlank = false;
    int m_seenCrash = 0;
    bool m_crashRecord = false; ///< non-volatile
    bool m_crashThisCycle = false;
    double m_crashDeltaV = 0.0;
    bool m_crashBelted = false;
    bool m_lamp = false;
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
    bool mil = false;      ///< lamp lit right now (blinks while milFlash)
    bool milFlash = false; ///< catalyst-damaging misfire: the MIL flashes
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
    bool airbag = false;    ///< airbag warning lamp (only with the SRS)
    bool seatBelt = false;  ///< belt reminder lit right now (blinks with the chime)
    bool tpms = false;      ///< TPMS lamp lit right now (blinks while tpmsFlash)
    bool tpmsFlash = false; ///< system fault: flashes for 60 s after ignition on, then steady
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
