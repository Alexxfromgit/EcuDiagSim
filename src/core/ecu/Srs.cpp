#include "ecu/Ecus.h"

#include <algorithm>

namespace ecusim {

Srs::Srs(SimContext &ctx)
    : Ecu(ctx, {QStringLiteral("SRS"), QStringLiteral("Airbag control module"), DiagId::SrsRequest, false, false,
               QStringLiteral("SRS_4.2.0"), QStringLiteral("0285A10007")})
{
    using namespace dtc;
    subscribe(msg::AbsStatus, 500);

    auto &d = dtcs();
    for (int i = 0; i < 2; ++i) {
        d.define(dtcDef(squib(i, FtbResistanceLow), 20));
        d.define(dtcDef(squib(i, FtbResistanceHigh), 20));
    }
    d.define(dtcDef(B0050BeltSwitch, 20));
    d.define(dtcDef(B0052Deployment, 1));
    d.define(dtcDef(U3000NotConfigured, 5));

    auto &s = diag();
    // Squib resistances as the module measures them (0.01 ohm, FFFF = open).
    for (int i = 0; i < 2; ++i)
        s.addDid(static_cast<quint16>(0x5001 + i), [this, i] {
            const double ohms = m_ctx.vehicle.squibResistance(i);
            return ohms > 600.0 ? QByteArray(2, static_cast<char>(0xFF)) : encU16(ohms * 100.0);
        });
    s.addDid(0x5003, [this] { return encU8((buckled() ? 0x01 : 0) | (m_ctx.vehicle.beltSwitchOpen ? 0x02 : 0)); });
    // Crash record: stored, delta-v in km/h, driver belted. A deployed module keeps it for good.
    s.addDid(kCrashRecordDid, [this] {
        QByteArray a = encU8(m_crashRecord ? 1 : 0);
        a.append(encU8(m_crashDeltaV));
        a.append(encU8(m_crashBelted ? 1 : 0));
        return a;
    });
    // Variant coding: bit0 passenger airbag fitted, bit1 seat belt reminder.
    s.addDid(0x0100, [this] { return encU8(m_codingBlank ? 0xFF : m_coding); });
    s.addWritableDid(0x0100, 1, [this](const QByteArray &data) -> quint8 {
        const quint8 coding = static_cast<quint8>(data[0]);
        if (coding & ~(PassengerAirbag | BeltReminder))
            return Nrc::RequestOutOfRange;
        m_coding = coding;
        if (m_codingBlank) {
            m_codingBlank = false;
            if (onConfigured)
                onConfigured(QStringLiteral("srs"));
        }
        return 0;
    });
    // Crash data must survive any attempt to erase it: only a new module starts clean.
    s.clearGuard = [this]() -> quint8 { return m_crashRecord ? Nrc::ConditionsNotCorrect : 0; };
    setSnapshotDids({0x5001, 0x5002, 0x5003});

    every(10, [this] { task10ms(); });
    every(100, [this] { task100ms(); });
}

void Srs::setCodingBlank(bool blank)
{
    if (blank) {
        // A new module from the parts store: no coding, no history.
        m_codingBlank = true;
        m_crashRecord = false;
        m_crashDeltaV = 0.0;
        dtcs().clear(0xFFFFFF);
    } else if (m_codingBlank) {
        m_codingBlank = false;
        m_coding = kDefaultCoding;
    }
}

void Srs::onWake()
{
    m_crashThisCycle = false;
    m_seenCrash = m_ctx.vehicle.crashCount();
}

bool Srs::buckled() const
{
    return m_ctx.driver.seatBelt && !m_ctx.vehicle.beltSwitchOpen;
}

void Srs::task10ms()
{
    // The acceleration sensor: a frontal impact above 20 km/h delta-v fires the airbags.
    auto &v = m_ctx.vehicle;
    if (v.crashCount() == m_seenCrash)
        return;
    m_seenCrash = v.crashCount();
    if (v.crashDeltaV() < kDeployDeltaV || m_codingBlank)
        return;
    v.airbagFired[0] = true;
    v.airbagFired[1] = coded(PassengerAirbag);
    m_crashRecord = true;
    m_crashThisCycle = true;
    m_crashDeltaV = v.crashDeltaV();
    m_crashBelted = buckled();
}

void Srs::task100ms()
{
    using namespace dtc;
    const auto &v = m_ctx.vehicle;

    dtcs().report(U3000NotConfigured, m_codingBlank);
    // Squib circuits: a small test current, the module expects 1..4 ohm.
    for (int i = 0; i < 2; ++i) {
        if (i == 1 && !coded(PassengerAirbag))
            continue;
        const double ohms = v.squibResistance(i);
        dtcs().report(squib(i, FtbResistanceLow), ohms < 1.0);
        dtcs().report(squib(i, FtbResistanceHigh), ohms > 4.0);
    }
    // The buckle switch is resistor coded: buckled and unbuckled are two valid values, open is a fault.
    dtcs().report(B0050BeltSwitch, v.beltSwitchOpen);
    dtcs().report(B0052Deployment, m_crashRecord);

    m_lamp = m_codingBlank || m_crashRecord || dtcs().warningRequested();
    const double speed = rxTimedOut(msg::AbsStatus) ? 0.0 : rx(msg::AbsStatus, QStringLiteral("VehicleSpeed"));
    const bool beltWarn = coded(BeltReminder) && !buckled();
    send(msg::SrsStatus, {{QStringLiteral("AirbagLampReq"), m_lamp ? 1 : 0},
                             {QStringLiteral("BeltWarn"), beltWarn ? 1 : 0},
                             {QStringLiteral("BeltChime"), beltWarn && speed > 20.0 ? 1 : 0},
                             {QStringLiteral("DriverBuckled"), buckled() ? 1 : 0},
                             {QStringLiteral("CrashDetected"), m_crashThisCycle ? 1 : 0},
                             {QStringLiteral("DeltaV"), m_crashDeltaV}});
}

} // namespace ecusim
