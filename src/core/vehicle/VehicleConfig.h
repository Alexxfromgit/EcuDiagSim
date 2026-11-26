#pragma once

#include <QJsonArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace ecusim {

/// Optional systems that can be fitted to the virtual vehicle, like the equipment list of a
/// real car. The base car (engine, transmission, ABS, body, cluster) is always fitted.
enum class Equipment {
    EngineObd, ///< MAF, upstream O2 sensor with fuel trims, misfire detection
    Tpms,      ///< tyre pressure monitoring in the BCM
    Srs,       ///< airbag control module
    Esc,       ///< steering angle and yaw rate sensors, stability control in the ABS
};

/// What the Settings page shows for one catalog item.
struct EquipmentInfo
{
    Equipment item;
    QString id;          ///< stable identifier used in settings and lesson files, e.g. "tpms"
    QString name;
    QString description;
    QStringList adds;    ///< sensors, control units, lamps and DTC families it brings along
    bool implemented = false; ///< false: listed in the catalog, but not available yet
};

/// The equipment fitted to one Simulation. Fixed for the lifetime of a Simulation: fitting or
/// removing something means building a new vehicle, which keeps every run reproducible.
class VehicleConfig
{
public:
    VehicleConfig() = default;

    bool has(Equipment item) const { return (m_bits & bit(item)) != 0; }
    void fit(Equipment item, bool fitted = true);
    bool isEmpty() const { return m_bits == 0; }

    /// Identifiers of the fitted items, in catalog order.
    QStringList ids() const;
    /// Unknown identifiers and items that are not implemented yet are ignored.
    static VehicleConfig fromIds(const QStringList &ids);
    /// The "equipment" array of a lesson file.
    static VehicleConfig fromJson(const QJsonArray &ids);
    /// Everything fitted to either configuration.
    VehicleConfig united(const VehicleConfig &other) const;

    static const QList<EquipmentInfo> &catalog();
    static const EquipmentInfo *info(Equipment item);
    static std::optional<Equipment> fromId(const QString &id);

    friend bool operator==(const VehicleConfig &a, const VehicleConfig &b) { return a.m_bits == b.m_bits; }
    friend bool operator!=(const VehicleConfig &a, const VehicleConfig &b) { return a.m_bits != b.m_bits; }

private:
    static quint32 bit(Equipment item) { return 1u << static_cast<int>(item); }
    quint32 m_bits = 0;
};

} // namespace ecusim
