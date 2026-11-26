#include "ecu/Ecus.h"

#include <algorithm>
#include <cmath>

namespace ecusim {

AbsEcu::AbsEcu(SimContext &ctx)
    : Ecu(ctx, {QStringLiteral("ABS"), QStringLiteral("Anti-lock brake system"), DiagId::AbsRequest, false, false,
               QStringLiteral("ABS_3.1.7"), QStringLiteral("0265A30008")})
{
    using namespace dtc;
    subscribe(msg::EcmEngine, 200, U0100);
    subscribe(msg::BcmStatus, 500, U0140);

    auto &d = dtcs();
    for (int i = 0; i < 4; ++i) {
        d.define(dtcDef(wheel(i, FtbShortGround), 20));
        d.define(dtcDef(wheel(i, FtbShortBattery), 20));
        d.define(dtcDef(wheel(i, FtbOpen), 20));
        d.define(dtcDef(wheel(i, FtbSignalInvalid), 50));
    }
    d.define(networkDtc(U0100));
    d.define(networkDtc(U0140));
    d.define(dtcDef(U3000NotConfigured, 5));
    m_esc = ctx.config.has(Equipment::Esc);
    m_codingFlags = defaultFlags();

    auto &s = diag();
    if (m_esc) {
        d.define(dtcDef(C0460Low, 20));
        d.define(dtcDef(C0460Calibration, 1));
        d.define(dtcDef(C0460Plausibility, 50));
        d.define(dtcDef(C0196Low, 20));
        d.define(dtcDef(C0196High, 20));
        s.addDid(0x3020, [this] { return encU16((m_angle + 3276.8) * 10.0); });
        s.addDid(0x3021, [this] { return encU16((m_yaw + 327.68) * 100.0); });
        s.addDid(0x3022, [this] { return encU8(m_sasCalibrated ? 1 : 0); });
        // Steering angle calibration: learns the current reading as straight ahead.
        DiagnosticServer::Routine calibration;
        calibration.needsSecurity = true;
        calibration.start = [this](const QByteArray &option, QByteArray &status) -> quint8 {
            if (!option.isEmpty())
                return Nrc::IncorrectMessageLength;
            if (m_speed > 1.0 || m_ctx.sensors.sas.voltage() < 0.2)
                return Nrc::ConditionsNotCorrect;
            m_sasZero = m_ctx.sensors.sas.value();
            m_sasCalibrated = true;
            m_straightMs = 0;
            if (onConfigured)
                onConfigured(QStringLiteral("sas")); // a re-centred steering wheel is configured now
            status = encU8(DiagnosticServer::RoutineCompleted);
            return 0;
        };
        s.addRoutine(kSasCalibration, calibration);
    }
    // Variant coding: flags (bit0 stability control fitted), rolling circumference in mm (big endian).
    s.addDid(0x0100, [this] {
        if (m_codingBlank)
            return QByteArray(3, static_cast<char>(0xFF));
        QByteArray a = encU8(m_codingFlags);
        a.append(encU16(m_circumferenceMm));
        return a;
    });
    s.addWritableDid(0x0100, 3, [this](const QByteArray &data) -> quint8 {
        const quint8 flags = static_cast<quint8>(data[0]);
        const int circumference = (static_cast<quint8>(data[1]) << 8) | static_cast<quint8>(data[2]);
        if ((flags & ~kFlagEsc) != 0 || ((flags & kFlagEsc) && !m_esc) || circumference < 1600 || circumference > 2400)
            return Nrc::RequestOutOfRange;
        m_codingFlags = flags;
        m_circumferenceMm = circumference;
        if (m_codingBlank) {
            m_codingBlank = false;
            if (onConfigured)
                onConfigured(QStringLiteral("abs"));
        }
        return 0;
    });
    for (int i = 0; i < 4; ++i) {
        s.addDid(static_cast<quint16>(0x3001 + i),
            [this, i] { return encU16(m_wheel[static_cast<size_t>(i)] * 100.0); });
    }
    s.addDid(0x3005, [this] { return encU8(m_ctx.vehicle.brakePressureBar()); });
    s.addDid(0xF40D, [this] { return encU8(m_speed); });
    s.addDid(0xF442, [this] { return encU16(m_ctx.vehicle.batteryVoltage() * 1000.0); });
    s.addIoControl(
        0x3010,
        [this](bool take, const QByteArray &state) {
            if (!take) {
                m_lampForced = -1;
                return true;
            }
            if (state.size() != 1 || static_cast<quint8>(state[0]) > 1)
                return false;
            m_lampForced = state[0];
            return true;
        },
        [this] { return encU8(m_lamp ? 1 : 0); });
    setSnapshotDids({0xF40D, 0x3001, 0x3002, 0x3003, 0x3004, 0xF442});

    every(10, [this] { task10ms(); });
}

void AbsEcu::onSleep()
{
    // Without supply the hydraulic unit is passive: plain brakes, no ESC.
    m_ctx.vehicle.absPressure = {1.0, 1.0, 1.0, 1.0};
    m_ctx.vehicle.escBrakeDecel = 0.0;
    m_pressure = {1.0, 1.0, 1.0, 1.0};
    m_absActive = m_escActive = false;
}

void AbsEcu::setCodingBlank(bool blank)
{
    if (blank) {
        m_codingBlank = true;
        m_sasCalibrated = false; // a new module has never seen this steering wheel
    } else if (m_codingBlank) {
        // The original module is back, with its coding.
        m_codingBlank = false;
        m_codingFlags = defaultFlags();
        m_sasCalibrated = true;
        m_sasZero = 0.0;
        m_circumferenceMm = kDefaultCircumferenceMm;
    }
}

void AbsEcu::esc10ms()
{
    using namespace dtc;
    const Sensor &sas = m_ctx.sensors.sas;
    const Sensor &yaw = m_ctx.sensors.yaw;
    const bool sasLow = sas.voltage() < 0.2;
    const bool yawLow = yaw.voltage() < 0.2;
    const bool yawHigh = yaw.voltage() > 4.8;
    dtcs().report(C0460Low, sasLow);
    dtcs().report(C0196Low, yawLow);
    dtcs().report(C0196High, yawHigh);
    dtcs().report(C0460Calibration, !m_sasCalibrated);
    m_angle = sas.value() - m_sasZero;
    m_yaw = yaw.value();

    // Driving straight on (no yaw) above 30 km/h for 3 s: the steering wheel must be straight too.
    const bool sensorsOk = !sasLow && !yawLow && !yawHigh;
    if (sensorsOk && m_sasCalibrated && m_speed > 30.0) {
        m_straightMs = std::abs(m_yaw) < 1.5 ? m_straightMs + 10 : 0;
        if (m_straightMs >= 3000)
            dtcs().report(C0460Plausibility, std::abs(m_angle) > 8.0);
    } else {
        m_straightMs = 0;
    }

    bool escWarning = false;
    for (quint32 code : {C0460Low, C0460Calibration, C0460Plausibility, C0196Low, C0196High})
        escWarning = escWarning || (dtcs().status(code) & DtcStatus::WarningIndicatorRequested);
    m_escOff = m_codingBlank || !(m_codingFlags & kFlagEsc) || !m_sasCalibrated || !sensorsOk || escWarning;

    // What the driver asks for (steering angle and speed) against what the car does (yaw rate).
    // Clearly less yaw than asked: the car pushes wide (understeer) - brake and cut the torque.
    const double wheelRad = m_angle / VehicleModel::kSteeringRatio * 3.141592653589793 / 180.0;
    const double expected = m_speed / 3.6 / VehicleModel::kWheelbase * std::tan(wheelRad) * 180.0 / 3.141592653589793;
    const bool understeer = m_speed > 15.0 && std::abs(expected) > 3.0 && std::abs(m_yaw) < std::abs(expected) - 4.0;
    m_escActive = !m_escOff && understeer;
    m_ctx.vehicle.escBrakeDecel = m_escActive ? 2.5 : 0.0;

    if (m_txCycle % 2 == 0)
        send(msg::AbsChassis, {{QStringLiteral("SteeringAngle"), m_angle},
                                  {QStringLiteral("YawRate"), m_yaw},
                                  {QStringLiteral("EscActive"), m_escActive ? 1 : 0},
                                  {QStringLiteral("EscOff"), m_escOff ? 1 : 0},
                                  {QStringLiteral("SasCalibrated"), m_sasCalibrated ? 1 : 0}});
}

void AbsEcu::onWake()
{
    m_valid = {true, true, true, true};
    m_txCycle = 0;
}

void AbsEcu::task10ms()
{
    using namespace dtc;
    auto &wss = m_ctx.sensors.wss;

    // Without a rolling circumference the pulses cannot be turned into a speed.
    dtcs().report(U3000NotConfigured, m_codingBlank);
    const double perMm = m_codingBlank ? 0.0 : m_circumferenceMm / static_cast<double>(kDefaultCircumferenceMm);

    // --- electrical diagnosis of the sensor lines --------------------------
    std::array<bool, 4> electricalOk{};
    for (int i = 0; i < 4; ++i) {
        const auto idx = static_cast<size_t>(i);
        const CircuitState c = wss[idx].circuit();
        m_wheel[idx] = wss[idx].value() * perMm;
        electricalOk[idx] = c == CircuitState::Ok;
        dtcs().report(wheel(i, FtbOpen), c == CircuitState::Open);
        dtcs().report(wheel(i, FtbShortGround), c == CircuitState::ShortGround);
        dtcs().report(wheel(i, FtbShortBattery), c == CircuitState::ShortBattery);
    }

    // --- plausibility: compare each wheel with the median of the others ----
    // In a tight corner the inner wheels turn up to a third slower than the outer ones. The two
    // wheels on one side run on nearly the same radius, so a wheel that matches the other wheel
    // on its side is cornering, not a sensor fault.
    std::array<bool, 4> plausible{true, true, true, true};
    for (int i = 0; i < 4; ++i) {
        const auto idx = static_cast<size_t>(i);
        if (!electricalOk[idx])
            continue;
        std::vector<double> others;
        for (size_t k = 0; k < 4; ++k)
            if (k != idx && electricalOk[k])
                others.push_back(m_wheel[k]);
        if (others.size() < 2)
            continue;
        std::sort(others.begin(), others.end());
        const double reference = others[others.size() / 2];
        if (reference > 10.0 || m_wheel[idx] > 10.0) {
            const auto within = [](double a, double b) { return std::abs(a - b) <= 0.25 * std::max(b, 10.0); };
            const size_t sameSide = idx ^ 2u; // FL <-> RL, FR <-> RR
            plausible[idx] = within(m_wheel[idx], reference)
                || (electricalOk[sameSide] && within(m_wheel[idx], m_wheel[sameSide]));
            dtcs().report(wheel(i, FtbSignalInvalid), !plausible[idx]);
        }
    }

    // --- vehicle reference speed from the trustworthy wheels ---------------
    double sum = 0.0;
    int validCount = 0;
    for (int i = 0; i < 4; ++i) {
        const auto idx = static_cast<size_t>(i);
        const bool stored = dtcs().testFailed(wheel(i, FtbOpen)) || dtcs().testFailed(wheel(i, FtbShortGround))
            || dtcs().testFailed(wheel(i, FtbShortBattery)) || dtcs().testFailed(wheel(i, FtbSignalInvalid));
        m_valid[idx] = !m_codingBlank && electricalOk[idx] && plausible[idx] && !stored;
        if (m_valid[idx]) {
            sum += m_wheel[idx];
            ++validCount;
        }
    }
    m_speedValid = validCount >= 2;
    m_speed = validCount > 0 ? sum / validCount : 0.0;

    // With any wheel speed in doubt ABS and traction control are switched off and the lamp is lit.
    // Only the wheel sensors and the coding take the ABS down; a stability control fault does not.
    bool absWarning = dtcs().status(U3000NotConfigured) & DtcStatus::WarningIndicatorRequested;
    for (int i = 0; i < 4; ++i)
        for (quint8 ftb : {FtbOpen, FtbShortGround, FtbShortBattery, FtbSignalInvalid})
            absWarning = absWarning || (dtcs().status(wheel(i, ftb)) & DtcStatus::WarningIndicatorRequested);
    const bool degraded = validCount < 4 || absWarning;
    m_lamp = m_lampForced >= 0 ? m_lampForced == 1 : degraded;

    // --- anti-lock control: hold every wheel within 15 % of the car's speed -----------------
    // The reference speed comes from the fastest trustworthy wheel; the car itself cannot slow
    // down faster than about 1.3 g, so all wheels locking together does not fool it.
    double fastest = 0.0;
    for (int i = 0; i < 4; ++i)
        if (electricalOk[static_cast<size_t>(i)])
            fastest = std::max(fastest, m_wheel[static_cast<size_t>(i)]);
    m_vRef = std::max(fastest, m_vRef - 1.3 * 9.81 * 3.6 * 0.01);
    const bool braking = m_ctx.vehicle.brakePressureBar() > 5.0;
    m_absActive = false;
    for (int i = 0; i < 4; ++i) {
        const auto idx = static_cast<size_t>(i);
        double &p = m_pressure[idx];
        if (!degraded && braking && m_vRef > 5.0) {
            const double slip = (m_vRef - m_wheel[idx]) / m_vRef;
            // Dump while the wheel slips and remember the level where it let go; then rebuild
            // quickly to just below that level and creep up slowly: most of the time is spent
            // right under the grip limit, which is what makes ABS stop shorter than locked wheels.
            double &limit = m_lockLevel[idx];
            if (slip > 0.15) {
                limit = p;
                p = std::max(0.0, p * 0.5);
            } else if (slip < 0.05) {
                p = std::min(1.0, p < 0.9 * limit ? p + 0.05 : p + 0.002);
            }
        } else {
            p = 1.0;
            m_lockLevel[idx] = 1.0;
        }
        m_absActive = m_absActive || p < 0.98;
    }
    m_ctx.vehicle.absPressure = m_pressure;
    const bool absActive = m_absActive;
    if (m_esc)
        esc10ms();

    // --- ABS_WheelSpeeds with end-to-end protection -------------------------
    if (!faultFrozenCounter)
        m_counter = (m_counter + 1) & 0x0F;
    QCanBusFrame frame = m_ctx.dbc.encode(msg::AbsWheelSpeeds,
        {{QStringLiteral("WheelSpeed_FL"), m_wheel[0]},
            {QStringLiteral("WheelSpeed_FR"), m_wheel[1]},
            {QStringLiteral("WheelSpeed_RL"), m_wheel[2]},
            {QStringLiteral("WheelSpeed_RR"), m_wheel[3]},
            {QStringLiteral("WheelSpeed_FL_Valid"), m_valid[0] ? 1 : 0},
            {QStringLiteral("WheelSpeed_FR_Valid"), m_valid[1] ? 1 : 0},
            {QStringLiteral("WheelSpeed_RL_Valid"), m_valid[2] ? 1 : 0},
            {QStringLiteral("WheelSpeed_RR_Valid"), m_valid[3] ? 1 : 0},
            {QStringLiteral("ABS_AliveCounter"), m_counter},
            {QStringLiteral("ABS_Checksum"), 0}});
    QByteArray payload = frame.payload();
    quint8 crc = crc8J1850(payload.left(7));
    if (faultBadChecksum)
        crc ^= 0x5A;
    payload[7] = static_cast<char>(crc);
    frame.setPayload(payload);
    sendRaw(frame);

    if (++m_txCycle % 2 == 0) {
        send(msg::AbsStatus, {{QStringLiteral("VehicleSpeed"), m_speed},
                                 {QStringLiteral("BrakePressure"), m_ctx.vehicle.brakePressureBar()},
                                 {QStringLiteral("VehicleSpeedValid"), m_speedValid ? 1 : 0},
                                 {QStringLiteral("AbsActive"), absActive ? 1 : 0},
                                 {QStringLiteral("AbsLampReq"), m_lamp ? 1 : 0},
                                 {QStringLiteral("TcsLampReq"), m_lamp || (m_esc && m_escOff) ? 1 : 0}});
    }
}

} // namespace ecusim
