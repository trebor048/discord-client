#pragma once

#include <QStyledItemDelegate>
#include <QVariantMap>

namespace Acheron {
namespace UI {

class MemberListDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit MemberListDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

    void setIconsOnly(bool iconsOnly) { iconsOnly_ = iconsOnly; }
    bool iconsOnly() const { return iconsOnly_; }

private:
    void paintGroup(QPainter *painter, const QStyleOptionViewItem &option,
                    const QModelIndex &index) const;
    void paintMember(QPainter *painter, const QStyleOptionViewItem &option,
                     const QModelIndex &index) const;
    void drawPresenceIcon(QPainter *painter, const QVariantMap &presence,
                          const QRect &rect) const;
    void paintPlaceholder(QPainter *painter, const QStyleOptionViewItem &option) const;

    bool iconsOnly_ = false;
};

} // namespace UI
} // namespace Acheron
