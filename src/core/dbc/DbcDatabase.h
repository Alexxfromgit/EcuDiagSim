#pragma once

#include <QCanBusFrame>
#include <QCanFrameProcessor>
#include <QCanMessageDescription>
#include <QHash>
#include <QList>
#include <QString>
#include <QVariantMap>

namespace ecusim {

/// Identifiers of the application messages defined in resources/dbc/vehicle.dbc.
namespace msg {
constexpr quint32 AbsWheelSpeeds = 0x0B0;
constexpr quint32 AbsStatus = 0x0B1;
constexpr quint32 EcmEngine = 0x100;
constexpr quint32 TcmStatus = 0x1A0;
constexpr quint32 EcmStatus = 0x300;
constexpr quint32 BcmStatus = 0x350;
} // namespace msg

/// The CAN database: single source of truth for message layout.
/// Thin wrapper around Qt SerialBus' DBC parser and frame processor.
class DbcDatabase
{
public:
    bool loadFile(const QString &path);
    bool loadText(const QString &text);
    QString errorString() const { return m_error; }

    bool contains(quint32 id) const { return m_byId.contains(id); }
    const QCanMessageDescription *message(quint32 id) const;
    const QList<QCanMessageDescription> &messages() const { return m_messages; }
    QString messageName(quint32 id) const;
    QString sender(quint32 id) const;

    /// Packs physical signal values into a frame. Values are clamped to the signal range.
    QCanBusFrame encode(quint32 id, const QVariantMap &values) const;
    /// Unpacks a frame into physical values. Empty map for unknown identifiers.
    QVariantMap decode(const QCanBusFrame &frame) const;
    /// Human readable "Signal=value unit" list for the trace view.
    QString describe(const QCanBusFrame &frame) const;

private:
    QString m_error;
    QList<QCanMessageDescription> m_messages;
    QHash<quint32, QCanMessageDescription> m_byId;
    mutable QCanFrameProcessor m_processor;
};

} // namespace ecusim
