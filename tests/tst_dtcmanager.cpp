#include "diag/DtcCatalog.h"
#include "diag/DtcManager.h"

#include <QTest>

using namespace ecusim;
using namespace ecusim::DtcStatus;

namespace {
constexpr quint32 kCode = saeDtc('C', 0x0035, 0x13);

DtcDefinition def(quint32 code = kCode, int trips = 1, int threshold = 10)
{
    DtcDefinition d;
    d.code = code;
    d.text = QStringLiteral("test");
    d.failThreshold = threshold;
    d.passThreshold = threshold;
    d.tripsToConfirm = trips;
    d.healingCycles = 2;
    return d;
}

void report(DtcManager &m, quint32 code, bool failed, int times)
{
    for (int i = 0; i < times; ++i)
        m.report(code, failed);
}
} // namespace

class tst_DtcManager : public QObject
{
    Q_OBJECT

private slots:
    void codeConversion();
    void statusNames();
    void catalogIsComplete();
    void initialStatus();
    void debounce();
    void debounceJumps();
    void failedAndPassedAgain();
    void noReportsOutsideOperationCycle();
    void twoTripConfirmation();
    void pendingHealsAfterCleanCycle();
    void warningIndicatorHealing();
    void aging();
    void snapshotCapturedOnce();
    void clear();
    void milAndFreezeFrame();
    void incompleteCycleDoesNotHeal();
};

void tst_DtcManager::codeConversion()
{
    QCOMPARE(makeDtc("P0117"), quint32(0x011700));
    QCOMPARE(makeDtc("C0035", 0x13), quint32(0x403513));
    QCOMPARE(makeDtc("B1318", 0x16), quint32(0x931816));
    QCOMPARE(makeDtc("U0121", 0x87), quint32(0xC12187));
    QCOMPARE(saeDtc('U', 0x0121, 0x87), makeDtc("U0121", 0x87));
    QCOMPARE(saeDtc('P', 0x0562), makeDtc("P0562"));
    QCOMPARE(dtc::wheel(3, dtc::FtbSignalInvalid), makeDtc("C0050", 0x29));

    QCOMPARE(dtcToString(0x403513), QStringLiteral("C0035-13"));
    QCOMPARE(dtcToString(0x403513, false), QStringLiteral("C0035"));
    QCOMPARE(dtcToString(0xC12187), QStringLiteral("U0121-87"));
    QCOMPARE(dtcToString(0x3ABC00, false), QStringLiteral("P3ABC"));

    QCOMPARE(dtcFromString(QStringLiteral("c0035-13")), quint32(0x403513));
    QCOMPARE(dtcFromString(QStringLiteral(" P0118 ")), quint32(0x011800));
    QCOMPARE(dtcFromString(QStringLiteral("U012187")), quint32(0xC12187));
    QCOMPARE(dtcFromString(QStringLiteral("X0035")), quint32(0xFFFFFFFF));
    QCOMPARE(dtcFromString(QStringLiteral("P4000")), quint32(0xFFFFFFFF));
    QCOMPARE(dtcFromString(QStringLiteral("P01")), quint32(0xFFFFFFFF));
}

void tst_DtcManager::statusNames()
{
    QCOMPARE(dtcStatusNames(0x09), QStringList({QStringLiteral("testFailed"), QStringLiteral("confirmedDTC")}));
    QCOMPARE(dtcStatusNames(0xFF).size(), 8);
    QVERIFY(dtcStatusNames(0).isEmpty());
    QCOMPARE(failureTypeText(0x13), QStringLiteral("circuit open"));
}

void tst_DtcManager::catalogIsComplete()
{
    QSet<quint32> seen;
    for (const DtcCatalogEntry &e : dtcCatalog()) {
        QVERIFY2(!seen.contains(e.code), qPrintable(dtcToString(e.code)));
        seen.insert(e.code);
        QVERIFY(qstrlen(e.text) > 0 && qstrlen(e.detection) > 0 && qstrlen(e.ecus) > 0);
    }
    QCOMPARE(dtcDescription(dtc::P0118), QStringLiteral("Engine coolant temperature sensor circuit high"));
    // Unknown failure type byte falls back to the base code.
    QCOMPARE(dtcDescription(saeDtc('C', 0x0035, 0x77)), QStringLiteral("Left front wheel speed sensor"));
    QVERIFY(dtcDescription(saeDtc('P', 0x3FFF)).isEmpty());
}

void tst_DtcManager::initialStatus()
{
    DtcManager m;
    m.define(def());
    m.define(def()); // duplicate definitions are ignored
    QCOMPARE(m.entries().size(), 1);
    QCOMPARE(m.status(kCode), quint8(0x50));
    QCOMPARE(m.status(0x123456), quint8(0));
    QVERIFY(m.matching(Confirmed | Pending).isEmpty());
    QCOMPARE(m.storedCount(), 0);
}

void tst_DtcManager::debounce()
{
    DtcManager m;
    m.define(def());
    m.startOperationCycle();
    report(m, kCode, true, 9);
    QCOMPARE(m.status(kCode), quint8(0x50)); // not yet qualified
    m.report(kCode, true);
    // failed + failedThisCycle + pending + confirmed + failedSinceClear + warningIndicator
    QCOMPARE(m.status(kCode), quint8(0xAF));
    QVERIFY(m.testFailed(kCode));
    QVERIFY(m.warningRequested());
    QCOMPARE(m.find(kCode)->occurrenceCounter, 1);
    report(m, kCode, true, 100);
    QCOMPARE(m.find(kCode)->occurrenceCounter, 1);
}

void tst_DtcManager::debounceJumps()
{
    DtcManager m;
    m.define(def());
    m.startOperationCycle();
    // A single pass discards the accumulated failures, and vice versa.
    report(m, kCode, true, 9);
    m.report(kCode, false);
    report(m, kCode, true, 9);
    QVERIFY(!m.testFailed(kCode));
    m.report(kCode, true);
    QVERIFY(m.testFailed(kCode));

    report(m, kCode, false, 9);
    m.report(kCode, true);
    report(m, kCode, false, 9);
    QVERIFY(m.testFailed(kCode));
    m.report(kCode, false);
    QVERIFY(!m.testFailed(kCode));
}

void tst_DtcManager::failedAndPassedAgain()
{
    DtcManager m;
    m.define(def());
    m.startOperationCycle();
    report(m, kCode, false, 10);
    QCOMPARE(m.status(kCode), quint8(0x00)); // test completed and passed
    report(m, kCode, true, 10);
    report(m, kCode, false, 10);
    // History remains: failedThisCycle, pending, confirmed, failedSinceClear, warningIndicator
    QCOMPARE(m.status(kCode), quint8(0xAE));
    report(m, kCode, true, 10);
    QCOMPARE(m.find(kCode)->occurrenceCounter, 2);
}

void tst_DtcManager::noReportsOutsideOperationCycle()
{
    DtcManager m;
    m.define(def());
    report(m, kCode, true, 50);
    QCOMPARE(m.status(kCode), quint8(0x50));
    m.report(0xABCDEF, true); // unknown code: ignored
    m.startOperationCycle();
    m.endOperationCycle();
    report(m, kCode, true, 50);
    QCOMPARE(m.status(kCode), quint8(0x50));
}

void tst_DtcManager::twoTripConfirmation()
{
    DtcManager m;
    m.define(def(kCode, 2));
    m.startOperationCycle();
    report(m, kCode, true, 10);
    // First trip: pending only, no lamp.
    QCOMPARE(m.status(kCode), quint8(0x27));
    QVERIFY(!m.warningRequested());
    QCOMPARE(m.matching(Pending).size(), 1);
    QVERIFY(m.matching(Confirmed).isEmpty());
    report(m, kCode, true, 100); // more failures in the same trip do not count twice
    QVERIFY(!(m.status(kCode) & Confirmed));
    m.endOperationCycle();

    m.startOperationCycle();
    QCOMPARE(m.status(kCode), quint8(0x64)); // pending survives, test not completed yet this cycle
    report(m, kCode, true, 10);
    QCOMPARE(m.status(kCode), quint8(0xAF));
}

void tst_DtcManager::pendingHealsAfterCleanCycle()
{
    DtcManager m;
    m.define(def(kCode, 2));
    m.startOperationCycle();
    report(m, kCode, true, 10);
    m.endOperationCycle();

    m.startOperationCycle();
    report(m, kCode, false, 10);
    m.endOperationCycle();
    QVERIFY(!(m.status(kCode) & Pending));
    QCOMPARE(m.find(kCode)->tripCounter, 0);

    // The trip counter starts over: one more failing trip is pending again, not confirmed.
    m.startOperationCycle();
    report(m, kCode, true, 10);
    QVERIFY(m.status(kCode) & Pending);
    QVERIFY(!(m.status(kCode) & Confirmed));
}

void tst_DtcManager::warningIndicatorHealing()
{
    DtcManager m;
    m.define(def()); // healingCycles = 2
    m.startOperationCycle();
    report(m, kCode, true, 10);
    m.endOperationCycle();
    QVERIFY(m.warningRequested());

    for (int cycle = 1; cycle <= 2; ++cycle) {
        m.startOperationCycle();
        report(m, kCode, false, 10);
        m.endOperationCycle();
        QCOMPARE(m.warningRequested(), cycle < 2);
    }
    // The lamp is off, the DTC is still confirmed (history).
    QCOMPARE(m.status(kCode) & (Confirmed | WarningIndicatorRequested), int(Confirmed));
}

void tst_DtcManager::aging()
{
    DtcManager m;
    m.define(def());
    m.startOperationCycle();
    report(m, kCode, true, 10);
    m.endOperationCycle();
    for (int cycle = 1; cycle <= DtcManager::kAgingThreshold; ++cycle) {
        QVERIFY(m.status(kCode) & Confirmed);
        m.startOperationCycle();
        report(m, kCode, false, 10);
        m.endOperationCycle();
    }
    QVERIFY(!(m.status(kCode) & Confirmed));
    QVERIFY(m.find(kCode)->snapshot.isEmpty());
    QCOMPARE(m.storedCount(), 0);
}

void tst_DtcManager::snapshotCapturedOnce()
{
    DtcManager m;
    m.define(def());
    int value = 1;
    m.setSnapshotProvider([&] {
        return QList<SnapshotRecord>{{0xF40D, QByteArray(1, static_cast<char>(value))}};
    });
    m.startOperationCycle();
    m.setTime(1234);
    report(m, kCode, true, 9);
    value = 42; // the snapshot holds the values at the moment the fault qualifies
    m.report(kCode, true);
    QCOMPARE(m.find(kCode)->snapshot.size(), 1);
    QCOMPARE(m.find(kCode)->snapshot.first().data, QByteArray(1, 42));
    QCOMPARE(m.find(kCode)->firstFailedMs, qint64(1234));

    value = 99;
    report(m, kCode, false, 10);
    report(m, kCode, true, 10);
    QCOMPARE(m.find(kCode)->snapshot.first().data, QByteArray(1, 42)); // first occurrence is kept
}

void tst_DtcManager::clear()
{
    const quint32 other = saeDtc('C', 0x0040, 0x13);
    DtcManager m;
    m.define(def());
    m.define(def(other));
    m.setSnapshotProvider([] { return QList<SnapshotRecord>{{0x1234, QByteArray("x")}}; });
    m.startOperationCycle();
    report(m, kCode, true, 10);
    report(m, other, true, 10);
    QCOMPARE(m.storedCount(), 2);

    QVERIFY(!m.clear(0x112233)); // unknown group
    QCOMPARE(m.storedCount(), 2);
    QVERIFY(m.clear(other));
    QCOMPARE(m.status(other), quint8(0x50));
    QCOMPARE(m.status(kCode), quint8(0xAF));

    QVERIFY(m.clear());
    QCOMPARE(m.status(kCode), quint8(0x50));
    QVERIFY(m.find(kCode)->snapshot.isEmpty());
    QCOMPARE(m.find(kCode)->occurrenceCounter, 0);
    // The cycle is still running: a present fault is detected again.
    report(m, kCode, true, 10);
    QCOMPARE(m.status(kCode), quint8(0xAF));
}

void tst_DtcManager::milAndFreezeFrame()
{
    const quint32 chassis = kCode;
    const quint32 emission = saeDtc('P', 0x0118);
    DtcManager m;
    m.define(def(chassis));
    DtcDefinition e = def(emission);
    e.emissionRelevant = true;
    m.define(e);
    m.setSnapshotProvider([] { return QList<SnapshotRecord>{{0xF40C, QByteArray(2, 1)}}; });
    m.startOperationCycle();

    report(m, chassis, true, 10);
    QVERIFY(m.warningRequested());
    QVERIFY(!m.milRequested()); // only emission relevant DTCs request the MIL
    QVERIFY(!m.freezeFrameDtc());

    report(m, emission, true, 10);
    QVERIFY(m.milRequested());
    QVERIFY(m.freezeFrameDtc());
    QCOMPARE(m.freezeFrameDtc()->def.code, emission);
}

void tst_DtcManager::incompleteCycleDoesNotHeal()
{
    DtcManager m;
    m.define(def(kCode, 2));
    m.startOperationCycle();
    report(m, kCode, true, 10);
    m.endOperationCycle();
    // A cycle in which the monitor never ran says nothing about the fault.
    m.startOperationCycle();
    report(m, kCode, false, 3);
    m.endOperationCycle();
    QVERIFY(m.status(kCode) & Pending);
    QCOMPARE(m.find(kCode)->tripCounter, 1);
}

QTEST_GUILESS_MAIN(tst_DtcManager)
#include "tst_dtcmanager.moc"
