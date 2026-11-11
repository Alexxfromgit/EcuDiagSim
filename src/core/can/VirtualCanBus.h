#pragma once

#include <QCanBusFrame>
#include <QString>

#include <deque>
#include <functional>
#include <vector>

namespace ecusim {

/// Anything that can be attached to the virtual bus (ECUs, the scan tool, the log replayer).
class CanNode
{
public:
    virtual ~CanNode() = default;
    virtual QString nodeName() const = 0;
    /// Called for every frame transmitted by another node.
    virtual void onFrame(const QCanBusFrame &frame) = 0;
    /// A node that is unpowered or bus-off neither sends nor receives.
    virtual bool isOnline() const { return true; }
};

/// A frame as it appeared on the wire: simulation time stamp plus the sending node.
struct BusFrame
{
    qint64 timeUs = 0;
    QCanBusFrame frame;
    QString sender;
};

/// In-process classic CAN bus.
///
/// Nodes queue frames with send(); once per simulation tick the bus arbitrates:
/// the pending frame with the lowest identifier wins and occupies the bus for its
/// real bit time at the configured bit rate. Frames that do not fit into the tick
/// stay queued, so bus load and latency behave like on a real 500 kbit/s network.
class VirtualCanBus
{
public:
    using Listener = std::function<void(const BusFrame &)>;

    explicit VirtualCanBus(int bitrate = 500000);

    void attach(CanNode *node);
    void detach(CanNode *node);
    void addListener(Listener listener);

    /// Queues a frame for transmission. Returns false if the sender is offline.
    bool send(CanNode *sender, const QCanBusFrame &frame);

    /// Runs arbitration for the time slice [tickStartUs, tickStartUs + tickLenUs).
    void tick(qint64 tickStartUs, qint64 tickLenUs);

    /// Simulates a physical bus failure (e.g. CAN-H shorted to CAN-L): nothing gets through.
    void setBusFailure(bool failed);
    bool busFailure() const { return m_busFailure; }

    int bitrate() const { return m_bitrate; }
    /// Bus load over the last second, 0..1.
    double busLoad() const;
    quint64 frameCount() const { return m_frameCount; }
    int pendingCount() const { return static_cast<int>(m_pending.size()); }

    /// Approximate on-wire length of a frame in bits (incl. average bit stuffing and interframe space).
    static int frameBits(const QCanBusFrame &frame);

private:
    struct Pending
    {
        QCanBusFrame frame;
        CanNode *sender = nullptr;
        quint64 sequence = 0;
    };

    int m_bitrate;
    bool m_busFailure = false;
    std::vector<CanNode *> m_nodes;
    std::vector<Listener> m_listeners;
    std::vector<Pending> m_pending;
    quint64 m_sequence = 0;
    quint64 m_frameCount = 0;
    qint64 m_busFreeAtUs = 0;

    // bus load: bits per 100 ms window, last 10 windows
    qint64 m_windowStartUs = 0;
    qint64 m_windowBits = 0;
    std::deque<qint64> m_windows;
};

} // namespace ecusim
