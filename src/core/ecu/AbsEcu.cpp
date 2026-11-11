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

    auto &s = diag();
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

void AbsEcu::onWake()
{
    m_valid = {true, true, true, true};
    m_txCycle = 0;
}

void AbsEcu::task10ms()
{
    using namespace dtc;
    auto &wss = m_ctx.sensors.wss;

    // --- electrical diagnosis of the sensor lines --------------------------
    std::array<bool, 4> electricalOk{};
    for (int i = 0; i < 4; ++i) {
        const auto idx = static_cast<size_t>(i);
        const CircuitState c = wss[idx].circuit();
        m_wheel[idx] = wss[idx].value();
        electricalOk[idx] = c == CircuitState::Ok;
        dtcs().report(wheel(i, FtbOpen), c == CircuitState::Open);
        dtcs().report(wheel(i, FtbShortGround), c == CircuitState::ShortGround);
        dtcs().report(wheel(i, FtbShortBattery), c == CircuitState::ShortBattery);
    }

    // --- plausibility: compare each wheel with the median of the others ----
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
            plausible[idx] = std::abs(m_wheel[idx] - reference) <= 0.25 * std::max(reference, 10.0);
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
        m_valid[idx] = electricalOk[idx] && plausible[idx] && !stored;
        if (m_valid[idx]) {
            sum += m_wheel[idx];
            ++validCount;
        }
    }
    m_speedValid = validCount >= 2;
    m_speed = validCount > 0 ? sum / validCount : 0.0;

    // With any wheel speed in doubt ABS and traction control are switched off and the lamp is lit.
    const bool degraded = validCount < 4 || dtcs().warningRequested();
    m_lamp = m_lampForced >= 0 ? m_lampForced == 1 : degraded;
    const bool absActive = !degraded && m_ctx.driver.brakePedal > 0.8 && m_speed > 20.0;

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
                                 {QStringLiteral("TcsLampReq"), m_lamp ? 1 : 0}});
    }
}

} // namespace ecusim
