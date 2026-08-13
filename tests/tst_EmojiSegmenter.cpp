#include "Core/EmojiSegmenter.hpp"
#include <QTest>

using namespace Acheron::Core;

class TestEmojiSegmenter : public QObject
{
    Q_OBJECT

private slots:
    void testSingleEmoji();
    void testNoEmoji();
    void testEmptyString();
    void testMixedContent();
    void testZWJSequence();
    void testEmojiModifierSequence();
};

void TestEmojiSegmenter::testSingleEmoji()
{
    // U+1F600 GRINNING FACE
    QCOMPARE(countUnicodeEmojisSegmented(QString::fromUtf8("\xF0\x9F\x98\x80")), 1);
}

void TestEmojiSegmenter::testNoEmoji()
{
    // Plain text with no emoji should return -1 (non-emoji, non-whitespace content)
    QCOMPARE(countUnicodeEmojisSegmented("hello world"), -1);
}

void TestEmojiSegmenter::testEmptyString()
{
    // Empty string should return 0
    QCOMPARE(countUnicodeEmojisSegmented(QString()), 0);
}

void TestEmojiSegmenter::testMixedContent()
{
    // Mix of emoji and text should return -1
    QString mixed = QString::fromUtf8("hello \xF0\x9F\x98\x80 world");
    QCOMPARE(countUnicodeEmojisSegmented(mixed), -1);
}

void TestEmojiSegmenter::testEmojiModifierSequence()
{
    // Waving hand (U+1F44B) + Fitzpatrick modifier (U+1F3FD) — should count as one emoji.
    // Before the EmojiSegmenter.cpp fix (EMOJI_MODIFIER_BASE → EMOJI_MODIFIER_BASE_EMOJI)
    //   the modifier base was treated as text, breaking the sequence.
    QString waved = QString::fromUtf8(
        "\xF0\x9F\x91\x8B"   // U+1F44B WAVING HAND
        "\xF0\x9F\x8F\xBD"   // U+1F3FD EMOJI MODIFIER FITZPATRICK TYPE-4
    );
    QCOMPARE(countUnicodeEmojisSegmented(waved), 1);
}

void TestEmojiSegmenter::testZWJSequence()
{
    // Family emoji "👨‍👩‍👧‍👦" is a ZWJ sequence that should count as one emoji.
    // U+1F468 (man) + U+200D (ZWJ) + U+1F469 (woman) + U+200D (ZWJ)
    //   + U+1F467 (girl) + U+200D (ZWJ) + U+1F466 (boy)
    QString family = QString::fromUtf8(
        "\xF0\x9F\x91\xA8"   // U+1F468 MAN
        "\xE2\x80\x8D"       // U+200D ZWJ
        "\xF0\x9F\x91\xA9"   // U+1F469 WOMAN
        "\xE2\x80\x8D"       // U+200D ZWJ
        "\xF0\x9F\x91\xA7"   // U+1F467 GIRL
        "\xE2\x80\x8D"       // U+200D ZWJ
        "\xF0\x9F\x91\xA6"   // U+1F466 BOY
    );
    QCOMPARE(countUnicodeEmojisSegmented(family), 1);
}

QTEST_APPLESS_MAIN(TestEmojiSegmenter)
#include "tst_EmojiSegmenter.moc"
