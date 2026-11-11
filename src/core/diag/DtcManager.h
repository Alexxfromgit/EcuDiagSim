#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace ecusim {

/// ISO 14229-1 DTC status byte.
namespace DtcStatus {
enum : quint8 {
    TestFailed = 0x01,
    TestFailedThisOperationCycle = 0x02,
    Pending = 0x04,
    Confirmed = 0x08,
    TestNotCompletedSinceLastClear = 0x10,
    TestFailedSinceLastClear = 0x20,
    TestNotCompletedThisOperationCycle = 0x40,
    WarningIndicatorRequested = 0x80,
};
} // namespace DtcStatus

/// Builds the 3-byte UDS DTC number from the SAE J2012 text ("C0035") and a failure type byte.
quint32 makeDtc(const char *sae, quint8 failureType = 0x00);
/// "C0035" or, with the failure type byte, "C0035-13".
QString dtcToString(quint32 code, bool withFailureType = true);
/// Parses "C0035", "C0035-13" or "C003513". Returns 0xFFFFFFFF on error.
quint32 dtcFromString(const QString &text);
/// Short text for the common ISO 15031-6 failure type bytes.
QString failureTypeText(quint8 ftb);
/// Names of the bits set in a status byte.
QStringList dtcStatusNames(quint8 status);

struct DtcDefinition
{
    quint32 code = 0;
    QString text;
    bool emissionRelevant = false; ///< visible to OBD-II, requests the MIL
    bool warningIndicator = true;  ///< requests a warning lamp once confirmed
    int failThreshold = 10;        ///< failed test results until "testFailed"
    int passThreshold = 10;        ///< passed test results until "testPassed"
    int tripsToConfirm = 1;        ///< operation cycles with a failure until "confirmed"
    int healingCycles = 1;         ///< fault-free cycles until the lamp request is withdrawn
};

struct SnapshotRecord
{
    quint16 did = 0;
    QByteArray data;
};

struct DtcEntry
{
    DtcDefinition def;
    quint8 status = DtcStatus::TestNotCompletedSinceLastClear | DtcStatus::TestNotCompletedThisOperationCycle;
    int debounce = 0;
    int tripCounter = 0;
    int occurrenceCounter = 0;
    int agingCounter = 0;
    int healingCounter = 0;
    bool countedThisCycle = false;
    qint64 firstFailedMs = -1;
    QList<SnapshotRecord> snapshot;
};

/// Fault memory of one ECU, modelled on AUTOSAR Dem / ISO 14229-1 Annex D.
///
/// Monitors call report() with every test result. A counter based debounce turns the raw
/// results into testFailed/testPassed, and the operation cycle logic derives pending,
/// confirmed, the warning indicator request and aging from that.
class DtcManager
{
public:
    using SnapshotProvider = std::function<QList<SnapshotRecord>()>;
    static constexpr int kAgingThreshold = 40;

    void define(const DtcDefinition &definition);
    void setSnapshotProvider(SnapshotProvider provider) { m_snapshotProvider = std::move(provider); }
    void setTime(qint64 nowMs) { m_nowMs = nowMs; }

    /// Reports one monitor result. Unknown codes are ignored.
    void report(quint32 code, bool failed);

    void startOperationCycle();
    void endOperationCycle();
    bool operationCycleActive() const { return m_cycleActive; }
    /// Forgets debounce state, e.g. after an ECU reset.
    void resetDebounce();

    /// Clears fault memory. group 0xFFFFFF = all DTCs. Returns false if the group is unknown.
    bool clear(quint32 group = 0xFFFFFF);

    const QList<DtcEntry> &entries() const { return m_entries; }
    const DtcEntry *find(quint32 code) const;
    QList<const DtcEntry *> matching(quint8 statusMask) const;
    quint8 status(quint32 code) const;
    bool testFailed(quint32 code) const { return status(code) & DtcStatus::TestFailed; }

    bool warningRequested() const;
    bool milRequested() const;
    int storedCount() const; ///< pending or confirmed DTCs
    /// The DTC whose snapshot serves as the OBD freeze frame (first emission relevant one).
    const DtcEntry *freezeFrameDtc() const;

private:
    DtcEntry *lookup(quint32 code);
    void setFailed(DtcEntry &e);

    QList<DtcEntry> m_entries;
    QHash<quint32, int> m_index;
    SnapshotProvider m_snapshotProvider;
    qint64 m_nowMs = 0;
    bool m_cycleActive = false;
};

} // namespace ecusim
