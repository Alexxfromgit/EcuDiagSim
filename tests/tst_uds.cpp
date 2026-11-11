// UDS application layer tests against a stand-alone DiagnosticServer (no bus, no ISO-TP).
#include "diag/DiagnosticServer.h"
#include "diag/DtcCatalog.h"

#include <QTest>

using namespace ecusim;

namespace {
constexpr quint32 kDtcA = saeDtc('C', 0x0035, 0x13);
constexpr quint32 kDtcB = saeDtc('P', 0x0118);

QByteArray hex(const char *text)
{
    return fromHex(QString::fromLatin1(text));
}

struct Fixture
{
    DtcManager dtcs;
    DiagnosticServer server{dtcs};
    int resets = 0;
    int lamp = -1; ///< IO controlled output: -1 = ECU in control
    qint64 now = 0;

    Fixture()
    {
        DtcDefinition a;
        a.code = kDtcA;
        a.failThreshold = 1;
        a.passThreshold = 1;
        dtcs.define(a);
        DtcDefinition b;
        b.code = kDtcB;
        b.failThreshold = 1;
        b.passThreshold = 1;
        b.emissionRelevant = true;
        dtcs.define(b);
        dtcs.setSnapshotProvider([] {
            return QList<SnapshotRecord>{{0xF40D, hex("32")}, {0x3001, hex("1388")}};
        });
        dtcs.startOperationCycle();

        server.addDid(0xF190, [] { return QByteArray("WSMECUDG8TS000042"); });
        server.addDid(0x1234, [] { return hex("AB CD"); });
        server.addIoControl(
            0x3010,
            [this](bool take, const QByteArray &state) {
                if (take && (state.size() != 1 || static_cast<quint8>(state[0]) > 1))
                    return false;
                lamp = take ? state[0] : -1;
                return true;
            },
            [this] { return QByteArray(1, static_cast<char>(lamp == 1)); });
        server.onReset = [this] { ++resets; };
    }

    QByteArray ask(const char *request, bool functional = false)
    {
        return server.handleRequest(hex(request), functional);
    }
    void advance(int ms)
    {
        for (int i = 0; i < ms; ++i)
            server.tick(++now);
    }
    /// Extended session + security access with the correct key.
    void unlock()
    {
        QCOMPARE(ask("10 03").left(2), hex("50 03"));
        const QByteArray seedRsp = ask("27 01");
        QCOMPARE(seedRsp.size(), 6);
        const quint32 key = securityKeyFromSeed(seedOf(seedRsp));
        QCOMPARE(server.handleRequest(hex("27 02") + be32(key), false), hex("67 02"));
    }
    static quint32 seedOf(const QByteArray &rsp)
    {
        quint32 seed = 0;
        for (int i = 2; i < 6; ++i)
            seed = (seed << 8) | static_cast<quint8>(rsp[i]);
        return seed;
    }
    static QByteArray be32(quint32 v)
    {
        QByteArray a;
        for (int shift = 24; shift >= 0; shift -= 8)
            a.append(static_cast<char>((v >> shift) & 0xFF));
        return a;
    }
};
} // namespace

class tst_Uds : public QObject
{
    Q_OBJECT

private slots:
    void sessionControl();
    void suppressPositiveResponse();
    void unknownServiceAndFunctionalAddressing();
    void testerPresent();
    void s3Timeout();
    void readDataByIdentifier();
    void securityAccess();
    void securityAccessLockout();
    void ioControl();
    void ioControlReleasedOnSessionEnd();
    void readDtcInformation();
    void readDtcSnapshotAndExtendedData();
    void clearDiagnosticInformation();
    void ecuReset();
    void obdServicesDisabledByDefault();
    void responseTooLong();
    void helperFunctions();
};

void tst_Uds::sessionControl()
{
    Fixture f;
    QCOMPARE(f.server.session(), DiagnosticServer::Session::Default);
    QCOMPARE(f.ask("10 03"), hex("50 03 00 32 01 F4"));
    QCOMPARE(f.server.session(), DiagnosticServer::Session::Extended);
    QCOMPARE(f.ask("22 F1 86"), hex("62 F1 86 03"));
    QCOMPARE(f.ask("10 01"), hex("50 01 00 32 01 F4"));
    QCOMPARE(f.server.session(), DiagnosticServer::Session::Default);

    QCOMPARE(f.ask("10 02"), hex("7F 10 12")); // programming session not supported
    QCOMPARE(f.ask("10 7F"), hex("7F 10 12"));
    QCOMPARE(f.ask("10"), hex("7F 10 13"));
    QCOMPARE(f.ask("10 03 00"), hex("7F 10 13"));
}

void tst_Uds::suppressPositiveResponse()
{
    Fixture f;
    QVERIFY(f.ask("10 83").isEmpty());
    QCOMPARE(f.server.session(), DiagnosticServer::Session::Extended);
    QVERIFY(f.ask("3E 80").isEmpty());
    // Negative responses are never suppressed.
    QCOMPARE(f.ask("10 82"), hex("7F 10 12"));
    QCOMPARE(f.ask("3E 81"), hex("7F 3E 12"));
}

void tst_Uds::unknownServiceAndFunctionalAddressing()
{
    Fixture f;
    QCOMPARE(f.ask("31 01 FF 00"), hex("7F 31 11"));
    QCOMPARE(f.ask("22 99 99"), hex("7F 22 31"));
    // Functionally addressed: "not supported" answers are not sent ...
    QVERIFY(f.ask("31 01 FF 00", true).isEmpty());
    QVERIFY(f.ask("22 99 99", true).isEmpty());
    QVERIFY(f.ask("10 02", true).isEmpty());
    QVERIFY(f.ask("2F 30 10 03 01", true).isEmpty()); // serviceNotSupportedInActiveSession
    // ... but other negative responses and all positive ones are.
    QCOMPARE(f.ask("10", true), hex("7F 10 13"));
    QCOMPARE(f.ask("3E 00", true), hex("7E 00"));
    QVERIFY(f.server.handleRequest({}, false).isEmpty());
}

void tst_Uds::testerPresent()
{
    Fixture f;
    QCOMPARE(f.ask("3E 00"), hex("7E 00"));
    QCOMPARE(f.ask("3E 01"), hex("7F 3E 12"));
    QCOMPARE(f.ask("3E"), hex("7F 3E 13"));
}

void tst_Uds::s3Timeout()
{
    Fixture f;
    f.advance(10);
    f.ask("10 03");
    f.advance(DiagnosticServer::kS3TimeoutMs - 1);
    QCOMPARE(f.server.session(), DiagnosticServer::Session::Extended);
    f.advance(5);
    QCOMPARE(f.server.session(), DiagnosticServer::Session::Default);

    // Any request restarts the timer; TesterPresent is the usual way to keep a session alive.
    f.ask("10 03");
    for (int i = 0; i < 5; ++i) {
        f.advance(2000);
        QVERIFY(f.ask("3E 80").isEmpty());
    }
    QCOMPARE(f.server.session(), DiagnosticServer::Session::Extended);
}

void tst_Uds::readDataByIdentifier()
{
    Fixture f;
    QCOMPARE(f.ask("22 F1 90"), hex("62 F1 90") + QByteArray("WSMECUDG8TS000042"));
    QCOMPARE(f.ask("22 12 34"), hex("62 12 34 AB CD"));
    QCOMPARE(f.ask("22 12 34 F1 86"), hex("62 12 34 AB CD F1 86 01"));
    // Unsupported identifiers are skipped; 0x31 only when none is supported.
    QCOMPARE(f.ask("22 12 34 99 99"), hex("62 12 34 AB CD"));
    QCOMPARE(f.ask("22 99 99 12 34"), hex("62 12 34 AB CD"));
    QCOMPARE(f.ask("22 99 99 88 88"), hex("7F 22 31"));
    QCOMPARE(f.ask("22"), hex("7F 22 13"));
    QCOMPARE(f.ask("22 12"), hex("7F 22 13"));
    QCOMPARE(f.ask("22 12 34 F1"), hex("7F 22 13"));
    QVERIFY(f.server.hasDid(0x1234));
    QVERIFY(f.server.readDid(0x9999).isEmpty());
}

void tst_Uds::securityAccess()
{
    Fixture f;
    QCOMPARE(f.ask("27 01"), hex("7F 27 7F")); // not in the default session
    f.ask("10 03");
    QCOMPARE(f.ask("27 02 00 00 00 00"), hex("7F 27 24")); // key without seed
    QCOMPARE(f.ask("27 03"), hex("7F 27 12"));
    QCOMPARE(f.ask("27 01 00"), hex("7F 27 13"));

    const QByteArray seedRsp = f.ask("27 01");
    QCOMPARE(seedRsp.left(2), hex("67 01"));
    QCOMPARE(seedRsp.size(), 6);
    const quint32 seed = Fixture::seedOf(seedRsp);
    QVERIFY(seed != 0);
    QCOMPARE(f.ask("27 02 00 00"), hex("7F 27 13"));

    QVERIFY(!f.server.unlocked());
    QCOMPARE(f.server.handleRequest(hex("27 02") + Fixture::be32(securityKeyFromSeed(seed)), false), hex("67 02"));
    QVERIFY(f.server.unlocked());
    // Already unlocked: the seed is all zeros.
    QCOMPARE(f.ask("27 01"), hex("67 01 00 00 00 00"));

    // A fresh seed every time, and a session change locks the ECU again.
    f.ask("10 03");
    QVERIFY(!f.server.unlocked());
    QVERIFY(Fixture::seedOf(f.ask("27 01")) != seed);
}

void tst_Uds::securityAccessLockout()
{
    Fixture f;
    f.advance(1);
    f.ask("10 03");
    for (int attempt = 1; attempt <= DiagnosticServer::kMaxKeyAttempts; ++attempt) {
        QCOMPARE(f.ask("27 01").left(2), hex("67 01"));
        const QByteArray rsp = f.ask("27 02 DE AD BE EF");
        QCOMPARE(rsp, attempt < DiagnosticServer::kMaxKeyAttempts ? hex("7F 27 35") : hex("7F 27 36"));
    }
    QCOMPARE(f.ask("27 01"), hex("7F 27 37")); // requiredTimeDelayNotExpired

    // Resetting the ECU must not be a way around the delay.
    f.server.powerOnReset();
    f.ask("10 03");
    QCOMPARE(f.ask("27 01"), hex("7F 27 37"));

    for (int i = 0; i < 6; ++i) { // keep the session alive while the delay runs
        f.advance(2000);
        f.ask("3E 80");
    }
    QCOMPARE(f.server.session(), DiagnosticServer::Session::Extended);
    f.unlock();
    QVERIFY(f.server.unlocked());
}

void tst_Uds::ioControl()
{
    Fixture f;
    QCOMPARE(f.ask("2F 30 10 03 01"), hex("7F 2F 7F"));
    f.ask("10 03");
    QCOMPARE(f.ask("2F 30 10 03 01"), hex("7F 2F 33")); // securityAccessDenied
    QCOMPARE(f.ask("2F 99 99 03 01"), hex("7F 2F 31"));
    QCOMPARE(f.ask("2F 30 10"), hex("7F 2F 13"));
    f.unlock();

    QCOMPARE(f.ask("2F 30 10 03 01"), hex("6F 30 10 03 01"));
    QCOMPARE(f.lamp, 1);
    QCOMPARE(f.ask("2F 30 10 03 00"), hex("6F 30 10 03 00"));
    QCOMPARE(f.lamp, 0);
    QCOMPARE(f.ask("2F 30 10 03 05"), hex("7F 2F 31")); // invalid state
    QCOMPARE(f.ask("2F 30 10 03"), hex("7F 2F 13"));    // state missing
    QCOMPARE(f.ask("2F 30 10 01"), hex("7F 2F 31"));    // resetToDefault not supported
    QCOMPARE(f.ask("2F 30 10 00"), hex("6F 30 10 00 00"));
    QCOMPARE(f.lamp, -1);
}

void tst_Uds::ioControlReleasedOnSessionEnd()
{
    Fixture f;
    f.unlock();
    f.ask("2F 30 10 03 01");
    QCOMPARE(f.lamp, 1);
    f.ask("10 01");
    QCOMPARE(f.lamp, -1);

    f.unlock();
    f.ask("2F 30 10 03 01");
    f.advance(DiagnosticServer::kS3TimeoutMs + 10);
    QCOMPARE(f.lamp, -1);

    f.unlock();
    f.ask("2F 30 10 03 01");
    f.server.powerOnReset();
    QCOMPARE(f.lamp, -1);
    QVERIFY(!f.server.unlocked());
}

void tst_Uds::readDtcInformation()
{
    Fixture f;
    QCOMPARE(f.ask("19 01 FF"), hex("59 01 FF 01 00 02")); // both match "not completed" bits
    QCOMPARE(f.ask("19 01 08"), hex("59 01 FF 01 00 00"));
    QCOMPARE(f.ask("19 02 08"), hex("59 02 FF"));
    QCOMPARE(f.ask("19 0A"), hex("59 0A FF 40 35 13 50 01 18 00 50"));

    f.dtcs.report(kDtcA, true);
    QCOMPARE(f.ask("19 01 08"), hex("59 01 FF 01 00 01"));
    QCOMPARE(f.ask("19 02 08"), hex("59 02 FF 40 35 13 AF"));
    QCOMPARE(f.ask("19 02 01"), hex("59 02 FF 40 35 13 AF"));
    f.dtcs.report(kDtcB, true);
    QCOMPARE(f.ask("19 02 08"), hex("59 02 FF 40 35 13 AF 01 18 00 AF"));
    f.dtcs.report(kDtcA, false);
    QCOMPARE(f.ask("19 02 01"), hex("59 02 FF 01 18 00 AF"));
    QCOMPARE(f.ask("19 82 01"), QByteArray()); // suppressPosRsp also applies here

    QCOMPARE(f.ask("19"), hex("7F 19 13"));
    QCOMPARE(f.ask("19 02"), hex("7F 19 13"));
    QCOMPARE(f.ask("19 0A 00"), hex("7F 19 13"));
    QCOMPARE(f.ask("19 55"), hex("7F 19 12"));
}

void tst_Uds::readDtcSnapshotAndExtendedData()
{
    Fixture f;
    // Nothing stored yet: DTC and status only.
    QCOMPARE(f.ask("19 04 40 35 13 01"), hex("59 04 40 35 13 50"));
    QCOMPARE(f.ask("19 06 40 35 13 01"), hex("59 06 40 35 13 50"));

    f.dtcs.report(kDtcA, true);
    // record 01, 2 identifiers: F40D = 32, 3001 = 13 88
    QCOMPARE(f.ask("19 04 40 35 13 01"), hex("59 04 40 35 13 AF 01 02 F4 0D 32 30 01 13 88"));
    QCOMPARE(f.ask("19 04 40 35 13 FF"), hex("59 04 40 35 13 AF 01 02 F4 0D 32 30 01 13 88"));
    // extended data record 01: occurrence counter, aging counter
    QCOMPARE(f.ask("19 06 40 35 13 01"), hex("59 06 40 35 13 AF 01 01 00"));
    f.dtcs.report(kDtcA, false);
    f.dtcs.report(kDtcA, true);
    QCOMPARE(f.ask("19 06 40 35 13 FF"), hex("59 06 40 35 13 AF 01 02 00"));

    QCOMPARE(f.ask("19 04 11 22 33 01"), hex("7F 19 31")); // unknown DTC
    QCOMPARE(f.ask("19 04 40 35 13 02"), hex("7F 19 31")); // unknown record
    QCOMPARE(f.ask("19 04 40 35 13"), hex("7F 19 13"));
}

void tst_Uds::clearDiagnosticInformation()
{
    Fixture f;
    f.dtcs.report(kDtcA, true);
    f.dtcs.report(kDtcB, true);
    QCOMPARE(f.ask("14 11 22 33"), hex("7F 14 31"));
    QCOMPARE(f.ask("14 FF FF"), hex("7F 14 13"));
    QCOMPARE(f.dtcs.storedCount(), 2);

    QCOMPARE(f.ask("14 40 35 13"), hex("54"));
    QCOMPARE(f.dtcs.storedCount(), 1);
    QCOMPARE(f.ask("14 FF FF FF"), hex("54"));
    QCOMPARE(f.dtcs.storedCount(), 0);
    QCOMPARE(f.ask("19 02 FF"), hex("59 02 FF 40 35 13 50 01 18 00 50"));
}

void tst_Uds::ecuReset()
{
    Fixture f;
    QCOMPARE(f.ask("11 01"), hex("51 01"));
    QCOMPARE(f.ask("11 03"), hex("51 03"));
    QCOMPARE(f.resets, 2);
    QCOMPARE(f.ask("11 02"), hex("7F 11 12"));
    QCOMPARE(f.ask("11"), hex("7F 11 13"));
    QVERIFY(f.ask("11 81").isEmpty());
    QCOMPARE(f.resets, 3);
}

void tst_Uds::obdServicesDisabledByDefault()
{
    Fixture f;
    // An ECU without OBD duties rejects the legislated services when asked directly
    // and stays silent when they are broadcast.
    QCOMPARE(f.ask("01 00"), hex("7F 01 11"));
    QVERIFY(f.ask("01 00", true).isEmpty());
    QVERIFY(f.ask("03", true).isEmpty());

    f.server.enableObd(true);
    f.server.addObdPid(0x0D, [] { return hex("50"); });
    QCOMPARE(f.ask("01 0D", true), hex("41 0D 50"));
    QCOMPARE(f.ask("22 F4 0D"), hex("62 F4 0D 50")); // the same value as a UDS data identifier
    // PID 0x0D is bit 19 of the mask (MSB = PID 0x01)
    QCOMPARE(f.ask("01 00", true), hex("41 00 00 08 00 00"));
    f.dtcs.report(kDtcB, true);
    f.dtcs.report(kDtcA, true);
    QCOMPARE(f.ask("03", true), hex("43 01 01 18")); // only the emission relevant DTC
    QCOMPARE(f.ask("07", true), hex("47 01 01 18"));
    QCOMPARE(f.ask("04", true), hex("44"));
    QCOMPARE(f.ask("03", true), hex("43 00"));
    QVERIFY(f.ask("01 0C", true).isEmpty()); // unsupported PID: no answer
    QVERIFY(f.ask("08 00", true).isEmpty()); // unsupported mode: no answer
}

void tst_Uds::responseTooLong()
{
    Fixture f;
    // 300 x F190 needs a 5701 byte response, more than ISO-TP can carry.
    QByteArray request = hex("22");
    for (int i = 0; i < 300; ++i)
        request += hex("F1 90");
    QCOMPARE(f.server.handleRequest(request, false), hex("7F 22 14"));
    QCOMPARE(nrcName(0x14), QStringLiteral("responseTooLong"));
}

void tst_Uds::helperFunctions()
{
    QCOMPARE(toHex(hex("7f2231")), QStringLiteral("7F 22 31"));
    QCOMPARE(fromHex(QStringLiteral("0x7F, 0x22,0x31")), hex("7F 22 31"));
    QCOMPARE(fromHex(QStringLiteral("7F:22-31")), hex("7F 22 31"));
    QVERIFY(fromHex(QStringLiteral("7F 2")).isEmpty());
    QVERIFY(fromHex(QStringLiteral("hello")).isEmpty());
    QCOMPARE(nrcName(0x33), QStringLiteral("securityAccessDenied"));
    QCOMPARE(nrcName(0xEE), QStringLiteral("NRC 0xee"));
    QCOMPARE(serviceName(0x22), QStringLiteral("ReadDataByIdentifier"));
    QCOMPARE(securityKeyFromSeed(0), quint32(0xEA507718));
    QVERIFY(securityKeyFromSeed(1) != securityKeyFromSeed(2));
}

QTEST_GUILESS_MAIN(tst_Uds)
#include "tst_uds.moc"
