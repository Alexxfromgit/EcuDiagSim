#include "TraceModel.h"

#include <QSet>

#include "dbc/DbcDatabase.h"
#include "diag/UdsDefs.h"
#include "tester/DiagnosticTester.h"

#include <algorithm>
#include <climits>

using namespace ecusim;

TraceModel::TraceModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

QHash<int, QByteArray> TraceModel::roleNames() const
{
    return {{TimeRole, "time"}, {IdRole, "canId"}, {DlcRole, "dlc"}, {DataRole, "bytes"}, {NameRole, "name"},
        {DecodedRole, "decoded"}, {SenderRole, "sender"}, {KindRole, "kind"}, {CycleRole, "cycle"},
        {CountRole, "frames"}};
}

int TraceModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

QString TraceModel::describeIsoTp(const QByteArray &p)
{
    if (p.isEmpty())
        return {};
    const quint8 pci = static_cast<quint8>(p[0]);
    switch (pci >> 4) {
    case 0: {
        const int len = pci & 0x0F;
        const QByteArray data = p.mid(1, len);
        QString text = QStringLiteral("Single frame, %1 byte(s): %2").arg(len).arg(toHex(data));
        if (!data.isEmpty()) {
            const quint8 sid = static_cast<quint8>(data[0]);
            if (sid == 0x7F && data.size() >= 3)
                text += QStringLiteral("  -  negative response: %1").arg(nrcName(static_cast<quint8>(data[2])));
            else if (sid & 0x40)
                text += QStringLiteral("  -  positive response to %1").arg(serviceName(static_cast<quint8>(sid - 0x40)));
            else
                text += QStringLiteral("  -  %1").arg(serviceName(sid));
        }
        return text;
    }
    case 1:
        return p.size() < 2 ? QString()
                            : QStringLiteral("First frame, total %1 bytes: %2")
                                  .arg(((pci & 0x0F) << 8) | static_cast<quint8>(p[1]))
                                  .arg(toHex(p.mid(2)));
    case 2:
        return QStringLiteral("Consecutive frame #%1: %2").arg(pci & 0x0F).arg(toHex(p.mid(1)));
    case 3: {
        static const char *kStatus[] = {"continue to send", "wait", "overflow"};
        const int fs = pci & 0x0F;
        if (p.size() < 3 || fs > 2)
            return QStringLiteral("Flow control");
        return QStringLiteral("Flow control: %1, block size %2, STmin %3 ms")
            .arg(QLatin1String(kStatus[fs]))
            .arg(static_cast<quint8>(p[1]))
            .arg(static_cast<quint8>(p[2]));
    }
    default:
        return {};
    }
}

QVariant TraceModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_rows.size()))
        return {};
    const Row &row = m_rows[static_cast<size_t>(index.row())];
    const QCanBusFrame &f = row.frame.frame;
    const quint32 id = f.frameId();
    const bool diag = DiagId::isDiagnostic(id);

    switch (role) {
    case TimeRole:
        return QString::number(static_cast<double>(row.frame.timeUs) / 1e6, 'f', 4);
    case IdRole:
        return QStringLiteral("%1").arg(id, 3, 16, QLatin1Char('0')).toUpper();
    case DlcRole:
        return f.payload().size();
    case DataRole:
        return toHex(f.payload());
    case NameRole:
        if (diag) {
            if (id == DiagId::Functional)
                return QStringLiteral("Diag request (functional)");
            return id >= 0x7E8 ? QStringLiteral("Diag response %1").arg(DiagnosticTester::ecuName(id))
                               : QStringLiteral("Diag request %1").arg(DiagnosticTester::ecuName(id));
        }
        return m_dbc ? m_dbc->messageName(id) : QString();
    case DecodedRole:
        if (diag)
            return describeIsoTp(f.payload());
        return m_dbc ? m_dbc->describe(f) : QString();
    case SenderRole:
        return row.frame.sender;
    case KindRole:
        return !diag ? 0 : (id >= 0x7E8 ? 2 : 1);
    case CycleRole:
        return row.cycleUs > 0 ? QString::number(static_cast<double>(row.cycleUs) / 1000.0, 'f', 1) : QString();
    case CountRole:
        return static_cast<qulonglong>(row.count);
    default:
        return {};
    }
}

bool TraceModel::accepts(const BusFrame &frame) const
{
    if (m_filter.isEmpty())
        return true;
    const quint32 id = frame.frame.frameId();
    const QString haystack = QStringLiteral("%1 %2 %3 %4")
                                 .arg(id, 3, 16, QLatin1Char('0'))
                                 .arg(m_dbc ? m_dbc->messageName(id) : QString(), frame.sender,
                                     DiagId::isDiagnostic(id) ? QStringLiteral("diag") : QString());
    // Space separated terms are alternatives: "0B0 7E" shows both.
    const auto terms = QStringView(m_filter).split(u' ', Qt::SkipEmptyParts);
    for (QStringView term : terms)
        if (haystack.contains(term, Qt::CaseInsensitive))
            return true;
    return false;
}

void TraceModel::append(const BusFrame &frame)
{
    if (m_paused)
        return;
    m_pending.push_back(frame);
    // Never let a hidden or stalled view accumulate without bound.
    if (m_pending.size() > 20000)
        m_pending.erase(m_pending.begin(), m_pending.begin() + 10000);
}

void TraceModel::flush()
{
    if (m_pending.empty())
        return;
    std::vector<BusFrame> incoming;
    incoming.swap(m_pending);
    const int before = count();

    if (m_fixed) {
        QSet<quint32> changed; // resolved to rows at the end: inserts shift the row numbers
        for (const BusFrame &f : incoming) {
            if (!accepts(f))
                continue;
            const quint32 id = f.frame.frameId();
            auto it = m_rowOfId.constFind(id);
            if (it == m_rowOfId.cend()) {
                // Keep rows sorted by identifier.
                const auto pos = std::lower_bound(m_rows.begin(), m_rows.end(), id,
                    [](const Row &r, quint32 value) { return r.frame.frame.frameId() < value; });
                const int index = static_cast<int>(pos - m_rows.begin());
                beginInsertRows({}, index, index);
                m_rows.insert(pos, Row{f, 0, 1});
                endInsertRows();
                m_rowOfId.clear();
                for (size_t i = 0; i < m_rows.size(); ++i)
                    m_rowOfId.insert(m_rows[i].frame.frame.frameId(), static_cast<int>(i));
            } else {
                Row &row = m_rows[static_cast<size_t>(it.value())];
                row.cycleUs = f.timeUs - row.frame.timeUs;
                row.frame = f;
                ++row.count;
                changed.insert(id);
            }
        }
        int firstChanged = INT_MAX, lastChanged = -1;
        for (quint32 id : std::as_const(changed)) {
            const int row = m_rowOfId.value(id, -1);
            firstChanged = std::min(firstChanged, row);
            lastChanged = std::max(lastChanged, row);
        }
        if (lastChanged >= 0)
            emit dataChanged(index(firstChanged), index(lastChanged));
    } else {
        std::vector<Row> accepted;
        for (const BusFrame &f : incoming)
            if (accepts(f))
                accepted.push_back(Row{f, 0, 1});
        if (static_cast<int>(accepted.size()) > kMaxRows)
            accepted.erase(accepted.begin(), accepted.end() - kMaxRows);
        if (!accepted.empty()) {
            const int overflow = count() + static_cast<int>(accepted.size()) - kMaxRows;
            if (overflow > 0) {
                beginRemoveRows({}, 0, overflow - 1);
                m_rows.erase(m_rows.begin(), m_rows.begin() + overflow);
                endRemoveRows();
            }
            const int first = count();
            beginInsertRows({}, first, first + static_cast<int>(accepted.size()) - 1);
            m_rows.insert(m_rows.end(), accepted.begin(), accepted.end());
            endInsertRows();
        }
    }
    if (count() != before)
        emit countChanged();
}

void TraceModel::clear()
{
    beginResetModel();
    m_rows.clear();
    m_rowOfId.clear();
    m_pending.clear();
    endResetModel();
    emit countChanged();
}

void TraceModel::setPaused(bool paused)
{
    if (m_paused == paused)
        return;
    m_paused = paused;
    emit pausedChanged();
}

void TraceModel::setFixedMode(bool fixed)
{
    if (m_fixed == fixed)
        return;
    m_fixed = fixed;
    clear();
    emit fixedModeChanged();
}

void TraceModel::setFilter(const QString &filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter.trimmed();
    clear();
    emit filterChanged();
}
