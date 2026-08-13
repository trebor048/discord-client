#pragma once

#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QPainter>
#include <QModelIndex>
#include <QSize>

class QAbstractProxyModel;

namespace Acheron {
namespace UI {
class ChannelDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit ChannelDelegate(QAbstractProxyModel *proxyModel = nullptr, QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    QAbstractProxyModel *proxyModel;
};
} // namespace UI
} // namespace Acheron
