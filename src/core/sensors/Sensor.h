#pragma once

#include <QList>
#include <QHash>
#include <QString>

#include <array>
#include <random>

namespace ecusim {

class VehicleModel;

/// All fault kinds the simulator can inject. The first block applies to sensors,
/// the second to other components (actuators, ECUs, the bus).
enum class FaultType {
    None,
    // sensors
    Stuck,          ///< output frozen (p1 = value, or the value at injection time if not given)
    Offset,         ///< physical value + p1
    Gain,           ///< physical value * p1
    Noise,          ///< gaussian noise, sigma = p1
    OpenCircuit,    ///< broken wire / unplugged connector
    ShortToGround,
    ShortToBattery,
    Intermittent,   ///< loose contact: open for p2 (0..1) of every p1 ms
    Erratic,        ///< random values across the whole range
    // components
    Failure,        ///< generic "component broken"
    PowerLoss,      ///< ECU without supply
    BusOff,         ///< ECU powered but its CAN controller is off the bus
    FrozenCounter,  ///< E2E alive counter no longer increments
    BadChecksum,    ///< E2E checksum wrong
    BusShort,       ///< CAN wiring fault, no communication at all
};

QString faultTypeName(FaultType type);       ///< stable identifier, e.g. "open"
QString faultTypeLabel(FaultType type);      ///< for the UI, e.g. "Open circuit"
FaultType faultTypeFromName(const QString &name);

struct Fault
{
    FaultType type = FaultType::None;
    double p1 = 0.0;
    double p2 = 0.0;
    bool hasP1 = false;
};

enum class CircuitState { Ok, Open, ShortGround, ShortBattery };

/// A sensor between the plant and an ECU input pin.
///
/// Analog sensors (thermistors, potentiometers) are modelled as a voltage on a 5 V ADC:
/// the ECU sees a voltage and converts it back to a physical value. That is why an open
/// circuit on an NTC temperature sensor reads as -40 degC (pull-up drives the pin to 5 V)
/// and a short to ground as +150 degC.
/// Digital sensors (wheel speed, crankshaft) deliver a frequency; with a circuit fault the
/// ECU simply sees no pulses, but can detect the electrical state of the line.
class Sensor
{
public:
    struct Config
    {
        QString id;
        QString name;
        QString unit;
        QString ecu;
        double physMin = 0.0;
        double physMax = 100.0;
        double voltsAtMin = 0.5;
        double voltsAtMax = 4.5;
        double openVolts = 5.0; ///< what the ADC reads with the sensor unplugged
        bool digital = false;
    };

    Sensor() = default;
    explicit Sensor(Config config);

    const Config &config() const { return m_config; }
    void update(double trueValue, qint64 nowMs);

    double trueValue() const { return m_true; }
    double value() const { return m_value; }     ///< as measured by the ECU
    double voltage() const { return m_voltage; } ///< at the ECU pin (analog sensors)
    CircuitState circuit() const { return m_circuit; }

    void setFault(const Fault &fault);
    void clearFault() { setFault({}); }
    const Fault &fault() const { return m_fault; }

    double toVoltage(double phys) const;
    double toPhysical(double volts) const;

private:
    Config m_config;
    Fault m_fault;
    double m_true = 0.0;
    double m_value = 0.0;
    double m_voltage = 0.0;
    double m_stuckValue = 0.0;
    double m_erraticValue = 0.0;
    bool m_stuckCaptured = false;
    CircuitState m_circuit = CircuitState::Ok;
    std::mt19937 m_rng{12345};
};

/// The sensors fitted to the virtual vehicle.
struct SensorSet
{
    SensorSet();

    std::array<Sensor, 4> wss; ///< wheel speed FL, FR, RL, RR (ABS)
    Sensor ect;                ///< engine coolant temperature (ECM)
    Sensor tps;                ///< throttle position (ECM)
    Sensor ckp;                ///< crankshaft position / engine speed (ECM)
    Sensor tft;                ///< transmission fluid temperature (TCM)

    void update(const VehicleModel &vehicle, qint64 nowMs);
    QList<Sensor *> all();
    Sensor *byId(const QString &id);
};

} // namespace ecusim
