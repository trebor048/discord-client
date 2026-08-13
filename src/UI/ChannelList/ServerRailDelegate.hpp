#pragma once

#include <QStyledItemDelegate>

namespace Acheron {
namespace UI {

class ServerRailDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    static constexpr int RailWidth = 58;
    static constexpr int ItemHeight = 46;

    explicit ServerRailDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

} // namespace UI
} // namespace Acheron
