#pragma once

#include <QDialog>
#include <QGraphicsOpacityEffect>
#include <QList>
#include <QModelIndex>
#include <QPropertyAnimation>
#include <QSet>
#include <QTimer>

#include "Core/Result.hpp"
#include "Discord/Entities.hpp"
#include "UI/TabBar/TabBar.hpp"

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;
class QShowEvent;
class QLabel;
class QPushButton;
class QTabWidget;
class QTimer;

namespace Acheron { namespace Discord { class Client; } }

namespace Acheron {
namespace UI {

class ChannelTreeModel;
class ServerRailModel;

class ChannelQuickSwitch : public QDialog
{
    Q_OBJECT
public:
    explicit ChannelQuickSwitch(ChannelTreeModel *model, ServerRailModel *railModel,
                                const TabEntry &currentEntry, const QList<TabEntry> &recentEntries,
                                Acheron::Discord::Client *discordClient = nullptr,
                                QWidget *parent = nullptr);

    TabEntry selectedEntry() const;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void done(int r) override;

private:
    enum class Section {
        All = 0,
        Favorites,
        Recent,
    };

    enum class ItemKind {
        Channel,
    };

    enum class SortMode {
        ServerOrder,
        ByName,
        ByUnread,
    };

    void rebuildList(const QString &filterText);
    void rebuildCurrentSection();
    struct ChannelEntry
    {
        TabEntry tab;
        QString searchKey;
        QString displayText;
        QString secondaryText;
        QIcon icon;
    };

    struct GuildSection
    {
        Core::Snowflake accountId;
        Core::Snowflake guildId;
        QString title;
        QString searchKey;
        QIcon icon;
        QList<ChannelEntry> channels;
    };

    GuildSection buildGuildSection(Core::Snowflake accountId, Core::Snowflake guildId,
                                   const QString &title, const QIcon &icon) const;
    GuildSection buildDMSection(Core::Snowflake accountId, const QString &accountTitle) const;
    QList<ChannelEntry> collectChannels(const QModelIndex &parentIndex, const QString &guildName,
                                        Core::Snowflake accountId, Core::Snowflake guildId,
                                        const QString &accountName) const;
    QList<ChannelEntry> collectDMChannels(Core::Snowflake accountId, const QString &accountName) const;
    QList<ChannelEntry> collectApiDMChannels(Core::Snowflake accountId, const QString &accountName,
                                             const QList<Acheron::Discord::Channel> &channels) const;
    void startDMSearch();
    void handleDMResults(const Acheron::Core::Result<QList<Acheron::Discord::Channel>> &result);
    QList<GuildSection> buildAllGuildSections() const;
    QList<ChannelEntry> buildRecentItems() const;
    QList<ChannelEntry> buildFavoriteItems() const;
    QIcon iconForIndex(const QModelIndex &index) const;
    QString accountNameFor(const QModelIndex &accountIndex) const;
    QString guildNameFor(const QModelIndex &guildIndex) const;
    void rebuildAllTree(const QString &filterText);
    void rebuildFlatTree(QTreeWidget *tree, const QList<ChannelEntry> &channels,
                         const QString &filterText, const QString &emptyText);
    void appendChannelItem(QTreeWidgetItem *parent, const ChannelEntry &entry,
                           bool showSecondaryText);
    void sortChannels(QList<ChannelEntry> &channels) const;
    void cycleSortMode();
    bool itemMatchesFilter(const ChannelEntry &entry, const QString &filterText) const;
    void setFirstSelectableItemCurrent();
    void acceptCurrentSelection();
    TabEntry entryForItem(const QTreeWidgetItem *item) const;
    bool isChannelItem(const QTreeWidgetItem *item) const;
    QString guildKey(Core::Snowflake accountId, Core::Snowflake guildId) const;
    QString channelKey(const TabEntry &entry) const;
    QTreeWidget *activeTree() const;
    void updateHintLabel(const QString &filterText) const;
    void updateFavoriteButton();
    QStringList favoriteChannelKeys() const;
    bool isFavoriteEntry(const TabEntry &entry) const;
    void setFavoriteEntry(const TabEntry &entry, bool favorite);
    void toggleFavorite();
    ChannelEntry channelEntryForTab(const TabEntry &entry) const;

    ChannelTreeModel *model;
    ServerRailModel *railModel;
    TabEntry currentEntry;
    QList<TabEntry> recentEntries;
    QLineEdit *searchEdit = nullptr;
    QLabel *hintLabel = nullptr;
    QTabWidget *sectionTabs = nullptr;
    QTreeWidget *allTree = nullptr;
    QTreeWidget *favoritesTree = nullptr;
    QTreeWidget *recentTree = nullptr;
    QPushButton *favoriteButton = nullptr;
    QPushButton *openButton = nullptr;
    QPushButton *closeButton = nullptr;
    QPushButton *sortButton = nullptr;
    SortMode sortMode = SortMode::ServerOrder;
    QWidget *panel = nullptr;
    QGraphicsOpacityEffect *panelOpacity = nullptr;
    QPropertyAnimation *panelFadeAnimation = nullptr;
    bool introAnimationPlayed = false;
    QSet<QString> collapsedGuilds;
    Acheron::Discord::Client *discordClient = nullptr;
    Core::Snowflake apiAccountId;
    QTimer *dmSearchTimer = nullptr;
    QList<Acheron::Discord::Channel> apiDMs;
    QString pendingFilterText;
    bool dmInitialFetchTriggered = false;
    bool initialTreeBuilt = false;
};

} // namespace UI
} // namespace Acheron
