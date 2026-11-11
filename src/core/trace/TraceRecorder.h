#pragma once

#include "can/VirtualCanBus.h"

#include <QDateTime>
#include <QList>
#include <QString>

#include <deque>

namespace ecusim {

/// Keeps the most recent bus traffic in a ring buffer and reads/writes the two
/// most common text log formats: Linux can-utils "candump -L" and Vector ASC.
class TraceRecorder
{
public:
    explicit TraceRecorder(size_t capacity = 200'000);

    void record(const BusFrame &frame);
    void clear();
    const std::deque<BusFrame> &frames() const { return m_frames; }
    quint64 totalCount() const { return m_total; }

    bool saveCandump(const QString &path, QString *error = nullptr) const;
    bool saveAsc(const QString &path, const QDateTime &start, QString *error = nullptr) const;

    static QString candumpLine(const BusFrame &frame, const QString &interfaceName = QStringLiteral("vcan0"));
    static QString ascLine(const BusFrame &frame);
    static QString ascHeader(const QDateTime &start);

    /// Loads a candump or ASC log (auto-detected). Unparseable lines are skipped.
    static QList<BusFrame> load(const QString &path, QString *error = nullptr);
    static QList<BusFrame> parse(const QString &text);

private:
    size_t m_capacity;
    quint64 m_total = 0;
    std::deque<BusFrame> m_frames;
};

/// Plays a recorded log back onto the bus, preserving the relative timing.
class TraceReplayer : public CanNode
{
public:
    explicit TraceReplayer(VirtualCanBus &bus);
    ~TraceReplayer() override;

    QString nodeName() const override { return QStringLiteral("Replay"); }
    void onFrame(const QCanBusFrame &) override {}

    void start(const QList<BusFrame> &frames, qint64 nowUs);
    void stop();
    void tick(qint64 nowUs);

    bool active() const { return m_active; }
    double progress() const;

private:
    VirtualCanBus &m_bus;
    QList<BusFrame> m_frames;
    qsizetype m_index = 0;
    qint64 m_offsetUs = 0;
    bool m_active = false;
};

} // namespace ecusim
