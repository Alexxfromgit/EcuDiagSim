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
    d.define(dtcDef(P2176, 20, true));
    d.define(dtcDef(P0462, 20, true));
    d.define(dtcDef(P0463, 20, true));
    d.define(dtcDef(P0522, 20));
    d.define(dtcDef(P0523, 20));
    d.define(dtcDef(P0524, 20));
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
    s.addObdPid(0x2F, [this] { return encU8(fuelLevel() / VehicleModel::kTankLitres * 255.0); });
    s.addObdPid(0x42, [this] { return encU16(m_ctx.vehicle.batteryVoltage() * 1000.0); });
    s.addObdPid(0x5E, [this] { return encU16(m_ctx.vehicle.fuelRateLph() * 20.0); });

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
    s.addDid(0x1005, [this] { return encU8(m_oilBar / 0.05); });
    s.addDid(0x1006, [this] { return encU16(m_ctx.sensors.eop.voltage() * 1000.0); });
    s.addDid(0x1007, [this] { return encU16(m_ctx.sensors.fls.voltage() * 1000.0); });
    s.addDid(0x1004, [this] {
        QByteArray a = encU8(m_throttleAdapted ? 1 : 0);
        a.append(encU16(m_learnedClosedVolts * 1000.0));
        return a;
    });

    // --- throttle adaptation: learns the closed position of a new throttle body ---
    DiagnosticServer::Routine adaptation;
    adaptation.start = [this](const QByteArray &option, QByteArray &status) -> quint8 {
        if (!option.isEmpty())
            return Nrc::IncorrectMessageLength;
        if (m_learn == Learn::Running)
            return Nrc::RequestSequenceError;
        // Ignition on, engine stopped, foot off the pedal, vehicle at rest.
        const bool moving = !rxTimedOut(msg::AbsStatus) && rx(msg::AbsStatus, QStringLiteral("VehicleSpeed")) > 1.0;
        if (m_running || m_rpm > 50.0 || m_ctx.driver.throttlePedal > 0.02 || moving)
            return Nrc::ConditionsNotCorrect;
        m_learn = Learn::Running;
        m_learnStartMs = now();
        m_learnMaxError = 0.0;
        status = adaptationStatus();
        return 0;
    };
    adaptation.stop = [this](QByteArray &status) -> quint8 {
        if (m_learn != Learn::Running)
            return Nrc::RequestSequenceError;
        abortAdaptation();
        status = adaptationStatus();
        return 0;
    };
    adaptation.results = [this] { return adaptationStatus(); };
    s.addRoutine(kThrottleAdaptation, adaptation);

    // --- engine OBD pack: air flow, oxygen sensor, fuel trims, misfire ---
    m_obdPack = ctx.config.has(Equipment::EngineObd);
    if (m_obdPack) {
        d.define(dtcDef(P0101, 500, true, 2)); // 10 ms task: 5 s
        d.define(dtcDef(P0102, 100, true));
        d.define(dtcDef(P0103, 100, true));
        d.define(dtcDef(P0131, 50, true));     // 100 ms task: 5 s
        d.define(dtcDef(P0132, 20, true));
        d.define(dtcDef(P0134, 1, true));      // one 10 s window without switching
        d.define(dtcDef(P0171, 50, true, 2));
        d.define(dtcDef(P0172, 50, true, 2));
        d.define(dtcDef(P0300, 1, true));      // per 200-revolution window
        for (int c = 1; c <= 4; ++c)
            d.define(dtcDef(misfireDtc(c), 1, true));

        // SAE J1979: trims as (A - 128) * 100 / 128 %, MAF as (256 A + B) / 100 g/s.
        const auto trim = [](double t) { return encU8(t * 128.0 + 128.0); };
        s.addObdPid(0x06, [this, trim] { return trim(m_stft); });
        s.addObdPid(0x07, [this, trim] { return trim(m_ltft); });
        s.addObdPid(0x10, [this] { return encU16(m_mafGps * 100.0); });
        s.addObdPid(0x14, [this, trim] {
            QByteArray a = encU8(m_ctx.sensors.o2.voltage() / 0.005);
            a.append(m_closedLoop ? trim(m_stft) : QByteArray(1, static_cast<char>(0xFF))); // FF: not used for trim
            return a;
        });
        s.addDid(0x1010, [this] {
            QByteArray a;
            for (quint64 n : m_reportedMisfires)
                a.append(encU8(static_cast<double>(n)));
            return a;
        });
        s.addDid(0x1011, [this] { return encU16(m_ctx.sensors.maf.voltage() * 1000.0); });

        DiagnosticServer::Routine reset;
        reset.start = [this](const QByteArray &option, QByteArray &status) -> quint8 {
            if (!option.isEmpty())
                return Nrc::IncorrectMessageLength;
            m_ltft = 0.0;
            m_stft = 0.0;
            status = encU8(DiagnosticServer::RoutineCompleted);
            return 0;
        };
        s.addRoutine(kResetFuelTrims, reset);
    }
    if (m_obdPack)
        setSnapshotDids({0xF404, 0xF405, 0xF406, 0xF407, 0xF40C, 0xF40D, 0xF410, 0xF411, 0xF442});
    else
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
    m_haveFuel = false;
    m_prevRpm = 0.0;
    m_o2Dead = false;
    m_closedLoop = false;
    // Monitor windows start empty in every driving cycle: firings and O2 swings from the last
    // trip must not count towards this one.
    m_windowFirings = 0;
    m_windowMisfires = {};
    m_misfireBits = 0;
    m_o2WindowMs = 0;
    m_o2Min = 1.0;
    m_o2Max = 0.0;
}

void Ecm::onSleep()
{
    // Without supply the injectors and the throttle actuator are dead.
    if (m_learn == Learn::Running)
        abortAdaptation();
    m_ctx.vehicle.fuelEnable = false;
    m_ctx.vehicle.throttleCmd = 0.0;
    m_ctx.vehicle.mixtureControl = false;
    m_running = false;
    m_milFlash = false;
}

void Ecm::mixtureControl10ms()
{
    using namespace dtc;
    auto &v = m_ctx.vehicle;
    const Sensor &maf = m_ctx.sensors.maf;

    // --- air: the meter, checked against a speed-density model from rpm and throttle ---------
    const double mafVolts = maf.voltage();
    const bool mafLow = mafVolts < 0.2;
    const bool mafHigh = mafVolts > 4.8;
    dtcs().report(P0102, mafLow);
    dtcs().report(P0103, mafHigh);
    const double modelled = VehicleModel::expectedAirGps(m_rpm, m_throttlePct / 100.0);
    const double measured = maf.value();
    if (m_running && m_runMs > 5000 && m_coolant > 60.0 && !mafLow && !mafHigh)
        dtcs().report(P0101, std::abs(measured - modelled) > 0.3 * std::max(modelled, 2.0));
    m_mafOk = !mafLow && !mafHigh && !dtcs().testFailed(P0101) && !dtcs().testFailed(P0102)
        && !dtcs().testFailed(P0103);
    m_mafGps = m_mafOk ? measured : modelled; // substitute: speed-density

    // --- closed loop: a P-jump at every switch of the sensor, then a slow ramp -----------------
    const double o2Volts = m_ctx.sensors.o2.voltage();
    const bool fuelCut = v.fuelRateLph() <= 0.0;
    // The heated sensor is ready 3 s after the start. If it then sits around the 0.45 V bias for
    // 3 s although the loop drives the mixture, it is dead or disconnected: back to open loop
    // instead of chasing it, until it moves again.
    if (o2Volts > 0.55 || o2Volts < 0.35) {
        m_o2ActiveMs = now();
        m_o2Dead = false;
    }
    if (m_closedLoop && now() - m_o2ActiveMs > 3000)
        m_o2Dead = true;
    const bool wasClosed = m_closedLoop;
    m_closedLoop = m_running && m_runMs > 3000 && m_coolant > 40.0 && m_o2Ok && !m_o2Dead && !fuelCut;
    if (m_closedLoop && !wasClosed)
        m_o2ActiveMs = now(); // give the loop time to make the sensor switch
    if (m_closedLoop) {
        const int side = o2Volts > 0.45 ? 1 : -1; // rich : lean
        if (m_o2Side != 0 && side != m_o2Side)
            m_stft -= side * 0.03; // P-jump at every switch
        m_stft -= side * 0.0004;   // then 4 %/s towards the other side
        m_o2Side = side;
        m_stft = std::clamp(m_stft, -kTrimLimit, kTrimLimit);
    } else if (!fuelCut) {
        m_stft = 0.0; // open loop: the learned value alone
        m_o2Side = 0;
    }
    v.mixtureControl = true;
    v.fuelMassGps = m_mafGps / 14.7 * (1.0 + m_stft + m_ltft);
}

void Ecm::obdMonitors100ms()
{
    using namespace dtc;
    auto &v = m_ctx.vehicle;

    // --- long-term adaptation: moves a persistent short-term correction into the learned value ---
    // Only while the sensor really switches: a stuck sensor must not teach the adaptation.
    if (m_closedLoop && now() - m_o2ActiveMs < 1000 && std::abs(m_stft) > 0.02) {
        const double move = m_stft * 0.04;
        m_ltft = std::clamp(m_ltft + move, -kTrimLimit, kTrimLimit);
        m_stft -= move;
    }

    // --- oxygen sensor ---------------------------------------------------------------------------
    const double o2Volts = m_ctx.sensors.o2.voltage();
    const bool o2Expected = m_running && m_runMs > 3000 && m_coolant > 40.0 && v.fuelRateLph() > 0.0;
    dtcs().report(P0132, o2Volts > 1.2);
    if (o2Expected) {
        dtcs().report(P0131, o2Volts < 0.04);
        m_o2Min = std::min(m_o2Min, o2Volts);
        m_o2Max = std::max(m_o2Max, o2Volts);
        m_o2WindowMs += 100;
        if (m_o2WindowMs >= 10000) {
            dtcs().report(P0134, m_o2Max < 0.6 && m_o2Min > 0.3);
            m_o2Min = 1.0;
            m_o2Max = 0.0;
            m_o2WindowMs = 0;
        }
    }
    m_o2Ok = o2Volts <= 1.2 && !dtcs().testFailed(P0131) && !dtcs().testFailed(P0132) && !dtcs().testFailed(P0134);

    // --- mixture: trims at the limit mean the engine gets air or fuel the ECM does not know about ---
    if (m_closedLoop && m_runMs > 8000) {
        const double total = m_stft + m_ltft;
        dtcs().report(P0171, total > 0.23);
        dtcs().report(P0172, total < -0.23);
    }

    // --- misfire: counted per cylinder over 200 revolutions (400 firings) ----------------------------
    const quint64 firings = v.firingEvents();
    const bool crankOk = m_ctx.sensors.ckp.circuit() == CircuitState::Ok;
    if (m_running && crankOk && m_runMs > 1000) {
        m_windowFirings += firings - m_lastFirings;
        for (int c = 0; c < 4; ++c)
            m_windowMisfires[static_cast<size_t>(c)] += v.misfires(c + 1) - m_lastMisfires[static_cast<size_t>(c)];
    }
    m_lastFirings = firings;
    for (int c = 0; c < 4; ++c)
        m_lastMisfires[static_cast<size_t>(c)] = v.misfires(c + 1);
    if (m_windowFirings >= 400) {
        int cylinders = 0;
        quint64 total = 0;
        quint8 bits = 0;
        for (int c = 0; c < 4; ++c) {
            const quint64 n = m_windowMisfires[static_cast<size_t>(c)];
            const bool bad = static_cast<double>(n) / (static_cast<double>(m_windowFirings) / 4.0) > 0.05;
            dtcs().report(misfireDtc(c + 1), bad);
            cylinders += bad ? 1 : 0;
            bits |= bad ? static_cast<quint8>(1u << c) : 0;
            total += n;
            m_reportedMisfires[static_cast<size_t>(c)] = n;
        }
        dtcs().report(P0300, cylinders > 1);
        // Above 8 % of all firings the unburned fuel overheats the catalyst: the MIL flashes.
        m_milFlash = static_cast<double>(total) / static_cast<double>(m_windowFirings) > 0.08;
        m_misfireBits = bits;
        m_windowFirings = 0;
        m_windowMisfires = {};
    }
    if (!m_running)
        m_milFlash = false;
}

QByteArray Ecm::adaptationStatus() const
{
    QByteArray a = encU8(static_cast<int>(m_learn));
    a.append(encU16(m_learnedClosedVolts * 1000.0));
    return a;
}

void Ecm::abortAdaptation()
{
    m_learn = Learn::Aborted;
    m_ctx.vehicle.throttleCmd = 0.0;
}

void Ecm::runAdaptation(double pedal)
{
    auto &v = m_ctx.vehicle;
    const Sensor &tps = m_ctx.sensors.tps;
    // Somebody pressed the pedal or turned the key to start: the learn is invalid.
    if (pedal > 0.05 || v.starterEngaged() || m_running) {
        abortAdaptation();
        return;
    }
    const qint64 t = now() - m_learnStartMs;
    // Open fully in 1 s, close again in 1 s, then rest closed and measure the stop.
    const double cmd = t < 1000 ? t / 1000.0 : (t < 2000 ? 1.0 - (t - 1000) / 1000.0 : 0.0);
    v.throttleCmd = cmd;
    // The sensor must follow the plate (the motor needs ~50 ms, hence the tolerance).
    if (t > 100)
        m_learnMaxError = std::max(m_learnMaxError, std::abs(tps.value() - cmd * 100.0));
    if (t < kAdaptationMs)
        return;

    v.throttleCmd = 0.0;
    const double closed = tps.voltage();
    const bool ok = m_learnMaxError <= 15.0 && closed > 0.2 && closed < 1.0;
    m_learn = ok ? Learn::Completed : Learn::Failed;
    if (ok) {
        m_learnedClosedVolts = closed;
        if (!m_throttleAdapted) {
            m_throttleAdapted = true;
            if (onConfigured)
                onConfigured(QStringLiteral("throttle_body"));
        }
    }
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
    // A stalling engine runs down through the low speeds; a broken sensor drops to zero at once.
    // Only the second one is a crankshaft sensor fault.
    if (m_running && noSignal && m_prevRpm < 400.0) {
        m_running = false;
        m_noSignalMs = 1000;
    }
    if (!noSignal)
        m_prevRpm = m_rpm; // last engine speed seen with a signal
    // The ECM drives the starter relay, so it knows whether the engine is really being cranked
    // (the starter is inhibited outside P/N and with a flat battery).
    const bool cranking = v.starterEngaged() && !m_running;
    m_crankMs = cranking ? m_crankMs + 10 : 0;
    // The engine must be turning while it runs or while the starter has had time to spin it.
    if (m_running || m_crankMs > 300)
        dtcs().report(P0335, noSignal);
    m_noSignalMs = noSignal ? std::min(m_noSignalMs + 10, 1000) : 0;
    if (m_running && m_noSignalMs >= 200)
        m_running = false;
    else if (!m_running && m_rpm > 400.0) {
        m_running = true;
        m_runMs = 0; // PID 0x1F counts from engine start
    }
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
    // A throttle body whose closed position is unknown cannot be trusted at idle either.
    dtcs().report(P2176, !m_throttleAdapted);
    m_limp = tpsFault || !m_throttleAdapted;
    const double pedal = std::clamp(driver.throttlePedal, 0.0, 1.0);
    if (m_learn == Learn::Running)
        runAdaptation(pedal);
    else
        v.throttleCmd = m_limp ? std::min(pedal, 0.25) : pedal;
    m_throttlePct = tpsFault ? v.throttleCmd * 100.0 : sensors.tps.value();
    if (m_obdPack)
        mixtureControl10ms();

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

    // --- fuel level sender ------------------------------------------------------
    const double fuelVolts = sensors.fls.voltage();
    const bool flsHigh = fuelVolts > 4.8; // open circuit: the pull-up wins, reads empty
    const bool flsLow = fuelVolts < 0.2;  // short to ground, reads full
    dtcs().report(P0463, flsHigh);
    dtcs().report(P0462, flsLow);
    m_fuelValid = !flsHigh && !flsLow && !dtcs().testFailed(P0462) && !dtcs().testFailed(P0463);
    // Damped against fuel sloshing in the tank (time constant 3 s). While the circuit is faulty
    // the last good level is kept, so a repaired sender does not start from zero.
    if (!flsHigh && !flsLow) {
        const double rawFuel = sensors.fls.value();
        m_fuelL = m_haveFuel ? m_fuelL + (rawFuel - m_fuelL) * 0.1 / 3.0 : rawFuel;
        m_haveFuel = true;
    }

    // --- oil pressure -------------------------------------------------------------
    const double oilVolts = sensors.eop.voltage();
    const bool eopLow = oilVolts < 0.2;
    const bool eopHigh = oilVolts > 4.8;
    dtcs().report(P0522, eopLow);
    dtcs().report(P0523, eopHigh);
    const bool eopOk = !eopLow && !eopHigh && !dtcs().testFailed(P0522) && !dtcs().testFailed(P0523);
    m_oilBar = eopOk ? sensors.eop.value() : 0.0;
    if (m_running && m_runMs > 3000 && eopOk)
        dtcs().report(P0524, m_oilBar < 0.4);
    // The lamp is lit with the engine stopped (no pressure) and, fail-safe, with a sensor fault.
    m_oilWarn = !eopOk || m_oilBar < 0.4;

    if (m_obdPack)
        obdMonitors100ms();

    m_mil = dtcs().milRequested();

    send(msg::EcmStatus, {{QStringLiteral("CoolantTemp"), m_coolant},
                             {QStringLiteral("MilReq"), m_mil ? 1 : 0},
                             {QStringLiteral("CoolantWarnReq"), m_coolantWarn ? 1 : 0},
                             {QStringLiteral("EngineRunning"), m_running ? 1 : 0},
                             {QStringLiteral("EngineLimp"), m_limp ? 1 : 0},
                             {QStringLiteral("MilFlash"), m_milFlash ? 1 : 0}});
    if (m_obdPack)
        send(msg::EcmMixture, {{QStringLiteral("AirMassFlow"), m_mafGps},
                                  {QStringLiteral("O2Voltage"), sensors.o2.voltage()},
                                  {QStringLiteral("ShortTermFuelTrim"), m_stft * 100.0},
                                  {QStringLiteral("LongTermFuelTrim"), m_ltft * 100.0},
                                  {QStringLiteral("ClosedLoop"), m_closedLoop ? 1 : 0},
                                  {QStringLiteral("MisfireCyl"), m_misfireBits}});
    send(msg::EcmFuel, {{QStringLiteral("FuelLevel"), fuelLevel()},
                           {QStringLiteral("FuelLevelValid"), m_fuelValid ? 1 : 0},
                           {QStringLiteral("OilPressureWarn"), m_oilWarn ? 1 : 0},
                           {QStringLiteral("FuelRate"), m_ctx.vehicle.fuelRateLph()},
                           {QStringLiteral("OilPressure"), m_oilBar}});
}

} // namespace ecusim
