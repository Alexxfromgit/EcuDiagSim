#include "tester/DiagnosticTester.h"

#include <algorithm>

namespace ecusim {

namespace {
constexpr int kEcuCount = 8; // ISO 15765-4 allows eight physical addresses 0x7E0..0x7E7
const char *kEcuNames[kEcuCount] = {"ECM", "TCM", "ABS", "BCM", "ECU5", "ECU6", "ECU7", "ECU8"};
} // namespace

const DiagResponse *DiagResult::positive() const
{
    for (const DiagResponse &r : responses)
        if (!r.isNegative())
            return &r;
    return nullptr;
}

QString DiagnosticTester::ecuName(quint32 canId)
{
    if (canId == DiagId::Functional)
        return QStringLiteral("ALL");
    const quint32 index = canId >= 0x7E8 ? canId - 0x7E8 : canId - 0x7E0;
    return index < kEcuCount ? QString::fromLatin1(kEcuNames[index]) : QStringLiteral("?");
}

quint32 DiagnosticTester::requestIdForEcu(const QString &name)
{
    if (name.compare(QLatin1String("ALL"), Qt::CaseInsensitive) == 0
        || name.compare(QLatin1String("functional"), Qt::CaseInsensitive) == 0)
        return DiagId::Functional;
    for (quint32 i = 0; i < kEcuCount; ++i)
        if (name.compare(QLatin1String(kEcuNames[i]), Qt::CaseInsensitive) == 0)
            return 0x7E0 + i;
    return 0;
}

DiagnosticTester::DiagnosticTester(VirtualCanBus &bus)
    : m_bus(bus)
{
    m_bus.attach(this);
    for (quint32 i = 0; i < kEcuCount; ++i) {
        IsoTpLink::Config cfg;
        cfg.txId = 0x7E0 + i;
        cfg.rxId = 0x7E8 + i;
        cfg.padByte = 0x55;
        auto link = std::make_unique<IsoTpLink>(cfg);
        link->sendFrame = [this](const QCanBusFrame &frame) { m_bus.send(this, frame); };
        link->onMessage = [this, id = cfg.rxId](const QByteArray &payload, bool) {
            if (!m_active)
                return;
            // "Response pending": the ECU needs more time, keep waiting for the real answer.
            if (payload.size() >= 3 && static_cast<quint8>(payload[0]) == 0x7F
                && static_cast<quint8>(payload[2]) == Nrc::ResponsePending) {
                m_deadline = m_now + kP2ExtendedMs;
                return;
            }
            m_result.responses.append({id, ecuName(id), payload});
            if (m_job.requestId != DiagId::Functional)
                finish();
        };
        link->onError = [this](IsoTpLink::Error error) {
            if (m_active)
                m_result.transportError = IsoTpLink::errorText(error);
        };
        m_links.push_back(std::move(link));
    }
}

DiagnosticTester::~DiagnosticTester()
{
    m_bus.detach(this);
}

IsoTpLink *DiagnosticTester::linkForResponse(quint32 responseId)
{
    if (responseId < 0x7E8 || responseId >= 0x7E8 + kEcuCount)
        return nullptr;
    return m_links[responseId - 0x7E8].get();
}

void DiagnosticTester::onFrame(const QCanBusFrame &frame)
{
    if (IsoTpLink *link = linkForResponse(frame.frameId()))
        link->handleFrame(frame);
}

void DiagnosticTester::request(quint32 requestId, const QByteArray &data, Callback callback, bool expectResponse,
    bool background)
{
    m_queue.push_back({requestId, data, std::move(callback), expectResponse, background});
}

void DiagnosticTester::setKeepAlive(quint32 requestId, bool enabled)
{
    if (enabled)
        m_keepAlive.insert(requestId);
    else
        m_keepAlive.erase(requestId);
}

void DiagnosticTester::start(Job job)
{
    m_job = std::move(job);
    m_result = {};
    m_result.requestId = m_job.requestId;
    m_result.request = m_job.data;
    m_result.responseExpected = m_job.expectResponse;
    m_result.background = m_job.background;
    m_active = true;
    m_deadline = m_now + kP2Ms;

    if (m_job.requestId == DiagId::Functional) {
        // Functional requests must fit into a single frame.
        if (m_job.data.size() > 7) {
            m_result.transportError = QStringLiteral("functional request longer than 7 bytes");
            finish();
            return;
        }
        QByteArray d;
        d.append(static_cast<char>(m_job.data.size()));
        d.append(m_job.data);
        d.append(8 - d.size(), static_cast<char>(0x55));
        m_bus.send(this, QCanBusFrame(DiagId::Functional, d));
    } else {
        IsoTpLink *link = linkForResponse(m_job.requestId + DiagId::ResponseOffset);
        if (!link || !link->send(m_job.data)) {
            m_result.transportError = QStringLiteral("cannot send request");
            finish();
            return;
        }
    }
    if (!m_job.expectResponse)
        finish();
}

void DiagnosticTester::finish()
{
    m_active = false;
    const DiagResult result = m_result;
    const Callback callback = m_job.callback;
    if (onResult)
        onResult(result);
    if (callback)
        callback(result);
}

void DiagnosticTester::tick(qint64 nowMs)
{
    m_now = nowMs;
    for (auto &link : m_links)
        link->tick(nowMs);

    if (m_active) {
        bool transferring = false;
        for (auto &link : m_links)
            transferring = transferring || link->isSending() || link->isReceiving();
        if (transferring)
            m_deadline = std::max(m_deadline, nowMs + kP2Ms); // P2 restarts when the transfer ends
        else if (nowMs >= m_deadline)
            finish();
    }

    if (!m_active && m_queue.empty() && !m_keepAlive.empty() && nowMs - m_lastKeepAlive >= kTesterPresentMs) {
        m_lastKeepAlive = nowMs;
        for (quint32 id : m_keepAlive)
            m_queue.push_back({id, QByteArray::fromHex("3E80"), {}, false, true});
    }

    if (!m_active && !m_queue.empty()) {
        Job job = std::move(m_queue.front());
        m_queue.pop_front();
        start(std::move(job));
    }
}

} // namespace ecusim
