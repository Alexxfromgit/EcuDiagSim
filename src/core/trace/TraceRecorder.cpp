#include "trace/TraceRecorder.h"

#include <QFile>
#include <QLocale>
#include <QRegularExpression>
#include <QTextStream>

namespace ecusim {

TraceRecorder::TraceRecorder(size_t capacity)
    : m_capacity(capacity)
{
}

void TraceRecorder::record(const BusFrame &frame)
{
    m_frames.push_back(frame);
    if (m_frames.size() > m_capacity)
        m_frames.pop_front();
    ++m_total;
}

void TraceRecorder::clear()
{
    m_frames.clear();
}

QString TraceRecorder::candumpLine(const BusFrame &f, const QString &interfaceName)
{
    // (0000000012.345678) vcan0 0B0#1122334455667788
    const bool ext = f.frame.hasExtendedFrameFormat();
    const QString id = QStringLiteral("%1").arg(f.frame.frameId(), ext ? 8 : 3, 16, QLatin1Char('0')).toUpper();
    return QStringLiteral("(%1.%2) %3 %4#%5")
        .arg(f.timeUs / 1'000'000, 10, 10, QLatin1Char('0'))
        .arg(f.timeUs % 1'000'000, 6, 10, QLatin1Char('0'))
        .arg(interfaceName, id, QString::fromLatin1(f.frame.payload().toHex()).toUpper());
}

QString TraceRecorder::ascHeader(const QDateTime &start)
{
    const QLocale c = QLocale::c();
    return QStringLiteral("date %1\nbase hex  timestamps absolute\ninternal events logged\n"
                          "Begin Triggerblock %1\n   0.000000 Start of measurement\n")
        .arg(c.toString(start, QStringLiteral("ddd MMM d hh:mm:ss.zzz ap yyyy")));
}

QString TraceRecorder::ascLine(const BusFrame &f)
{
    //    0.010000 1  0B0             Rx   d 8 11 22 33 44 55 66 77 88
    QString id = QString::number(f.frame.frameId(), 16).toUpper();
    if (f.frame.hasExtendedFrameFormat())
        id += QLatin1Char('x');
    const QByteArray p = f.frame.payload();
    return QStringLiteral("%1 1  %2 Rx   d %3 %4")
        .arg(static_cast<double>(f.timeUs) / 1e6, 11, 'f', 6)
        .arg(id, -15)
        .arg(p.size())
        .arg(QString::fromLatin1(p.toHex(' ')).toUpper());
}

bool TraceRecorder::saveCandump(const QString &path, QString *error) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QTextStream out(&file);
    for (const BusFrame &f : m_frames)
        out << candumpLine(f) << '\n';
    return true;
}

bool TraceRecorder::saveAsc(const QString &path, const QDateTime &start, QString *error) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QTextStream out(&file);
    out << ascHeader(start);
    for (const BusFrame &f : m_frames)
        out << ascLine(f) << '\n';
    out << "End TriggerBlock\n";
    return true;
}

QList<BusFrame> TraceRecorder::parse(const QString &text)
{
    static const QRegularExpression candump(
        QStringLiteral(R"(^\((\d+)\.(\d{1,6})\)\s+(\S+)\s+([0-9A-Fa-f]{1,8})#([0-9A-Fa-f]*)\s*$)"));
    static const QRegularExpression asc(QStringLiteral(
        R"(^\s*(\d+\.\d+)\s+\d+\s+([0-9A-Fa-f]+)(x?)\s+(?:Rx|Tx)\s+d\s+(\d+)((?:\s+[0-9A-Fa-f]{2})*)\s*)"));

    QList<BusFrame> frames;
    const auto lines = QStringView(text).split(u'\n');
    for (QStringView rawLine : lines) {
        const QString line = rawLine.trimmed().toString();
        if (line.isEmpty())
            continue;
        BusFrame f;
        f.sender = QStringLiteral("Replay");
        quint32 id = 0;
        QByteArray payload;
        bool extended = false;

        if (auto m = candump.match(line); m.hasMatch()) {
            const QString frac = m.captured(2).leftJustified(6, QLatin1Char('0'));
            f.timeUs = m.captured(1).toLongLong() * 1'000'000 + frac.toLongLong();
            id = m.captured(4).toUInt(nullptr, 16);
            extended = m.captured(4).size() > 3;
            payload = QByteArray::fromHex(m.captured(5).toLatin1());
        } else if (auto a = asc.match(line); a.hasMatch()) {
            f.timeUs = qRound64(a.captured(1).toDouble() * 1e6);
            id = a.captured(2).toUInt(nullptr, 16);
            extended = !a.captured(3).isEmpty();
            payload = QByteArray::fromHex(a.captured(5).remove(QLatin1Char(' ')).toLatin1());
            payload.truncate(a.captured(4).toInt());
        } else {
            continue;
        }
        if (payload.size() > 8)
            continue;
        f.frame = QCanBusFrame(id, payload);
        f.frame.setExtendedFrameFormat(extended);
        f.frame.setTimeStamp(QCanBusFrame::TimeStamp::fromMicroSeconds(f.timeUs));
        frames.append(f);
    }
    return frames;
}

QList<BusFrame> TraceRecorder::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = file.errorString();
        return {};
    }
    const QList<BusFrame> frames = parse(QString::fromUtf8(file.readAll()));
    if (frames.isEmpty() && error)
        *error = QStringLiteral("no CAN frames found (expected candump or Vector ASC format)");
    return frames;
}

// --- replay ---------------------------------------------------------------

TraceReplayer::TraceReplayer(VirtualCanBus &bus)
    : m_bus(bus)
{
    m_bus.attach(this);
}

TraceReplayer::~TraceReplayer()
{
    m_bus.detach(this);
}

void TraceReplayer::start(const QList<BusFrame> &frames, qint64 nowUs)
{
    m_frames = frames;
    m_index = 0;
    m_active = !frames.isEmpty();
    m_offsetUs = m_active ? nowUs - frames.first().timeUs : 0;
}

void TraceReplayer::stop()
{
    m_active = false;
    m_frames.clear();
    m_index = 0;
}

void TraceReplayer::tick(qint64 nowUs)
{
    if (!m_active)
        return;
    while (m_index < m_frames.size() && m_frames.at(m_index).timeUs + m_offsetUs <= nowUs) {
        m_bus.send(this, m_frames.at(m_index).frame);
        ++m_index;
    }
}

double TraceReplayer::progress() const
{
    return m_frames.isEmpty() ? 0.0 : static_cast<double>(m_index) / static_cast<double>(m_frames.size());
}

} // namespace ecusim
