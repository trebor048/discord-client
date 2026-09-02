#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include "Snowflake.hpp"

namespace Acheron {
namespace Core {
namespace TokenUtils {

static Snowflake getIdAndCheckToken(const QString &token)
{
    auto parts = token.split('.');
    if (parts.size() < 3)
        return Snowflake::Invalid;

    auto decodeB64Url = [](QByteArray d) {
        // Discord tokens use base64url without padding; fromBase64 with
        // Base64UrlEncoding handles '-'/'_' but still needs padding.
        while (d.size() % 4 != 0) d.append('=');
        return QByteArray::fromBase64(d, QByteArray::Base64UrlEncoding);
    };
    QByteArray decoded0 = decodeB64Url(parts[0].toUtf8());

    bool ok;
    Snowflake id = decoded0.toULongLong(&ok);

    if (!ok) {
        // spacebar
        QByteArray decoded1 = decodeB64Url(parts[1].toUtf8());
        QJsonParseError err{};
        QJsonDocument doc = QJsonDocument::fromJson(decoded1, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            return Snowflake::Invalid;
        id = doc.object()["id"].toVariant().toULongLong(&ok);
    }

    return ok ? id : Snowflake::Invalid;
}

} // namespace TokenUtils
} // namespace Core
} // namespace Acheron
