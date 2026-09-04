#include "IngestThread.hpp"

#include "Enums.hpp"
#include "Core/Logging.hpp"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <array>
namespace Acheron {
namespace Discord {

IngestThread::IngestThread(QObject *parent)
    : QObject(parent)
{
    memset(&stream, 0, sizeof(stream));
}

IngestThread::~IngestThread()
{
    stop();
    if (streamActive)
        inflateEnd(&stream);
}

void IngestThread::start()
{
    bool expected = false;
    if (!running.compare_exchange_strong(expected, true)) {
        qCDebug(LogNetwork) << "Attempt to start already running IngestThread";
        return;
    }
    thread = std::thread(&IngestThread::threadLoop, this);
}

void IngestThread::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        running = false;
    }

    cv.notify_one();

    // Two callers can reach here concurrently (Gateway::stop on the UI thread
    // and the network loop's tail on the network thread); std::thread::join
    // must not run twice on the same thread object.
    std::lock_guard<std::mutex> joinLock(joinMutex);
    if (thread.joinable())
        thread.join();
}

void IngestThread::push(const QByteArray &data)
{
    {
        std::lock_guard lock(mutex);
        queue.push_back({ data, generation.load() });
        queuedBytes += data.size();

        // Drop oldest chunks when the queue exceeds its cap. Dropping is safer
        // than blocking the network thread (which would starve the websocket
        // recv loop) — a gap in the compressed stream will surface as a
        // decompression error and trigger a clean reconnect.
        while ((queuedBytes > maxQueuedBytes || queue.size() > maxQueuedMessages) &&
               queue.size() > 1) {
            qCWarning(LogNetwork) << "Ingest queue overflow (" << queuedBytes
                                  << "bytes," << queue.size() << "messages) — dropping oldest chunk";
            queuedBytes -= queue.front().data.size();
            queue.pop_front();
        }
    }

    cv.notify_one();
}

void IngestThread::reset()
{
    // Only touch queue (mutex-protected) and atomic generation.
    // Do NOT touch zlib state — that's owned by the worker thread.
    std::lock_guard lock(mutex);
    generation++;
    queue.clear();
    queuedBytes = 0;
}

void IngestThread::threadLoop()
{
    uint64_t activeGeneration = generation.load();

    while (true) {
        QByteArray data;
        uint64_t dataGen;
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [this] { return !queue.empty() || !running; });
            if (!running)
                break;

            data = queue.front().data;
            dataGen = queue.front().generation;
            queuedBytes -= data.size();
            queue.pop_front();
        }

        // Generation changed — reset zlib state on the worker thread (safe, no race)
        if (dataGen != activeGeneration) {
            if (streamActive) {
                inflateEnd(&stream);
                streamActive = false;
            }
            memset(&stream, 0, sizeof(stream));
            pendingInput.clear();
            activeGeneration = dataGen;
        }

        if (data.isEmpty())
            continue;

        if (!streamActive) {
            if (inflateInit2(&stream, MAX_WBITS + 32) != Z_OK) {
                qCWarning(LogNetwork) << "inflateInit2 failed";
                continue;
            }
            streamActive = true;
        }

        // Discord's zlib-stream: one continuous deflate stream whose payloads
        // are delimited by the 4-byte marker 00 00 FF FF. The marker lives in
        // the COMPRESSED stream — it is a sync-flush empty stored block that
        // inflate() consumes without emitting output — so it never shows up in
        // the decompressed bytes and must be detected on the compressed input.
        // WebSocket frame boundaries do NOT align with payload boundaries: one
        // frame can carry several payloads and one payload can span frames, so
        // accumulate the compressed bytes here and split on the marker as soon
        // as it becomes complete, then inflate each complete segment alone.
        pendingInput.append(data);

        static const QByteArray delimiter("\x00\x00\xff\xff", 4);

        // Scan with a moving cursor instead of remove(0, k) per payload (which
        // memmoved the remaining buffer on every payload) and inflate straight
        // from pendingInput's buffer; the consumed prefix is stripped once per
        // batch below. The marker lives in the COMPRESSED stream — inflate()
        // consumes it as an empty stored block without emitting output — so
        // each segment passed to inflate ends exactly at the marker, exactly
        // as before.
        int cursor = 0;
        int delimIndex = pendingInput.indexOf(delimiter, cursor);
        while (delimIndex != -1) {
            // Everything up to and including the marker is exactly one payload.
            const int segmentEnd = delimIndex + delimiter.size();
            const char *segmentStart = pendingInput.constData() + cursor;
            int segmentSize = segmentEnd - cursor;

            std::array<char, 32768> out;
            QByteArray decompressed;
            // Small payloads (the common case) skip most reallocations; large
            // ones just keep growing past the reservation.
            decompressed.reserve(qMin(segmentSize, 8192));

            int ret = Z_OK;
            do {
                stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(segmentStart));
                stream.avail_in = static_cast<uInt>(segmentSize);
                stream.avail_out = sizeof(out);
                stream.next_out = reinterpret_cast<Bytef *>(out.data());

                ret = inflate(&stream, Z_SYNC_FLUSH);
                // Advance past the input consumed this call. zlib never writes
                // through next_in, so the read-only pendingInput buffer is safe.
                const int consumed = segmentSize - static_cast<int>(stream.avail_in);
                segmentStart += consumed;
                segmentSize = static_cast<int>(stream.avail_in);

                if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
                    qCWarning(LogNetwork) << "inflate failed:" << ret;
                    inflateEnd(&stream);
                    streamActive = false;
                    pendingInput.clear();
                    emit decompressionError();
                    break;
                }

                int have = sizeof(out) - stream.avail_out;
                decompressed.append(out.data(), have);

                if (ret == Z_BUF_ERROR)
                    break; // no progress possible with the current input
            } while (stream.avail_out == 0);

            if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR)
                break; // stream was reset; stop processing this batch

            cursor = segmentEnd;

            // trimmed() would allocate a full copy just to test emptiness —
            // scan in place instead. Whitespace-only payloads are still
            // skipped (QChar::isSpace covers the ASCII whitespace set).
            bool hasContent = false;
            for (int i = 0; i < decompressed.size(); ++i) {
                if (!QChar(static_cast<uchar>(decompressed.at(i))).isSpace()) {
                    hasContent = true;
                    break;
                }
            }
            if (hasContent) {
                QJsonParseError error;
                QJsonDocument doc = QJsonDocument::fromJson(decompressed, &error);
                if (error.error != QJsonParseError::NoError) {
                    qCWarning(LogNetwork) << "Failed to parse messages:" << error.errorString();
                } else if (doc.isObject()) {
                    if (doc.object().value("op").toInt() == static_cast<int>(OpCode::HEARTBEAT_ACK))
                        emit heartbeatAckReceived();
                    emit payloadReceived(doc.object());
                }
            }

            delimIndex = pendingInput.indexOf(delimiter, cursor);
        }

        // Strip the consumed prefix in one pass instead of per payload.
        if (cursor > 0)
            pendingInput.remove(0, cursor);

        // Guard against a runaway buffer if the marker never arrives (stream
        // corrupted or connection died mid-payload).
        if (pendingInput.size() > kMaxBufferedBytes) {
            qCWarning(LogNetwork) << "Compressed buffer overflow — forcing reconnect";
            inflateEnd(&stream);
            streamActive = false;
            pendingInput.clear();
            emit decompressionError();
        }
    }
}

} // namespace Discord
} // namespace Acheron
