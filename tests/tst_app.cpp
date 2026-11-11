// View models behind the QML UI, driven headless: AppController, ScannerViewModel, TraceModel.
#include "AppController.h"

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>

using namespace ecusim;

namespace {
/// A controller whose simulation only moves when the test steps it.
struct Rig
{
    AppController app;
    ScannerViewModel *scanner = app.scanner();

    Rig()
    {
        app.setRunning(false);
        app.setIgnition(1);
        app.step(1500);
    }

    QStringList logLines(const QString &kind) const
    {
        QStringList lines;
        for (const QVariant &v : scanner->log()) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("kind")).toString() == kind)
                lines << m.value(QStringLiteral("text")).toString();
        }
        return lines;
    }
};
} // namespace

class tst_App : public QObject
{
    Q_OBJECT

private slots:
    void pollingDoesNotLeakIntoDtcTable();
    void overlappingRequestsKeepTheirOwnRows();
    void userRequestsAreLoggedAfterResetDuringPoll();
    void pollingStaysOutOfTheLog();
    void sessionStateFollowsTheEcu();
    void traceModelIsConsistent();
};

// Review finding: with "Refresh live data" on, Read codes showed live-data rows as DTC cards.
void tst_App::pollingDoesNotLeakIntoDtcTable()
{
    Rig r;
    QVERIFY(r.app.injectFault(QStringLiteral("ect"), QStringLiteral("open"), {}, {}));
    r.app.step(1500); // P0118 qualifies

    r.scanner->setPollMode(1);
    r.scanner->poll();          // queues two background Mode 01 requests
    r.scanner->obdReadCodes();  // queued behind them
    r.app.step(3000);

    const QVariantList dtcs = r.scanner->dtcs();
    QCOMPARE(dtcs.size(), 1);
    const QVariantMap row = dtcs.first().toMap();
    QCOMPARE(row.value(QStringLiteral("code")).toString(), QStringLiteral("P0118"));
    QCOMPARE(row.value(QStringLiteral("ecu")).toString(), QStringLiteral("ECM"));
    QVERIFY(row.value(QStringLiteral("confirmed")).toBool());
    // The poll still produced its own table.
    QVERIFY(r.scanner->valuesTitle().contains(QStringLiteral("Mode 01")));
    for (const QVariant &v : r.scanner->values())
        QVERIFY(v.toMap().contains(QStringLiteral("name")));
}

void tst_App::overlappingRequestsKeepTheirOwnRows()
{
    Rig r;
    r.app.startEngine();
    r.app.step(4000);
    // Two clicks less than a second apart: each table must be complete and unmixed.
    r.scanner->obdVehicleInfo();
    r.scanner->obdLiveData();
    r.app.step(3000);
    QVERIFY(r.scanner->valuesTitle().contains(QStringLiteral("Mode 01")));
    for (const QVariant &v : r.scanner->values()) {
        const QString name = v.toMap().value(QStringLiteral("name")).toString();
        QVERIFY2(name != QStringLiteral("VIN") && name != QStringLiteral("ECU name"), qPrintable(name));
    }
    // ECM answers 8 values, TCM 4 (monitor status, speed, voltage are shared PIDs).
    QVERIFY(r.scanner->values().size() >= 8);
}

// Review finding: a poll in flight during Reset swallowed the next user requests from the log.
void tst_App::userRequestsAreLoggedAfterResetDuringPoll()
{
    Rig r;
    r.scanner->setPollMode(2);
    r.scanner->poll();
    r.app.step(1); // request on its way
    r.app.reset();
    r.app.setRunning(false);
    r.scanner->setPollMode(0);
    r.app.setIgnition(1);
    r.app.step(1500);

    r.scanner->udsReadDtcs(0x0D);
    r.scanner->udsIdentification();
    r.app.step(1000);
    const QStringList tx = r.logLines(QStringLiteral("tx"));
    QCOMPARE(tx.size(), 2);
    QVERIFY(tx.at(0).contains(QStringLiteral("19 02 0D")));
    QVERIFY(tx.at(1).contains(QStringLiteral("22 F1 90")));
    QCOMPARE(r.logLines(QStringLiteral("rx")).size(), 2);
}

void tst_App::pollingStaysOutOfTheLog()
{
    Rig r;
    r.scanner->setPollMode(2);
    for (int i = 0; i < 5; ++i) {
        r.scanner->poll();
        r.app.step(500);
    }
    QVERIFY(r.scanner->valuesTitle().contains(QStringLiteral("live data")));
    QVERIFY(r.scanner->log().isEmpty());
}

// Review finding: the session / unlock chips kept showing a state the ECU had already left.
void tst_App::sessionStateFollowsTheEcu()
{
    Rig r;
    r.scanner->setTarget(2); // ABS
    r.scanner->udsSession(3);
    r.scanner->udsUnlock();
    r.app.step(500);
    QCOMPARE(r.scanner->sessionText(), QStringLiteral("Extended session"));
    QVERIFY(r.scanner->unlocked());

    // S3 timeout: no requests for more than 5 s.
    r.app.step(DiagnosticServer::kS3TimeoutMs + 500);
    r.scanner->updateSessionState();
    QCOMPARE(r.scanner->sessionText(), QStringLiteral("Default session"));
    QVERIFY(!r.scanner->unlocked());
    // ... and the ECU agrees.
    r.scanner->udsIoControl(0x3010, 1);
    r.app.step(300);
    QVERIFY(r.logLines(QStringLiteral("neg")).last().contains(QStringLiteral("7F 2F 7F")));

    // With tester present the session survives.
    r.scanner->setKeepAlive(true);
    r.scanner->udsSession(3);
    r.app.step(DiagnosticServer::kS3TimeoutMs + 3000);
    r.scanner->updateSessionState();
    QCOMPARE(r.scanner->sessionText(), QStringLiteral("Extended session"));
    QCOMPARE(r.app.sim().abs().diag().session(), DiagnosticServer::Session::Extended);

    // A power cycle ends it.
    QVERIFY(r.app.injectFault(QStringLiteral("abs"), QStringLiteral("power_loss"), {}, {}));
    r.app.step(100);
    r.scanner->updateSessionState();
    QCOMPARE(r.scanner->sessionText(), QStringLiteral("Default session"));
    r.scanner->setKeepAlive(false);
}

void tst_App::traceModelIsConsistent()
{
    Rig r;
    TraceModel *trace = r.app.trace();
    QAbstractItemModelTester tester(trace, QAbstractItemModelTester::FailureReportingMode::QtTest);

    // Chronological mode is capped.
    r.app.step(10000); // 320 frames per second
    QCOMPARE(trace->rowCount(), TraceModel::kMaxRows);

    // By ID: one sorted row per identifier, updated in place.
    trace->setFixedMode(true);
    r.app.step(50);
    qRegisterMetaType<QList<int>>();
    QSignalSpy changed(trace, &QAbstractItemModel::dataChanged);
    r.app.step(500);
    QStringList ids;
    for (int i = 0; i < trace->rowCount(); ++i)
        ids << trace->data(trace->index(i), TraceModel::IdRole).toString();
    QCOMPARE(ids, QStringList({"0B0", "0B1", "100", "1A0", "300", "350"}));
    QVERIFY(!changed.isEmpty());
    const QString cycle = trace->data(trace->index(0), TraceModel::CycleRole).toString();
    QCOMPARE(cycle, QStringLiteral("10.0"));

    // A new identifier appearing in the same batch as updates (diagnostic traffic).
    r.scanner->udsIdentification();
    r.app.step(500);
    ids.clear();
    for (int i = 0; i < trace->rowCount(); ++i)
        ids << trace->data(trace->index(i), TraceModel::IdRole).toString();
    QVERIFY(ids.contains(QStringLiteral("7E0")) && ids.contains(QStringLiteral("7E8")));
    QVERIFY(std::is_sorted(ids.begin(), ids.end()));

    trace->setFilter(QStringLiteral("ABS"));
    r.app.step(200);
    QCOMPARE(trace->rowCount(), 2);
}

QTEST_GUILESS_MAIN(tst_App)
#include "tst_app.moc"
