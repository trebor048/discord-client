#include "ServerRailView.hpp"

#include "ServerRailModel.hpp"
#include "Core/AnimationUtils.hpp"

#include <QContextMenuEvent>
#include <QMenu>
#include <QMouseEvent>

namespace Acheron {
namespace UI {

ServerRailView::ServerRailView(QWidget *parent)
    : QListView(parent)
{
    setSelectionMode(QAbstractItemView::NoSelection);
    setFocusPolicy(Qt::NoFocus);
    setUniformItemSizes(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
}

void ServerRailView::setNotifyListContains(std::function<bool(Core::Snowflake)> provider)
{
    m_notifyListContains = std::move(provider);
}

void ServerRailView::setIgnoreEntitiesContains(std::function<bool(Core::Snowflake)> provider)
{
    m_ignoreEntitiesContains = std::move(provider);
}

void ServerRailView::setModel(QAbstractItemModel *model)
{
    // Note: deliberately no dataChanged -> fade hook here. Fading the whole
    // viewport on every data change (unread badges, mention counts, voice
    // counts, icon fetches) made the entire rail flash on every event.
    QListView::setModel(model);
}

void ServerRailView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return QListView::mousePressEvent(event);

    QModelIndex idx = indexAt(event->pos());
    if (!idx.isValid())
        return QListView::mousePressEvent(event);

    auto kind = static_cast<ServerRailModel::Kind>(idx.data(ServerRailModel::KindRole).toInt());
    Core::Snowflake accountId(idx.data(ServerRailModel::AccountIdRole).toULongLong());
    Core::Snowflake id(idx.data(ServerRailModel::IdRole).toULongLong());

    switch (kind) {
    case ServerRailModel::Kind::AccountHome:
        emit accountHomeClicked(accountId);
        break;
    case ServerRailModel::Kind::Server:
        emit guildClicked(accountId, id);
        break;
    case ServerRailModel::Kind::Folder:
        emit folderToggleClicked(accountId, id);
        break;
    }

    event->accept();
}

void ServerRailView::contextMenuEvent(QContextMenuEvent *event)
{
    QModelIndex idx = indexAt(event->pos());
    if (!idx.isValid())
        return;

    auto kind = static_cast<ServerRailModel::Kind>(idx.data(ServerRailModel::KindRole).toInt());
    if (kind != ServerRailModel::Kind::Folder && kind != ServerRailModel::Kind::Server)
        return;

    Core::Snowflake accountId(idx.data(ServerRailModel::AccountIdRole).toULongLong());
    Core::Snowflake id(idx.data(ServerRailModel::IdRole).toULongLong());
    const bool isFolder = kind == ServerRailModel::Kind::Folder;
    const bool isUnread = idx.data(ServerRailModel::IsUnreadRole).toBool();
    const int mentions = idx.data(ServerRailModel::MentionCountRole).toInt();

    QMenu menu(this);
    QAction *markRead = menu.addAction(tr("Mark As Read"));
    markRead->setEnabled(isUnread || mentions > 0);
    connect(markRead, &QAction::triggered, this, [this, accountId, id, isFolder]() {
        emit markAsReadRequested(accountId, id, isFolder);
    });

    if (!isFolder) {
        Core::Snowflake ownerId(idx.data(ServerRailModel::OwnerIdRole).toULongLong());

        menu.addSeparator();
        QAction *settingsAction = menu.addAction(tr("Server Settings"));
        connect(settingsAction, &QAction::triggered, this, [this, accountId, id]() {
            emit serverSettingsRequested(accountId, id);
        });

        menu.addSeparator();
        QAction *leaveAction = menu.addAction(tr("Leave"));
        leaveAction->setEnabled(ownerId != accountId);
        connect(leaveAction, &QAction::triggered, this, [this, accountId, id]() {
            emit leaveGuildRequested(accountId, id);
        });

        menu.addSeparator();
        const bool listened = m_notifyListContains ? m_notifyListContains(id) : false;
        QAction *notifyAction = menu.addAction(
                listened ? tr("Stop listening to toasts") : tr("Listen to toasts"));
        connect(notifyAction, &QAction::triggered, this, [this, accountId, id]() {
            emit notifyListToggleRequested(accountId, id);
        });

        const bool ignored = m_ignoreEntitiesContains ? m_ignoreEntitiesContains(id) : false;
        QAction *ignoreAction = menu.addAction(
                ignored ? tr("Unignore toasts") : tr("Ignore toasts"));
        connect(ignoreAction, &QAction::triggered, this, [this, accountId, id]() {
            emit ignoreToggleRequested(accountId, id);
        });
    }

    menu.exec(event->globalPos());
}

void ServerRailView::mouseMoveEvent(QMouseEvent *event)
{
    QModelIndex idx = indexAt(event->pos());
    bool hovered = idx.isValid();

    viewport()->setCursor(hovered ? Qt::PointingHandCursor : Qt::ArrowCursor);

    if (hovered && idx != m_lastHovered) {
        m_lastHovered = QPersistentModelIndex(idx);
        // brief opacity pulse on hover: 0.85 -> 1.0 -> clean up
        Core::AnimationUtils::fadeTo(viewport(), 0.85, 1.0, 180);
    } else if (!hovered) {
        m_lastHovered = QPersistentModelIndex();
    }

    QListView::mouseMoveEvent(event);
}

} // namespace UI
} // namespace Acheron
