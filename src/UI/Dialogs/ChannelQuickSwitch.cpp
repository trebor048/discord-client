#include "ChannelQuickSwitch.hpp"

#include "Core/AnimationUtils.hpp"
#include "Core/Animation/AnimationConfig.hpp"
#include "Core/Theme/Manager.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPointer>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QSettings>
#include <QShowEvent>
#include <QTabWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

#include "UI/ChannelList/ChannelNode.hpp"
#include "UI/ChannelList/ChannelTreeModel.hpp"
#include "UI/ChannelList/ServerRailModel.hpp"

#include "Core/Result.hpp"
#include "Discord/Client.hpp"

namespace Acheron {
namespace UI {

namespace {
enum ItemRoles {
    ItemKindRole = Qt::UserRole,
    ChannelIdRole,
    GuildIdRole,
    AccountIdRole,
    IsDmRole,
    SearchKeyRole,
    UnreadRole,
    MentionCountRole,
    MutedRole,
};

constexpr char kFavoriteChannelsKey[] = "channelQuickSwitch/favorites";

class QuickSwitchTreeWidget : public QTreeWidget
{
public:
    explicit QuickSwitchTreeWidget(ChannelQuickSwitch *dialog, QWidget *parent = nullptr)
        : QTreeWidget(parent), dialog(dialog)
    {
    }

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            QTreeWidgetItem *item = itemAt(event->pos());
            if (item && item->childCount() > 0) {
                qint64 now = QDateTime::currentMSecsSinceEpoch();
                if (lastHeaderItem == item && now - lastHeaderToggleMs < QApplication::doubleClickInterval()) {
                    event->accept();
                    return;
                }
                item->setExpanded(!item->isExpanded());
                lastHeaderItem = item;
                lastHeaderToggleMs = now;
                event->accept();
                return;
            }
        }

        QTreeWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            QTreeWidgetItem *item = itemAt(event->pos());
            if (item && item->childCount() == 0) {
                if (dialog && dialog->selectedEntry().channelId.isValid())
                    dialog->accept();
                event->accept();
                return;
            }
        }

        QTreeWidget::mouseDoubleClickEvent(event);
    }

    void keyPressEvent(QKeyEvent *event) override
    {
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && dialog) {
            QTreeWidgetItem *item = currentItem();
            if (!item)
                return;
            if (item->childCount() > 0) {
                item->setExpanded(!item->isExpanded());
                event->accept();
                return;
            }
            if (dialog->selectedEntry().channelId.isValid())
                dialog->accept();
            event->accept();
            return;
        }

        QTreeWidget::keyPressEvent(event);
    }

private:
    ChannelQuickSwitch *dialog;
    QTreeWidgetItem *lastHeaderItem = nullptr;
    qint64 lastHeaderToggleMs = 0;
};
} // namespace

ChannelQuickSwitch::ChannelQuickSwitch(ChannelTreeModel *model, ServerRailModel *railModel,
                                       const TabEntry &currentEntry,
                                       const QList<TabEntry> &recentEntries,
                                       Discord::Client *discordClient, QWidget *parent)
    : QDialog(parent), model(model), railModel(railModel), currentEntry(currentEntry),
      recentEntries(recentEntries), discordClient(discordClient)
{
    setWindowTitle(tr("Quick Switch"));
    setModal(true);
    setWindowFlag(Qt::FramelessWindowHint, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMinimumSize(460, 560);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 16);

    panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("quickSwitchPanel"));
    panel->setStyleSheet(QStringLiteral(
            "#quickSwitchPanel { background: palette(window); border: 1px solid palette(mid); "
            "border-radius: %1px; }")
                                 .arg(Core::Theme::Manager::instance().roundness()));
    panelOpacity = new QGraphicsOpacityEffect(panel);
    panelOpacity->setOpacity(0.0);
    panel->setGraphicsEffect(panelOpacity);
    panelFadeAnimation = new QPropertyAnimation(panelOpacity, "opacity", this);
    panelFadeAnimation->setDuration(Core::AnimationConfig::instance().scaled(220));
    panelFadeAnimation->setEasingCurve(QEasingCurve::OutCubic);

    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    searchEdit = new QLineEdit(panel);
    searchEdit->setPlaceholderText(tr("Search channels, servers, or DMs"));
    searchEdit->setClearButtonEnabled(true);
    layout->addWidget(searchEdit);

    hintLabel = new QLabel(tr("Use Up/Down to move, Enter to switch, Esc to close"), panel);
    hintLabel->setStyleSheet(QStringLiteral("color: palette(placeholder-text); font-size: 11px;"));
    layout->addWidget(hintLabel);

    sectionTabs = new QTabWidget(panel);
    sectionTabs->addTab(new QWidget(sectionTabs), tr("All"));
    sectionTabs->addTab(new QWidget(sectionTabs), tr("Favorites"));
    sectionTabs->addTab(new QWidget(sectionTabs), tr("Recent"));
    layout->addWidget(sectionTabs, 1);

    const int rSmall = std::max(2, Core::Theme::Manager::instance().roundness() / 2);
    const QString treeStyle = QStringLiteral(
            "QTreeWidget { background: transparent; border: none; }"
            "QTreeWidget::item { padding: 6px 4px; border-radius: %1px; }"
            "QTreeWidget::item:has-children { margin-top: 6px; padding-top: 8px; color: palette(text); }"
            "QTreeWidget::item:!has-children { padding-left: 8px; }"
            "QTreeWidget::item:selected { background: palette(highlight); color: palette(highlighted-text); }")
                                      .arg(rSmall);

    auto configureTree = [&](QTreeWidget *tree, bool grouped) {
        tree->setColumnCount(1);
        tree->setHeaderHidden(true);
        tree->setRootIsDecorated(grouped);
        tree->setIndentation(grouped ? 18 : 0);
        tree->setSelectionMode(QAbstractItemView::SingleSelection);
        tree->setUniformRowHeights(false);
        tree->setAlternatingRowColors(false);
        tree->setExpandsOnDoubleClick(false);
        tree->setAnimated(grouped);
        tree->setFocusPolicy(Qt::StrongFocus);
        tree->setAllColumnsShowFocus(true);
        tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        tree->header()->setStretchLastSection(true);
        tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
        tree->header()->hide();
        tree->setStyleSheet(treeStyle);
    };

    auto addTreePage = [&](QTreeWidget *&tree, int tabIndex, bool grouped) {
        QWidget *page = sectionTabs->widget(tabIndex);
        auto *pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 6, 0, 0);
        tree = new QuickSwitchTreeWidget(this, page);
        configureTree(tree, grouped);
        pageLayout->addWidget(tree, 1);
    };

    addTreePage(allTree, static_cast<int>(Section::All), true);
    addTreePage(favoritesTree, static_cast<int>(Section::Favorites), false);
    addTreePage(recentTree, static_cast<int>(Section::Recent), false);

    auto *footer = new QWidget(panel);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(0, 0, 0, 0);

    sortButton = new QPushButton(footer);
    sortButton->setFlat(true);
    sortButton->setFixedSize(24, 24);
    sortButton->setToolTip(tr("Sort: Server order (click to change)"));
    sortButton->setStyleSheet(QStringLiteral(
            "QPushButton { border: none; border-radius: 4px; }"
            "QPushButton:hover { background: palette(light); }"));
    connect(sortButton, &QPushButton::clicked, this, &ChannelQuickSwitch::cycleSortMode);
    footerLayout->addWidget(sortButton);

    footerLayout->addStretch();
    openButton = new QPushButton(tr("Open"), footer);
    openButton->setEnabled(false);
    footerLayout->addWidget(openButton);
    connect(openButton, &QPushButton::clicked, this, &ChannelQuickSwitch::acceptCurrentSelection);

    favoriteButton = new QPushButton(tr("Favorite"), footer);
    favoriteButton->setCheckable(true);
    favoriteButton->setEnabled(false);
    footerLayout->addWidget(favoriteButton);

    closeButton = new QPushButton(tr("Close"), footer);
    connect(closeButton, &QPushButton::clicked, this, &ChannelQuickSwitch::reject);
    footerLayout->addWidget(closeButton);
    layout->addWidget(footer);

    outer->addWidget(panel, 1);

    connect(searchEdit, &QLineEdit::textChanged, this, &ChannelQuickSwitch::rebuildList);
    searchEdit->installEventFilter(this);

    if (discordClient) {
        apiAccountId = discordClient->getMe().id;
        dmSearchTimer = new QTimer(this);
        dmSearchTimer->setSingleShot(true);
        dmSearchTimer->setInterval(350);
        connect(dmSearchTimer, &QTimer::timeout, this, &ChannelQuickSwitch::startDMSearch);
    }
    connect(searchEdit, &QLineEdit::returnPressed, this, [this]() {
        acceptCurrentSelection();
    });

    connect(sectionTabs, &QTabWidget::currentChanged, this, [this]() {
        rebuildCurrentSection();
        if (QTreeWidget *tree = activeTree()) {
            Acheron::Core::AnimationUtils::fadeIn(tree, 120);
            tree->setFocus();
        }
    });
    connect(favoriteButton, &QAbstractButton::clicked, this, &ChannelQuickSwitch::toggleFavorite);

    auto bindTreeSignals = [this](QTreeWidget *tree, bool trackCollapse) {
        connect(tree, &QTreeWidget::itemExpanded, this, [this, trackCollapse](QTreeWidgetItem *item) {
            if (!trackCollapse || !item || item->childCount() == 0)
                return;
            collapsedGuilds.remove(guildKey(item->data(0, AccountIdRole).toULongLong(),
                                            item->data(0, GuildIdRole).toULongLong()));
        });
        connect(tree, &QTreeWidget::itemCollapsed, this, [this, trackCollapse](QTreeWidgetItem *item) {
            if (!trackCollapse || !item || item->childCount() == 0)
                return;
            collapsedGuilds.insert(guildKey(item->data(0, AccountIdRole).toULongLong(),
                                            item->data(0, GuildIdRole).toULongLong()));
        });
        connect(tree, &QTreeWidget::currentItemChanged, this,
                [this](QTreeWidgetItem *, QTreeWidgetItem *) { updateFavoriteButton(); });
        connect(tree, &QTreeWidget::itemActivated, this,
                [this](QTreeWidgetItem *, int) { acceptCurrentSelection(); });
    };

    bindTreeSignals(allTree, true);
    bindTreeSignals(favoritesTree, false);
    bindTreeSignals(recentTree, false);

    searchEdit->setFocus();
}

void ChannelQuickSwitch::done(int r)
{
    Acheron::Core::AnimationUtils::fadeOut(panel, 100);
    QTimer::singleShot(110, this, [this, r]() {
        QDialog::done(r);
    });
}

TabEntry ChannelQuickSwitch::selectedEntry() const
{
    QTreeWidget *tree = activeTree();
    if (!tree)
        return {};

    QTreeWidgetItem *item = tree->currentItem();
    return entryForItem(item);
}

bool ChannelQuickSwitch::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == searchEdit && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            reject();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Down || keyEvent->key() == Qt::Key_Up ||
            keyEvent->key() == Qt::Key_PageDown || keyEvent->key() == Qt::Key_PageUp) {
            if (QTreeWidget *tree = activeTree()) {
                tree->setFocus();
                if (!tree->currentItem())
                    setFirstSelectableItemCurrent();
                QCoreApplication::sendEvent(tree, event);
                return true;
            }
        }
    }

    return QDialog::eventFilter(obj, event);
}

void ChannelQuickSwitch::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);

    if (introAnimationPlayed || !panelOpacity || !panelFadeAnimation)
        return;

    introAnimationPlayed = true;
    panelFadeAnimation->stop();
    panelOpacity->setOpacity(0.0);
    panelFadeAnimation->setStartValue(0.0);
    panelFadeAnimation->setEndValue(1.0);
    panelFadeAnimation->start();

    // Build the initial tree lazily on first show instead of in the constructor,
    // so opening the picker doesn't block on a full rebuild of every guild.
    if (model && !initialTreeBuilt) {
        initialTreeBuilt = true;
        rebuildList({});
    }

    if (QTreeWidget *tree = activeTree())
        Acheron::Core::AnimationUtils::fadeIn(tree, 180);

    if (discordClient && dmSearchTimer && !dmInitialFetchTriggered) {
        dmInitialFetchTriggered = true;
        pendingFilterText.clear();
        QPointer<ChannelQuickSwitch> guard(this);
        discordClient->fetchDMChannels([guard](const Core::Result<QList<Discord::Channel>> &result) {
            if (guard)
                guard->handleDMResults(result);
        });
    }
}

QIcon ChannelQuickSwitch::iconForIndex(const QModelIndex &index) const
{
    const QPixmap pixmap = qvariant_cast<QPixmap>(index.data(Qt::DecorationRole));
    return pixmap.isNull() ? QIcon() : QIcon(pixmap);
}

QString ChannelQuickSwitch::accountNameFor(const QModelIndex &accountIndex) const
{
    return accountIndex.isValid() ? accountIndex.data(Qt::DisplayRole).toString() : QString();
}

QString ChannelQuickSwitch::guildNameFor(const QModelIndex &guildIndex) const
{
    return guildIndex.isValid() ? guildIndex.data(Qt::DisplayRole).toString() : QString();
}

QString ChannelQuickSwitch::guildKey(Core::Snowflake accountId, Core::Snowflake guildId) const
{
    return QStringLiteral("%1:%2")
            .arg(static_cast<qulonglong>(accountId))
            .arg(static_cast<qulonglong>(guildId));
}

bool ChannelQuickSwitch::isChannelItem(const QTreeWidgetItem *item) const
{
    return item && item->childCount() == 0 &&
           item->data(0, ItemKindRole).toInt() == static_cast<int>(ItemKind::Channel);
}

TabEntry ChannelQuickSwitch::entryForItem(const QTreeWidgetItem *item) const
{
    if (!isChannelItem(item))
        return {};

    TabEntry entry;
    entry.channelId = Core::Snowflake(item->data(0, ChannelIdRole).toULongLong());
    entry.guildId = Core::Snowflake(item->data(0, GuildIdRole).toULongLong());
    entry.accountId = Core::Snowflake(item->data(0, AccountIdRole).toULongLong());
    entry.isDm = item->data(0, IsDmRole).toBool();
    entry.name = item->text(0);
    return entry;
}

QString ChannelQuickSwitch::channelKey(const TabEntry &entry) const
{
    return QStringLiteral("%1:%2:%3:%4")
            .arg(static_cast<qulonglong>(entry.accountId))
            .arg(static_cast<qulonglong>(entry.guildId))
            .arg(static_cast<qulonglong>(entry.channelId))
            .arg(entry.isDm ? 1 : 0);
}

bool ChannelQuickSwitch::itemMatchesFilter(const ChannelEntry &entry, const QString &filterText) const
{
    if (filterText.isEmpty())
        return true;
    return entry.searchKey.contains(filterText);
}

QList<ChannelQuickSwitch::ChannelEntry> ChannelQuickSwitch::collectChannels(
        const QModelIndex &parentIndex, const QString &guildName, Core::Snowflake accountId,
        Core::Snowflake guildId, const QString &accountName) const
{
    QList<ChannelEntry> out;
    if (!model)
        return out;

    std::function<void(const QModelIndex &)> walk = [&](const QModelIndex &parent) {
        const int rows = model->rowCount(parent);
        for (int row = 0; row < rows; ++row) {
            QModelIndex idx = model->index(row, 0, parent);
            if (!idx.isValid())
                continue;

            auto type = static_cast<ChannelNode::Type>(idx.data(ChannelTreeModel::TypeRole).toInt());
            if (type == ChannelNode::Type::Channel || type == ChannelNode::Type::VoiceChannel) {
                ChannelEntry entry;
                entry.tab.channelId = Core::Snowflake(idx.data(ChannelTreeModel::IdRole).toULongLong());
                entry.tab.guildId = guildId;
                entry.tab.accountId = accountId;
                entry.tab.name = idx.data(Qt::DisplayRole).toString();
                entry.tab.isDm = false;
                entry.displayText = entry.tab.name;
                entry.secondaryText = guildName;
                entry.icon = iconForIndex(idx);
                entry.searchKey = QStringLiteral("%1 %2 %3")
                                          .arg(entry.displayText, guildName, accountName)
                                          .toCaseFolded();
                out.append(entry);
            }

            if (type == ChannelNode::Type::Category || type == ChannelNode::Type::Account ||
                type == ChannelNode::Type::Server || type == ChannelNode::Type::Folder ||
                type == ChannelNode::Type::DMHeader) {
                walk(idx);
            }
        }
    };

    walk(parentIndex);
    return out;
}

QList<ChannelQuickSwitch::ChannelEntry> ChannelQuickSwitch::collectDMChannels(
        Core::Snowflake accountId, const QString &accountName) const
{
    QList<ChannelEntry> out;
    if (!model || !accountId.isValid())
        return out;

    QModelIndex dmHeader = model->dmHeaderIndex(accountId);
    if (!dmHeader.isValid())
        return out;

    const int rows = model->rowCount(dmHeader);
    for (int row = 0; row < rows; ++row) {
        QModelIndex idx = model->index(row, 0, dmHeader);
        if (!idx.isValid())
            continue;

        auto type = static_cast<ChannelNode::Type>(idx.data(ChannelTreeModel::TypeRole).toInt());
        if (type != ChannelNode::Type::DMChannel)
            continue;

        ChannelEntry entry;
        entry.tab.channelId = Core::Snowflake(idx.data(ChannelTreeModel::IdRole).toULongLong());
        entry.tab.guildId = Core::Snowflake();
        entry.tab.accountId = accountId;
        entry.tab.name = idx.data(Qt::DisplayRole).toString();
        entry.tab.isDm = true;
        entry.displayText = entry.tab.name;
        entry.secondaryText = tr("Direct Messages");
        entry.icon = iconForIndex(idx);
        entry.searchKey = QStringLiteral("%1 %2 %3")
                                  .arg(entry.displayText, entry.secondaryText, accountName)
                                  .toCaseFolded();
        out.append(entry);
    }
    return out;
}

QList<ChannelQuickSwitch::ChannelEntry> ChannelQuickSwitch::collectApiDMChannels(
        Core::Snowflake accountId, const QString &accountName,
        const QList<Discord::Channel> &channels) const
{
    QList<ChannelEntry> out;

    QSet<Core::Snowflake> modelIds;
    {
        QModelIndex dmHeader = model ? model->dmHeaderIndex(accountId) : QModelIndex();
        if (dmHeader.isValid()) {
            const int rows = model->rowCount(dmHeader);
            for (int row = 0; row < rows; ++row) {
                QModelIndex idx = model->index(row, 0, dmHeader);
                if (!idx.isValid())
                    continue;
                modelIds.insert(Core::Snowflake(idx.data(ChannelTreeModel::IdRole).toULongLong()));
            }
        }
    }

    for (const auto &channel : channels) {
        if (!channel.id.hasValue())
            continue;
        if (channel.guildId.hasValue())
            continue;
        const Core::Snowflake cid = channel.id.get();
        if (modelIds.contains(cid))
            continue;

        QString displayName = channel.name.hasValue() ? channel.name.get() : QString();
        if (displayName.isEmpty() && channel.recipients.hasValue()) {
            QStringList names;
            for (const auto &user : channel.recipients.get())
                names.append(user.getDisplayName());
            if (names.isEmpty())
                names.append(tr("Unnamed"));
            displayName = names.join(QStringLiteral(", "));
        }
        if (displayName.isEmpty())
            displayName = tr("Unnamed");

        ChannelEntry entry;
        entry.tab.channelId = cid;
        entry.tab.guildId = Core::Snowflake();
        entry.tab.accountId = accountId;
        entry.tab.name = displayName;
        entry.tab.isDm = true;
        entry.displayText = displayName;
        entry.secondaryText = tr("Direct Messages");
        entry.icon = QIcon();
        entry.searchKey = QStringLiteral("%1 %2 %3")
                                  .arg(entry.displayText, entry.secondaryText, accountName)
                                  .toCaseFolded();
        out.append(entry);
    }
    return out;
}

void ChannelQuickSwitch::startDMSearch()
{
    if (!discordClient)
        return;
    const QString filter = pendingFilterText.trimmed();
    if (filter.isEmpty())
        return;

    // The initial DM list is fetched once in showEvent(); results (even an
    // empty list) are stored in apiDMs.  Filtering is client-side, so there is
    // nothing to gain from refetching on every keystroke.
    if (dmInitialFetchTriggered)
        return;

    QPointer<ChannelQuickSwitch> guard(this);
    discordClient->fetchDMChannels([guard](const Core::Result<QList<Discord::Channel>> &result) {
        if (guard)
            guard->handleDMResults(result);
    });
}

void ChannelQuickSwitch::handleDMResults(const Core::Result<QList<Discord::Channel>> &result)
{
    if (!result.success())
        return;

    apiDMs = result.value.value();
    rebuildCurrentSection();
}

ChannelQuickSwitch::GuildSection ChannelQuickSwitch::buildDMSection(
        Core::Snowflake accountId, const QString &accountTitle) const
{
    GuildSection section;
    section.accountId = accountId;
    section.guildId = Core::Snowflake();
    section.title = accountTitle;
    section.channels = collectDMChannels(accountId, accountTitle);
    if (accountId == apiAccountId && !apiDMs.isEmpty())
        section.channels.append(collectApiDMChannels(accountId, accountTitle, apiDMs));
    section.searchKey = accountTitle.toCaseFolded();
    return section;
}

ChannelQuickSwitch::ChannelEntry ChannelQuickSwitch::channelEntryForTab(const TabEntry &entry) const
{
    ChannelEntry row;
    row.tab = entry;
    row.displayText = entry.name.isEmpty() ? tr("(no channel)") : entry.name;
    row.icon = QIcon();
    QString accountName;

    if (model) {
        ChannelNode *node = model->findChannelTreeNode(entry.channelId, entry.accountId);
        if (node) {
            ChannelNode *guildNode = ChannelTreeModel::findGuildNode(node);
            if (guildNode && !entry.isDm) {
                QModelIndex guildIndex = model->indexForNode(guildNode);
                if (guildIndex.isValid())
                    row.icon = iconForIndex(guildIndex);
            } else {
                QModelIndex idx = model->indexForNode(node);
                if (idx.isValid())
                    row.icon = iconForIndex(idx);
            }
            if (guildNode)
                row.secondaryText = guildNode->name;
            ChannelNode *accountNode = ChannelTreeModel::getAccountNodeFor(node);
            if (accountNode)
                accountName = accountNode->name;
        }
    }

    if (row.secondaryText.isEmpty())
        row.secondaryText = entry.isDm ? tr("Direct Messages") : QString();

    row.searchKey = QStringLiteral("%1 %2 %3")
                            .arg(row.displayText, row.secondaryText, accountName)
                            .toCaseFolded();
    return row;
}

ChannelQuickSwitch::GuildSection ChannelQuickSwitch::buildGuildSection(Core::Snowflake accountId,
                                                                       Core::Snowflake guildId,
                                                                       const QString &title,
                                                                       const QIcon &icon) const
{
    GuildSection section;
    section.accountId = accountId;
    section.guildId = guildId;
    section.title = title;
    section.icon = icon;

    if (!model || !accountId.isValid() || !guildId.isValid())
        return section;

    QModelIndex guildIndex = model->serverIndex(accountId, guildId);
    if (!guildIndex.isValid())
        return section;

    QString accountName;
    QModelIndex cursor = guildIndex.parent();
    while (cursor.isValid()) {
        auto type = static_cast<ChannelNode::Type>(cursor.data(ChannelTreeModel::TypeRole).toInt());
        if (type == ChannelNode::Type::Account) {
            accountName = accountNameFor(cursor);
            break;
        }
        cursor = cursor.parent();
    }

    section.channels = collectChannels(guildIndex, title, accountId, guildId, accountName);
    section.searchKey = QStringLiteral("%1 %2").arg(title, accountName).toCaseFolded();
    return section;
}

QList<ChannelQuickSwitch::GuildSection> ChannelQuickSwitch::buildAllGuildSections() const
{
    QList<GuildSection> sections;
    if (!model)
        return sections;

    const bool skipCurrentGuild = currentEntry.channelId.isValid() && currentEntry.guildId.isValid() &&
                                  !currentEntry.isDm && currentEntry.accountId.isValid();

    auto appendFromSource = [&](Core::Snowflake accountId, Core::Snowflake guildId,
                                const QString &title, const QIcon &icon) {
        if (skipCurrentGuild && accountId == currentEntry.accountId && guildId == currentEntry.guildId)
            return;
        GuildSection section = buildGuildSection(accountId, guildId, title, icon);
        if (!section.channels.isEmpty())
            sections.append(std::move(section));
    };

    if (railModel) {
        std::function<void(const QModelIndex &)> walk = [&](const QModelIndex &parent) {
            const int rows = railModel->rowCount(parent);
            for (int row = 0; row < rows; ++row) {
                QModelIndex idx = railModel->index(row, 0, parent);
                if (!idx.isValid())
                    continue;

                auto kind = static_cast<ServerRailModel::Kind>(idx.data(ServerRailModel::KindRole).toInt());
                if (kind == ServerRailModel::Kind::Folder) {
                    walk(idx);
                    continue;
                }

                if (kind != ServerRailModel::Kind::Server)
                    continue;

                Core::Snowflake accountId(idx.data(ServerRailModel::AccountIdRole).toULongLong());
                Core::Snowflake guildId(idx.data(ServerRailModel::IdRole).toULongLong());
                QString title = idx.data(Qt::DisplayRole).toString();
                QIcon icon = iconForIndex(model->serverIndex(accountId, guildId));
                appendFromSource(accountId, guildId, title, icon);
            }
        };
        walk({});
        // Also collect DM channels for all accounts
        {
            const int actRows = model->rowCount({});
            for (int actRow = 0; actRow < actRows; ++actRow) {
                QModelIndex actIndex = model->index(actRow, 0, {});
                if (!actIndex.isValid())
                    continue;
                auto actType = static_cast<ChannelNode::Type>(actIndex.data(ChannelTreeModel::TypeRole).toInt());
                if (actType != ChannelNode::Type::Account)
                    continue;
                Core::Snowflake actId(actIndex.data(ChannelTreeModel::IdRole).toULongLong());
                GuildSection dmSection = buildDMSection(actId,
                        tr("Direct Messages — %1").arg(accountNameFor(actIndex)));
                if (!dmSection.channels.isEmpty())
                    sections.append(std::move(dmSection));
            }
        }
        return sections;
    }

    const int accountRows = model->rowCount({});
    for (int accountRow = 0; accountRow < accountRows; ++accountRow) {
        QModelIndex accountIndex = model->index(accountRow, 0, {});
        if (!accountIndex.isValid())
            continue;

        auto accountType = static_cast<ChannelNode::Type>(accountIndex.data(ChannelTreeModel::TypeRole).toInt());
        if (accountType != ChannelNode::Type::Account)
            continue;

        const Core::Snowflake accountId(accountIndex.data(ChannelTreeModel::IdRole).toULongLong());
        const QString accountName = accountNameFor(accountIndex);
        const int childRows = model->rowCount(accountIndex);
        for (int childRow = 0; childRow < childRows; ++childRow) {
            QModelIndex childIndex = model->index(childRow, 0, accountIndex);
            if (!childIndex.isValid())
                continue;
            auto childType = static_cast<ChannelNode::Type>(childIndex.data(ChannelTreeModel::TypeRole).toInt());
            if (childType != ChannelNode::Type::Server)
                continue;
            const Core::Snowflake guildId(childIndex.data(ChannelTreeModel::IdRole).toULongLong());
            if (skipCurrentGuild && accountId == currentEntry.accountId && guildId == currentEntry.guildId)
                continue;
            const QString guildName = childIndex.data(Qt::DisplayRole).toString();
            QIcon icon = iconForIndex(childIndex);
            GuildSection section = buildGuildSection(accountId, guildId, guildName, icon);
            if (!section.channels.isEmpty())
                sections.append(std::move(section));
        }
        // DM channels for this account
        GuildSection dmSection = buildDMSection(accountId,
                tr("Direct Messages — %1").arg(accountName));
        if (!dmSection.channels.isEmpty())
            sections.append(std::move(dmSection));
    }

    return sections;
}

QList<ChannelQuickSwitch::ChannelEntry> ChannelQuickSwitch::buildRecentItems() const
{
    QList<ChannelEntry> items;
    items.reserve(recentEntries.size());
    for (const auto &entry : recentEntries) {
        if (!entry.channelId.isValid())
            continue;
        items.append(channelEntryForTab(entry));
    }
    return items;
}

QStringList ChannelQuickSwitch::favoriteChannelKeys() const
{
    QSettings settings;
    QStringList keys = settings.value(QLatin1String(kFavoriteChannelsKey)).toStringList();
    keys.removeDuplicates();
    return keys;
}

bool ChannelQuickSwitch::isFavoriteEntry(const TabEntry &entry) const
{
    if (!entry.channelId.isValid())
        return false;
    return favoriteChannelKeys().contains(channelKey(entry));
}

void ChannelQuickSwitch::setFavoriteEntry(const TabEntry &entry, bool favorite)
{
    if (!entry.channelId.isValid())
        return;

    QStringList keys = favoriteChannelKeys();
    const QString key = channelKey(entry);
    keys.removeAll(key);
    if (favorite)
        keys.prepend(key);

    QSettings settings;
    settings.setValue(QLatin1String(kFavoriteChannelsKey), keys);
}

QList<ChannelQuickSwitch::ChannelEntry> ChannelQuickSwitch::buildFavoriteItems() const
{
    QList<ChannelEntry> items;
    const QStringList keys = favoriteChannelKeys();
    items.reserve(keys.size());

    for (const QString &key : keys) {
        for (const auto &entry : recentEntries) {
            if (channelKey(entry) == key) {
                items.append(channelEntryForTab(entry));
                break;
            }
        }
    }

    const QList<GuildSection> sections = buildAllGuildSections();
    for (const auto &section : sections) {
        for (const auto &channel : section.channels) {
            if (keys.contains(channelKey(channel.tab)) &&
                std::none_of(items.begin(), items.end(), [&](const ChannelEntry &entry) {
                    return channelKey(entry.tab) == channelKey(channel.tab);
                })) {
                items.append(channel);
            }
        }
    }

    if (currentEntry.accountId.isValid() && currentEntry.guildId.isValid() && !currentEntry.isDm) {
        QString currentGuildName;
        QIcon currentGuildIcon;
        if (model) {
            QModelIndex guildIndex = model->serverIndex(currentEntry.accountId, currentEntry.guildId);
            if (guildIndex.isValid()) {
                currentGuildName = guildIndex.data(Qt::DisplayRole).toString();
                currentGuildIcon = iconForIndex(guildIndex);
            }
        }
        GuildSection currentGuild = buildGuildSection(currentEntry.accountId, currentEntry.guildId,
                                                      currentGuildName, currentGuildIcon);
        for (const auto &channel : currentGuild.channels) {
            if (keys.contains(channelKey(channel.tab)) &&
                std::none_of(items.begin(), items.end(), [&](const ChannelEntry &entry) {
                    return channelKey(entry.tab) == channelKey(channel.tab);
                })) {
                items.append(channel);
            }
        }
    }

    if (currentEntry.channelId.isValid() && isFavoriteEntry(currentEntry)) {
        const QString currentKey = channelKey(currentEntry);
        if (std::none_of(items.begin(), items.end(), [&](const ChannelEntry &entry) {
                return channelKey(entry.tab) == currentKey;
            })) {
            items.prepend(channelEntryForTab(currentEntry));
        }
    }

    return items;
}

QTreeWidget *ChannelQuickSwitch::activeTree() const
{
    if (!sectionTabs)
        return nullptr;

    switch (static_cast<Section>(sectionTabs->currentIndex())) {
    case Section::All:
        return allTree;
    case Section::Favorites:
        return favoritesTree;
    case Section::Recent:
        return recentTree;
    }
    return allTree;
}

void ChannelQuickSwitch::updateHintLabel(const QString &filterText) const
{
    if (!hintLabel)
        return;

    hintLabel->setText(filterText.trimmed().isEmpty()
                               ? tr("Use Up/Down to move, Enter to open in a new tab, Esc to close")
                               : tr("Filtering channels as you type"));
}

void ChannelQuickSwitch::updateFavoriteButton()
{
    if (!favoriteButton)
        return;

    const TabEntry entry = selectedEntry();
    const bool valid = entry.channelId.isValid();
    if (openButton)
        openButton->setEnabled(valid);
    favoriteButton->setEnabled(valid);
    favoriteButton->setChecked(valid && isFavoriteEntry(entry));
    favoriteButton->setText(valid && isFavoriteEntry(entry) ? tr("Remove Favorite")
                                                            : tr("Add Favorite"));
    favoriteButton->setToolTip(valid && isFavoriteEntry(entry)
                                       ? tr("Remove this channel from favorites")
                                       : tr("Add this channel to favorites"));
}

void ChannelQuickSwitch::toggleFavorite()
{
    const TabEntry entry = selectedEntry();
    if (!entry.channelId.isValid())
        return;

    const bool favorite = favoriteButton && favoriteButton->isChecked();
    setFavoriteEntry(entry, favorite);
    rebuildCurrentSection();
    updateFavoriteButton();
}

void ChannelQuickSwitch::setFirstSelectableItemCurrent()
{
    QTreeWidget *tree = activeTree();
    if (!tree)
        return;

    QTreeWidgetItem *firstLeaf = nullptr;
    std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *item) {
        if (!item || firstLeaf)
            return;
        if (isChannelItem(item)) {
            firstLeaf = item;
            return;
        }
        for (int i = 0; i < item->childCount(); ++i)
            walk(item->child(i));
    };

    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        walk(tree->topLevelItem(i));
        if (firstLeaf)
            break;
    }

    if (firstLeaf) {
        QTreeWidgetItem *ancestor = firstLeaf->parent();
        while (ancestor) {
            ancestor->setExpanded(true);
            ancestor = ancestor->parent();
        }
        tree->setCurrentItem(firstLeaf);
        tree->scrollToItem(firstLeaf, QAbstractItemView::PositionAtTop);
    } else if (tree->topLevelItemCount() > 0) {
        QTreeWidgetItem *firstTop = tree->topLevelItem(0);
        tree->setCurrentItem(firstTop);
    }
}

void ChannelQuickSwitch::acceptCurrentSelection()
{
    TabEntry entry = selectedEntry();
    if (entry.channelId.isValid())
        accept();
}

void ChannelQuickSwitch::rebuildList(const QString &filterText)
{
    updateHintLabel(filterText);
    rebuildCurrentSection();

    if (discordClient && dmSearchTimer) {
        pendingFilterText = filterText;
        dmSearchTimer->start();
    }
}

void ChannelQuickSwitch::rebuildCurrentSection()
{
    const QString filterText = searchEdit ? searchEdit->text() : QString();

    switch (static_cast<Section>(sectionTabs ? sectionTabs->currentIndex() : 0)) {
    case Section::All:
        rebuildAllTree(filterText);
        break;
    case Section::Favorites:
        rebuildFlatTree(favoritesTree, buildFavoriteItems(), filterText, tr("No favorite channels yet"));
        break;
    case Section::Recent:
        rebuildFlatTree(recentTree, buildRecentItems(), filterText, tr("No recent channels yet"));
        break;
    }

    updateFavoriteButton();
}

void ChannelQuickSwitch::appendChannelItem(QTreeWidgetItem *parent, const ChannelEntry &entry,
                                            bool showSecondaryText)
{
    auto *item = new QTreeWidgetItem(parent);
    item->setText(0, showSecondaryText && !entry.secondaryText.isEmpty()
                             ? QStringLiteral("%1  •  %2").arg(entry.displayText, entry.secondaryText)
                             : entry.displayText);
    item->setIcon(0, entry.icon);
    item->setData(0, ItemKindRole, static_cast<int>(ItemKind::Channel));
    item->setData(0, ChannelIdRole, static_cast<qulonglong>(entry.tab.channelId));
    item->setData(0, GuildIdRole, static_cast<qulonglong>(entry.tab.guildId));
    item->setData(0, AccountIdRole, static_cast<qulonglong>(entry.tab.accountId));
    item->setData(0, IsDmRole, entry.tab.isDm);
    item->setData(0, SearchKeyRole, entry.searchKey);
    item->setToolTip(0, entry.secondaryText);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);

    // Read state indicators
    if (model && entry.tab.channelId.isValid()) {
        ChannelNode *node = model->findChannelTreeNode(entry.tab.channelId, entry.tab.accountId);
        if (node) {
            QModelIndex idx = model->indexForNode(node);
            if (idx.isValid()) {
                const bool isUnread = idx.data(ChannelTreeModel::IsUnreadRole).toBool();
                const int mentionCount = idx.data(ChannelTreeModel::MentionCountRole).toInt();
                const bool isMuted = idx.data(ChannelTreeModel::IsMutedRole).toBool();

                item->setData(0, UnreadRole, isUnread);
                item->setData(0, MentionCountRole, mentionCount);
                item->setData(0, MutedRole, isMuted);

                if (!isMuted) {
                    if (isUnread) {
                        QFont f = item->font(0);
                        f.setBold(true);
                        item->setFont(0, f);
                    }
                    if (mentionCount > 0) {
                        QString txt = item->text(0);
                        item->setText(0, QStringLiteral("%1  (%2)").arg(txt).arg(mentionCount));
                    }
                } else {
                    item->setForeground(0, palette().brush(QPalette::Disabled, QPalette::Text));
                }
            }
        }
    }
}

void ChannelQuickSwitch::sortChannels(QList<ChannelEntry> &channels) const
{
    switch (sortMode) {
    case SortMode::ByName:
        std::sort(channels.begin(), channels.end(), [](const ChannelEntry &a, const ChannelEntry &b) {
            return a.displayText.toCaseFolded().localeAwareCompare(b.displayText.toCaseFolded()) < 0;
        });
        break;
    case SortMode::ByUnread:
        std::sort(channels.begin(), channels.end(), [this](const ChannelEntry &a, const ChannelEntry &b) {
            bool aUnread = false;
            int aMentions = 0;
            if (model && a.tab.channelId.isValid()) {
                ChannelNode *node = model->findChannelTreeNode(a.tab.channelId, a.tab.accountId);
                QModelIndex idx = node ? model->indexForNode(node) : QModelIndex();
                if (idx.isValid()) {
                    aUnread = idx.data(ChannelTreeModel::IsUnreadRole).toBool();
                    aMentions = idx.data(ChannelTreeModel::MentionCountRole).toInt();
                }
            }
            bool bUnread = false;
            int bMentions = 0;
            if (model && b.tab.channelId.isValid()) {
                ChannelNode *node = model->findChannelTreeNode(b.tab.channelId, b.tab.accountId);
                QModelIndex idx = node ? model->indexForNode(node) : QModelIndex();
                if (idx.isValid()) {
                    bUnread = idx.data(ChannelTreeModel::IsUnreadRole).toBool();
                    bMentions = idx.data(ChannelTreeModel::MentionCountRole).toInt();
                }
            }
            if (aMentions != bMentions)
                return aMentions > bMentions;
            if (aUnread != bUnread)
                return aUnread;
            return a.displayText.toCaseFolded().localeAwareCompare(b.displayText.toCaseFolded()) < 0;
        });
        break;
    case SortMode::ServerOrder:
        break;
    }
}

void ChannelQuickSwitch::cycleSortMode()
{
    switch (sortMode) {
    case SortMode::ServerOrder:
        sortMode = SortMode::ByName;
        break;
    case SortMode::ByName:
        sortMode = SortMode::ByUnread;
        break;
    case SortMode::ByUnread:
        sortMode = SortMode::ServerOrder;
        break;
    }
    if (sortButton) {
        switch (sortMode) {
        case SortMode::ServerOrder:
            sortButton->setIcon(QIcon::fromTheme(QStringLiteral("view-sort-ascending")));
            sortButton->setToolTip(tr("Sort: Server order (click to change)"));
            break;
        case SortMode::ByName:
            sortButton->setIcon(QIcon::fromTheme(QStringLiteral("format-text-direction-ltr")));
            sortButton->setToolTip(tr("Sort: Alphabetical (click to change)"));
            break;
        case SortMode::ByUnread:
            sortButton->setIcon(QIcon::fromTheme(QStringLiteral("mail-unread")));
            sortButton->setToolTip(tr("Sort: Unread first (click to change)"));
            break;
        }
    }
    rebuildCurrentSection();
}

void ChannelQuickSwitch::rebuildFlatTree(QTreeWidget *tree, const QList<ChannelEntry> &channels,
                                         const QString &filterText, const QString &emptyText)
{
    if (!tree)
        return;

    tree->clear();
    const QString filter = filterText.trimmed().toCaseFolded();

    QList<ChannelEntry> sorted = channels;
    sortChannels(sorted);

    bool hasMatches = false;
    for (const auto &channel : sorted) {
        if (!itemMatchesFilter(channel, filter))
            continue;
        appendChannelItem(tree->invisibleRootItem(), channel, false);
        hasMatches = true;
    }

    if (!hasMatches) {
        auto *empty = new QTreeWidgetItem(tree);
        empty->setText(0, filterText.isEmpty()
                                  ? emptyText
                                  : tr("No channels found for \"%1\"").arg(filterText.trimmed()));
        empty->setFlags(Qt::NoItemFlags);
        empty->setForeground(0, palette().brush(QPalette::PlaceholderText));
    }

    setFirstSelectableItemCurrent();
}

void ChannelQuickSwitch::rebuildAllTree(const QString &filterText)
{
    if (!allTree)
        return;

    allTree->clear();
    const QString filter = filterText.trimmed().toCaseFolded();
    const bool hasCurrentGuild = currentEntry.channelId.isValid() && currentEntry.guildId.isValid() &&
                                 !currentEntry.isDm && currentEntry.accountId.isValid();

    if (hasCurrentGuild) {
        QModelIndex guildIndex = model->serverIndex(currentEntry.accountId, currentEntry.guildId);
        if (guildIndex.isValid()) {
            auto *item = new QTreeWidgetItem(allTree);
            const int row = allTree->indexOfTopLevelItem(item);
            if (row >= 0)
                allTree->setFirstColumnSpanned(row, allTree->rootIndex(), true);
            item->setText(0, guildIndex.data(Qt::DisplayRole).toString());
            item->setIcon(0, iconForIndex(guildIndex));
            item->setData(0, AccountIdRole, static_cast<qulonglong>(currentEntry.accountId));
            item->setData(0, GuildIdRole, static_cast<qulonglong>(currentEntry.guildId));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            QFont font = item->font(0);
            font.setBold(true);
            item->setFont(0, font);
            item->setExpanded(!collapsedGuilds.contains(guildKey(currentEntry.accountId,
                                                                currentEntry.guildId)) ||
                              !filter.isEmpty());

            QString accountName;
            QModelIndex accountIndex = guildIndex.parent();
            while (accountIndex.isValid()) {
                auto type = static_cast<ChannelNode::Type>(accountIndex.data(ChannelTreeModel::TypeRole).toInt());
                if (type == ChannelNode::Type::Account) {
                    accountName = accountNameFor(accountIndex);
                    break;
                }
                accountIndex = accountIndex.parent();
            }

            QList<ChannelEntry> channels = collectChannels(guildIndex, guildIndex.data(Qt::DisplayRole).toString(),
                                                           currentEntry.accountId, currentEntry.guildId,
                                                           accountName);
            sortChannels(channels);
            for (const auto &channel : channels) {
                if (!itemMatchesFilter(channel, filter))
                    continue;
                appendChannelItem(item, channel, false);
            }
            if (item->childCount() == 0 && !filter.isEmpty()) {
                delete item;
            }
        }
    }

    const auto guildSections = buildAllGuildSections();
    for (const auto &section : guildSections) {
        if (hasCurrentGuild && section.accountId == currentEntry.accountId &&
            section.guildId == currentEntry.guildId) {
            continue;
        }
        if (!section.accountId.isValid())
            continue;

        auto *item = new QTreeWidgetItem(allTree);
        const int row = allTree->indexOfTopLevelItem(item);
        if (row >= 0)
            allTree->setFirstColumnSpanned(row, allTree->rootIndex(), true);
        item->setText(0, section.title);
        item->setIcon(0, section.icon);
        item->setData(0, AccountIdRole, static_cast<qulonglong>(section.accountId));
        item->setData(0, GuildIdRole, static_cast<qulonglong>(section.guildId));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        QFont font = item->font(0);
        font.setBold(true);
        item->setFont(0, font);
        item->setExpanded(!collapsedGuilds.contains(guildKey(section.accountId, section.guildId)) ||
                          !filter.isEmpty());

        bool hasMatches = false;
        QList<ChannelEntry> sectionChannels = section.channels;
        sortChannels(sectionChannels);
        for (const auto &channel : sectionChannels) {
            if (!itemMatchesFilter(channel, filter))
                continue;
            appendChannelItem(item, channel, false);
            hasMatches = true;
        }
        if (!hasMatches && !filter.isEmpty()) {
            delete item;
            continue;
        }
        if (!filter.isEmpty())
            item->setExpanded(true);
    }

    if (allTree->topLevelItemCount() == 0) {
        auto *empty = new QTreeWidgetItem(allTree);
        empty->setText(0, filter.isEmpty()
                                  ? tr("No channels available")
                                  : tr("No channels found for \"%1\"").arg(filterText.trimmed()));
        empty->setFlags(Qt::NoItemFlags);
        empty->setForeground(0, palette().brush(QPalette::PlaceholderText));
    }

    setFirstSelectableItemCurrent();
}

} // namespace UI
} // namespace Acheron
