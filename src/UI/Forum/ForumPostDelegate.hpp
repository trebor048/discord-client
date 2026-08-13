#pragma once

#include <QStyledItemDelegate>

namespace Acheron {
namespace UI {

class ForumPostDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit ForumPostDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

} // namespace UI
} // namespace Acheron
