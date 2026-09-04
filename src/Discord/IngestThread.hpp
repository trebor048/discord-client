#pragma once

#include <QObject>
#include <QtZlib/zlib.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace Acheron {
namespace Discord {

class IngestThread : public QObject
{
    Q_OBJECT
public:
    explicit IngestThread(QObject *parent = nullptr);
    ~IngestThread() override;

    void start();
    void stop();

    void push(const QByteArray &data);
    void reset();

signals:
    void payloadReceived(QJsonObject root);
    void heartbeatAckReceived();
    void decompressionError();

private:
    struct TaggedData
    {
        QByteArray data;
        uint64_t generation;
    };

    void threadLoop();

private:
    std::thread thread;
    std::atomic<bool> running = false;

    std::mutex mutex;
    std::condition_variable cv;
    // Serializes stop() so a UI-thread stop (Gateway::stop) and the network
    // thread's final cleanup (networkLoop tail) can't both join the same
    // std::thread concurrently.
    std::mutex joinMutex;
    std::deque<TaggedData> queue;
    size_t queuedBytes = 0;
    // Backpressure cap: if the consumer falls this far behind, drop oldest
    // chunks rather than grow memory unboundedly.
    static constexpr size_t maxQueuedBytes = 4 * 1024 * 1024;
    static constexpr size_t maxQueuedMessages = 1000;
    // Accumulated compressed input not yet delimited by a 00 00 FF FF marker —
    // a websocket frame can carry several payloads or end mid-payload, so the
    // compressed bytes are buffered here and split on the marker (which lives
    // in the compressed stream, never in the decompressed output).
    QByteArray pendingInput;
    // Cap for the compressed-but-not-yet-delimited buffer; beyond this the
    // stream is assumed broken and a reconnect is forced.
    static constexpr int kMaxBufferedBytes = 16 * 1024 * 1024;

    z_stream stream;
    bool streamActive = false;
    std::atomic<uint64_t> generation{ 0 };
};

} // namespace Discord
} // namespace Acheron
