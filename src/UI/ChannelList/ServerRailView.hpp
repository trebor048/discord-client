#pragma once

#include <QListView>
#include <QPersistentModelIndex>

#include <functional>

#include "Core/Snowflake.hpp"

namespace Acheron {
namespace UI {

class ServerRailView : public QListView
{
    Q_OBJECT
public:
    explicit ServerRailView(QWidget *parent = nullptr);

    void setModel(QAbstractItemModel *model) override;

    // Provider used to label the "Listen to toasts" context action (true when
    // the guild is already on the notify list). Set by MainWindow.
    void setNotifyListContains(std::function<bool(Core::Snowflake)> provider);
    // Provider for the "Ignore toasts" action (true when the guild is ignored).
    void setIgnoreEntitiesContains(std::function<bool(Core::Snowflake)> provider);

signals:
    void accountHomeClicked(Core::Snowflake accountId);
    void guildClicked(Core::Snowflake accountId, Core::Snowflake guildId);
    void folderToggleClicked(Core::Snowflake accountId, Core::Snowflake folderId);
    void markAsReadRequested(Core::Snowflake accountId, Core::Snowflake id, bool isFolder);
    void leaveGuildRequested(Core::Snowflake accountId, Core::Snowflake guildId);
    void serverSettingsRequested(Core::Snowflake accountId, Core::Snowflake guildId);
    void notifyListToggleRequested(Core::Snowflake accountId, Core::Snowflake guildId);
    void ignoreToggleRequested(Core::Snowflake accountId, Core::Snowflake guildId);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QPersistentModelIndex m_lastHovered;
    std::function<bool(Core::Snowflake)> m_notifyListContains;
    std::function<bool(Core::Snowflake)> m_ignoreEntitiesContains;
};

} // namespace UI
} // namespace Acheron
