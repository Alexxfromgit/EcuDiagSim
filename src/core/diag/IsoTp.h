#pragma once

#include <QByteArray>
#include <QCanBusFrame>

#include <functional>

namespace ecusim {

/// ISO 15765-2 transport protocol endpoint for classic CAN with normal (11-bit) addressing.
///
/// One link handles one request/response identifier pair in both directions:
///   single frame       [0x0L d0 .. d6]
///   first frame        [0x1L LL d0 .. d5]            L = 12 bit total length
///   consecutive frame  [0x2N d0 .. d6]               N = sequence number 1..F,0,1..
///   flow control       [0x3S BS STmin]               S = 0 continue, 1 wait, 2 overflow
class IsoTpLink
{
public:
    enum class Error {
        TimeoutFlowControl, ///< N_Bs: no flow control after a first frame / block
        TimeoutConsecutive, ///< N_Cr: sender stopped in the middle of a message
        WrongSequence,      ///< consecutive frame with an unexpected sequence number
        Overflow,           ///< receiver reported a buffer overflow
        UnexpectedFrame,
    };

    struct Config
    {
        quint32 txId = 0;
        quint32 rxId = 0;
        quint32 functionalRxId = 0; ///< optional second receive ID, single frames only
        quint8 blockSize = 0;       ///< BS we announce as receiver (0 = send everything)
        quint8 stMinMs = 0;         ///< STmin we announce as receiver
        bool padding = true;
        quint8 padByte = 0xCC;
        int nBsMs = 1000;
        int nCrMs = 1000;
        int maxLength = 4095;
    };

    explicit IsoTpLink(const Config &config);

    std::function<void(const QCanBusFrame &)> sendFrame;
    std::function<void(const QByteArray &payload, bool functional)> onMessage;
    std::function<void(Error)> onError;

    const Config &config() const { return m_config; }

    /// Starts transmitting a message. Returns false if busy or the payload is too long.
    bool send(const QByteArray &payload);
    /// Feeds a received CAN frame. Returns true if the frame belonged to this link.
    bool handleFrame(const QCanBusFrame &frame);
    /// Advances timers; call once per millisecond.
    void tick(qint64 nowMs);
    void reset();

    bool isSending() const { return m_tx != TxState::Idle; }
    bool isReceiving() const { return m_rxActive; }

    static QString errorText(Error error);

private:
    enum class TxState { Idle, WaitFlowControl, SendConsecutive };

    void transmit(QByteArray data);
    void sendFlowControl(quint8 flowStatus);
    void fail(Error error);
    void handleFlowControl(const QByteArray &d);

    Config m_config;
    qint64 m_now = 0;

    TxState m_tx = TxState::Idle;
    QByteArray m_txData;
    int m_txOffset = 0;
    quint8 m_txSn = 0;
    int m_txBlockRemaining = 0;
    int m_txStMin = 0;
    qint64 m_txDeadline = 0;
    qint64 m_txNextCf = 0;

    bool m_rxActive = false;
    QByteArray m_rxData;
    int m_rxLength = 0;
    quint8 m_rxSn = 0;
    int m_rxBlockCount = 0;
    qint64 m_rxDeadline = 0;
};

} // namespace ecusim
