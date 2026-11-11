#include "dbc/DbcDatabase.h"

#include <QCanDbcFileParser>
#include <QCanSignalDescription>
#include <QCanUniqueIdDescription>
#include <QFile>

#include <algorithm>
#include <cmath>

namespace ecusim {

bool DbcDatabase::loadFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("Cannot open %1: %2").arg(path, file.errorString());
        return false;
    }
    return loadText(QString::fromUtf8(file.readAll()));
}

bool DbcDatabase::loadText(const QString &text)
{
    QCanDbcFileParser parser;
    if (!parser.parseData(text)) {
        m_error = parser.errorString();
        return false;
    }
    m_messages = parser.messageDescriptions();
    std::sort(m_messages.begin(), m_messages.end(),
        [](const QCanMessageDescription &a, const QCanMessageDescription &b) {
            return a.uniqueId() < b.uniqueId();
        });
    m_byId.clear();
    for (const auto &m : std::as_const(m_messages))
        m_byId.insert(static_cast<quint32>(m.uniqueId()), m);

    m_processor.setUniqueIdDescription(QCanDbcFileParser::uniqueIdDescription());
    m_processor.setMessageDescriptions(m_messages);
    m_error.clear();
    return !m_messages.isEmpty();
}

const QCanMessageDescription *DbcDatabase::message(quint32 id) const
{
    auto it = m_byId.constFind(id);
    return it == m_byId.cend() ? nullptr : &it.value();
}

QString DbcDatabase::messageName(quint32 id) const
{
    const auto *m = message(id);
    return m ? m->name() : QString();
}

QString DbcDatabase::sender(quint32 id) const
{
    const auto *m = message(id);
    return m ? m->transmitter() : QString();
}

QCanBusFrame DbcDatabase::encode(quint32 id, const QVariantMap &values) const
{
    const auto *m = message(id);
    if (!m)
        return QCanBusFrame(QCanBusFrame::InvalidFrame);

    // Quantise ourselves so the result does not depend on how the frame processor rounds.
    QVariantMap prepared;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        const QCanSignalDescription sig = m->signalDescriptionForName(it.key());
        if (!sig.isValid())
            continue;
        double v = it.value().toDouble();
        const double lo = sig.minimum();
        const double hi = sig.maximum();
        if (std::isfinite(lo) && std::isfinite(hi) && hi > lo)
            v = std::clamp(v, lo, hi);
        const double factor = sig.factor() != 0.0 && std::isfinite(sig.factor()) ? sig.factor() : 1.0;
        const double offset = std::isfinite(sig.offset()) ? sig.offset() : 0.0;
        const double raw = std::round((v - offset) / factor);
        prepared.insert(it.key(), raw * factor + offset + factor * 1e-4);
    }

    QCanBusFrame frame = m_processor.prepareFrame(m->uniqueId(), prepared);
    QByteArray payload = frame.payload();
    if (payload.size() < m->size()) {
        payload.append(m->size() - payload.size(), '\0');
        frame.setPayload(payload);
    }
    return frame;
}

QVariantMap DbcDatabase::decode(const QCanBusFrame &frame) const
{
    if (!m_byId.contains(frame.frameId()))
        return {};
    return m_processor.parseFrame(frame).signalValues;
}

QString DbcDatabase::describe(const QCanBusFrame &frame) const
{
    const auto *m = message(frame.frameId());
    if (!m)
        return {};
    const QVariantMap values = decode(frame);
    QStringList parts;
    // Keep DBC order (by start bit) rather than the map's alphabetical order.
    QList<QCanSignalDescription> sigs = m->signalDescriptions();
    std::sort(sigs.begin(), sigs.end(),
        [](const QCanSignalDescription &a, const QCanSignalDescription &b) { return a.startBit() < b.startBit(); });
    for (const auto &sig : std::as_const(sigs)) {
        if (!values.contains(sig.name()))
            continue;
        const double v = values.value(sig.name()).toDouble();
        QString text = QStringLiteral("%1=%2").arg(sig.name(), QString::number(v, 'g', 6));
        if (!sig.physicalUnit().isEmpty())
            text += QLatin1Char(' ') + sig.physicalUnit();
        parts << text;
    }
    return parts.join(QStringLiteral("  "));
}

} // namespace ecusim
