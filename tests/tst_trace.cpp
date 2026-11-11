#include "TestUtil.h"

#include <QTemporaryDir>

using namespace ecusim;
using namespace testutil;

class tst_Trace : public QObject
{
    Q_OBJECT

private slots:
    void lineFormats();
    void parseForeignLogs();
    void ringBuffer();
    void saveAndLoad_data();
    void saveAndLoad();
    void replayDrivesTheCluster();
};

namespace {
BusFrame frame(qint64 timeUs, quint32 id, const char *dataHex)
{
    BusFrame f;
    f.timeUs = timeUs;
    f.frame = QCanBusFrame(id, QByteArray::fromHex(dataHex));
    f.sender = QStringLiteral("X");
    return f;
}
} // namespace

void tst_Trace::lineFormats()
{
    const BusFrame f = frame(12'345'678, 0x0B0, "1122334455667788");
    QCOMPARE(TraceRecorder::candumpLine(f), QStringLiteral("(0000000012.345678) vcan0 0B0#1122334455667788"));
    QCOMPARE(TraceRecorder::candumpLine(frame(1000, 0x7DF, "0201"), QStringLiteral("can1")),
        QStringLiteral("(0000000000.001000) can1 7DF#0201"));
    QCOMPARE(TraceRecorder::ascLine(f),
        QStringLiteral("  12.345678 1  B0              Rx   d 8 11 22 33 44 55 66 77 88"));

    const QString header = TraceRecorder::ascHeader(QDateTime(QDate(2026, 11, 10), QTime(9, 5, 3, 250)));
    QVERIFY2(header.startsWith(QStringLiteral("date Tue Nov 10 09:05:03.250 am 2026\nbase hex  timestamps absolute\n")),
        qPrintable(header));
}

void tst_Trace::parseForeignLogs()
{
    // Lines as written by can-utils and by CANoe/CANalyzer, plus noise that must be skipped.
    const QString text = QStringLiteral(
        "(1700000000.100000) can0 123#DEADBEEF\n"
        "(1700000000.2) can0 18DAF110#0102\n"
        "garbage line\n"
        "date Mon Jan 1 10:00:00.000 am 2024\n"
        "base hex  timestamps absolute\n"
        "   1.500000 1  7E8             Rx   d 8 06 41 0C 1A F8 AA AA AA  Length = 0 BitCount = 0\n"
        "   1.600000 2  18DAF110x       Tx   d 2 01 02\n"
        "   1.700000 1  Statistic: D 0 R 0 XD 0 XR 0 E 0 O 0 B 0.00%\n"
        "End TriggerBlock\n");
    const QList<BusFrame> frames = TraceRecorder::parse(text);
    QCOMPARE(frames.size(), 4);
    QCOMPARE(frames.at(0).frame.frameId(), quint32(0x123));
    QCOMPARE(frames.at(0).frame.payload(), QByteArray::fromHex("DEADBEEF"));
    QCOMPARE(frames.at(0).timeUs, qint64(1700000000100000));
    QCOMPARE(frames.at(1).timeUs, qint64(1700000000200000));
    QVERIFY(frames.at(1).frame.hasExtendedFrameFormat());
    QCOMPARE(frames.at(2).frame.frameId(), quint32(0x7E8));
    QCOMPARE(frames.at(2).frame.payload(), QByteArray::fromHex("06410C1AF8AAAAAA"));
    QCOMPARE(frames.at(2).timeUs, qint64(1'500'000));
    QVERIFY(frames.at(3).frame.hasExtendedFrameFormat());
    QCOMPARE(frames.at(3).frame.frameId(), quint32(0x18DAF110));

    QString error;
    QVERIFY(TraceRecorder::load(QStringLiteral("/does/not/exist.log"), &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void tst_Trace::ringBuffer()
{
    TraceRecorder rec(100);
    for (int i = 0; i < 250; ++i)
        rec.record(frame(i * 1000, 0x100, "00"));
    QCOMPARE(rec.frames().size(), size_t(100));
    QCOMPARE(rec.totalCount(), quint64(250));
    QCOMPARE(rec.frames().front().timeUs, qint64(150'000));
    rec.clear();
    QVERIFY(rec.frames().empty());
}

void tst_Trace::saveAndLoad_data()
{
    QTest::addColumn<bool>("asc");
    QTest::newRow("candump") << false;
    QTest::newRow("asc") << true;
}

void tst_Trace::saveAndLoad()
{
    QFETCH(bool, asc);
    Simulation sim;
    startEngine(sim);
    exchange(sim, DiagId::Functional, hex("09 02")); // some diagnostic traffic incl. multi-frame
    const auto &recorded = sim.trace().frames();
    QVERIFY(recorded.size() > 1000);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(asc ? QStringLiteral("trace.asc") : QStringLiteral("trace.log"));
    QString error;
    if (asc)
        QVERIFY2(sim.trace().saveAsc(path, QDateTime(QDate(2026, 1, 2), QTime(3, 4, 5)), &error), qPrintable(error));
    else
        QVERIFY2(sim.trace().saveCandump(path, &error), qPrintable(error));

    const QList<BusFrame> loaded = TraceRecorder::load(path, &error);
    QCOMPARE(size_t(loaded.size()), recorded.size());
    for (qsizetype i = 0; i < loaded.size(); ++i) {
        const BusFrame &a = recorded[size_t(i)];
        const BusFrame &b = loaded.at(i);
        QCOMPARE(b.timeUs, a.timeUs);
        QCOMPARE(b.frame.frameId(), a.frame.frameId());
        QCOMPARE(b.frame.payload(), a.frame.payload());
    }
    QVERIFY(!sim.trace().saveCandump(dir.filePath(QStringLiteral("no/such/dir/x.log")), &error));
}

void tst_Trace::replayDrivesTheCluster()
{
    // Record a short drive ...
    QList<BusFrame> log;
    double recordedSpeed = 0;
    {
        Simulation sim;
        startEngine(sim);
        driveTo(sim, 40.0);
        sim.run(2000);
        recordedSpeed = sim.ipc().state().speedKph;
        const auto &frames = sim.trace().frames();
        // keep the last four seconds
        for (const BusFrame &f : frames)
            if (f.timeUs >= sim.nowUs() - 4'000'000)
                log << f;
    }
    QVERIFY(log.size() > 1000);

    // ... and play it into a fresh vehicle whose own ECUs stay silent.
    Simulation sim;
    sim.startReplay(log);
    QVERIFY(sim.replaying());
    sim.run(3900);
    QVERIFY(!sim.ecm().awake() && !sim.abs().awake());
    QVERIFY(sim.replayProgress() > 0.9);
    const ClusterState &c = sim.ipc().state();
    QVERIFY(c.powered);
    QVERIFY2(qAbs(c.speedKph - recordedSpeed) < 3.0, qPrintable(QString::number(c.speedKph)));
    QVERIFY(c.rpm > 700.0);
    QVERIFY(!c.abs && !c.mil);
    QCOMPARE(sim.trace().frames().back().sender, QStringLiteral("Replay"));

    // When the log ends the cluster sees the messages time out.
    sim.run(2000);
    QVERIFY(c.abs && c.mil);
    QCOMPARE(c.speedKph, 0.0);

    sim.stopReplay();
    QVERIFY(!sim.replaying());
    sim.run(1500);
    QVERIFY(sim.ecm().awake());
    QCOMPARE(sim.signal(QStringLiteral("IgnitionState")), 1.0);
}

QTEST_GUILESS_MAIN(tst_Trace)
#include "tst_trace.moc"
