#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

namespace Acheron {
namespace Core {

struct PendingAttachment
{
    QString filePath;
    QImage image; // pasted
    QByteArray data;
    QString filename;
    QString description;
    QString mimeType;
    qint64 size = 0;
    bool isSpoiler = false;
};

} // namespace Core
} // namespace Acheron
