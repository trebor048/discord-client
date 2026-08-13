#include "CodeHighlighter.hpp"

#include <QChar>
#include <QSet>
#include <QStringList>

namespace Acheron {
namespace Core {
namespace Markdown {

namespace {

enum class Lang {
    Unknown,
    Cpp,
    Python,
    Js,
    Json,
};

Lang parseLanguage(const QString &raw)
{
    const QString lang = raw.trimmed().toLower();
    if (lang.isEmpty())
        return Lang::Unknown;

    if (lang == QLatin1String("cpp") || lang == QLatin1String("c++") ||
        lang == QLatin1String("c") || lang == QLatin1String("cc") ||
        lang == QLatin1String("cxx") || lang == QLatin1String("h") ||
        lang == QLatin1String("hpp") || lang == QLatin1String("hxx"))
        return Lang::Cpp;

    if (lang == QLatin1String("python") || lang == QLatin1String("py"))
        return Lang::Python;

    if (lang == QLatin1String("js") || lang == QLatin1String("javascript") ||
        lang == QLatin1String("jsx") || lang == QLatin1String("ts") ||
        lang == QLatin1String("typescript"))
        return Lang::Js;

    if (lang == QLatin1String("json"))
        return Lang::Json;

    return Lang::Unknown;
}

QSet<QString> makeWordSet(const char *spaceSeparated)
{
    const QStringList words = QString::fromLatin1(spaceSeparated).split(QLatin1Char(' '));
    return QSet<QString>(words.cbegin(), words.cend());
}

bool isKeyword(Lang lang, const QString &word)
{
    switch (lang) {
    case Lang::Cpp: {
        static const QSet<QString> keywords = makeWordSet(
            "for while if else return int float double char bool void class struct enum "
            "namespace const auto new delete public private protected static virtual "
            "template using typedef include define pragma true false nullptr break "
            "continue switch case default try catch throw");
        return keywords.contains(word);
    }
    case Lang::Python: {
        static const QSet<QString> keywords = makeWordSet(
            "def class return if elif else for while import from as with try except "
            "finally pass break continue lambda yield global nonlocal True False None "
            "and or not in is raise assert");
        return keywords.contains(word);
    }
    case Lang::Js: {
        static const QSet<QString> keywords = makeWordSet(
            "function const let var return if else for while class new extends "
            "import export from default async await try catch finally throw typeof "
            "instanceof this null undefined true false switch case break continue");
        return keywords.contains(word);
    }
    case Lang::Json: {
        static const QSet<QString> keywords = makeWordSet("true false null");
        return keywords.contains(word);
    }
    default:
        return false;
    }
}

bool isIdentifierStart(QChar c)
{
    return c.isLetter() || c == QLatin1Char('_');
}

bool isIdentifierPart(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

bool isHexDigit(QChar c)
{
    return c.isDigit() || (c >= QLatin1Char('a') && c <= QLatin1Char('f')) ||
           (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
}

QString spanClass(HighlightKind kind)
{
    switch (kind) {
    case HighlightKind::Keyword:
        return QStringLiteral("code-kw");
    case HighlightKind::String:
        return QStringLiteral("code-str");
    case HighlightKind::Comment:
        return QStringLiteral("code-com");
    case HighlightKind::Number:
        return QStringLiteral("code-num");
    }
    return QStringLiteral("code-plain");
}

} // namespace

bool isHighlightableLanguage(const QString &language)
{
    return parseLanguage(language) != Lang::Unknown;
}

std::vector<HighlightSpan> highlightLine(const QString &language, const QString &line,
                                         HighlightState &state)
{
    const Lang lang = parseLanguage(language);
    std::vector<HighlightSpan> spans;

    const int n = line.size();
    if (lang == Lang::Unknown || n == 0)
        return spans;

    const auto pushSpan = [&spans](int start, int length, HighlightKind kind) {
        if (length > 0)
            spans.push_back({ start, length, kind });
    };

    const bool blockCommentLang = (lang == Lang::Cpp || lang == Lang::Js);
    int i = 0;

    // Resume a construct left open on a previous line.
    if (state.inBlockComment && blockCommentLang) {
        const int end = line.indexOf(QStringLiteral("*/"));
        if (end < 0) {
            pushSpan(0, n, HighlightKind::Comment);
            return spans;
        }
        pushSpan(0, end + 2, HighlightKind::Comment);
        i = end + 2;
        state.inBlockComment = false;
    } else if (lang == Lang::Python && state.tripleQuote != QLatin1Char('\0')) {
        const QString closer = state.tripleQuote == QLatin1Char('\'')
                                       ? QStringLiteral("'''")
                                       : QStringLiteral("\"\"\"");
        const int end = line.indexOf(closer);
        if (end < 0) {
            pushSpan(0, n, HighlightKind::String);
            return spans;
        }
        pushSpan(0, end + 3, HighlightKind::String);
        i = end + 3;
        state.tripleQuote = QLatin1Char('\0');
    }

    while (i < n) {
        const QChar c = line.at(i);

        // Line comments.
        if (blockCommentLang && c == QLatin1Char('/') && i + 1 < n &&
            line.at(i + 1) == QLatin1Char('/')) {
            pushSpan(i, n - i, HighlightKind::Comment);
            break;
        }
        if (lang == Lang::Python && c == QLatin1Char('#')) {
            pushSpan(i, n - i, HighlightKind::Comment);
            break;
        }

        // Block comments (cpp/js only).
        if (blockCommentLang && c == QLatin1Char('/') && i + 1 < n &&
            line.at(i + 1) == QLatin1Char('*')) {
            const int end = line.indexOf(QStringLiteral("*/"), i + 2);
            if (end < 0) {
                pushSpan(i, n - i, HighlightKind::Comment);
                state.inBlockComment = true;
                break;
            }
            pushSpan(i, end + 2 - i, HighlightKind::Comment);
            i = end + 2;
            continue;
        }

        // Strings. Python additionally supports triple-quoted (multi-line) strings.
        const bool isQuote =
            (c == QLatin1Char('"')) || (c == QLatin1Char('\'') && lang != Lang::Json);
        if (isQuote) {
            if (lang == Lang::Python && i + 2 < n && line.at(i + 1) == c &&
                line.at(i + 2) == c) {
                const QString closer = QString(c).repeated(3);
                const int end = line.indexOf(closer, i + 3);
                if (end < 0) {
                    pushSpan(i, n - i, HighlightKind::String);
                    state.tripleQuote = c;
                    break;
                }
                pushSpan(i, end + 3 - i, HighlightKind::String);
                i = end + 3;
                continue;
            }

            int end = i + 1;
            while (end < n) {
                const QChar sc = line.at(end);
                if (sc == QLatin1Char('\\')) {
                    end += 2;
                    continue;
                }
                if (sc == c) {
                    ++end;
                    break;
                }
                ++end;
            }
            pushSpan(i, end - i, HighlightKind::String);
            i = end;
            continue;
        }

        // Numbers.
        if (c.isDigit() || (c == QLatin1Char('.') && i + 1 < n && line.at(i + 1).isDigit())) {
            int end = i + 1;
            if (line.at(i) == QLatin1Char('0') && end < n &&
                (line.at(end) == QLatin1Char('x') || line.at(end) == QLatin1Char('X') ||
                 line.at(end) == QLatin1Char('b') || line.at(end) == QLatin1Char('B') ||
                 line.at(end) == QLatin1Char('o') || line.at(end) == QLatin1Char('O'))) {
                ++end;
                while (end < n &&
                       (isHexDigit(line.at(end)) || line.at(end) == QLatin1Char('_')))
                    ++end;
            } else {
                bool sawDot = false;
                while (end < n) {
                    const QChar nc = line.at(end);
                    if (nc.isDigit() || nc == QLatin1Char('_')) {
                        ++end;
                        continue;
                    }
                    if (nc == QLatin1Char('.') && !sawDot) {
                        sawDot = true;
                        ++end;
                        continue;
                    }
                    if ((nc == QLatin1Char('e') || nc == QLatin1Char('E')) && end + 1 < n &&
                        (line.at(end + 1).isDigit() || line.at(end + 1) == QLatin1Char('+') ||
                         line.at(end + 1) == QLatin1Char('-'))) {
                        end += 2;
                        while (end < n && line.at(end).isDigit())
                            ++end;
                        break;
                    }
                    break;
                }
            }
            pushSpan(i, end - i, HighlightKind::Number);
            i = end;
            continue;
        }

        // Identifiers / keywords.
        if (isIdentifierStart(c)) {
            int end = i + 1;
            while (end < n && isIdentifierPart(line.at(end)))
                ++end;
            if (isKeyword(lang, line.mid(i, end - i)))
                pushSpan(i, end - i, HighlightKind::Keyword);
            i = end;
            continue;
        }

        // Plain character.
        ++i;
    }

    return spans;
}

QString highlightCodeHtml(const QString &language, const QString &code)
{
    if (!isHighlightableLanguage(language))
        return code.toHtmlEscaped();

    const QStringList lines = code.split(QLatin1Char('\n'));
    HighlightState state;
    QString result;

    for (int li = 0; li < lines.size(); ++li) {
        if (li > 0)
            result += QStringLiteral("<br>");
        const QString &line = lines.at(li);
        const std::vector<HighlightSpan> spans = highlightLine(language, line, state);

        int pos = 0;
        for (const HighlightSpan &s : spans) {
            if (s.start > pos)
                result += line.mid(pos, s.start - pos).toHtmlEscaped();
            result += QStringLiteral("<span class=\"%1\">%2</span>")
                              .arg(spanClass(s.kind),
                                   line.mid(s.start, s.length).toHtmlEscaped());
            pos = s.start + s.length;
        }
        if (pos < line.size())
            result += line.mid(pos).toHtmlEscaped();
    }

    return result;
}

} // namespace Markdown
} // namespace Core
} // namespace Acheron
