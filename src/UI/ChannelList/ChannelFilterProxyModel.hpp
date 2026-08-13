#pragma once

#include <QSortFilterProxyModel>

#include "Core/Session.hpp"
#include "Core/Snowflake.hpp"

namespace Acheron {
namespace UI {

class ChannelTreeModel;

class ChannelFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    enum Roles {
        SelectedRole = Qt::UserRole,
    };

    explicit ChannelFilterProxyModel(Core::Session *session, QObject *parent = nullptr);

    void setSourceModel(QAbstractItemModel *sourceModel) override;
    QVariant data(const QModelIndex &index, int role) const override;

    void setSelectedChannel(Core::Snowflake channelId, Core::Snowflake accountId);
    Core::Snowflake selectedChannel() const { return selectedChannelId; }

public slots:
    void invalidateFilter();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;

private:
    bool hasVisibleChildren(const QModelIndex &parent) const;
    Core::Snowflake getUserIdForNode(const QModelIndex &index) const;
    bool hasChannelViewPermission(const QModelIndex &sourceIndex) const;

    Core::Session *session;
    ChannelTreeModel *channelModel;
    Core::Snowflake selectedChannelId;
    Core::Snowflake selectedAccountId;
};

} // namespace UI
} // namespace Acheron
