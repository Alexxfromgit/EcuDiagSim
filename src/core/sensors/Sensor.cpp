#include "sensors/Sensor.h"

#include "vehicle/VehicleModel.h"

#include <algorithm>
#include <cmath>

namespace ecusim {

namespace {
struct FaultName
{
    FaultType type;
    const char *name;
    const char *label;
};
constexpr FaultName kFaultNames[] = {
    {FaultType::None, "none", "None"},
    {FaultType::Stuck, "stuck", "Stuck value"},
    {FaultType::Offset, "offset", "Offset"},
    {FaultType::Gain, "gain", "Gain error"},
    {FaultType::Noise, "noise", "Noise"},
    {FaultType::OpenCircuit, "open", "Open circuit"},
    {FaultType::ShortToGround, "short_gnd", "Short to ground"},
    {FaultType::ShortToBattery, "short_bat", "Short to battery"},
    {FaultType::Intermittent, "intermittent", "Intermittent contact"},
    {FaultType::Erratic, "erratic", "Erratic signal"},
    {FaultType::Failure, "failure", "Component failure"},
    {FaultType::PowerLoss, "power_loss", "Power supply lost"},
    {FaultType::BusOff, "bus_off", "CAN bus-off"},
    {FaultType::FrozenCounter, "frozen_counter", "Alive counter frozen"},
    {FaultType::BadChecksum, "bad_checksum", "Wrong checksum"},
    {FaultType::BusShort, "bus_short", "CAN wiring short"},
    {FaultType::Unconfigured, "unconfigured", "Replaced, not configured"},
    {FaultType::Misfire, "misfire", "Misfire (cylinder)"},
};
} // namespace

QString faultTypeName(FaultType type)
{
    for (const auto &f : kFaultNames)
        if (f.type == type)
            return QString::fromLatin1(f.name);
    return QStringLiteral("none");
}

QString faultTypeLabel(FaultType type)
{
    for (const auto &f : kFaultNames)
        if (f.type == type)
            return QString::fromLatin1(f.label);
    return QStringLiteral("None");
}

FaultType faultTypeFromName(const QString &name)
{
    for (const auto &f : kFaultNames)
        if (name.compare(QLatin1String(f.name), Qt::CaseInsensitive) == 0)
            return f.type;
    return FaultType::None;
}

Sensor::Sensor(Config config)
    : m_config(std::move(config))
{
    // Give every sensor its own reproducible noise sequence.
    m_rng.seed(static_cast<unsigned>(qHash(m_config.id)));
}

double Sensor::toVoltage(double phys) const
{
    const double span = m_config.physMax - m_config.physMin;
    const double t = std::clamp((phys - m_config.physMin) / span, 0.0, 1.0);
    return m_config.voltsAtMin + t * (m_config.voltsAtMax - m_config.voltsAtMin);
}

double Sensor::toPhysical(double volts) const
{
    const double span = m_config.voltsAtMax - m_config.voltsAtMin;
    const double t = std::clamp((volts - m_config.voltsAtMin) / span, 0.0, 1.0);
    return m_config.physMin + t * (m_config.physMax - m_config.physMin);
}

void Sensor::setFault(const Fault &fault)
{
    m_fault = fault;
    m_stuckCaptured = false;
}

void Sensor::update(double trueValue, qint64 nowMs)
{
    m_true = trueValue;
    double phys = trueValue;
    m_circuit = CircuitState::Ok;

    switch (m_fault.type) {
    case FaultType::Stuck:
        if (!m_stuckCaptured) {
            m_stuckValue = m_fault.hasP1 ? m_fault.p1 : trueValue;
            m_stuckCaptured = true;
        }
        phys = m_stuckValue;
        break;
    case FaultType::Offset:
        phys += m_fault.p1;
        break;
    case FaultType::Gain:
        phys *= m_fault.hasP1 ? m_fault.p1 : 0.5;
        break;
    case FaultType::Noise: {
        const double sigma = m_fault.hasP1 ? m_fault.p1 : 0.05 * (m_config.physMax - m_config.physMin);
        // Box-Muller instead of std::normal_distribution: identical on every standard library.
        const double u1 = std::max(1e-12, m_rng() / 4294967296.0);
        const double u2 = m_rng() / 4294967296.0;
        phys += sigma * std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
        break;
    }
    case FaultType::Erratic:
        if (nowMs % 50 == 0)
            m_erraticValue = m_config.physMin + (m_rng() / 4294967296.0) * (m_config.physMax - m_config.physMin);
        phys = m_erraticValue;
        break;
    case FaultType::OpenCircuit:
        m_circuit = CircuitState::Open;
        break;
    case FaultType::ShortToGround:
        m_circuit = CircuitState::ShortGround;
        break;
    case FaultType::ShortToBattery:
        m_circuit = CircuitState::ShortBattery;
        break;
    case FaultType::Intermittent: {
        const qint64 period = std::max<qint64>(20, m_fault.hasP1 ? static_cast<qint64>(m_fault.p1) : 400);
        const double duty = m_fault.p2 > 0.0 ? std::clamp(m_fault.p2, 0.05, 0.95) : 0.5;
        if (static_cast<double>(nowMs % period) < duty * static_cast<double>(period))
            m_circuit = CircuitState::Open;
        break;
    }
    default:
        break;
    }

    if (m_config.digital) {
        m_voltage = 0.0;
        m_value = m_circuit == CircuitState::Ok ? std::clamp(phys, m_config.physMin, m_config.physMax) : 0.0;
        return;
    }

    switch (m_circuit) {
    case CircuitState::Ok:
        m_voltage = toVoltage(phys);
        break;
    case CircuitState::Open:
        m_voltage = m_config.openVolts;
        break;
    case CircuitState::ShortGround:
        m_voltage = 0.0;
        break;
    case CircuitState::ShortBattery:
        m_voltage = 5.0; // ADC input is clamped to the reference voltage
        break;
    }
    m_value = toPhysical(m_voltage);
}

SensorSet::SensorSet()
{
    const char *wheelIds[4] = {"wss_fl", "wss_fr", "wss_rl", "wss_rr"};
    const char *wheelNames[4] = {"Wheel speed sensor FL", "Wheel speed sensor FR", "Wheel speed sensor RL",
        "Wheel speed sensor RR"};
    for (size_t i = 0; i < 4; ++i) {
        Sensor::Config c;
        c.id = QString::fromLatin1(wheelIds[i]);
        c.name = QString::fromLatin1(wheelNames[i]);
        c.unit = QStringLiteral("km/h");
        c.ecu = QStringLiteral("ABS");
        c.physMin = 0.0;
        c.physMax = 400.0;
        c.digital = true;
        c.shortName = QStringLiteral("Wheel ") + QString::fromLatin1(wheelIds[i] + 4).toUpper();
        c.label = QStringLiteral("WSS");
        wss[i] = Sensor(c);
    }
    // NTC thermistors against a pull-up: cold = high voltage.
    ect = Sensor({QStringLiteral("ect"), QStringLiteral("Coolant temperature sensor"), QStringLiteral("°C"),
        QStringLiteral("ECM"), -40.0, 150.0, 4.5, 0.5, 5.0, false, QStringLiteral("Coolant"), QStringLiteral("ECT")});
    tft = Sensor({QStringLiteral("tft"), QStringLiteral("Transmission fluid temp. sensor"), QStringLiteral("°C"),
        QStringLiteral("TCM"), -40.0, 150.0, 4.5, 0.5, 5.0, false, QStringLiteral("Trans. fluid"), QStringLiteral("TFT")});
    // Potentiometer with a pull-down in the ECU: unplugged reads 0 V.
    tps = Sensor({QStringLiteral("tps"), QStringLiteral("Throttle position sensor"), QStringLiteral("%"),
        QStringLiteral("ECM"), 0.0, 100.0, 0.5, 4.5, 0.0, false, QStringLiteral("Throttle"), QStringLiteral("TPS")});
    // Pressure sensor, 0.5..4.5 V for 0..10 bar, pull-down in the ECM: unplugged reads 0 V.
    eop = Sensor({QStringLiteral("eop"), QStringLiteral("Oil pressure sensor"), QStringLiteral("bar"),
        QStringLiteral("ECM"), 0.0, 10.0, 0.5, 4.5, 0.0, false, QStringLiteral("Oil pressure"), QStringLiteral("EOP")});
    // Float arm on a resistor track against a pull-up: empty = high voltage, so a broken wire
    // reads "empty", never "full".
    fls = Sensor({QStringLiteral("fls"), QStringLiteral("Fuel level sender"), QStringLiteral("l"), QStringLiteral("ECM"),
        0.0, VehicleModel::kTankLitres, 4.5, 0.5, 5.0, false, QStringLiteral("Fuel level"), QStringLiteral("FUEL")});
    // Hot-film air flow meter, 0.5..4.5 V for 0..200 g/s, pull-down: unplugged reads 0 V.
    maf = Sensor({QStringLiteral("maf"), QStringLiteral("Mass air flow sensor"), QStringLiteral("g/s"),
        QStringLiteral("ECM"), 0.0, 200.0, 0.5, 4.5, 0.0, false, QStringLiteral("Air flow"), QStringLiteral("MAF"), false});
    // Narrowband zirconia cell: the value is its voltage. The ECM biases the input to 0.45 V,
    // so an open circuit sits in the middle and never switches.
    o2 = Sensor({QStringLiteral("o2"), QStringLiteral("Oxygen sensor B1S1"), QStringLiteral("V"), QStringLiteral("ECM"),
        0.0, 1.0, 0.0, 1.0, 0.45, false, QStringLiteral("O2 B1S1"), QStringLiteral("O2"), false});
    ckp = Sensor({QStringLiteral("ckp"), QStringLiteral("Crankshaft position sensor"), QStringLiteral("rpm"),
        QStringLiteral("ECM"), 0.0, 8000.0, 0.0, 0.0, 0.0, true, QStringLiteral("Crankshaft"), QStringLiteral("CKP")});
}

void SensorSet::update(const VehicleModel &vehicle, qint64 nowMs)
{
    for (int i = 0; i < 4; ++i)
        wss[static_cast<size_t>(i)].update(vehicle.wheelSpeedKph(i), nowMs);
    ect.update(vehicle.coolantTemp(), nowMs);
    tps.update(vehicle.throttlePos() * 100.0, nowMs);
    ckp.update(vehicle.engineRpm(), nowMs);
    tft.update(vehicle.transTemp(), nowMs);
    eop.update(vehicle.oilPressureBar(), nowMs);
    fls.update(vehicle.fuelLevel(), nowMs);
    if (maf.config().fitted) {
        maf.update(vehicle.airMassGps(), nowMs);
        o2.update(vehicle.o2SensorVolts(), nowMs);
    }
}

void SensorSet::fitEngineObd(bool fitted)
{
    for (Sensor *s : {&maf, &o2}) {
        Sensor::Config c = s->config();
        c.fitted = fitted;
        *s = Sensor(c);
    }
}

QList<Sensor *> SensorSet::all()
{
    // Table order: wheels, then the engine and transmission sensors.
    QList<Sensor *> list{&wss[0], &wss[1], &wss[2], &wss[3], &ckp, &ect, &tps};
    if (maf.config().fitted)
        list << &maf << &o2;
    list << &eop << &fls << &tft;
    return list;
}

Sensor *SensorSet::byId(const QString &id)
{
    for (Sensor *s : all())
        if (s->config().id == id)
            return s;
    return nullptr;
}

} // namespace ecusim
