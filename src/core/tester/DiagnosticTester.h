#pragma once

#include "can/VirtualCanBus.h"
#include "diag/IsoTp.h"
#include "diag/UdsDefs.h"

#include <QList>

#include <deque>
#include <functional>
#include <memory>
#include <set>
#include <vector>

namespace ecusim {

struct DiagResponse
{
    quint32 canId = 0;
    QString ecu;
    QByteArray data;

    bool isNegative() const { return data.size() >= 3 && static_cast<quint8>(data[0]) == 0x7F; }
    quint8 nrc() const { return isNegative() ? static_cast<quint8>(data[2]) : 0; }
};

struct DiagResult
{
    quint32 requestId = 0;
    QByteArray request;
    QList<DiagResponse> responses;
    QString transportError;
    bool responseExpected = true;
    bool background = false; ///< polling / keep-alive traffic a UI may leave out of its log

    bool timedOut() const { return responseExpected && responses.isEmpty(); }
    /// The first positive response, or nullptr.
    const DiagResponse *positive() const;
    const DiagResponse *first() const { return responses.isEmpty() ? nullptr : &responses.first(); }
};

/// The scan tool plugged into the OBD connector: an ISO-TP client that sends requests
/// with physical (0x7E0..0x7E7) or functional (0x7DF) addressing and collects responses.
/// Requests are queued and processed one at a time, as P2 timing requires.
class DiagnosticTester : public CanNode
{
public:
    using Callback = std::function<void(const DiagResult &)>;

    static constexpr int kP2Ms = 150;          ///< wait for a response
    static constexpr int kP2ExtendedMs = 5000; ///< after "response pending" (NRC 0x78)
    static constexpr int kTesterPresentMs = 2000;

    explicit DiagnosticTester(VirtualCanBus &bus);
    ~DiagnosticTester() override;

    QString nodeName() const override { return QStringLiteral("Tester"); }
    void onFrame(const QCanBusFrame &frame) override;
    void tick(qint64 nowMs);

    /// Queues a request. requestId is DiagId::Functional or a physical request ID.
    void request(quint32 requestId, const QByteArray &data, Callback callback = {}, bool expectResponse = true,
        bool background = false);
    bool busy() const { return m_active || !m_queue.empty(); }

    /// Sends TesterPresent (3E 80, no response) periodically to keep a session alive.
    void setKeepAlive(quint32 requestId, bool enabled);
    bool keepAlive(quint32 requestId) const { return m_keepAlive.count(requestId) > 0; }

    /// Observer for every completed exchange (the UI log hooks in here).
    std::function<void(const DiagResult &)> onResult;

    static QString ecuName(quint32 canId);
    static quint32 requestIdForEcu(const QString &name);

private:
    struct Job
    {
        quint32 requestId = 0;
        QByteArray data;
        Callback callback;
        bool expectResponse = true;
        bool background = false;
    };

    IsoTpLink *linkForResponse(quint32 responseId);
    void start(Job job);
    void finish();

    VirtualCanBus &m_bus;
    std::vector<std::unique_ptr<IsoTpLink>> m_links;
    std::deque<Job> m_queue;
    bool m_active = false;
    Job m_job;
    DiagResult m_result;
    qint64 m_now = 0;
    qint64 m_deadline = 0;
    qint64 m_lastKeepAlive = 0;
    std::set<quint32> m_keepAlive; ///< ordered: TesterPresent goes out in ID order, every run
};

} // namespace ecusim
