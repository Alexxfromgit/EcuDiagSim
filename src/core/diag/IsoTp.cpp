#include "diag/IsoTp.h"

#include <algorithm>

namespace ecusim {

IsoTpLink::IsoTpLink(const Config &config)
    : m_config(config)
{
}

QString IsoTpLink::errorText(Error error)
{
    switch (error) {
    case Error::TimeoutFlowControl: return QStringLiteral("N_Bs timeout: no flow control received");
    case Error::TimeoutConsecutive: return QStringLiteral("N_Cr timeout: consecutive frame missing");
    case Error::WrongSequence: return QStringLiteral("wrong consecutive frame sequence number");
    case Error::Overflow: return QStringLiteral("receiver reported buffer overflow");
    case Error::UnexpectedFrame: return QStringLiteral("unexpected frame");
    }
    return {};
}

void IsoTpLink::reset()
{
    m_tx = TxState::Idle;
    m_txData.clear();
    m_rxActive = false;
    m_rxData.clear();
}

void IsoTpLink::transmit(QByteArray data)
{
    if (m_config.padding && data.size() < 8)
        data.append(8 - data.size(), static_cast<char>(m_config.padByte));
    if (sendFrame)
        sendFrame(QCanBusFrame(m_config.txId, data));
}

void IsoTpLink::fail(Error error)
{
    if (error == Error::TimeoutConsecutive || error == Error::WrongSequence) {
        m_rxActive = false;
        m_rxData.clear();
    } else {
        m_tx = TxState::Idle;
        m_txData.clear();
    }
    if (onError)
        onError(error);
}

bool IsoTpLink::send(const QByteArray &payload)
{
    if (m_tx != TxState::Idle || payload.isEmpty() || payload.size() > m_config.maxLength)
        return false;

    if (payload.size() <= 7) {
        QByteArray d;
        d.append(static_cast<char>(payload.size()));
        d.append(payload);
        transmit(d);
        return true;
    }

    m_txData = payload;
    QByteArray d;
    d.append(static_cast<char>(0x10 | ((payload.size() >> 8) & 0x0F)));
    d.append(static_cast<char>(payload.size() & 0xFF));
    d.append(payload.left(6));
    m_txOffset = 6;
    m_txSn = 1;
    m_tx = TxState::WaitFlowControl;
    m_txDeadline = m_now + m_config.nBsMs;
    transmit(d);
    return true;
}

void IsoTpLink::sendFlowControl(quint8 flowStatus)
{
    QByteArray d;
    d.append(static_cast<char>(0x30 | flowStatus));
    d.append(static_cast<char>(m_config.blockSize));
    d.append(static_cast<char>(m_config.stMinMs));
    transmit(d);
}

void IsoTpLink::handleFlowControl(const QByteArray &d)
{
    if (m_tx != TxState::WaitFlowControl || d.size() < 3)
        return;
    switch (static_cast<quint8>(d[0]) & 0x0F) {
    case 0: { // continue to send
        m_txBlockRemaining = static_cast<quint8>(d[1]);
        const quint8 st = static_cast<quint8>(d[2]);
        // 0x00..0x7F = milliseconds, 0xF1..0xF9 = 100..900 us (rounded up to our 1 ms tick)
        m_txStMin = st <= 0x7F ? st : 1;
        m_tx = TxState::SendConsecutive;
        m_txNextCf = m_now;
        break;
    }
    case 1: // wait
        m_txDeadline = m_now + m_config.nBsMs;
        break;
    default:
        fail(Error::Overflow);
        break;
    }
}

bool IsoTpLink::handleFrame(const QCanBusFrame &frame)
{
    const quint32 id = frame.frameId();
    const bool functional = m_config.functionalRxId != 0 && id == m_config.functionalRxId;
    if (id != m_config.rxId && !functional)
        return false;

    const QByteArray d = frame.payload();
    if (d.isEmpty())
        return true;
    const quint8 pci = static_cast<quint8>(d[0]);

    switch (pci >> 4) {
    case 0: { // single frame
        const int len = pci & 0x0F;
        if (len == 0 || len > 7 || len > d.size() - 1)
            return true;
        if (m_rxActive && !functional) {
            // A new message on the same connection terminates the reception in progress.
            m_rxActive = false;
            m_rxData.clear();
        }
        if (onMessage)
            onMessage(d.mid(1, len), functional);
        break;
    }
    case 1: { // first frame
        if (functional || d.size() < 8)
            return true; // segmented messages are not allowed with functional addressing
        const int len = ((pci & 0x0F) << 8) | static_cast<quint8>(d[1]);
        if (len < 8)
            return true;
        if (len > m_config.maxLength) {
            sendFlowControl(2);
            return true;
        }
        m_rxActive = true;
        m_rxLength = len;
        m_rxData = d.mid(2, 6);
        m_rxSn = 1;
        m_rxBlockCount = 0;
        m_rxDeadline = m_now + m_config.nCrMs;
        sendFlowControl(0);
        break;
    }
    case 2: { // consecutive frame
        if (functional || !m_rxActive)
            return true;
        if ((pci & 0x0F) != m_rxSn) {
            fail(Error::WrongSequence);
            return true;
        }
        m_rxSn = (m_rxSn + 1) & 0x0F;
        const int remaining = m_rxLength - static_cast<int>(m_rxData.size());
        m_rxData.append(d.mid(1, std::min(remaining, 7)));
        m_rxDeadline = m_now + m_config.nCrMs;
        if (m_rxData.size() >= m_rxLength) {
            m_rxActive = false;
            const QByteArray message = m_rxData;
            m_rxData.clear();
            if (onMessage)
                onMessage(message, false);
        } else if (m_config.blockSize != 0 && ++m_rxBlockCount >= m_config.blockSize) {
            m_rxBlockCount = 0;
            sendFlowControl(0);
        }
        break;
    }
    case 3: // flow control
        if (!functional)
            handleFlowControl(d);
        break;
    default:
        break;
    }
    return true;
}

void IsoTpLink::tick(qint64 nowMs)
{
    m_now = nowMs;

    if (m_rxActive && nowMs > m_rxDeadline)
        fail(Error::TimeoutConsecutive);

    if (m_tx == TxState::WaitFlowControl && nowMs > m_txDeadline) {
        fail(Error::TimeoutFlowControl);
    } else if (m_tx == TxState::SendConsecutive && nowMs >= m_txNextCf) {
        QByteArray d;
        d.append(static_cast<char>(0x20 | m_txSn));
        d.append(m_txData.mid(m_txOffset, 7));
        m_txOffset += 7;
        m_txSn = (m_txSn + 1) & 0x0F;
        transmit(d);
        if (m_txOffset >= m_txData.size()) {
            m_tx = TxState::Idle;
            m_txData.clear();
        } else if (m_txBlockRemaining > 0 && --m_txBlockRemaining == 0) {
            m_tx = TxState::WaitFlowControl;
            m_txDeadline = nowMs + m_config.nBsMs;
        } else {
            m_txNextCf = nowMs + std::max(1, m_txStMin);
        }
    }
}

} // namespace ecusim
