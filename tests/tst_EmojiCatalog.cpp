#include "Core/EmojiCatalog.hpp"
#include "UI/Dialogs/EmojiPreferences.hpp"

#include <algorithm>
#include <QCoreApplication>
#include <QDir>
#include <QSet>
#include <QSettings>
#include <QTest>

using namespace Acheron::Core;
using namespace Acheron::UI;

class TestEmojiCatalog : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void lookupAndSearch();
    void preferencesDeduplicateAndFilter();
    void categoryNamesAndItemsForCategory();
    void totalCountIsInUnicode151Range();
    void customEmojiRegistrationAndSearch();
    void customEmojiSelectionAndValidation();
    void customEmojiRecentsAndFavorites();
};

void TestEmojiCatalog::init()
{
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       QDir::tempPath() + QStringLiteral("/acheron-emoji-tests"));
    QCoreApplication::setOrganizationName(QStringLiteral("AcheronTests"));
    QCoreApplication::setApplicationName(QStringLiteral("EmojiCatalogTests"));
    QSettings settings;
    settings.clear();
    settings.sync();
    EmojiCatalog::clearCustomEmojis();
}

void TestEmojiCatalog::lookupAndSearch()
{
    QCOMPARE(EmojiCatalog::unicodeForName(QStringLiteral("smile")),
             QString::fromUcs4(U"\U0001F604"));

    const auto results = EmojiCatalog::search(QStringLiteral("heart"));
    QVERIFY(!results.isEmpty());
    QVERIFY(std::all_of(results.begin(), results.end(), [](const auto &item) {
        return item.name.contains(QStringLiteral("heart"), Qt::CaseInsensitive);
    }));

    const auto completions = EmojiCatalog::completionNames(QStringLiteral("smi"));
    QVERIFY(completions.contains(QStringLiteral("smile")));
    QVERIFY(completions.contains(QStringLiteral("smiley")));
}

void TestEmojiCatalog::preferencesDeduplicateAndFilter()
{
    QSettings settings;
    settings.clear();

    const QString smile = EmojiCatalog::unicodeForName(QStringLiteral("smile"));
    const QString heart = EmojiCatalog::unicodeForName(QStringLiteral("heart"));

    EmojiPreferences::setRecents({smile, QStringLiteral("not-an-emoji"), smile, heart});
    QCOMPARE(EmojiPreferences::recents(), QStringList({smile, heart}));

    EmojiPreferences::addRecent(heart);
    QCOMPARE(EmojiPreferences::recents(), QStringList({heart, smile}));

    EmojiPreferences::setFavorites({heart, heart, QStringLiteral("bad")});
    QCOMPARE(EmojiPreferences::favorites(), QStringList({heart}));
}

void TestEmojiCatalog::categoryNamesAndItemsForCategory()
{
    const QStringList categories = EmojiCatalog::categoryNames();
    QCOMPARE(categories.size(), 9);
    QVERIFY(categories.contains(QStringLiteral("smileys")));
    QVERIFY(categories.contains(QStringLiteral("people")));
    QVERIFY(categories.contains(QStringLiteral("animals")));
    QVERIFY(categories.contains(QStringLiteral("food")));
    QVERIFY(categories.contains(QStringLiteral("travel")));
    QVERIFY(categories.contains(QStringLiteral("activities")));
    QVERIFY(categories.contains(QStringLiteral("objects")));
    QVERIFY(categories.contains(QStringLiteral("symbols")));
    QVERIFY(categories.contains(QStringLiteral("flags")));

    const auto smileys = EmojiCatalog::itemsForCategory(QStringLiteral("smileys"));
    QVERIFY(!smileys.isEmpty());
    QVERIFY(std::all_of(smileys.begin(), smileys.end(), [](const auto &item) {
        return item.category == QStringLiteral("smileys");
    }));

    const auto people = EmojiCatalog::itemsForCategory(QStringLiteral("people"));
    QVERIFY(!people.isEmpty());

    const auto unknown = EmojiCatalog::itemsForCategory(QStringLiteral("not-a-category"));
    QVERIFY(unknown.isEmpty());

    QSet<QString> seenCategories;
    for (const auto &item : EmojiCatalog::items())
        seenCategories.insert(item.category);
    for (const QString &category : categories)
        QVERIFY(seenCategories.contains(category));
}

void TestEmojiCatalog::totalCountIsInUnicode151Range()
{
    const auto all = EmojiCatalog::items();
    QVERIFY(all.size() >= 3700);
    QVERIFY(all.size() <= 3900);
}

void TestEmojiCatalog::customEmojiRegistrationAndSearch()
{
    EmojiCatalogItem custom;
    custom.name = QStringLiteral("pepehands");
    custom.customId = QStringLiteral("123456789012345678");
    custom.animated = false;
    custom.guildName = QStringLiteral("Test Guild");
    EmojiCatalog::registerCustomEmoji(custom);

    const auto all = EmojiCatalog::items();
    QVERIFY(std::any_of(all.begin(), all.end(), [&custom](const auto &item) {
        return item.customId == custom.customId;
    }));

    const auto searchResults = EmojiCatalog::search(QStringLiteral("pepe"));
    QCOMPARE(searchResults.size(), 1);
    QCOMPARE(searchResults.first().customId, custom.customId);

    const auto completions = EmojiCatalog::completionNames(QStringLiteral("pep"));
    QVERIFY(completions.contains(custom.name));

    QCOMPARE(EmojiCatalog::valueForName(custom.name), custom.selectionValue());
    QVERIFY(EmojiCatalog::unicodeForName(custom.name).isEmpty());

    QVERIFY(EmojiCatalog::customEmojis().contains(custom));
}

void TestEmojiCatalog::customEmojiSelectionAndValidation()
{
    EmojiCatalogItem animated;
    animated.name = QStringLiteral("partywumpus");
    animated.customId = QStringLiteral("987654321098765432");
    animated.animated = true;
    EmojiCatalog::registerCustomEmoji(animated);

    const QString value = animated.selectionValue();
    QCOMPARE(value, QStringLiteral("<a:partywumpus:987654321098765432>"));

    QVERIFY(EmojiCatalog::isSupportedSelection(value));

    const auto selection = EmojiCatalog::selectionForRaw(value);
    QVERIFY(selection.has_value());
    QVERIFY(selection->isCustom);
    QCOMPARE(selection->customId, animated.customId);
    QCOMPARE(selection->name, animated.name);
    QVERIFY(selection->animated);

    QVERIFY(!EmojiCatalog::isSupportedSelection(QStringLiteral("<a:unknown:123456789012345678>")));
    QVERIFY(!EmojiCatalog::isSupportedSelection(QStringLiteral("not-an-emoji")));

    const auto fromRaw = EmojiSelectionValue::fromRaw(value);
    QVERIFY(fromRaw.has_value());
    QCOMPARE(fromRaw->raw, value);
}

void TestEmojiCatalog::customEmojiRecentsAndFavorites()
{
    EmojiCatalogItem custom;
    custom.name = QStringLiteral("kek");
    custom.customId = QStringLiteral("111111111111111111");
    EmojiCatalog::registerCustomEmoji(custom);

    const QString value = custom.selectionValue();
    EmojiPreferences::setRecents({value});
    QCOMPARE(EmojiPreferences::recents(), QStringList({value}));

    EmojiPreferences::setFavorite(value, true);
    QVERIFY(EmojiPreferences::isFavorite(value));
    QCOMPARE(EmojiPreferences::favorites(), QStringList({value}));

    EmojiCatalog::unregisterCustomEmoji(custom.customId);
    QVERIFY(!EmojiCatalog::isSupportedSelection(value));
    QCOMPARE(EmojiPreferences::recents().size(), 0);
    QCOMPARE(EmojiPreferences::favorites().size(), 0);
}

QTEST_APPLESS_MAIN(TestEmojiCatalog)
#include "tst_EmojiCatalog.moc"

