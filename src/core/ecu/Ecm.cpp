#include "ecu/Ecus.h"

#include <algorithm>
#include <cmath>

namespace ecusim {

quint8 crc8J1850(const QByteArray &data)
{
    quint8 crc = 0xFF;
    for (const char c : data) {
        crc ^= static_cast<quint8>(c);
        for (int bit = 0; bit < 8; ++bit)
            crc = static_cast<quint8>((crc & 0x80) ? ((crc << 1) ^ 0x1D) : (crc << 1));
    }
    return static_cast<quint8>(crc ^ 0xFF);
}

Ecm::Ecm(SimContext &ctx)
    : Ecu(ctx, {QStringLiteral("ECM"), QStringLiteral("Engine control module"), DiagId::EcmRequest, false, true,
               QStringLiteral("ECM_2.4.1"), QStringLiteral("0261S10042")})
{
    using namespace dtc;
    subscribe(msg::AbsWheelSpeeds, 100);
    subscribe(msg::AbsStatus, 200, U0121);
    subscribe(msg::TcmStatus, 200, U0101);
    subscribe(msg::BcmStatus, 500, U0140);

    auto &d = dtcs();
    d.define(dtcDef(P0116, 3, true, 2));
    d.define(dtcDef(P0117, 5, true));
    d.define(dtcDef(P0118, 5, true));
    d.define(dtcDef(P0121, 50, true, 2));
    d.define(dtcDef(P0122, 20, true));
    d.define(dtcDef(P0123, 20, true));
    d.define(dtcDef(P0217, 30, true));
    d.define(dtcDef(P0335, 10, true));
    d.define(dtcDef(P0562, 30, true));
    d.define(dtcDef(P0700, 3, true));
    d.define(networkDtc(U0101));
    d.define(networkDtc(U0121));
    d.define(networkDtc(U0140));
    d.define(networkDtc(U0415Counter, 10));
    d.define(networkDtc(U0415Checksum, 10));

    // --- OBD-II Mode 01 PIDs (encodings per SAE J1979) ---
    auto &s = diag();
    s.addObdPid(0x01, [this] {
        int count = 0;
        for (const DtcEntry *e : dtcs().matching(DtcStatus::Confirmed))
            count += e->def.emissionRelevant ? 1 : 0;
        QByteArray a;
        a.append(static_cast<char>((m_mil ? 0x80 : 0x00) | std::min(count, 127)));
        a.append(static_cast<char>(0x07)); // misfire, fuel system, components monitors supported
        a.append(2, '\0');
        return a;
    });
    s.addObdPid(0x04, [this] { return encU8(m_ctx.vehicle.engineTorque() / 260.0 * 255.0); });
    s.addObdPid(0x05, [this] { return encU8(m_coolant + 40.0); });
    s.addObdPid(0x0C, [this] { return encU16(m_rpm * 4.0); });
    s.addObdPid(0x0D, [this] { return encU8(rx(msg::AbsStatus, QStringLiteral("VehicleSpeed"))); });
    s.addObdPid(0x11, [this] { return encU8(m_throttlePct * 2.55); });
    s.addObdPid(0x1F, [this] { return encU16(static_cast<double>(m_runMs) / 1000.0); });
    s.addObdPid(0x42, [this] { return encU16(m_ctx.vehicle.batteryVoltage() * 1000.0); });

    // --- OBD-II Mode 09 ---
    s.addObdInfoType(0x02, [] { return QByteArray(kVin); });
    s.addObdInfoType(0x04, [] { return QByteArray("ECUSIM-ECM-0241").leftJustified(16, '\0'); });
    s.addObdInfoType(0x0A, [] { return QByteArray("ECM-EngineControl").leftJustified(20, '\0'); });

    // --- manufacturer specific data identifiers ---
    s.addDid(0x1001, [this] { return encU16(m_ctx.sensors.ect.voltage() * 1000.0); });
    s.addDid(0x1002, [this] { return encU16(m_ctx.sensors.tps.voltage() * 1000.0); });
    s.addDid(0x1003, [this] {
        return encU8((m_running ? 0x01 : 0x00) | (m_limp ? 0x02 : 0x00) | (m_mil ? 0x04 : 0x00));
    });
    setSnapshotDids({0xF404, 0xF405, 0xF40C, 0xF40D, 0xF411, 0xF442});

    every(10, [this] { task10ms(); });
    every(100, [this] { task100ms(); });
}

void Ecm::onWake()
{
    m_running = false;
    m_noSignalMs = 1000;
    m_crankMs = 0;
    m_haveLastEct = false;
    m_lastCounter = -1;
    m_runMs = 0;
}

void Ecm::onSleep()
{
    // Without supply the injectors and the throttle actuator are dead.
    m_ctx.vehicle.fuelEnable = false;
    m_ctx.vehicle.throttleCmd = 0.0;
    m_running = false;
}

void Ecm::onMessage(quint32 id, const QCanBusFrame &frame)
{
    if (id != msg::AbsWheelSpeeds || !monitoringEnabled())
        return;
    // End-to-end check: the CRC covers bytes 0..6, the alive counter must move on.
    const QByteArray p = frame.payload();
    if (p.size() < 8)
        return;
    const bool crcOk = crc8J1850(p.left(7)) == static_cast<quint8>(p[7]);
    const int counter = (static_cast<quint8>(p[6]) >> 4) & 0x0F;
    dtcs().report(dtc::U0415Checksum, !crcOk);
    if (m_lastCounter >= 0)
        dtcs().report(dtc::U0415Counter, counter == m_lastCounter);
    m_lastCounter = counter;
}

void Ecm::task10ms()
{
    using namespace dtc;
    auto &v = m_ctx.vehicle;
    auto &sensors = m_ctx.sensors;
    const DriverInputs &driver = m_ctx.driver;

    // --- crankshaft sensor / engine state -----------------------------------
    m_rpm = sensors.ckp.value();
    const bool noSignal = m_rpm < 50.0;
    const bool cranking = driver.ignition == Ignition::Start && !m_running;
    m_crankMs = cranking ? m_crankMs + 10 : 0;
    // The engine must be turning while it runs or while the starter has had time to spin it.
    if (m_running || m_crankMs > 300)
        dtcs().report(P0335, noSignal);
    m_noSignalMs = noSignal ? std::min(m_noSignalMs + 10, 1000) : 0;
    if (m_running && m_noSignalMs >= 200)
        m_running = false;
    else if (!m_running && m_rpm > 400.0)
        m_running = true;
    // No crank signal -> no injection/ignition timing -> the engine cannot run.
    v.fuelEnable = m_noSignalMs < 100;
    if (m_running)
        m_runMs += 10;

    // --- throttle -------------------------------------------------------------
    const double tpsVolts = sensors.tps.voltage();
    const bool tpsLow = tpsVolts < 0.2;
    const bool tpsHigh = tpsVolts > 4.8;
    dtcs().report(P0122, tpsLow);
    dtcs().report(P0123, tpsHigh);
    const double commandedPct = v.throttleCmd * 100.0;
    if (m_running && !tpsLow && !tpsHigh)
        dtcs().report(P0121, std::abs(sensors.tps.value() - commandedPct) > 20.0);
    const bool tpsFault = tpsLow || tpsHigh || dtcs().testFailed(P0121) || dtcs().testFailed(P0122)
        || dtcs().testFailed(P0123);

    // Limp home: without a trustworthy throttle feedback the opening is limited.
    m_limp = tpsFault;
    const double pedal = std::clamp(driver.throttlePedal, 0.0, 1.0);
    v.throttleCmd = m_limp ? std::min(pedal, 0.25) : pedal;
    m_throttlePct = tpsFault ? v.throttleCmd * 100.0 : sensors.tps.value();

    send(msg::EcmEngine, {{QStringLiteral("EngineRPM"), m_rpm},
                             {QStringLiteral("ThrottlePos"), m_throttlePct},
                             {QStringLiteral("PedalPos"), pedal * 100.0},
                             {QStringLiteral("EngineTorque"), v.engineTorque()}});
}

void Ecm::task100ms()
{
    using namespace dtc;
    auto &sensors = m_ctx.sensors;

    // --- coolant temperature ----------------------------------------------
    const double volts = sensors.ect.voltage();
    const bool high = volts > 4.8; // open circuit: the pull-up wins
    const bool low = volts < 0.2;  // short to ground
    dtcs().report(P0118, high);
    dtcs().report(P0117, low);
    const double raw = sensors.ect.value();
    if (!high && !low) {
        bool implausible = m_haveLastEct && std::abs(raw - m_lastEct) > 10.0;
        if (!m_haveLastEct)
            m_startEct = raw;
        // A cold engine must warm up: a sensor that has not moved after 90 s is stuck.
        if (m_runMs > 90'000 && m_startEct < 50.0 && std::abs(raw - m_startEct) < 3.0)
            implausible = true;
        dtcs().report(P0116, implausible);
        m_lastEct = raw;
        m_haveLastEct = true;
    } else {
        m_haveLastEct = false;
    }
    const bool ectFault = high || low || dtcs().testFailed(P0116) || dtcs().testFailed(P0117)
        || dtcs().testFailed(P0118);
    m_coolant = ectFault ? 80.0 : raw; // substitute value keeps the engine driveable
    dtcs().report(P0217, m_coolant > 118.0);
    m_coolantWarn = m_coolant > 112.0;

    // --- supply voltage and TCM lamp request --------------------------------
    if (m_running)
        dtcs().report(P0562, m_ctx.vehicle.batteryVoltage() < 11.0);
    if (monitoringEnabled() && !rxTimedOut(msg::TcmStatus))
        dtcs().report(P0700, rx(msg::TcmStatus, QStringLiteral("TcmMilReq")) > 0.5);

    m_mil = dtcs().milRequested();

    send(msg::EcmStatus, {{QStringLiteral("CoolantTemp"), m_coolant},
                             {QStringLiteral("MilReq"), m_mil ? 1 : 0},
                             {QStringLiteral("CoolantWarnReq"), m_coolantWarn ? 1 : 0},
                             {QStringLiteral("EngineRunning"), m_running ? 1 : 0},
                             {QStringLiteral("EngineLimp"), m_limp ? 1 : 0}});
}

} // namespace ecusim
