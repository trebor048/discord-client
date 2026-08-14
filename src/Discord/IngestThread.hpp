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
    std::deque<TaggedData> queue;
    size_t queuedBytes = 0;
    // Backpressure cap: if the consumer falls this far behind, drop oldest
    // chunks rather than grow memory unboundedly.
    static constexpr size_t maxQueuedBytes = 4 * 1024 * 1024;
    static constexpr size_t maxQueuedMessages = 1000;
    QByteArray decompressedBuffer;

    z_stream stream;
    bool streamActive = false;
    std::atomic<uint64_t> generation{ 0 };
};

} // namespace Discord
} // namespace Acheron
