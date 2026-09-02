#pragma once

#include <QObject>
#include <QJsonArray>
#include <QList>
#include <QPair>
#include <QTimer>

#include <curl/curl.h>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "CaptchaResolver.hpp"

namespace Acheron {
namespace Discord {

class ClientIdentity;
class RequestWorker;
struct RequestDescriptor;

struct HttpResponse
{
    int statusCode = 0;
    QByteArray body;
    QString error;
    bool success = false;
    int retryAfterMs = 0; // populated on HTTP 429
};

struct FileUpload
{
    QString filename;
    QByteArray data;
    QString mimeType;
};

using HttpCallback = std::function<void(const HttpResponse &)>;

class HttpClient : public QObject
{
    Q_OBJECT
public:
    explicit HttpClient(const QString &baseUrl, const QString &token, ClientIdentity &identity,
                        CaptchaResolver *captchaResolver = nullptr, QObject *parent = nullptr);
    ~HttpClient() override;

    enum class Method {
        GET,
        POST,
        PUT,
        PATCH,
        DELETE_, // thanks windows.h for the DELETE macro
    };

    void get(const QString &endpoint, const QUrlQuery &query, HttpCallback callback);
    void post(const QString &endpoint, const QJsonObject &body, HttpCallback callback);
    void patch(const QString &endpoint, const QJsonObject &body, HttpCallback callback);
    void patch(const QString &endpoint, const QJsonArray &body, HttpCallback callback);
    void patch(const QString &endpoint, const QJsonObject &body,
               const QList<QPair<QString, QString>> &headers, HttpCallback callback);
    void put(const QString &endpoint, const QJsonObject &body, HttpCallback callback);
    void put(const QString &endpoint, const QJsonObject &body,
             const QList<QPair<QString, QString>> &headers, HttpCallback callback);
    void delete_(const QString &endpoint, HttpCallback callback);
    void delete_(const QString &endpoint, const QJsonObject &body, HttpCallback callback);
    void delete_(const QString &endpoint, const QList<QPair<QString, QString>> &headers,
                 HttpCallback callback);
    void postMultipart(const QString &endpoint, const QJsonObject &jsonPayload,
                       const QList<FileUpload> &files, HttpCallback callback);
    // for gcp uploads
    void putExternalFile(const QString &absoluteUrl, const QString &filePath,
                         const QString &contentType, HttpCallback callback,
                         std::function<void(qint64 sent, qint64 total)> progress = {},
                         std::shared_ptr<std::atomic<bool>> cancelFlag = {});
    void putExternal(const QString &absoluteUrl, const QByteArray &data,
                     const QString &contentType, HttpCallback callback,
                     std::function<void(qint64 sent, qint64 total)> progress = {},
                     std::shared_ptr<std::atomic<bool>> cancelFlag = {});

private:
    void executeRequest(Method method, const QString &url, const QByteArray &data,
                        const QList<QPair<QString, QString>> &headers, HttpCallback callback);
    void executeRequest(Method method, const QString &url, const QByteArray &data,
                        HttpCallback callback);
    void executeMultipartRequest(const QString &url, const QByteArray &jsonData,
                                 const QList<FileUpload> &files, HttpCallback callback);
    void submitExternalPut(RequestDescriptor &descriptor,
                           std::function<void(qint64, qint64)> progress);

    void onRequestComplete(RequestDescriptor descriptor, HttpResponse response,
                           std::optional<CaptchaChallenge> challenge);

    // 429-retry descriptors parked in single-shot timers. A plain
    // QTimer::singleShot(retryAfterMs, this, ...) silently drops the descriptor
    // (and thus the caller's callback) if this client is destroyed before the
    // timer fires — the worker's shutdown drain never sees it. Track them so
    // the destructor can fail them back to their callbacks. The descriptor is
    // heap-allocated because RequestDescriptor is only forward-declared here,
    // and the vector holds move-only entries (unique_ptr).
    struct PendingRetry
    {
        QTimer *timer = nullptr;
        std::unique_ptr<RequestDescriptor> descriptor;
    };
    std::vector<PendingRetry> m_retryTimers;

    QString baseUrl;
    QString token;
    ClientIdentity &identity;
    CaptchaResolver *captchaResolver;

    std::unique_ptr<RequestWorker> worker;

    friend class RequestWorker;
};

} // namespace Discord
} // namespace Acheron
