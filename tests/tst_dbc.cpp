#include "TestUtil.h"

#include <QCanSignalDescription>

using namespace ecusim;

class tst_Dbc : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void messagesAreKnown();
    void bitLayout();
    void roundTrip_data();
    void roundTrip();
    void valuesAreClamped();
    void unknownFrame();
    void crc8();
    void describe();

private:
    DbcDatabase m_dbc;
};

void tst_Dbc::initTestCase()
{
    QVERIFY2(m_dbc.loadFile(QStringLiteral(":/ecusim/dbc/vehicle.dbc")), qPrintable(m_dbc.errorString()));
}

void tst_Dbc::messagesAreKnown()
{
    QCOMPARE(m_dbc.messages().size(), 6);
    QCOMPARE(m_dbc.messageName(msg::AbsWheelSpeeds), QStringLiteral("ABS_WheelSpeeds"));
    QCOMPARE(m_dbc.sender(msg::AbsWheelSpeeds), QStringLiteral("ABS"));
    QCOMPARE(m_dbc.messageName(msg::EcmEngine), QStringLiteral("ECM_Engine"));
    QCOMPARE(m_dbc.sender(msg::TcmStatus), QStringLiteral("TCM"));
    QCOMPARE(m_dbc.sender(msg::BcmStatus), QStringLiteral("BCM"));
    QVERIFY(m_dbc.contains(msg::AbsStatus) && m_dbc.contains(msg::EcmStatus));
    QVERIFY(!m_dbc.contains(0x7DF));
    for (const auto &m : m_dbc.messages())
        QCOMPARE(int(m.size()), 8);
}

void tst_Dbc::bitLayout()
{
    // EngineRPM: 16 bit little endian at bit 0, factor 0.25 -> 1000 rpm = 4000 = 0x0FA0
    QCanBusFrame f = m_dbc.encode(msg::EcmEngine, {{QStringLiteral("EngineRPM"), 1000.0}});
    QCOMPARE(f.frameId(), quint32(0x100));
    QCOMPARE(f.payload().size(), 8);
    QCOMPARE(f.payload().left(2), QByteArray::fromHex("A00F"));

    // Two 12 bit signals share byte 1: FL = 100.0 km/h (0x3E8), FR = 50.0 km/h (0x1F4)
    f = m_dbc.encode(msg::AbsWheelSpeeds,
        {{QStringLiteral("WheelSpeed_FL"), 100.0}, {QStringLiteral("WheelSpeed_FR"), 50.0},
            {QStringLiteral("WheelSpeed_RR_Valid"), 1}, {QStringLiteral("ABS_AliveCounter"), 9}});
    QCOMPARE(f.payload().left(3), QByteArray::fromHex("E8431F"));
    QCOMPARE(quint8(f.payload().at(6)), quint8(0x98)); // counter 9 in the high nibble, RR valid = bit 3

    // Offset: CoolantTemp 90 degC -> raw 130
    f = m_dbc.encode(msg::EcmStatus, {{QStringLiteral("CoolantTemp"), 90.0}, {QStringLiteral("MilReq"), 1}});
    QCOMPARE(quint8(f.payload().at(0)), quint8(130));
    QCOMPARE(quint8(f.payload().at(1)), quint8(0x01));
}

void tst_Dbc::roundTrip_data()
{
    QTest::addColumn<quint32>("id");
    QTest::addColumn<QString>("signal");
    QTest::addColumn<double>("value");

    QTest::newRow("rpm") << msg::EcmEngine << "EngineRPM" << 2513.25;
    QTest::newRow("throttle") << msg::EcmEngine << "ThrottlePos" << 33.2;
    QTest::newRow("torque-negative") << msg::EcmEngine << "EngineTorque" << -25.0;
    QTest::newRow("torque") << msg::EcmEngine << "EngineTorque" << 212.5;
    QTest::newRow("coolant-cold") << msg::EcmStatus << "CoolantTemp" << -40.0;
    QTest::newRow("coolant") << msg::EcmStatus << "CoolantTemp" << 91.0;
    QTest::newRow("speed") << msg::AbsStatus << "VehicleSpeed" << 123.45;
    QTest::newRow("wheel-rl") << msg::AbsWheelSpeeds << "WheelSpeed_RL" << 87.3;
    QTest::newRow("wheel-rr") << msg::AbsWheelSpeeds << "WheelSpeed_RR" << 409.5;
    QTest::newRow("counter") << msg::AbsWheelSpeeds << "ABS_AliveCounter" << 15.0;
    QTest::newRow("gear") << msg::TcmStatus << "CurrentGear" << 7.0;
    QTest::newRow("trans-temp") << msg::TcmStatus << "TransTemp" << 135.0;
    QTest::newRow("battery") << msg::BcmStatus << "BatteryVoltage" << 14.2;
    QTest::newRow("ignition") << msg::BcmStatus << "IgnitionState" << 2.0;
}

void tst_Dbc::roundTrip()
{
    QFETCH(quint32, id);
    QFETCH(QString, signal);
    QFETCH(double, value);

    const QCanBusFrame frame = m_dbc.encode(id, {{signal, value}});
    const QVariantMap decoded = m_dbc.decode(frame);
    QVERIFY(decoded.contains(signal));
    QVERIFY2(qAbs(decoded.value(signal).toDouble() - value) < 1e-6,
        qPrintable(QStringLiteral("%1 != %2").arg(decoded.value(signal).toDouble()).arg(value)));
    // Nothing else leaks into neighbouring signals.
    for (auto it = decoded.cbegin(); it != decoded.cend(); ++it) {
        if (it.key() == signal)
            continue;
        const auto sig = m_dbc.message(id)->signalDescriptionForName(it.key());
        QVERIFY2(qAbs(it.value().toDouble() - sig.offset()) < 1e-6, qPrintable(it.key()));
    }
}

void tst_Dbc::valuesAreClamped()
{
    QCanBusFrame f = m_dbc.encode(msg::EcmStatus, {{QStringLiteral("CoolantTemp"), 999.0}});
    QCOMPARE(m_dbc.decode(f).value(QStringLiteral("CoolantTemp")).toDouble(), 215.0);
    f = m_dbc.encode(msg::EcmStatus, {{QStringLiteral("CoolantTemp"), -100.0}});
    QCOMPARE(m_dbc.decode(f).value(QStringLiteral("CoolantTemp")).toDouble(), -40.0);
    f = m_dbc.encode(msg::AbsWheelSpeeds, {{QStringLiteral("WheelSpeed_FL"), -5.0}});
    QCOMPARE(m_dbc.decode(f).value(QStringLiteral("WheelSpeed_FL")).toDouble(), 0.0);
}

void tst_Dbc::unknownFrame()
{
    QVERIFY(m_dbc.decode(QCanBusFrame(0x123, QByteArray(8, '\0'))).isEmpty());
    QVERIFY(!m_dbc.encode(0x123, {}).isValid());
    QVERIFY(m_dbc.describe(QCanBusFrame(0x7E8, QByteArray(8, '\0'))).isEmpty());
}

void tst_Dbc::crc8()
{
    // Standard check value of CRC-8/SAE-J1850
    QCOMPARE(crc8J1850(QByteArray("123456789")), quint8(0x4B));
}

void tst_Dbc::describe()
{
    const QCanBusFrame f = m_dbc.encode(msg::EcmEngine,
        {{QStringLiteral("EngineRPM"), 800.0}, {QStringLiteral("ThrottlePos"), 10.0}});
    const QString text = m_dbc.describe(f);
    QVERIFY2(text.startsWith(QStringLiteral("EngineRPM=800 rpm  ThrottlePos=10 %")), qPrintable(text));
}

QTEST_GUILESS_MAIN(tst_Dbc)
#include "tst_dbc.moc"
