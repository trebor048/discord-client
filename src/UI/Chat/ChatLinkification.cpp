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
        // Strip trailing punctuation, but preserve balanced closing brackets so
        // a legitimately bracketed URL like "wiki/HTTP_(disambiguation)" or
        // "docs/[foo]" is not truncated.
        while (!url.isEmpty()) {
            const QChar c = url.back();
            const auto unbalanced = [&url](QChar open, QChar close) {
                return url.count(open) < url.count(close);
            };
            if (c == QLatin1Char(')') && unbalanced(QLatin1Char('('), QLatin1Char(')')))
                url.chop(1);
            else if (c == QLatin1Char(']') && unbalanced(QLatin1Char('['), QLatin1Char(']')))
                url.chop(1);
            else if (c == QLatin1Char('}') && unbalanced(QLatin1Char('{'), QLatin1Char('}')))
                url.chop(1);
            else if (QStringLiteral(",.;:!?").contains(c))
                url.chop(1);
            else
                break;
        }
        result.append(url);
    }
    return result;
}

} // namespace Acheron::UI::ChatLayout
