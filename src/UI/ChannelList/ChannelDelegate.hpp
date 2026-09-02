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

    // Fill-to-viewport support: the channel tree distributes leftover sidebar
    // height across the visible rows when the list is shorter than the panel,
    // so a small server doesn't leave a large empty region below the last
    // channel. 0 = natural row heights (the default, and always the case when
    // the content overflows and a scrollbar is needed).
    void setFillExtra(int extra) { m_fillExtra = extra; }
    [[nodiscard]] int fillExtra() const { return m_fillExtra; }

private:
    QAbstractProxyModel *proxyModel;
    int m_fillExtra = 0;
};
} // namespace UI
} // namespace Acheron
