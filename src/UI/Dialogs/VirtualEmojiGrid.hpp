#pragma once

#include <QWidget>

#include <QHash>
#include <QList>
#include <QSize>
#include <QString>
#include <QVector>

#include <functional>

#include "Core/EmojiCatalog.hpp"

class QEvent;
class QLabel;
class QObject;
class QScrollArea;
class QToolButton;

namespace Acheron {
namespace UI {

// Shared grid metrics so the picker dialog and the virtualized grid agree on
// geometry without duplicating magic numbers.
namespace EmojiGridMetrics {
    inline constexpr int kColumns = 12;
    inline constexpr int kCellSize = 44;
    inline constexpr int kIconSize = 36;
} // namespace EmojiGridMetrics

struct EmojiGridSection
{
    QString key;    // stable id (case-folded category name, or guild id)
    QString header; // display text for the section header
    QList<Core::EmojiCatalogItem> items;
};

// A scrollable emoji grid that recycles a small, fixed pool of QToolButtons.
// Instead of constructing one widget per emoji (~1800 per open), it renders
// only the rows currently in (or near) the viewport and reassigns pooled
// buttons to different cells as the user scrolls.
class VirtualEmojiGrid : public QWidget
{
    Q_OBJECT
public:
    explicit VirtualEmojiGrid(QWidget *parent = nullptr);

    void attachScrollArea(QScrollArea *area);

    void setSections(const QList<EmojiGridSection> &sections);
    void clear();

    QString selectedValue() const;
    bool selectValue(const QString &value); // data-driven; scrolls into view
    void clearSelection();

    void scrollToSection(const QString &key);
    QString sectionKeyAtTop(int scrollY) const;

    using IconApplicator = std::function<void(QToolButton *, const Core::EmojiCatalogItem &)>;
    void setIconApplicator(IconApplicator applicator);
    void refreshValue(const QString &value);

signals:
    void itemClicked(const QString &value);
    void itemContextMenuRequested(QToolButton *button, const Core::EmojiCatalogItem &item);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuildLayoutModel();
    void relayout();
    QToolButton *createPoolButton();
    void ensureButtonPoolSize(int viewportHeight);
    int cellTopY(const QString &value) const;
    void ensureVisible(int y, int height);

    QScrollArea *m_scrollArea = nullptr;
    QList<EmojiGridSection> m_sections;
    IconApplicator m_applicator;
    QString m_selectedValue;
    QSize m_defaultIconSize;

    // Layout model (content-space offsets).
    QVector<int> m_sectionTopY;
    QHash<QString, int> m_sectionIndexByKey;
    int m_totalHeight = 1;
    int m_headerHeight = 24;

    // Recycled widget pool.
    QVector<QToolButton *> m_buttons;
    QVector<QLabel *> m_headers;
    QHash<QToolButton *, QString> m_buttonValue;
    QHash<QToolButton *, Core::EmojiCatalogItem> m_buttonItem;
    QHash<QString, QToolButton *> m_valueToButton;
};

} // namespace UI
} // namespace Acheron
