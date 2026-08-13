#include "NotificationTypes.hpp"

#include <QCryptographicHash>

namespace Acheron {
namespace Core {
namespace Notification {

QString positionToString(NotificationPosition pos)
{
    switch (pos) {
    case NotificationPosition::TopLeft: return "top-left";
    case NotificationPosition::TopRight: return "top-right";
    case NotificationPosition::BottomLeft: return "bottom-left";
    case NotificationPosition::BottomRight: return "bottom-right";
    case NotificationPosition::Center: return "center";
    }
    return "bottom-left";
}

NotificationPosition stringToPosition(const QString &str)
{
    if (str == "top-left") return NotificationPosition::TopLeft;
    if (str == "top-right") return NotificationPosition::TopRight;
    if (str == "bottom-right") return NotificationPosition::BottomRight;
    if (str == "center") return NotificationPosition::Center;
    return NotificationPosition::BottomLeft;
}

QString streamingTreatmentToString(NotificationSettings::StreamingTreatment t)
{
    switch (t) {
    case NotificationSettings::StreamingTreatment::Normal: return "normal";
    case NotificationSettings::StreamingTreatment::NoContent: return "no-content";
    case NotificationSettings::StreamingTreatment::Ignore: return "ignore";
    }
    return "normal";
}

NotificationSettings::StreamingTreatment stringToStreamingTreatment(const QString &str)
{
    if (str == "no-content") return NotificationSettings::StreamingTreatment::NoContent;
    if (str == "ignore") return NotificationSettings::StreamingTreatment::Ignore;
    return NotificationSettings::StreamingTreatment::Normal;
}

QString nativeModeToString(NotificationSettings::NativeMode m)
{
    switch (m) {
    case NotificationSettings::NativeMode::Never: return "never";
    case NotificationSettings::NativeMode::Always: return "always";
    case NotificationSettings::NativeMode::NotFocused: return "not-focused";
    }
    return "never";
}

NotificationSettings::NativeMode stringToNativeMode(const QString &str)
{
    if (str == "always") return NotificationSettings::NativeMode::Always;
    if (str == "not-focused") return NotificationSettings::NativeMode::NotFocused;
    return NotificationSettings::NativeMode::Never;
}

QColor generateBadgeColor(const QString &id)
{
    // Generate a deterministic color from the ID using a hash
    QByteArray hash = QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Md5);
    quint32 hashValue = *reinterpret_cast<const quint32 *>(hash.constData());

    int hue = hashValue % 360;
    int sat = 55 + ((hashValue >> 8) % 20);
    int light = 48 + ((hashValue >> 16) % 12);

    return QColor::fromHsl(hue, sat, light);
}

} // namespace Notification
} // namespace Core
} // namespace Acheron