#include "vehicle/VehicleConfig.h"

namespace ecusim {

void VehicleConfig::fit(Equipment item, bool fitted)
{
    if (fitted)
        m_bits |= bit(item);
    else
        m_bits &= ~bit(item);
}

QStringList VehicleConfig::ids() const
{
    QStringList list;
    for (const EquipmentInfo &e : catalog())
        if (has(e.item))
            list << e.id;
    return list;
}

VehicleConfig VehicleConfig::fromIds(const QStringList &ids)
{
    VehicleConfig config;
    for (const QString &id : ids) {
        const std::optional<Equipment> item = fromId(id);
        if (item && info(*item)->implemented)
            config.fit(*item);
    }
    return config;
}

VehicleConfig VehicleConfig::fromJson(const QJsonArray &ids)
{
    QStringList list;
    for (const QJsonValue &v : ids)
        list << v.toString();
    return fromIds(list);
}

VehicleConfig VehicleConfig::united(const VehicleConfig &other) const
{
    VehicleConfig config;
    config.m_bits = m_bits | other.m_bits;
    return config;
}

const QList<EquipmentInfo> &VehicleConfig::catalog()
{
    static const QList<EquipmentInfo> kCatalog = {
        {Equipment::EngineObd, QStringLiteral("engine_obd"), QStringLiteral("Engine OBD pack"),
            QStringLiteral("Mass air flow and upstream oxygen sensors with fuel trim adaptation, and misfire "
                           "detection from the crankshaft speed."),
            {QStringLiteral("MAF sensor"), QStringLiteral("O2 sensor B1S1"), QStringLiteral("Fuel trims"),
                QStringLiteral("Misfire monitor"), QStringLiteral("P0101–P0171, P0300–P0304")},
            true},
        {Equipment::Tpms, QStringLiteral("tpms"), QStringLiteral("Tyre pressure monitoring"),
            QStringLiteral("Four radio sensors in the wheels, read by the body control module. Sensor IDs are "
                           "learned with a scan tool routine."),
            {QStringLiteral("4 TPMS sensors"), QStringLiteral("BCM function"), QStringLiteral("TPMS lamp"),
                QStringLiteral("C0750–C0765")},
            true},
        {Equipment::Srs, QStringLiteral("srs"), QStringLiteral("Airbag system (SRS)"),
            QStringLiteral("Airbag control module with driver and passenger squibs, seat belt buckle switch and "
                           "crash sensor."),
            {QStringLiteral("SRS control unit"), QStringLiteral("2 squibs"), QStringLiteral("Belt switch"),
                QStringLiteral("Airbag lamp"), QStringLiteral("B0001–B0052")},
            false},
        {Equipment::Esc, QStringLiteral("esc"), QStringLiteral("Stability control (ESC)"),
            QStringLiteral("Steering angle and yaw rate sensors: the ABS unit adds stability control, wheel slip "
                           "control and a steering angle calibration routine."),
            {QStringLiteral("Steering angle sensor"), QStringLiteral("Yaw rate sensor"), QStringLiteral("ESC lamp"),
                QStringLiteral("C0196, C0460")},
            false},
    };
    return kCatalog;
}

const EquipmentInfo *VehicleConfig::info(Equipment item)
{
    for (const EquipmentInfo &e : catalog())
        if (e.item == item)
            return &e;
    return nullptr;
}

std::optional<Equipment> VehicleConfig::fromId(const QString &id)
{
    for (const EquipmentInfo &e : catalog())
        if (e.id.compare(id, Qt::CaseInsensitive) == 0)
            return e.item;
    return std::nullopt;
}

} // namespace ecusim
