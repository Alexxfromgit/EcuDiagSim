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
    explicit Ecm(SimContext &ctx);
    bool engineRunning() const { return m_running; }
    bool milOn() const { return m_mil; }
    bool limp() const { return m_limp; }
    double coolantUsed() const { return m_coolant; }

protected:
    void onWake() override;
    void onSleep() override;
    void onMessage(quint32 id, const QCanBusFrame &frame) override;

private:
    void task10ms();
    void task100ms();

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
    explicit AbsEcu(SimContext &ctx);
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
    explicit Bcm(SimContext &ctx);
    bool lowBeamOn() const { return m_lowBeam; }
    bool doorsLocked() const { return m_locked; }

private:
    void task20ms();
    void task100ms();

    bool m_lowBeam = false;
    bool m_locked = false;
    bool m_blink = false;
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
};

/// Instrument panel cluster: a pure consumer that turns CAN signals into gauges and lamps.
/// Missing messages light the corresponding lamp (fail-safe), just like a real cluster.
class Ipc : public Ecu
{
public:
    static constexpr int kBulbCheckMs = 3000;

    explicit Ipc(SimContext &ctx);
    const ClusterState &state() const { return m_state; }

protected:
    void onSleep() override { m_state = {}; }

private:
    void task20ms();
    ClusterState m_state;
};

} // namespace ecusim
