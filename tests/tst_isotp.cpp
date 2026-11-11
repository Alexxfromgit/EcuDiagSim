#include "diag/IsoTp.h"

#include <QTest>

#include <deque>
#include <optional>

using namespace ecusim;

namespace {

/// Two ISO-TP endpoints wired back to back; frames are delivered one tick later.
struct Wire
{
    IsoTpLink a;
    IsoTpLink b;
    std::deque<QCanBusFrame> toA, toB;
    QList<QCanBusFrame> log; ///< everything that crossed the wire, in order
    QList<qint64> logTime;
    QList<QByteArray> receivedByA, receivedByB;
    std::optional<IsoTpLink::Error> errorA, errorB;
    qint64 now = 0;
    bool dropToB = false;

    static IsoTpLink::Config cfg(quint32 tx, quint32 rx)
    {
        IsoTpLink::Config c;
        c.txId = tx;
        c.rxId = rx;
        c.nBsMs = 100;
        c.nCrMs = 100;
        return c;
    }

    explicit Wire(IsoTpLink::Config ca = cfg(0x7E0, 0x7E8), IsoTpLink::Config cb = cfg(0x7E8, 0x7E0))
        : a(ca)
        , b(cb)
    {
        a.sendFrame = [this](const QCanBusFrame &f) {
            log << f;
            logTime << now;
            if (!dropToB)
                toB.push_back(f);
        };
        b.sendFrame = [this](const QCanBusFrame &f) {
            log << f;
            logTime << now;
            toA.push_back(f);
        };
        a.onMessage = [this](const QByteArray &m, bool) { receivedByA << m; };
        b.onMessage = [this](const QByteArray &m, bool) { receivedByB << m; };
        a.onError = [this](IsoTpLink::Error e) { errorA = e; };
        b.onError = [this](IsoTpLink::Error e) { errorB = e; };
    }

    void run(int ms)
    {
        for (int i = 0; i < ms; ++i) {
            ++now;
            a.tick(now);
            b.tick(now);
            std::deque<QCanBusFrame> inA, inB;
            inA.swap(toA);
            inB.swap(toB);
            for (const auto &f : inA)
                a.handleFrame(f);
            for (const auto &f : inB)
                b.handleFrame(f);
        }
    }

    int count(quint8 pciType) const
    {
        int n = 0;
        for (const auto &f : log)
            n += (static_cast<quint8>(f.payload().at(0)) >> 4) == pciType ? 1 : 0;
        return n;
    }
};

QByteArray pattern(int size)
{
    QByteArray d(size, '\0');
    for (int i = 0; i < size; ++i)
        d[i] = static_cast<char>(i * 7 + 1);
    return d;
}

} // namespace

class tst_IsoTp : public QObject
{
    Q_OBJECT

private slots:
    void singleFrame();
    void singleFrameWithoutPadding();
    void multiFrame();
    void sequenceNumberWraps();
    void maximumLength();
    void blockSizeAndSeparationTime();
    void timeoutWithoutFlowControl();
    void timeoutMissingConsecutiveFrame();
    void wrongSequenceNumber();
    void overflowIsRejected();
    void flowControlWait();
    void reservedFlowControlValues();
    void functionalAddressing();
    void busyAndInvalidRequests();
    void foreignFramesAreIgnored();
};

void tst_IsoTp::singleFrame()
{
    Wire w;
    QVERIFY(w.a.send(QByteArray::fromHex("22F190")));
    QVERIFY(!w.a.isSending());
    w.run(2);
    QCOMPARE(w.log.size(), 1);
    QCOMPARE(w.log.first().frameId(), quint32(0x7E0));
    QCOMPARE(w.log.first().payload(), QByteArray::fromHex("0322F190CCCCCCCC"));
    QCOMPARE(w.receivedByB, QList<QByteArray>{QByteArray::fromHex("22F190")});

    QVERIFY(w.a.send(pattern(7)));
    w.run(2);
    QCOMPARE(w.receivedByB.last(), pattern(7));
}

void tst_IsoTp::singleFrameWithoutPadding()
{
    auto ca = Wire::cfg(0x7E0, 0x7E8);
    ca.padding = false;
    Wire w(ca);
    QVERIFY(w.a.send(QByteArray::fromHex("3E00")));
    w.run(2);
    QCOMPARE(w.log.first().payload(), QByteArray::fromHex("023E00"));
    QCOMPARE(w.receivedByB.size(), 1);
}

void tst_IsoTp::multiFrame()
{
    Wire w;
    const QByteArray message = pattern(20);
    QVERIFY(w.a.send(message));
    QVERIFY(w.a.isSending());
    w.run(20);

    QCOMPARE(w.receivedByB, QList<QByteArray>{message});
    QVERIFY(!w.a.isSending());
    QVERIFY(!w.b.isReceiving());
    // FF (6 bytes) + FC + CF (7) + CF (7)
    QCOMPARE(w.log.size(), 4);
    QCOMPARE(w.log.at(0).payload().left(2), QByteArray::fromHex("1014"));
    QCOMPARE(w.log.at(0).payload().mid(2), message.left(6));
    QCOMPARE(w.log.at(1).frameId(), quint32(0x7E8));
    QCOMPARE(w.log.at(1).payload(), QByteArray::fromHex("300000CCCCCCCCCC"));
    QCOMPARE(quint8(w.log.at(2).payload().at(0)), quint8(0x21));
    QCOMPARE(quint8(w.log.at(3).payload().at(0)), quint8(0x22));
    QCOMPARE(w.log.at(3).payload().mid(1), message.mid(13));
    QVERIFY(!w.errorA && !w.errorB);
}

void tst_IsoTp::sequenceNumberWraps()
{
    Wire w;
    const QByteArray message = pattern(6 + 7 * 20); // 20 consecutive frames: SN 1..F,0,1..4
    QVERIFY(w.a.send(message));
    w.run(60);
    QCOMPARE(w.receivedByB, QList<QByteArray>{message});
    QCOMPARE(w.count(2), 20);
    QCOMPARE(quint8(w.log.at(2 + 14).payload().at(0)), quint8(0x2F));
    QCOMPARE(quint8(w.log.at(2 + 15).payload().at(0)), quint8(0x20));
}

void tst_IsoTp::maximumLength()
{
    Wire w;
    const QByteArray message = pattern(4095);
    QVERIFY(w.a.send(message));
    w.run(700);
    QCOMPARE(w.receivedByB.size(), 1);
    QCOMPARE(w.receivedByB.first(), message);
    QCOMPARE(w.log.first().payload().left(2), QByteArray::fromHex("1FFF"));
}

void tst_IsoTp::blockSizeAndSeparationTime()
{
    auto cb = Wire::cfg(0x7E8, 0x7E0);
    cb.blockSize = 3;
    cb.stMinMs = 5;
    Wire w(Wire::cfg(0x7E0, 0x7E8), cb);
    const QByteArray message = pattern(6 + 7 * 7); // 7 consecutive frames -> blocks of 3, 3, 1
    QVERIFY(w.a.send(message));
    w.run(100);

    QCOMPARE(w.receivedByB, QList<QByteArray>{message});
    QCOMPARE(w.count(3), 3); // one flow control per block
    QCOMPARE(w.log.at(1).payload().left(3), QByteArray::fromHex("300305"));

    // Consecutive frames inside a block are at least STmin apart.
    qint64 lastCf = -1;
    int cfInBlock = 0;
    for (qsizetype i = 0; i < w.log.size(); ++i) {
        const quint8 type = static_cast<quint8>(w.log.at(i).payload().at(0)) >> 4;
        if (type == 3) {
            cfInBlock = 0;
        } else if (type == 2) {
            if (cfInBlock > 0)
                QVERIFY(w.logTime.at(i) - lastCf >= 5);
            lastCf = w.logTime.at(i);
            ++cfInBlock;
        }
    }
}

void tst_IsoTp::timeoutWithoutFlowControl()
{
    Wire w;
    w.dropToB = true; // nobody answers
    QVERIFY(w.a.send(pattern(30)));
    w.run(99);
    QVERIFY(w.a.isSending());
    w.run(5);
    QVERIFY(!w.a.isSending());
    QCOMPARE(w.errorA, std::optional(IsoTpLink::Error::TimeoutFlowControl));
    QCOMPARE(w.log.size(), 1);
    // The link is usable again.
    w.dropToB = false;
    QVERIFY(w.a.send(QByteArray::fromHex("3E00")));
    w.run(2);
    QCOMPARE(w.receivedByB.size(), 1);
}

void tst_IsoTp::timeoutMissingConsecutiveFrame()
{
    Wire w;
    w.b.handleFrame(QCanBusFrame(0x7E0, QByteArray::fromHex("1014010203040506")));
    QVERIFY(w.b.isReceiving());
    w.run(105);
    QVERIFY(!w.b.isReceiving());
    QCOMPARE(w.errorB, std::optional(IsoTpLink::Error::TimeoutConsecutive));
    QVERIFY(w.receivedByB.isEmpty());
}

void tst_IsoTp::wrongSequenceNumber()
{
    Wire w;
    w.b.handleFrame(QCanBusFrame(0x7E0, QByteArray::fromHex("1014010203040506")));
    w.b.handleFrame(QCanBusFrame(0x7E0, QByteArray::fromHex("2207080910111213"))); // expected 0x21
    QCOMPARE(w.errorB, std::optional(IsoTpLink::Error::WrongSequence));
    QVERIFY(!w.b.isReceiving());
    QVERIFY(w.receivedByB.isEmpty());
    // A stray consecutive frame afterwards is ignored.
    w.b.handleFrame(QCanBusFrame(0x7E0, QByteArray::fromHex("2107080910111213")));
    QVERIFY(w.receivedByB.isEmpty());
}

void tst_IsoTp::overflowIsRejected()
{
    auto cb = Wire::cfg(0x7E8, 0x7E0);
    cb.maxLength = 64;
    Wire w(Wire::cfg(0x7E0, 0x7E8), cb);
    QVERIFY(w.a.send(pattern(100)));
    w.run(10);
    QCOMPARE(w.log.at(1).payload().left(1), QByteArray::fromHex("32"));
    QCOMPARE(w.errorA, std::optional(IsoTpLink::Error::Overflow));
    QVERIFY(!w.a.isSending());
    QVERIFY(w.receivedByB.isEmpty());
}

void tst_IsoTp::flowControlWait()
{
    Wire w;
    w.dropToB = true;
    QVERIFY(w.a.send(pattern(20)));
    // The receiver keeps asking for more time, then lets the sender continue.
    for (int i = 0; i < 3; ++i) {
        w.run(80);
        w.a.handleFrame(QCanBusFrame(0x7E8, QByteArray::fromHex("310000")));
        QVERIFY(w.a.isSending());
    }
    QVERIFY(!w.errorA);
    w.a.handleFrame(QCanBusFrame(0x7E8, QByteArray::fromHex("300000")));
    w.run(5);
    QVERIFY(!w.a.isSending());
    QCOMPARE(w.count(2), 2);
}

void tst_IsoTp::reservedFlowControlValues()
{
    // Reserved STmin (0x80..0xF0, 0xFA..0xFF) must be treated as 127 ms.
    Wire w;
    w.dropToB = true;
    QVERIFY(w.a.send(pattern(20)));
    w.run(1);
    w.a.handleFrame(QCanBusFrame(0x7E8, QByteArray::fromHex("300080")));
    w.run(1);
    QCOMPARE(w.count(2), 1); // first consecutive frame right away
    w.run(120);
    QCOMPARE(w.count(2), 1); // ... the second one only after 127 ms
    w.run(10);
    QCOMPARE(w.count(2), 2);

    // A reserved flow status aborts the transmission with its own error.
    Wire v;
    v.dropToB = true;
    QVERIFY(v.a.send(pattern(20)));
    v.a.handleFrame(QCanBusFrame(0x7E8, QByteArray::fromHex("350000")));
    QVERIFY(!v.a.isSending());
    QCOMPARE(v.errorA, std::optional(IsoTpLink::Error::InvalidFlowStatus));
}

void tst_IsoTp::functionalAddressing()
{
    auto cb = Wire::cfg(0x7E8, 0x7E0);
    cb.functionalRxId = 0x7DF;
    IsoTpLink link(cb);
    QList<QPair<QByteArray, bool>> received;
    int sent = 0;
    link.onMessage = [&](const QByteArray &m, bool functional) { received.append({m, functional}); };
    link.sendFrame = [&](const QCanBusFrame &) { ++sent; };

    QVERIFY(link.handleFrame(QCanBusFrame(0x7DF, QByteArray::fromHex("0201005555555555"))));
    QCOMPARE(received.size(), 1);
    QCOMPARE(received.first().first, QByteArray::fromHex("0100"));
    QVERIFY(received.first().second);

    // Segmented requests are not allowed with functional addressing: no flow control.
    QVERIFY(link.handleFrame(QCanBusFrame(0x7DF, QByteArray::fromHex("1014010203040506"))));
    QCOMPARE(sent, 0);
    QVERIFY(!link.isReceiving());

    QVERIFY(link.handleFrame(QCanBusFrame(0x7E0, QByteArray::fromHex("023E005555555555"))));
    QVERIFY(!received.last().second);
}

void tst_IsoTp::busyAndInvalidRequests()
{
    Wire w;
    QVERIFY(!w.a.send({}));
    QVERIFY(!w.a.send(pattern(4096)));
    QVERIFY(w.a.send(pattern(50)));
    QVERIFY(!w.a.send(QByteArray::fromHex("3E00"))); // transmission in progress
    w.run(30);
    QVERIFY(w.a.send(QByteArray::fromHex("3E00")));

    // Malformed single frames are dropped silently.
    w.b.handleFrame(QCanBusFrame(0x7E0, QByteArray::fromHex("00AABB")));
    w.b.handleFrame(QCanBusFrame(0x7E0, QByteArray::fromHex("05AABB")));
    w.b.handleFrame(QCanBusFrame(0x7E0, QByteArray::fromHex("08AABBCCDDEEFF11")));
    w.run(2);
    QCOMPARE(w.receivedByB.size(), 2);
}

void tst_IsoTp::foreignFramesAreIgnored()
{
    Wire w;
    QVERIFY(!w.b.handleFrame(QCanBusFrame(0x100, QByteArray::fromHex("023E00"))));
    QVERIFY(!w.b.handleFrame(QCanBusFrame(0x7E1, QByteArray::fromHex("023E00"))));
    QVERIFY(w.receivedByB.isEmpty());
}

QTEST_GUILESS_MAIN(tst_IsoTp)
#include "tst_isotp.moc"
