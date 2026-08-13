#pragma once

#include <QChar>
#include <QString>

#include <vector>

namespace Acheron {
namespace Core {
namespace Markdown {

enum class HighlightKind {
    Keyword,
    String,
    Comment,
    Number,
};

struct HighlightSpan
{
    int start = 0;
    int length = 0;
    HighlightKind kind = HighlightKind::Keyword;
};

// Scanner state carried across lines so a multi-line block comment (/* */) or
// a Python triple-quoted string is not mis-classified on continuation lines.
struct HighlightState
{
    bool inBlockComment = false;
    QChar tripleQuote; // '\0' when not inside a python triple-quoted string
};

// Tokenize a single line of source into non-overlapping, ascending spans.
// Text inside strings and comments is never scanned for keywords, so a
// keyword-looking token there is never mis-classified. `state` carries the
// scanner position across lines for multi-line constructs.
std::vector<HighlightSpan> highlightLine(const QString &language, const QString &line,
                                         HighlightState &state);

// True when `language` (e.g. "cpp", "python", "js", "json") is a supported
// highlight target. Unknown languages render as plain text.
bool isHighlightableLanguage(const QString &language);

// Highlight a whole fenced code block and render it as HTML with theme-class
// spans (<span class="code-kw">, "code-str", "code-com", "code-num"). Newlines
// become <br>. Unsupported languages are HTML-escaped verbatim. The returned
// HTML is meant to be wrapped in a .code-block span (see richTextStyleSheet).
QString highlightCodeHtml(const QString &language, const QString &code);

} // namespace Markdown
} // namespace Core
} // namespace Acheron
