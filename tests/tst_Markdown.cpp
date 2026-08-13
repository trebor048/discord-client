#include "Core/Markdown/Parser.hpp"
#include "Core/Markdown/CodeHighlighter.hpp"

#include <QElapsedTimer>
#include <QSet>
#include <QTest>

using namespace Acheron::Core::Markdown;

namespace {
QSet<QString> keywordStrings(const QString &line, const std::vector<HighlightSpan> &spans)
{
    QSet<QString> out;
    for (const auto &s : spans) {
        if (s.kind == HighlightKind::Keyword)
            out.insert(line.mid(s.start, s.length));
    }
    return out;
}
} // namespace

class TestMarkdown : public QObject
{
    Q_OBJECT
private slots:
    void testNewlines();
    void testBold();
    void testItalic();
    void testStrikethrough();
    void testInlineCode();
    void testLink();
    void testAutoUrl();
    void testUserMention();
    void testChannelMention();
    void testEmptyInput();
    void testPlainText();
    void testEscaped();
    void testJavaScriptLink();
    void testFencedCodeBlock();
    void testCodeHighlightKeyword();
    void testCodeHighlightNoKeywordInString();
    void testCodeHighlightNoKeywordInComment();
    void testCodeHighlightBlockCommentAcrossLines();
    void testCodeHighlightNumberAndString();
};

void TestMarkdown::testNewlines()
{
    Parser parser;

    ParseState state;
    state.isInline = true;
    auto nodes = parser.parse("i\nlove\n\ncats", state);
    auto html = parser.toHtml(nodes);

    QVERIFY(html == "i<br>love<br><br>cats");

    return;
}

void TestMarkdown::testBold()
{
    Parser parser;

    ParseState state;
    state.isInline = true;
    auto nodes = parser.parse("**bold**", state);
    auto html = parser.toHtml(nodes);

    QVERIFY(html == "<strong>bold</strong>");

    return;
}

void TestMarkdown::testItalic()
{
    Parser parser;

    ParseState state;
    state.isInline = true;
    auto nodes = parser.parse("*italic*", state);
    auto html = parser.toHtml(nodes);

    QVERIFY(html == "<em>italic</em>");

    return;
}

void TestMarkdown::testStrikethrough()
{
    Parser parser;

    ParseState state;
    state.isInline = true;
    auto nodes = parser.parse("~~strikethrough~~", state);
    auto html = parser.toHtml(nodes);

    QVERIFY(html == "<s>strikethrough</s>");

    return;
}

void TestMarkdown::testInlineCode()
{
    Parser parser;

    ParseState state;
    state.isInline = true;
    auto nodes = parser.parse("`code`", state);
    auto html = parser.toHtml(nodes);

    QVERIFY(html == "<code>code</code>");

    return;
}

void TestMarkdown::testLink()
{
    Parser parser;

    ParseState state;
    state.isInline = true;
    auto nodes = parser.parse("[title](https://url.com)", state);
    auto html = parser.toHtml(nodes);

    QVERIFY(html == "<a href=\"https://url.com\">title</a>");

    return;
}

void TestMarkdown::testAutoUrl()
{
    Parser parser;

    ParseState state;
    state.isInline = true;
    auto nodes = parser.parse("https://example.com", state);
    auto html = parser.toHtml(nodes);

    QVERIFY(html == "<a href=\"https://example.com\">https://example.com</a>");

    return;
}

void TestMarkdown::testUserMention()
{
    Parser parser;

    ParseState state;
    state.isInline = true;
    auto nodes = parser.parse("<@123>", state);
    auto html = parser.toHtml(nodes);

    QVERIFY(html == "<span class=\"mention\">@123</span>");

    return;
}

void TestMarkdown::testChannelMention()
{
    Parser parser;

    ParseState state;
    state.isInline = true;
    auto nodes = parser.parse("<#456>", state);
    auto html = parser.toHtml(nodes);

    QVERIFY(html == "<a href=\"acheron://channel/456\" class=\"mention\">#456</a>");

    return;
}

void TestMarkdown::testEmptyInput()
{
    Parser parser;

    ParseState state;
    state.isInline = true;
    auto nodes = parser.parse("", state);
    auto html = parser.toHtml(nodes);

    QVERIFY(html == "");

    return;
}

void TestMarkdown::testPlainText()
{
    Parser parser;

    ParseState state;
    state.isInline = true;
    auto nodes = parser.parse("hello world", state);
    auto html = parser.toHtml(nodes);

    QVERIFY(html == "hello world");

    return;
}

void TestMarkdown::testEscaped()
{
    Parser parser;

    ParseState state;
    state.isInline = true;
    auto nodes = parser.parse(R"(\*text\*)", state);
    auto html = parser.toHtml(nodes);

    QVERIFY(html == "*text*");

    return;
}

void TestMarkdown::testJavaScriptLink()
{
    Parser parser;

    ParseState state;
    state.isInline = true;
    auto nodes = parser.parse("[bad](javascript:alert(1))", state);
    auto html = parser.toHtml(nodes);

    QVERIFY(html == "<a href=\"about:blank\">bad</a>");

    return;
}

void TestMarkdown::testFencedCodeBlock()
{
    Parser parser;

    ParseState state;
    state.isInline = true;
    auto nodes = parser.parse("```cpp\nfor (;;) {}\n```", state);
    auto html = parser.toHtml(nodes);

    QVERIFY(html.contains(QLatin1String("<span class=\"code-block\">")));
    QVERIFY(html.contains(QLatin1String("code-kw\">for</span>")));
    QVERIFY(!html.contains(QLatin1String("```")));

    return;
}

void TestMarkdown::testCodeHighlightKeyword()
{
    HighlightState state;
    const QString line = QStringLiteral("for (int i = 0; i < 3; i++)");
    auto spans = highlightLine(QStringLiteral("cpp"), line, state);

    QSet<QString> keywords = keywordStrings(line, spans);
    QVERIFY(keywords.contains(QStringLiteral("for")));
    QVERIFY(keywords.contains(QStringLiteral("int")));
    QVERIFY(!keywords.contains(QStringLiteral("i")));

    return;
}

void TestMarkdown::testCodeHighlightNoKeywordInString()
{
    HighlightState state;
    const QString line = QStringLiteral("x = \"def\"");
    auto spans = highlightLine(QStringLiteral("python"), line, state);

    // "def" is a python keyword but here it is inside a string literal.
    bool hasString = false;
    for (const auto &s : spans) {
        if (s.kind == HighlightKind::String)
            hasString = true;
        QVERIFY(s.kind != HighlightKind::Keyword);
    }
    QVERIFY(hasString);

    return;
}

void TestMarkdown::testCodeHighlightNoKeywordInComment()
{
    HighlightState state;
    const QString line = QStringLiteral("// return for while");
    auto spans = highlightLine(QStringLiteral("cpp"), line, state);

    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans[0].kind, HighlightKind::Comment);
    QCOMPARE(spans[0].start, 0);
    QCOMPARE(spans[0].length, line.size());

    return;
}

void TestMarkdown::testCodeHighlightBlockCommentAcrossLines()
{
    HighlightState state;
    auto first = highlightLine(QStringLiteral("cpp"), QStringLiteral("int x; /* start"), state);
    QVERIFY(state.inBlockComment);

    const QString secondLine = QStringLiteral("return end */ int y;");
    auto second = highlightLine(QStringLiteral("cpp"), secondLine, state);
    QVERIFY(!state.inBlockComment);

    // Only "int" (after the comment closes) is a keyword; "return" inside the
    // block comment must not be classified.
    QSet<QString> keywords = keywordStrings(secondLine, second);
    QVERIFY(keywords.contains(QStringLiteral("int")));
    QVERIFY(!keywords.contains(QStringLiteral("return")));

    return;
}

void TestMarkdown::testCodeHighlightNumberAndString()
{
    HighlightState state;
    const QString line = QStringLiteral("{\"count\": 42, \"ok\": true}");
    auto spans = highlightLine(QStringLiteral("json"), line, state);

    bool hasNumber = false;
    bool hasKeyword = false;
    bool hasString = false;
    for (const auto &s : spans) {
        if (s.kind == HighlightKind::Number)
            hasNumber = true;
        if (s.kind == HighlightKind::Keyword)
            hasKeyword = true;
        if (s.kind == HighlightKind::String)
            hasString = true;
    }
    QVERIFY(hasNumber);  // 42
    QVERIFY(hasKeyword); // true
    QVERIFY(hasString);  // "count", "ok"

    return;
}

QTEST_APPLESS_MAIN(TestMarkdown)
#include "tst_Markdown.moc"

