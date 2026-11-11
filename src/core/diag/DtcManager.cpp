#include "diag/DtcManager.h"

#include <QStringList>

namespace ecusim {

namespace {
void setBits(quint8 &status, quint8 bits) { status = static_cast<quint8>(status | bits); }
void clearBits(quint8 &status, quint8 bits) { status = static_cast<quint8>(status & ~bits); }
} // namespace

quint32 makeDtc(const char *sae, quint8 failureType)
{
    const quint32 code = dtcFromString(QString::fromLatin1(sae));
    return code == 0xFFFFFFFF ? code : ((code & 0xFFFF00) | failureType);
}

QString dtcToString(quint32 code, bool withFailureType)
{
    static const char kSystems[] = {'P', 'C', 'B', 'U'};
    const quint32 hi = (code >> 8) & 0xFFFF;
    QString s;
    s += QLatin1Char(kSystems[(hi >> 14) & 0x3]);
    s += QString::number((hi >> 12) & 0x3);
    s += QStringLiteral("%1").arg(hi & 0xFFF, 3, 16, QLatin1Char('0')).toUpper();
    if (withFailureType)
        s += QStringLiteral("-%1").arg(code & 0xFF, 2, 16, QLatin1Char('0')).toUpper();
    return s;
}

quint32 dtcFromString(const QString &text)
{
    QString t = text.trimmed().toUpper();
    t.remove(QLatin1Char('-'));
    if (t.size() != 5 && t.size() != 7)
        return 0xFFFFFFFF;
    const int system = static_cast<int>(QStringLiteral("PCBU").indexOf(t.at(0)));
    bool ok1 = false, ok2 = false, ok3 = true;
    const uint first = t.mid(1, 1).toUInt(&ok1, 16);
    const uint rest = t.mid(2, 3).toUInt(&ok2, 16);
    uint ftb = 0;
    if (t.size() == 7)
        ftb = t.mid(5, 2).toUInt(&ok3, 16);
    if (system < 0 || !ok1 || !ok2 || !ok3 || first > 3)
        return 0xFFFFFFFF;
    const quint32 hi = (static_cast<quint32>(system) << 14) | (first << 12) | rest;
    return (hi << 8) | ftb;
}

QString failureTypeText(quint8 ftb)
{
    switch (ftb) {
    case 0x00: return {};
    case 0x11: return QStringLiteral("circuit short to ground");
    case 0x12: return QStringLiteral("circuit short to battery");
    case 0x13: return QStringLiteral("circuit open");
    case 0x16: return QStringLiteral("circuit voltage below threshold");
    case 0x17: return QStringLiteral("circuit voltage above threshold");
    case 0x1C: return QStringLiteral("circuit voltage out of range");
    case 0x29: return QStringLiteral("signal invalid");
    case 0x2F: return QStringLiteral("signal erratic");
    case 0x62: return QStringLiteral("signal compare failure");
    case 0x64: return QStringLiteral("signal plausibility failure");
    case 0x82: return QStringLiteral("alive / sequence counter incorrect");
    case 0x83: return QStringLiteral("value of signal protection calculation incorrect");
    case 0x87: return QStringLiteral("missing message");
    case 0x98: return QStringLiteral("component or system over temperature");
    default: return QStringLiteral("failure type 0x%1").arg(ftb, 2, 16, QLatin1Char('0'));
    }
}

QStringList dtcStatusNames(quint8 status)
{
    static const char *kNames[8] = {"testFailed", "testFailedThisOperationCycle", "pendingDTC", "confirmedDTC",
        "testNotCompletedSinceLastClear", "testFailedSinceLastClear", "testNotCompletedThisOperationCycle",
        "warningIndicatorRequested"};
    QStringList names;
    for (int bit = 0; bit < 8; ++bit)
        if (status & (1u << bit))
            names << QString::fromLatin1(kNames[bit]);
    return names;
}

void DtcManager::define(const DtcDefinition &definition)
{
    if (m_index.contains(definition.code))
        return;
    DtcEntry e;
    e.def = definition;
    m_index.insert(definition.code, static_cast<int>(m_entries.size()));
    m_entries.append(e);
}

DtcEntry *DtcManager::lookup(quint32 code)
{
    auto it = m_index.constFind(code);
    return it == m_index.cend() ? nullptr : &m_entries[it.value()];
}

const DtcEntry *DtcManager::find(quint32 code) const
{
    auto it = m_index.constFind(code);
    return it == m_index.cend() ? nullptr : &m_entries.at(it.value());
}

quint8 DtcManager::status(quint32 code) const
{
    const DtcEntry *e = find(code);
    return e ? e->status : 0;
}

void DtcManager::report(quint32 code, bool failed)
{
    DtcEntry *e = lookup(code);
    if (!e || !m_cycleActive)
        return;

    if (failed) {
        if (e->debounce < 0)
            e->debounce = 0; // "jump up": a failure discards accumulated passes
        if (++e->debounce >= e->def.failThreshold) {
            e->debounce = e->def.failThreshold;
            setFailed(*e);
        }
    } else {
        if (e->debounce > 0)
            e->debounce = 0;
        if (--e->debounce <= -e->def.passThreshold) {
            e->debounce = -e->def.passThreshold;
            clearBits(e->status, DtcStatus::TestFailed | DtcStatus::TestNotCompletedSinceLastClear | DtcStatus::TestNotCompletedThisOperationCycle);
        }
    }
}

void DtcManager::setFailed(DtcEntry &e)
{
    if (!(e.status & DtcStatus::TestFailed))
        ++e.occurrenceCounter;
    setBits(e.status, DtcStatus::TestFailed | DtcStatus::TestFailedThisOperationCycle | DtcStatus::TestFailedSinceLastClear | DtcStatus::Pending);
    clearBits(e.status, DtcStatus::TestNotCompletedSinceLastClear | DtcStatus::TestNotCompletedThisOperationCycle);
    e.agingCounter = 0;
    e.healingCounter = 0;

    if (!e.countedThisCycle) {
        e.countedThisCycle = true;
        ++e.tripCounter;
    }
    if (e.firstFailedMs < 0) {
        e.firstFailedMs = m_nowMs;
        if (m_snapshotProvider)
            e.snapshot = m_snapshotProvider();
    }
    if (e.tripCounter >= e.def.tripsToConfirm) {
        setBits(e.status, DtcStatus::Confirmed);
        if (e.def.warningIndicator)
            setBits(e.status, DtcStatus::WarningIndicatorRequested);
    }
}

void DtcManager::startOperationCycle()
{
    m_cycleActive = true;
    for (DtcEntry &e : m_entries) {
        clearBits(e.status, DtcStatus::TestFailed | DtcStatus::TestFailedThisOperationCycle);
        setBits(e.status, DtcStatus::TestNotCompletedThisOperationCycle);
        e.debounce = 0;
        e.countedThisCycle = false;
    }
}

void DtcManager::endOperationCycle()
{
    if (!m_cycleActive)
        return;
    m_cycleActive = false;
    for (DtcEntry &e : m_entries) {
        const bool completed = !(e.status & DtcStatus::TestNotCompletedThisOperationCycle);
        const bool failed = e.status & DtcStatus::TestFailedThisOperationCycle;
        if (!completed || failed)
            continue;
        // A complete cycle without a failure.
        clearBits(e.status, DtcStatus::Pending);
        e.tripCounter = 0;
        if (e.status & DtcStatus::WarningIndicatorRequested) {
            if (++e.healingCounter >= e.def.healingCycles)
                clearBits(e.status, DtcStatus::WarningIndicatorRequested);
        }
        if (e.status & DtcStatus::Confirmed) {
            if (++e.agingCounter >= kAgingThreshold) {
                clearBits(e.status, DtcStatus::Confirmed);
                e.snapshot.clear();
                e.firstFailedMs = -1;
                e.occurrenceCounter = 0;
            }
        }
    }
}

void DtcManager::resetDebounce()
{
    for (DtcEntry &e : m_entries) {
        e.debounce = 0;
        clearBits(e.status, DtcStatus::TestFailed);
    }
}

bool DtcManager::clear(quint32 group)
{
    bool any = false;
    for (DtcEntry &e : m_entries) {
        if (group != 0xFFFFFF && e.def.code != group)
            continue;
        any = true;
        const DtcDefinition def = e.def;
        e = DtcEntry{};
        e.def = def;
    }
    return any || group == 0xFFFFFF;
}

QList<const DtcEntry *> DtcManager::matching(quint8 statusMask) const
{
    QList<const DtcEntry *> result;
    for (const DtcEntry &e : m_entries)
        if (e.status & statusMask)
            result.append(&e);
    return result;
}

bool DtcManager::warningRequested() const
{
    for (const DtcEntry &e : m_entries)
        if (e.status & DtcStatus::WarningIndicatorRequested)
            return true;
    return false;
}

bool DtcManager::milRequested() const
{
    for (const DtcEntry &e : m_entries)
        if (e.def.emissionRelevant && (e.status & DtcStatus::WarningIndicatorRequested))
            return true;
    return false;
}

int DtcManager::storedCount() const
{
    int n = 0;
    for (const DtcEntry &e : m_entries)
        if (e.status & (DtcStatus::Pending | DtcStatus::Confirmed))
            ++n;
    return n;
}

const DtcEntry *DtcManager::freezeFrameDtc() const
{
    const DtcEntry *best = nullptr;
    for (const DtcEntry &e : m_entries) {
        if (!e.def.emissionRelevant || e.snapshot.isEmpty()
            || !(e.status & (DtcStatus::Pending | DtcStatus::Confirmed)))
            continue;
        if (!best || e.firstFailedMs < best->firstFailedMs)
            best = &e;
    }
    return best;
}

} // namespace ecusim
