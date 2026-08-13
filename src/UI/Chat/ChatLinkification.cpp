#include "ChatLayout.hpp"

#include <QRegularExpression>

namespace Acheron::UI::ChatLayout {

QStringList extractUrls(const QString &text)
{
    static const QRegularExpression urlRe(
        QStringLiteral(R"(https?://[^\s<>"']+)"),
        QRegularExpression::CaseInsensitiveOption);
    QStringList result;
    QRegularExpressionMatchIterator it = urlRe.globalMatch(text);
    while (it.hasNext()) {
        QString url = it.next().captured();
        while (!url.isEmpty() && QStringLiteral(",.;:!?)]}").contains(url.back()))
            url.chop(1);
        result.append(url);
    }
    return result;
}

} // namespace Acheron::UI::ChatLayout
