#pragma once

#include <QTreeView>
#include <QHash>

#include "Core/Snowflake.hpp"

namespace Acheron {
namespace UI {

class ChannelTreeView : public QTreeView
{
    Q_OBJECT
public:
    ChannelTreeView(QWidget *parent = nullptr);

    void setModel(QAbstractItemModel *m) override;
    void performDefaultExpansion();

    void setAccountVoiceChannel(Core::Snowflake accountId, Core::Snowflake channelId);
    [[nodiscard]] bool isAccountInVoice(Core::Snowflake accountId) const;

signals:
    void markAsReadRequested(const QModelIndex &proxyIndex);
    void openInNewTabRequested(const QModelIndex &proxyIndex);
    void openInNewWindowRequested(const QModelIndex &proxyIndex);
    void joinVoiceChannelRequested(const QModelIndex &proxyIndex);
    void disconnectVoiceRequested(const QModelIndex &proxyIndex);
    void joinThreadRequested(const QModelIndex &proxyIndex);
    void leaveThreadRequested(const QModelIndex &proxyIndex);
    void leaveGuildRequested(Core::Snowflake accountId, Core::Snowflake guildId);
    void serverSettingsRequested(Core::Snowflake accountId, Core::Snowflake guildId);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void currentChanged(const QModelIndex &current, const QModelIndex &previous) override;

private:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

    bool handleMouseEventForExpansion(QMouseEvent *event);
    bool canReorderIndex(const QModelIndex &sourceIndex) const;

    void onRowsInserted(const QModelIndex &parent, int first, int last);

    Core::Snowflake findAccountIdForIndex(const QModelIndex &sourceIndex) const;

    QHash<Core::Snowflake, Core::Snowflake> accountVoiceChannels; // accountId -> channelId
    QModelIndex reorderSourceIndex;
    QPoint reorderPressPos;
    bool reorderDragging = false;
};

} // namespace UI
} // namespace Acheron
