#include "UI/Chat/ChatLayout.hpp"
#include <QTest>
#include <QStringList>

using namespace Acheron::UI::ChatLayout;

class TestLinkification : public QObject
{
    Q_OBJECT
private slots:
    void testSimpleUrl();
    void testUrlWithPath();
    void testTrailingPunctuation();
    void testDiscordChannelLink();
    void testMultipleUrls();
    void testNoFalsePositive();
    void testUrlWithPort();
    void testUrlWithFragment();
    void testHttpUppercase();
    void testNestedParens();
    void testUrlInAngleBrackets();
    void testNoUrlAtAll();
    void testMalformedUrl();
};

void TestLinkification::testSimpleUrl()
{
    QStringList urls = extractUrls("Check this: https://example.com");
    QCOMPARE(urls.size(), 1);
    QCOMPARE(urls[0], QStringLiteral("https://example.com"));
}

void TestLinkification::testUrlWithPath()
{
    QStringList urls = extractUrls("https://example.com/path?query=1&foo=bar");
    QCOMPARE(urls.size(), 1);
    QCOMPARE(urls[0], QStringLiteral("https://example.com/path?query=1&foo=bar"));
}

void TestLinkification::testTrailingPunctuation()
{
    QStringList urls = extractUrls("(https://example.com)");
    QCOMPARE(urls.size(), 1);
    QCOMPARE(urls[0], QStringLiteral("https://example.com"));
}

void TestLinkification::testDiscordChannelLink()
{
    QStringList urls = extractUrls("Join here: https://discord.com/channels/123/456");
    QCOMPARE(urls.size(), 1);
    QCOMPARE(urls[0], QStringLiteral("https://discord.com/channels/123/456"));
}

void TestLinkification::testMultipleUrls()
{
    QStringList urls = extractUrls("First: https://a.com, second: https://b.org/path");
    QCOMPARE(urls.size(), 2);
    QCOMPARE(urls[0], QStringLiteral("https://a.com"));
    QCOMPARE(urls[1], QStringLiteral("https://b.org/path"));
}

void TestLinkification::testNoFalsePositive()
{
    QStringList urls = extractUrls("This is just text with no URLs");
    QCOMPARE(urls.size(), 0);
}

void TestLinkification::testUrlWithPort()
{
    QStringList urls = extractUrls("https://example.com:8080/path");
    QCOMPARE(urls.size(), 1);
    QCOMPARE(urls[0], QStringLiteral("https://example.com:8080/path"));
}

void TestLinkification::testUrlWithFragment()
{
    QStringList urls = extractUrls("https://example.com/page#section");
    QCOMPARE(urls.size(), 1);
    QCOMPARE(urls[0], QStringLiteral("https://example.com/page#section"));
}

void TestLinkification::testHttpUppercase()
{
    QStringList urls = extractUrls("HTTP://EXAMPLE.COM");
    QCOMPARE(urls.size(), 1);
    QCOMPARE(urls[0], QStringLiteral("HTTP://EXAMPLE.COM"));
}

void TestLinkification::testNestedParens()
{
    QStringList urls = extractUrls("((https://example.com))");
    QCOMPARE(urls.size(), 1);
    QCOMPARE(urls[0], QStringLiteral("https://example.com"));
}

void TestLinkification::testUrlInAngleBrackets()
{
    QStringList urls = extractUrls("<https://example.com>");
    QCOMPARE(urls.size(), 1);
    QCOMPARE(urls[0], QStringLiteral("https://example.com"));
}

void TestLinkification::testNoUrlAtAll()
{
    QStringList urls = extractUrls(QString());
    QCOMPARE(urls.size(), 0);
}

void TestLinkification::testMalformedUrl()
{
    QStringList urls = extractUrls("This is not a URL: https://");
    QCOMPARE(urls.size(), 0);
}

QTEST_MAIN(TestLinkification)
#include "tst_Linkification.moc"
