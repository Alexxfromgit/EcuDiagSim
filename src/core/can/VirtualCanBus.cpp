#include "can/VirtualCanBus.h"

#include <algorithm>
#include <numeric>

namespace ecusim {

namespace {
constexpr qint64 kWindowUs = 100'000;
constexpr int kWindowCount = 10;
} // namespace

VirtualCanBus::VirtualCanBus(int bitrate)
    : m_bitrate(bitrate)
{
}

void VirtualCanBus::attach(CanNode *node)
{
    if (std::find(m_nodes.begin(), m_nodes.end(), node) == m_nodes.end())
        m_nodes.push_back(node);
}

void VirtualCanBus::detach(CanNode *node)
{
    std::erase(m_nodes, node);
    std::erase_if(m_pending, [node](const Pending &p) { return p.sender == node; });
}

void VirtualCanBus::addListener(Listener listener)
{
    m_listeners.push_back(std::move(listener));
}

bool VirtualCanBus::send(CanNode *sender, const QCanBusFrame &frame)
{
    if (sender && !sender->isOnline())
        return false;
    m_pending.push_back({frame, sender, m_sequence++});
    return true;
}

void VirtualCanBus::setBusFailure(bool failed)
{
    m_busFailure = failed;
    if (failed)
        m_pending.clear();
}

int VirtualCanBus::frameBits(const QCanBusFrame &frame)
{
    const int dataBits = 8 * static_cast<int>(frame.payload().size());
    const bool extended = frame.hasExtendedFrameFormat();
    // SOF + arbitration + control + CRC + ACK + EOF + 3 bit intermission
    const int fixedBits = extended ? 67 : 47;
    // Stuff bits apply to the first 34 (54) bits plus data; assume half of the worst case.
    const int stuffable = (extended ? 54 : 34) + dataBits;
    return fixedBits + dataBits + (stuffable - 1) / 8;
}

void VirtualCanBus::tick(qint64 tickStartUs, qint64 tickLenUs)
{
    const qint64 tickEndUs = tickStartUs + tickLenUs;

    while (tickStartUs - m_windowStartUs >= kWindowUs) {
        m_windows.push_back(m_windowBits);
        if (static_cast<int>(m_windows.size()) > kWindowCount)
            m_windows.pop_front();
        m_windowBits = 0;
        m_windowStartUs += kWindowUs;
    }

    if (m_busFailure) {
        m_pending.clear();
        return;
    }

    m_busFreeAtUs = std::max(m_busFreeAtUs, tickStartUs);
    while (!m_pending.empty() && m_busFreeAtUs < tickEndUs) {
        // Arbitration: lowest identifier wins, FIFO among equal identifiers.
        auto winner = std::min_element(m_pending.begin(), m_pending.end(),
            [](const Pending &a, const Pending &b) {
                if (a.frame.frameId() != b.frame.frameId())
                    return a.frame.frameId() < b.frame.frameId();
                return a.sequence < b.sequence;
            });
        Pending tx = *winner;
        m_pending.erase(winner);

        if (tx.sender && !tx.sender->isOnline())
            continue; // node dropped off the bus while the frame was queued

        const int bits = frameBits(tx.frame);
        m_busFreeAtUs += static_cast<qint64>(bits) * 1'000'000 / m_bitrate;
        m_windowBits += bits;
        ++m_frameCount;

        BusFrame out;
        out.timeUs = m_busFreeAtUs;
        out.frame = tx.frame;
        out.frame.setTimeStamp(QCanBusFrame::TimeStamp::fromMicroSeconds(out.timeUs));
        out.sender = tx.sender ? tx.sender->nodeName() : QStringLiteral("?");

        for (const auto &listener : m_listeners)
            listener(out);
        // Copy: a receiver may attach/detach nodes or queue new frames while handling this one.
        const std::vector<CanNode *> receivers = m_nodes;
        for (CanNode *node : receivers) {
            if (node != tx.sender && node->isOnline())
                node->onFrame(out.frame);
        }
    }
}

double VirtualCanBus::busLoad() const
{
    if (m_windows.empty())
        return 0.0;
    const qint64 bits = std::accumulate(m_windows.begin(), m_windows.end(), qint64{0});
    const double seconds = static_cast<double>(m_windows.size()) * kWindowUs / 1e6;
    return static_cast<double>(bits) / (m_bitrate * seconds);
}

} // namespace ecusim
