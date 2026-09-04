#include "HttpClient.hpp"
#include "RequestWorker.hpp"

#include "Core/NetUtils.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>

namespace Acheron {
namespace Discord {

HttpClient::HttpClient(const QString &baseUrl, const QString &token, ClientIdentity &identity,
                       CaptchaResolver *captchaResolver, QObject *parent)
    : QObject(parent), baseUrl(baseUrl), token(token), identity(identity), captchaResolver(captchaResolver)
{
    worker = std::make_unique<RequestWorker>(this, token, identity, captchaResolver);
}

HttpClient::~HttpClient()
{
    // Fail any 429-retry descriptors still parked in their timers so their
    // callbacks are never left hanging (the timer's context-object teardown
    // would otherwise drop them silently).
    for (auto &pending : m_retryTimers) {
        if (pending.timer)
            pending.timer->stop();
        if (pending.descriptor && pending.descriptor->callback) {
            HttpResponse response;
            response.success = false;
            response.statusCode = 0;
            response.error = QStringLiteral("Request aborted: client is shutting down");
            pending.descriptor->callback(response);
        }
    }
    m_retryTimers.clear();

    if (worker)
        worker->shutdown();
}

void HttpClient::get(const QString &endpoint, const QUrlQuery &query, HttpCallback callback)
{
    QString url = baseUrl + endpoint;
    if (!query.isEmpty())
        url += "?" + query.toString(QUrl::FullyEncoded);
    executeRequest(Method::GET, url, {}, callback);
}

void HttpClient::post(const QString &endpoint, const QJsonObject &body, HttpCallback callback)
{
    QString url = baseUrl + endpoint;
    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    executeRequest(Method::POST, url, data, callback);
}

void HttpClient::patch(const QString &endpoint, const QJsonObject &body, HttpCallback callback)
{
    QString url = baseUrl + endpoint;
    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    executeRequest(Method::PATCH, url, data, callback);
}

void HttpClient::patch(const QString &endpoint, const QJsonArray &body, HttpCallback callback)
{
    QString url = baseUrl + endpoint;
    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    executeRequest(Method::PATCH, url, data, callback);
}

void HttpClient::patch(const QString &endpoint, const QJsonObject &body,
                       const QList<QPair<QString, QString>> &headers, HttpCallback callback)
{
    QString url = baseUrl + endpoint;
    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    executeRequest(Method::PATCH, url, data, headers, callback);
}

void HttpClient::put(const QString &endpoint, const QJsonObject &body, HttpCallback callback)
{
    QString url = baseUrl + endpoint;
    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    executeRequest(Method::PUT, url, data, callback);
}

void HttpClient::put(const QString &endpoint, const QJsonObject &body,
                     const QList<QPair<QString, QString>> &headers, HttpCallback callback)
{
    QString url = baseUrl + endpoint;
    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    executeRequest(Method::PUT, url, data, headers, callback);
}

void HttpClient::delete_(const QString &endpoint, HttpCallback callback)
{
    QString url = baseUrl + endpoint;
    executeRequest(Method::DELETE_, url, {}, callback);
}

void HttpClient::delete_(const QString &endpoint, const QJsonObject &body, HttpCallback callback)
{
    QString url = baseUrl + endpoint;
    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    executeRequest(Method::DELETE_, url, data, callback);
}

void HttpClient::delete_(const QString &endpoint, const QList<QPair<QString, QString>> &headers,
                         HttpCallback callback)
{
    QString url = baseUrl + endpoint;
    executeRequest(Method::DELETE_, url, {}, headers, callback);
}

void HttpClient::postMultipart(const QString &endpoint, const QJsonObject &jsonPayload,
                               const QList<FileUpload> &files, HttpCallback callback)
{
    QString url = baseUrl + endpoint;
    QByteArray jsonData = QJsonDocument(jsonPayload).toJson(QJsonDocument::Compact);
    executeMultipartRequest(url, jsonData, files, callback);
}

void HttpClient::putExternalFile(const QString &absoluteUrl, const QString &filePath,
                                 const QString &contentType, HttpCallback callback,
                                 std::function<void(qint64, qint64)> progress,
                                 std::shared_ptr<std::atomic<bool>> cancelFlag)
{
    QUrl u(absoluteUrl);
    if (u.scheme() != QStringLiteral("https") || u.host().isEmpty()) {
        if (callback) { HttpResponse r; r.success=false; r.statusCode=0; r.error=QStringLiteral("invalid external URL: wrong scheme"); callback(r); }
        return;
    }
    // SSRF: block private/loopback/ULA even if scheme is https (shared guard).
    if (Core::NetUtils::isPrivateHost(u.host())) {
        if (callback) { HttpResponse r; r.success=false; r.statusCode=0; r.error=QStringLiteral("blocked private host"); callback(r); }
        return;
    }
    RequestDescriptor descriptor;
    descriptor.method = Method::PUT;
    descriptor.url = absoluteUrl.toStdString();
    descriptor.uploadFilePath = filePath;
    descriptor.external = true;
    descriptor.contentType = contentType;
    descriptor.callback = std::move(callback);
    descriptor.cancelFlag = std::move(cancelFlag);
    submitExternalPut(descriptor, std::move(progress));
}

void HttpClient::putExternal(const QString &absoluteUrl, const QByteArray &data,
                             const QString &contentType, HttpCallback callback,
                             std::function<void(qint64, qint64)> progress,
                             std::shared_ptr<std::atomic<bool>> cancelFlag)
{
    QUrl u(absoluteUrl);
    if (u.scheme() != QStringLiteral("https") || u.host().isEmpty()) {
        if (callback) { HttpResponse r; r.success=false; r.statusCode=0; r.error=QStringLiteral("invalid external URL: wrong scheme"); callback(r); }
        return;
    }
    if (Core::NetUtils::isPrivateHost(u.host())) {
        if (callback) { HttpResponse r; r.success=false; r.statusCode=0; r.error=QStringLiteral("blocked private host"); callback(r); }
        return;
    }
    RequestDescriptor descriptor;
    descriptor.method = Method::PUT;
    descriptor.url = absoluteUrl.toStdString();
    descriptor.body = data;
    descriptor.external = true;
    descriptor.contentType = contentType;
    descriptor.callback = std::move(callback);
    descriptor.cancelFlag = std::move(cancelFlag);
    submitExternalPut(descriptor, std::move(progress));
}

void HttpClient::submitExternalPut(RequestDescriptor &descriptor,
                                   std::function<void(qint64, qint64)> progress)
{
    if (progress) {
        QPointer<HttpClient> guard(this);
        descriptor.progressCallback = [this, guard,
                                       progress = std::move(progress)](qint64 sent, qint64 total) {
            QMetaObject::invokeMethod(this, [guard, progress, sent, total]() {
                if (guard)
                    progress(sent, total);
            });
        };
    }
    worker->submit(std::move(descriptor));
}

void HttpClient::executeRequest(Method method, const QString &url, const QByteArray &data,
                                const QList<QPair<QString, QString>> &headers,
                                HttpCallback callback)
{
    RequestDescriptor descriptor;
    descriptor.method = method;
    descriptor.url = url.toStdString();
    descriptor.body = data;
    descriptor.multipart = false;
    descriptor.extraHeaders = headers;
    descriptor.callback = std::move(callback);
    worker->submit(std::move(descriptor));
}

void HttpClient::executeRequest(Method method, const QString &url, const QByteArray &data,
                                HttpCallback callback)
{
    executeRequest(method, url, data, {}, std::move(callback));
}

void HttpClient::executeMultipartRequest(const QString &url, const QByteArray &jsonData,
                                         const QList<FileUpload> &files, HttpCallback callback)
{
    RequestDescriptor descriptor;
    descriptor.method = Method::POST;
    descriptor.url = url.toStdString();
    descriptor.body = jsonData;
    descriptor.multipart = true;
    descriptor.files = files;
    descriptor.callback = std::move(callback);
    worker->submit(std::move(descriptor));
}

void HttpClient::onRequestComplete(RequestDescriptor descriptor, HttpResponse response,
                                   std::optional<CaptchaChallenge> challenge)
{
    // Retry on HTTP 429 (rate limit) after the specified delay, but cap the
    // attempts so a persistent rate limit can't retry forever (which would
    // leave the caller's callback never invoked).
    constexpr int kMaxRateLimitRetries = 3;
    if (response.statusCode == 429 && response.retryAfterMs > 0 &&
        descriptor.retryCount < kMaxRateLimitRetries && !descriptor.external) {
        descriptor.retryCount += 1;

        // Track the parked descriptor so ~HttpClient can still fail it back to
        // its callback (see m_retryTimers). The timer is parented to this so
        // Qt cleans it up on destruction after we've dispatched. The lambda
        // captures its own copy: the parked one may be moved-from by the
        // destructor's failure dispatch on the shutdown path.
        auto *timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(response.retryAfterMs);
        const QPointer<HttpClient> self(this);
        auto parked = std::make_unique<RequestDescriptor>(std::move(descriptor));
        RequestDescriptor forTimer = *parked;
        connect(timer, &QTimer::timeout, this, [this, self, timer,
                                                forTimer = std::move(forTimer)]() mutable {
            if (!self)
                return;
            for (auto it = m_retryTimers.begin(); it != m_retryTimers.end(); ++it) {
                if (it->timer == timer) {
                    m_retryTimers.erase(it);
                    break;
                }
            }
            worker->submit(std::move(forTimer));
            // The timer is single-shot and its entry was erased above; without
            // this it stays alive (parented to the client) until destruction,
            // so a session full of rate-limited requests leaks one QTimer each.
            timer->deleteLater();
        });
        m_retryTimers.push_back({ timer, std::move(parked) });
        timer->start();
        return;
    }

    if (!challenge || descriptor.captchaAttempt >= kMaxCaptchaAttempts || !captchaResolver) {
        if (captchaResolver && descriptor.captchaAttempt > 0)
            captchaResolver->notifyConcluded();
        if (descriptor.callback)
            descriptor.callback(response);
        return;
    }

    QPointer<HttpClient> self(this);
    captchaResolver->resolve(*challenge, [this, self,
                                          descriptor = std::move(descriptor),
                                          response = std::move(response)](std::optional<CaptchaSolution> s) mutable {
        if (!self)
            return;
        if (!s) {
            HttpResponse err = response;
            err.success = false;
            if (err.error.isEmpty())
                err.error = "Captcha canceled";
            descriptor.callback(err);
            return;
        }
        descriptor.captchaAttempt += 1;
        descriptor.solution = std::move(s);
        worker->submit(std::move(descriptor));
    });
}

} // namespace Discord
} // namespace Acheron
