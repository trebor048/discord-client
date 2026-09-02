#include "Parser.hpp"
#include "CodeHighlighter.hpp"
#include "Core/EmojiSegmenter.hpp"
#include "Discord/CdnUrls.hpp"

#include <QRegularExpression>

#include <algorithm>

using Acheron::Core::countUnicodeEmojisSegmented;

// Nesting depth of parse() for the current thread. The top-level call (depth 0)
// owns \r\n normalization + tab expansion; nested parses receive captured
// substrings of the already-normalized source and skip re-normalization.
static thread_local int sParseDepth = 0;

namespace Acheron {
namespace Core {
namespace Markdown {

Parser::Parser()
{
    setupDefaultRules();
    sortRules();
}

QList<AstNode> Parser::parse(QString source, ParseState state)
{
    QList<AstNode> result;
    result.reserve(source.size() / 8 + 1);

    // Normalize once at the top-level entry: nested parses always operate on
    // captured substrings of the already-normalized source (no \r or \t can
    // survive into them), so re-running the two replaces per nesting level is
    // pure waste. Use RAII so an exception (e.g. std::bad_alloc in reserve/regex)
    // cannot leave sParseDepth permanently incremented and skip future normalisation.
    struct DepthGuard {
        int &d;
        explicit DepthGuard(int &v) : d(v) { ++d; }
        ~DepthGuard() { --d; }
    };
    DepthGuard guard(sParseDepth);
    const bool nested = guard.d > 1;
    if (!nested) {
        source.replace(QRegularExpression(R"(\r\n?)"), "\n");
        source.replace("\t", "    ");
    }

    if (!state.isInline) {
        source += "\n\n";
    }

    int pos = 0;
    const int sourceLength = source.size();

    while (pos < sourceLength) {
        MarkdownRule *bestRule = nullptr;
        Capture bestCapture;
        double bestQuality = std::numeric_limits<double>::lowest();

        int currentBestOrder = -1;
        bool foundMatch = false;

        for (auto &rule : rules) {
            if (state.excludedRules.contains(rule.name))
                continue;

            // Cheap pre-filter: skip rules whose anchored regex cannot
            // possibly match the current starting character.
            if (!rule.firstChars.isEmpty() && !rule.firstChars.contains(source.at(pos)))
                continue;

            if (foundMatch && rule.order > currentBestOrder)
                break;

            Capture capture;

            if (rule.match) {
                capture = rule.match(source, pos, state);
            } else {
                capture = rule.regex.match(source, pos, QRegularExpression::NormalMatch,
                                           QRegularExpression::AnchorAtOffsetMatchOption);
            }

            if (capture.hasMatch()) {
                double quality = 0.0;
                if (rule.quality) {
                    quality = rule.quality(capture, state, state.prevCapture);
                }

                // Tie-breaking: on equal quality the later rule within the
                // same order wins (>=). Rules sharing an order (e.g. order 21:
                // user/channel/customEmoji/strong/u) must therefore be
                // mutually exclusive at any given position - the mention and
                // emoji rules are disambiguated by their distinct '<@', '<#',
                // '<a?:name:' prefixes, and strong ('**') vs u ('__') never
                // overlap - so no ambiguity remains.
                if (!foundMatch || quality >= bestQuality) {
                    bestRule = &rule;
                    bestCapture = capture;
                    bestQuality = quality;
                    currentBestOrder = rule.order;
                    foundMatch = true;
                }
            }
        }

        if (!foundMatch) {
            // bad! error!
            AstNode node;
            node.type = "text";
            node.content = source.mid(pos, 1);
            result.append(node);

            state.prevCapture = node.content;
            pos += 1;
            continue;
        }

        auto nestedParse = [this](QString source, ParseState state) -> QList<AstNode> {
            return this->parse(source, state);
        };

        AstNode parsedNode = bestRule->parse(bestCapture, nestedParse, state);

        if (parsedNode.type.isEmpty()) {
            parsedNode.type = bestRule->name;
        }

        // maybe flatten here
        result.append(parsedNode);

        // Because every rule is anchored at `pos`, captured(0) spans exactly
        // [pos, pos+length) of the original source.
        QString capturedStr = bestCapture.captured(0);
        state.prevCapture = capturedStr;
        pos += capturedStr.length();
    }

    return result;
}

QString Parser::toHtml(const QList<AstNode> &nodes, bool jumboEmoji)
{
    return toHtmlInternal(nodes, jumboEmoji);
}

bool Parser::isEmojiOnly(const QList<AstNode> &nodes, int maxEmojis)
{
    int totalEmojis = 0;
    for (const auto &node : nodes) {
        if (node.type == "customEmoji") {
            totalEmojis++;
            if (totalEmojis > maxEmojis)
                return false;
            continue;
        }
        if (node.type == "br" || node.type == "newline")
            continue;
        if (node.type == "text" || node.type.isEmpty()) {
            int unicodeCount = countUnicodeEmojisSegmented(node.content);
            if (unicodeCount < 0)
                return false;
            totalEmojis += unicodeCount;
            if (totalEmojis > maxEmojis)
                return false;
            continue;
        }
        // Any other node type (em, strong, url, link, code, etc.) disqualifies
        return false;
    }
    return totalEmojis > 0;
}

QString Parser::toHtmlInternal(const QList<AstNode> &nodes, bool jumboEmoji)
{
    QString result;
    result.reserve(nodes.size() * 48);
    for (const auto &node : nodes) {
        if (node.type == "user") {
            QString displayName;
            if (userResolver)
                displayName = userResolver(node.content);
            else
                displayName = node.content;

            result += QString("<span class=\"mention\">@%1</span>")
                              .arg(displayName.toHtmlEscaped());
            continue;
        }

        if (node.type == "channel") {
            QString displayName;
            if (channelResolver)
                displayName = channelResolver(node.content);
            else
                displayName = node.content;

            result += QString("<a href=\"acheron://channel/%1\" class=\"mention\">#%2</a>")
                              .arg(node.content.toHtmlEscaped())
                              .arg(displayName.toHtmlEscaped());
            continue;
        }

        if (node.type == "customEmoji") {
            QString id = node.content;
            QString name = node.attributes["name"].toString();
            bool animated = node.attributes["animated"].toBool();
            int emojiSize = jumboEmoji ? 44 : 22;
            QString url = Acheron::Discord::Cdn::emojiImage(id, animated, 128).toString();
            // Use vertical-align: middle and line-height: 1 to prevent
            // double-width emoji from breaking monospace/layout in chat.
            result += QString(R"(<img src="%1" alt=":%2:" width="%3" height="%3" )"
                              R"(style="vertical-align: middle; line-height: 1; )"
                              R"(display: inline-block;" />)")
                              .arg(url.toHtmlEscaped())
                              .arg(name.toHtmlEscaped())
                              .arg(emojiSize);
            continue;
        }

        if (jumboEmoji && node.type == "text") {
            QString escaped = node.content.toHtmlEscaped();
            if (!node.content.trimmed().isEmpty())
                result += QString(R"(<span style="font-size: 44px;">%1</span>)").arg(escaped);
            else
                result += escaped;
            continue;
        }

        auto ruleIt = ruleMap.constFind(node.type);
        if (ruleIt != ruleMap.constEnd() && ruleIt.value()->html) {
            auto renderChildren = [this, jumboEmoji](const QList<AstNode> &children) {
                return this->toHtmlInternal(children, jumboEmoji);
            };
            result += ruleIt.value()->html(node, renderChildren);
        } else {
            result += node.content.toHtmlEscaped();
        }
    }
    return result;
}

void Parser::setUserResolver(UserResolverFn resolver)
{
    userResolver = std::move(resolver);
}

void Parser::setChannelResolver(ChannelResolverFn resolver)
{
    channelResolver = std::move(resolver);
}

static MatchFn inlineRegex(QRegularExpression regex)
{
    return [regex](const QString &source, int offset, const ParseState &state) -> Capture {
        if (state.isInline)
            return regex.match(source, offset, QRegularExpression::NormalMatch,
                               QRegularExpression::AnchorAtOffsetMatchOption);
        else
            return Capture();
    };
}

static MatchFn blockRegex(QRegularExpression regex)
{
    return [regex](const QString &source, int offset, const ParseState &state) -> Capture {
        if (!state.isInline)
            return regex.match(source, offset, QRegularExpression::NormalMatch,
                               QRegularExpression::AnchorAtOffsetMatchOption);
        else
            return Capture();
    };
}

static MatchFn anyScopeRegex(QRegularExpression regex)
{
    return [regex](const QString &source, int offset, const ParseState &state) -> Capture {
        return regex.match(source, offset, QRegularExpression::NormalMatch,
                           QRegularExpression::AnchorAtOffsetMatchOption);
    };
}

void Parser::setupDefaultRules()
{
    MarkdownRule newline;
    newline.name = "newline";
    newline.order = 10;
    // \G anchors at the match() offset, unlike ^ which binds to the string
    // start; the parse loop advances an offset instead of trimming the string.
    newline.regex = QRegularExpression(R"(\G(?:\n *)*\n)");
    newline.match = blockRegex(newline.regex);
    newline.parse = [](const Capture &match, NestedParseFn nestedParse,
                       ParseState state) -> AstNode { return {}; };
    newline.html = [](const AstNode &node,
                      std::function<QString(const QList<AstNode> &)> renderChildren) -> QString {
        return "\n";
    };
    rules.append(newline);

    MarkdownRule escape;
    escape.name = "escape";
    escape.order = 12;
    escape.regex = QRegularExpression(R"(\G\\([^0-9A-Za-z\s]))");
    escape.match = inlineRegex(escape.regex);
    escape.parse = [](const Capture &match, NestedParseFn nestedParse,
                      ParseState state) -> AstNode {
        AstNode node;
        node.type = "text";
        node.content = match.captured(1);
        return node;
    };
    escape.html = [](const AstNode &node,
                     std::function<QString(const QList<AstNode> &)> renderChildren) -> QString {
        return node.content.toHtmlEscaped();
    };
    rules.append(escape);

    MarkdownRule codeBlock;
    codeBlock.name = "codeBlock";
    codeBlock.order = 14;
    codeBlock.regex = QRegularExpression(R"(\G```([a-zA-Z0-9_+#.-]*)\n([\s\S]*?)```)");
    codeBlock.match = anyScopeRegex(codeBlock.regex);
    codeBlock.parse = [](const Capture &match, NestedParseFn nestedParse,
                         ParseState state) -> AstNode {
        AstNode node;
        node.type = "codeBlock";
        node.content = match.captured(2);
        // The closing fence is conventionally on its own line, leaving one
        // trailing newline inside the capture that is not part of the code.
        if (node.content.endsWith(QLatin1Char('\n')))
            node.content.chop(1);
        node.attributes["language"] = match.captured(1).trimmed();
        return node;
    };
    codeBlock.html = [](const AstNode &node,
                        std::function<QString(const QList<AstNode> &)> renderChildren) -> QString {
        const QString language = node.attributes["language"].toString();
        return QStringLiteral("<span class=\"code-block\">%1</span>")
                .arg(highlightCodeHtml(language, node.content));
    };
    rules.append(codeBlock);

    MarkdownRule url;
    url.name = "url";
    url.order = 16;
    url.regex = QRegularExpression(R"(\G(https?:\/\/[^\s<]+[^<.,:;"')\]\s]))");
    url.match = inlineRegex(url.regex);
    url.parse = [](const Capture &match, NestedParseFn nestedParse, ParseState state) -> AstNode {
        AstNode node;
        node.type = "url";
        node.content = match.captured(1);
        node.attributes["href"] = node.content;
        return node;
    };
    url.html = [](const AstNode &node,
                  std::function<QString(const QList<AstNode> &)> renderChildren) -> QString {
        return QString("<a href=\"%1\">%2</a>")
                .arg(node.attributes["href"].toString().toHtmlEscaped())
                .arg(node.content.toHtmlEscaped());
    };
    rules.append(url);

    MarkdownRule link;
    link.name = "link";
    link.order = 17;
    link.regex = QRegularExpression(
            R"(\G\[((?:\[[^\]]*\]|[^\[\]]|\](?=[^\[]*\]))*)\]\(\s*<?((?:\([^)]*\)|[^\s\\]|\\.)*?)>?(?:\s+['"]([\s\S]*?)['"])?\s*\))");
    link.match = inlineRegex(link.regex);
    link.parse = [](const Capture &match, NestedParseFn nestedParse, ParseState state) -> AstNode {
        AstNode node;
        node.type = "link";
        node.children = nestedParse(match.captured(1), state);
        node.attributes["href"] = match.captured(2);
        node.attributes["title"] = match.captured(3);
        return node;
    };
    link.html = [](const AstNode &node,
                   std::function<QString(const QList<AstNode> &)> renderChildren) -> QString {
        QString href = node.attributes["href"].toString();
        // Only allow safe protocols: http, https, and acheron
        if (!href.startsWith("http://") && !href.startsWith("https://") && !href.startsWith("acheron://"))
            href = "about:blank";
        return QString("<a href=\"%1\">%2</a>")
                .arg(href.toHtmlEscaped())
                .arg(renderChildren(node.children));
    };
    rules.append(link);

    MarkdownRule em;
    em.name = "em";
    em.order = 20;
    em.regex = QRegularExpression(
            R"(\G\b_((?:__|\\[\s\S]|[^\\_])+?)_\b|\G\*(?=\S)((?:\*\*|\\[\s\S]|\s+(?:\\[\s\S]|[^\s\*\\]|\*\*)|[^\s\*\\])+?)\*(?!\*))");
    em.match = inlineRegex(em.regex);
    em.parse = [](const Capture &match, NestedParseFn nestedParse, ParseState state) -> AstNode {
        AstNode node;
        node.type = "em";
        QString innerContent = match.captured(2).isNull() ? match.captured(1) : match.captured(2);
        ParseState childState = state;
        childState.isInline = true;
        node.children = nestedParse(innerContent, childState);
        return node;
    };
    em.html = [](const AstNode &node,
                 std::function<QString(const QList<AstNode> &)> renderChildren) -> QString {
        return QString("<em>%1</em>").arg(renderChildren(node.children));
    };
    em.quality = [](const Capture &match, const ParseState &state,
                    const QString &prevCapture) -> double { return match.capturedLength() + 0.2; };
    rules.append(em);

    MarkdownRule user;
    user.name = "user";
    user.order = 21;
    user.regex = QRegularExpression(R"(\G<@!?([0-9]+)>)");
    user.match = anyScopeRegex(user.regex);
    user.parse = [](const Capture &match, NestedParseFn nestedParse, ParseState state) -> AstNode {
        AstNode node;
        node.type = "user";
        node.content = match.captured(1);
        return node;
    };
    // .html handled in toHtml() because of user resolution
    rules.append(user);

    MarkdownRule channel;
    channel.name = "channel";
    channel.order = 21;
    // Discord only treats <#id> as a channel mention; bare <id> is plain text.
    channel.regex = QRegularExpression(R"(\G<#([0-9]+)>)");
    channel.match = anyScopeRegex(channel.regex);
    channel.parse = [](const Capture &match, NestedParseFn nestedParse, ParseState state) -> AstNode {
        AstNode node;
        node.type = "channel";
        node.content = match.captured(1);
        return node;
    };
    // .html handled in toHtml() because of channel resolution
    rules.append(channel);

    MarkdownRule customEmoji;
    customEmoji.name = "customEmoji";
    customEmoji.order = 21;
    customEmoji.regex = QRegularExpression(R"(\G<(a?):([a-zA-Z0-9_]{1,32}):(\d+)>)");
    customEmoji.match = anyScopeRegex(customEmoji.regex);
    customEmoji.parse = [](const Capture &match, NestedParseFn nestedParse,
                           ParseState state) -> AstNode {
        AstNode node;
        node.type = "customEmoji";
        node.content = match.captured(3);
        node.attributes["name"] = match.captured(2);
        node.attributes["animated"] = !match.captured(1).isEmpty();
        return node;
    };
    // .html handled in toHtmlInternal() for jumbo emoji support
    rules.append(customEmoji);

    MarkdownRule strong;
    strong.name = "strong";
    strong.order = 21;
    strong.regex = QRegularExpression(R"(\G\*\*((?:\\[\s\S]|[^\\])+?)\*\*(?!\*))");
    strong.match = inlineRegex(strong.regex);
    strong.parse = [](const Capture &match, NestedParseFn nestedParse,
                      ParseState state) -> AstNode {
        AstNode node;
        node.type = "strong";
        QString innerContent = match.captured(1);
        ParseState childState = state;
        childState.isInline = true;
        node.children = nestedParse(innerContent, childState);
        return node;
    };
    strong.html = [](const AstNode &node,
                     std::function<QString(const QList<AstNode> &)> renderChildren) -> QString {
        return QString("<strong>%1</strong>").arg(renderChildren(node.children));
    };
    strong.quality = [](const Capture &match, const ParseState &state,
                        const QString &prevCapture) -> double {
        return match.capturedLength() + 0.1;
    };
    rules.append(strong);

    MarkdownRule u;
    u.name = "u";
    u.order = 21;
    u.regex = QRegularExpression(R"(\G__((?:\\[\s\S]|[^\\])+?)__(?!_))");
    u.match = inlineRegex(u.regex);
    u.parse = [](const Capture &match, NestedParseFn nestedParse, ParseState state) -> AstNode {
        AstNode node;
        node.type = "u";
        QString innerContent = match.captured(1);
        ParseState childState = state;
        childState.isInline = true;
        node.children = nestedParse(innerContent, childState);
        return node;
    };
    u.html = [](const AstNode &node,
                std::function<QString(const QList<AstNode> &)> renderChildren) -> QString {
        return QString("<u>%1</u>").arg(renderChildren(node.children));
    };
    u.quality = [](const Capture &match, const ParseState &state,
                   const QString &prevCapture) -> double { return match.capturedLength(); };
    rules.append(u);

    MarkdownRule strike;
    strike.name = "strike";
    strike.order = 22;
    strike.regex = QRegularExpression(R"(\G~~([\s\S]+?)~~(?!_))");
    strike.match = inlineRegex(strike.regex);
    strike.parse = [](const Capture &match, NestedParseFn nestedParse,
                      ParseState state) -> AstNode {
        AstNode node;
        node.type = "strike";
        QString innerContent = match.captured(1);
        ParseState childState = state;
        childState.isInline = true;
        node.children = nestedParse(innerContent, childState);
        return node;
    };
    strike.html = [](const AstNode &node,
                     std::function<QString(const QList<AstNode> &)> renderChildren) -> QString {
        return QString("<s>%1</s>").arg(renderChildren(node.children));
    };
    rules.append(strike);

    MarkdownRule spoiler;
    spoiler.name = "spoiler";
    spoiler.order = 22;
    spoiler.regex = QRegularExpression(R"(\G\|\|([\s\S]+?)\|\|)");
    spoiler.match = inlineRegex(spoiler.regex);
    spoiler.parse = [](const Capture &match, NestedParseFn nestedParse,
                       ParseState state) -> AstNode {
        AstNode node;
        node.type = "spoiler";
        QString innerContent = match.captured(1);
        ParseState childState = state;
        childState.isInline = true;
        node.children = nestedParse(innerContent, childState);
        return node;
    };
    spoiler.html = [](const AstNode &node,
                      std::function<QString(const QList<AstNode> &)> renderChildren) -> QString {
        return QString("<span class=\"spoiler\">%1</span>").arg(renderChildren(node.children));
    };
    rules.append(spoiler);

    MarkdownRule inlineCode;
    inlineCode.name = "inlineCode";
    inlineCode.order = 23;
    // Content may be empty (``) or a single space (` `); the lazy body plus
    // backreference handles both without requiring a non-backtick char.
    inlineCode.regex = QRegularExpression(R"(\G(`+)([\s\S]*?)\1(?!`))");
    inlineCode.match = inlineRegex(inlineCode.regex);
    inlineCode.parse = [](const Capture &match, NestedParseFn nestedParse,
                          ParseState state) -> AstNode {
        AstNode node;
        node.type = "inlineCode";
        node.content = match.captured(2);
        // CommonMark: strip one leading and one trailing space only when both
        // are present and the content is not entirely spaces.
        bool allSpaces = std::all_of(node.content.begin(), node.content.end(),
                                     [](QChar c) { return c == QLatin1Char(' '); });
        if (!allSpaces && node.content.startsWith(' ') && node.content.endsWith(' '))
            node.content = node.content.mid(1, node.content.size() - 2);
        return node;
    };
    inlineCode.html = [](const AstNode &node,
                         std::function<QString(const QList<AstNode> &)> renderChildren) -> QString {
        return QString("<code>%1</code>").arg(node.content.toHtmlEscaped());
    };
    rules.append(inlineCode);

    MarkdownRule br;
    br.name = "br";
    br.order = 24;
    br.regex = QRegularExpression(R"(\G\n)");
    br.match = anyScopeRegex(br.regex);
    br.parse = [](const Capture &match, NestedParseFn nestedParse, ParseState state) -> AstNode {
        return {};
    };
    br.html = [](const AstNode &node,
                 std::function<QString(const QList<AstNode> &)> renderChildren) -> QString {
        return "<br>";
    };
    rules.append(br);

    MarkdownRule text;
    text.name = "text";
    text.order = 25;
    text.regex =
            QRegularExpression(R"(\G[\s\S]+?(?=[^0-9A-Za-z\s\x{00C0}-\x{ffff}-]|\n\n|\n|\w+:\S|$))");
    text.match = anyScopeRegex(text.regex);
    text.parse = [](const Capture &match, NestedParseFn nestedParse, ParseState state) -> AstNode {
        AstNode node;
        node.content = match.captured(0);
        return node;
    };
    text.html = [](const AstNode &node,
                   std::function<QString(const QList<AstNode> &)> renderChildren) -> QString {
        return node.content.toHtmlEscaped();
    };
    rules.append(text);
}

void Parser::sortRules()
{
    // Possible starting characters per rule, used by the parse loop's
    // pre-filter. Rules not listed here (currently only "text") are attempted
    // at every position. Keep in sync with the regexes in setupDefaultRules().
    static const QMap<QString, QString> ruleFirstChars = {
        { "newline", "\n" },    { "escape", "\\" },   { "codeBlock", "`" },
        { "url", "h" },         { "link", "[" },      { "em", "_*" },
        { "user", "<" },        { "channel", "<" },   { "customEmoji", "<" },
        { "strong", "*" },      { "u", "_" },         { "strike", "~" },
        { "spoiler", "|" },     { "inlineCode", "`" }, { "br", "\n" },
    };

    for (auto &rule : rules) {
        auto it = ruleFirstChars.constFind(rule.name);
        if (it != ruleFirstChars.constEnd()) {
            for (QChar c : it.value())
                rule.firstChars.insert(c);
        }
    }

    std::sort(rules.begin(), rules.end(),
              [](const MarkdownRule &a, const MarkdownRule &b) { return a.order < b.order; });

    for (auto &rule : rules) {
        ruleMap[rule.name] = &rule;
    }
}

} // namespace Markdown
} // namespace Core
} // namespace Acheron
