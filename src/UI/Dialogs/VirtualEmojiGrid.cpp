#include "VirtualEmojiGrid.hpp"

#include <QEvent>
#include <QLabel>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>

namespace Acheron {
namespace UI {

namespace {
constexpr int kCellSpacing = 4;
constexpr int kHeaderGap = 6;
constexpr int kSectionGap = 10;
constexpr int kRecycleBufferPx = 200;
// Viewport height the initial recycled-button pool covers. Taller windows
// grow the pool on demand via ensureButtonPoolSize().
constexpr int kMaxViewportHeight = 1200;
constexpr int kButtonPoolSize =
        ((kMaxViewportHeight + 2 * kRecycleBufferPx)
         / (EmojiGridMetrics::kCellSize + kCellSpacing) + 1)
        * EmojiGridMetrics::kColumns; // 34 rows x 12 columns
constexpr int kHeaderPoolSize = 32;
} // namespace

VirtualEmojiGrid::VirtualEmojiGrid(QWidget *parent) : QWidget(parent)
{
    m_buttons.reserve(kButtonPoolSize);
    for (int i = 0; i < kButtonPoolSize; ++i)
        m_buttons.append(createPoolButton());

    // Capture the button's pristine icon size so recycled buttons can be reset
    // to their native rendering (unicode emoji) before a custom icon overrides
    // it with a 24px size.
    if (!m_buttons.isEmpty())
        m_defaultIconSize = m_buttons.first()->iconSize();

    m_headers.reserve(kHeaderPoolSize);
    for (int i = 0; i < kHeaderPoolSize; ++i) {
        auto *label = new QLabel(this);
        QFont font = label->font();
        font.setBold(true);
        label->setFont(font);
        label->hide();
        m_headers.append(label);
    }

    // Measure the bold header height once so section offsets are deterministic.
    QLabel probe;
    QFont probeFont = probe.font();
    probeFont.setBold(true);
    probe.setFont(probeFont);
    m_headerHeight = qMax(24, probe.sizeHint().height());
}

QToolButton *VirtualEmojiGrid::createPoolButton()
{
    auto *button = new QToolButton(this);
    button->setCheckable(true);
    button->setAutoRaise(true);
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setFixedSize(QSize(EmojiGridMetrics::kCellSize, EmojiGridMetrics::kCellSize));
    button->setProperty("emojiGridButton", true);
    button->setContextMenuPolicy(Qt::CustomContextMenu);
    button->hide();

    connect(button, &QToolButton::clicked, this, [this, button]() {
        const QString value = m_buttonValue.value(button);
        if (!value.isEmpty())
            emit itemClicked(value);
    });
    connect(button, &QWidget::customContextMenuRequested, this, [this, button](const QPoint &) {
        const auto it = m_buttonItem.constFind(button);
        if (it != m_buttonItem.constEnd())
            emit itemContextMenuRequested(button, it.value());
    });
    return button;
}

void VirtualEmojiGrid::ensureButtonPoolSize(int viewportHeight)
{
    // The initial pool covers viewports up to kMaxViewportHeight; taller
    // windows need more buttons or visible cells render as blank holes.
    const int cellPitch = EmojiGridMetrics::kCellSize + kCellSpacing;
    const int needed =
            ((viewportHeight + 2 * kRecycleBufferPx) / cellPitch + 1) * EmojiGridMetrics::kColumns;
    while (m_buttons.size() < needed)
        m_buttons.append(createPoolButton());
}

void VirtualEmojiGrid::attachScrollArea(QScrollArea *area)
{
    if (m_scrollArea == area)
        return;
    m_scrollArea = area;
    if (!area)
        return;

    connect(area->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this](int) { relayout(); });
    area->viewport()->installEventFilter(this);
}

void VirtualEmojiGrid::setSections(const QList<EmojiGridSection> &sections)
{
    m_sections = sections;
    m_selectedValue.clear();
    rebuildLayoutModel();
    // Rebuilding re-derives the content; reset scroll to the top so a tab
    // switch or search change does not carry over the previous scroll offset.
    if (m_scrollArea)
        m_scrollArea->verticalScrollBar()->setValue(0);
    relayout();
}

void VirtualEmojiGrid::clear()
{
    m_sections.clear();
    m_selectedValue.clear();
    rebuildLayoutModel();
    relayout();
}

QString VirtualEmojiGrid::selectedValue() const
{
    return m_selectedValue;
}

bool VirtualEmojiGrid::selectValue(const QString &value)
{
    const int y = cellTopY(value);
    if (y < 0)
        return false;

    m_selectedValue = value;
    relayout();
    ensureVisible(y, EmojiGridMetrics::kCellSize);
    return true;
}

void VirtualEmojiGrid::clearSelection()
{
    m_selectedValue.clear();
    relayout();
}

void VirtualEmojiGrid::scrollToSection(const QString &key)
{
    const int index = m_sectionIndexByKey.value(key, -1);
    if (index < 0 || !m_scrollArea)
        return;
    m_scrollArea->verticalScrollBar()->setValue(qMax(0, m_sectionTopY[index]));
}

QString VirtualEmojiGrid::sectionKeyAtTop(int scrollY) const
{
    QString key;
    for (int i = 0; i < m_sections.size(); ++i) {
        if (m_sectionTopY[i] <= scrollY)
            key = m_sections[i].key;
        else
            break;
    }
    return key;
}

void VirtualEmojiGrid::setIconApplicator(IconApplicator applicator)
{
    m_applicator = std::move(applicator);
}

void VirtualEmojiGrid::refreshValue(const QString &value)
{
    QToolButton *button = m_valueToButton.value(value, nullptr);
    if (!button)
        return;

    const auto it = m_buttonItem.constFind(button);
    if (it != m_buttonItem.constEnd() && m_applicator)
        m_applicator(button, it.value());
}

bool VirtualEmojiGrid::eventFilter(QObject *watched, QEvent *event)
{
    if (m_scrollArea && watched == m_scrollArea->viewport() && event->type() == QEvent::Resize)
        relayout();
    return QWidget::eventFilter(watched, event);
}

void VirtualEmojiGrid::rebuildLayoutModel()
{
    m_sectionTopY.clear();
    m_sectionIndexByKey.clear();

    const int count = m_sections.size();
    m_sectionTopY.resize(count);

    const int columns = EmojiGridMetrics::kColumns;
    const int cellPitch = EmojiGridMetrics::kCellSize + kCellSpacing;

    int cursor = 0;
    for (int i = 0; i < count; ++i) {
        const EmojiGridSection &section = m_sections.at(i);
        m_sectionTopY[i] = cursor;
        m_sectionIndexByKey.insert(section.key, i);

        const int rows = (section.items.size() + columns - 1) / columns;
        const int sectionHeight = m_headerHeight + kHeaderGap + rows * cellPitch;
        cursor += sectionHeight + (i + 1 < count ? kSectionGap : 0);
    }

    m_totalHeight = qMax(cursor, 1);
}

void VirtualEmojiGrid::relayout()
{
    if (!m_scrollArea)
        return;

    const int viewportWidth = qMax(1, m_scrollArea->viewport()->width());
    const int viewportHeight = m_scrollArea->viewport()->height();
    ensureButtonPoolSize(viewportHeight);
    const QSize target(viewportWidth, qMax(m_totalHeight, viewportHeight));
    if (size() != target)
        resize(target);

    const int scrollY = m_scrollArea->verticalScrollBar()->value();
    const int top = scrollY - kRecycleBufferPx;
    const int bottom = scrollY + viewportHeight + kRecycleBufferPx;

    m_valueToButton.clear();
    m_buttonValue.clear();
    m_buttonItem.clear();

    const int columns = EmojiGridMetrics::kColumns;
    const int cellPitch = EmojiGridMetrics::kCellSize + kCellSpacing;

    int buttonIndex = 0;
    int headerIndex = 0;

    for (int sectionIndex = 0; sectionIndex < m_sections.size(); ++sectionIndex) {
        const EmojiGridSection &section = m_sections.at(sectionIndex);
        const int headerY = m_sectionTopY[sectionIndex];
        const int rows = (section.items.size() + columns - 1) / columns;
        const int sectionBottom = headerY + m_headerHeight + kHeaderGap + rows * cellPitch;

        if (sectionBottom < top || headerY > bottom)
            continue; // whole section (including its header) is out of view

        if (headerY + m_headerHeight >= top && headerY <= bottom && headerIndex < m_headers.size()) {
            QLabel *label = m_headers[headerIndex++];
            label->setText(section.header);
            label->setGeometry(0, headerY, viewportWidth, m_headerHeight);
            label->show();
        }

        const int firstRowY = headerY + m_headerHeight + kHeaderGap;
        for (int i = 0; i < section.items.size(); ++i) {
            const int row = i / columns;
            const int col = i % columns;
            const int cellTop = firstRowY + row * cellPitch;
            if (cellTop + EmojiGridMetrics::kCellSize < top || cellTop > bottom)
                continue;
            if (buttonIndex >= m_buttons.size())
                continue; // unreachable: ensureButtonPoolSize() covers the viewport

            QToolButton *button = m_buttons[buttonIndex++];
            const Core::EmojiCatalogItem &item = section.items.at(i);
            const QString value = item.selectionValue();

            button->setGeometry(col * cellPitch, cellTop,
                                EmojiGridMetrics::kCellSize, EmojiGridMetrics::kCellSize);
            button->setIconSize(m_defaultIconSize);
            button->setChecked(value == m_selectedValue);
            button->setToolTip(QStringLiteral(":%1:").arg(item.name));
            button->setProperty("emojiValue", value);

            m_buttonValue.insert(button, value);
            m_buttonItem.insert(button, item);
            m_valueToButton.insert(value, button);

            if (m_applicator)
                m_applicator(button, item);
            button->show();
        }
    }

    for (; buttonIndex < m_buttons.size(); ++buttonIndex)
        m_buttons[buttonIndex]->hide();
    for (; headerIndex < m_headers.size(); ++headerIndex)
        m_headers[headerIndex]->hide();
}

int VirtualEmojiGrid::cellTopY(const QString &value) const
{
    const int columns = EmojiGridMetrics::kColumns;
    const int cellPitch = EmojiGridMetrics::kCellSize + kCellSpacing;

    for (int sectionIndex = 0; sectionIndex < m_sections.size(); ++sectionIndex) {
        const EmojiGridSection &section = m_sections.at(sectionIndex);
        for (int i = 0; i < section.items.size(); ++i) {
            if (section.items.at(i).selectionValue() == value) {
                const int row = i / columns;
                return m_sectionTopY[sectionIndex] + m_headerHeight + kHeaderGap + row * cellPitch;
            }
        }
    }
    return -1;
}

void VirtualEmojiGrid::ensureVisible(int y, int height)
{
    if (!m_scrollArea)
        return;

    QScrollBar *bar = m_scrollArea->verticalScrollBar();
    const int scrollY = bar->value();
    const int viewportHeight = m_scrollArea->viewport()->height();

    if (y < scrollY)
        bar->setValue(y);
    else if (y + height > scrollY + viewportHeight)
        bar->setValue(y + height - viewportHeight);
}

} // namespace UI
} // namespace Acheron
