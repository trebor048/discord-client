#include "VirtualEmojiGrid.hpp"

#include <QEvent>
#include <QLabel>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolButton>

#include <new>

namespace Acheron {
namespace UI {

namespace {
constexpr int kCellSpacing = 4;
constexpr int kHeaderGap = 6;
constexpr int kSectionGap = 10;
constexpr int kRecycleBufferPx = 200;
// Viewport height the initial recycled-button pool covers. Taller windows
// grow the pool on demand via ensureButtonPoolSize(). The picker dialog's
// minimum height is 520px, so a 640px budget already covers the whole dialog
// with recycle buffer; sizing the pool for a 1200px viewport upfront would
// allocate ~408 QToolButtons per grid at construction, and two grids (the
// picker owns both a category grid and a server grid) make that ~816 widgets
// — by far the largest single allocation burst in the app. Under low-memory
// conditions that burst is what trips the crash, so the pool starts small and
// grows on demand instead.
constexpr int kMaxViewportHeight = 640;
constexpr int kButtonPoolSize =
        ((kMaxViewportHeight + 2 * kRecycleBufferPx)
         / (EmojiGridMetrics::kCellSize + kCellSpacing) + 1)
        * EmojiGridMetrics::kColumns; // 22 rows x 12 columns
constexpr int kHeaderPoolSize = 32;
} // namespace

VirtualEmojiGrid::VirtualEmojiGrid(QWidget *parent) : QWidget(parent)
{
    // The initial pool is the largest single allocation burst in the app (two
    // grids of 264 buttons + 32 headers each live in the emoji picker). If it
    // fails under memory pressure, degrade to a smaller pool — relayout()
    // already skips cells beyond m_buttons.size() — instead of letting the
    // exception escape during dialog construction and aborting the process.
    try {
        m_buttons.reserve(kButtonPoolSize);
        for (int i = 0; i < kButtonPoolSize; ++i)
            m_buttons.append(createPoolButton());
    } catch (const std::bad_alloc &) {
        // Partial pool is fine: visible cells beyond the pool just don't
        // render until a later ensureButtonPoolSize() succeeds.
    }

    // Capture the button's pristine icon size so recycled buttons can be reset
    // to their native rendering (unicode emoji) before a custom icon overrides
    // it with a 24px size.
    if (!m_buttons.isEmpty())
        m_defaultIconSize = m_buttons.first()->iconSize();

    try {
        m_headers.reserve(kHeaderPoolSize);
        for (int i = 0; i < kHeaderPoolSize; ++i) {
            auto *label = new QLabel(this);
            QFont font = label->font();
            font.setBold(true);
            label->setFont(font);
            label->hide();
            m_headers.append(label);
        }
    } catch (const std::bad_alloc &) {
        // Degrade to whatever headers were created; relayout() hides missing
        // ones instead of crashing.
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
            ((viewportHeight + 2 * kRecycleBufferPx) / cellPitch + 1) * m_columns;
    while (m_buttons.size() < needed) {
        try {
            m_buttons.append(createPoolButton());
        } catch (const std::bad_alloc &) {
            // Out of memory: stop growing the pool. Visible cells beyond the
            // pool simply don't render (blank) instead of terminating the
            // whole app; the user can resize/scroll to repopulate.
            break;
        }
    }
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
    if (index < 0 || index >= m_sectionTopY.size() || !m_scrollArea)
        return;
    m_scrollArea->verticalScrollBar()->setValue(qMax(0, m_sectionTopY[index]));
}

QString VirtualEmojiGrid::sectionKeyAtTop(int scrollY) const
{
    QString key;
    const int n = qMin(m_sections.size(), m_sectionTopY.size());
    for (int i = 0; i < n; ++i) {
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

    int count = m_sections.size();
    m_sectionTopY.resize(count);

    // If the resize failed to allocate (out of memory), Qt leaves the vector
    // empty; indexing it below would write through a null pointer. Degrade to
    // an empty layout instead of crashing — the grid simply shows nothing
    // until the next relayout.
    if (m_sectionTopY.size() != count)
        count = 0;

    const int columns = m_columns;
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

    // Responsive columns: fit as many cells as the viewport width allows so
    // the grid fills the window horizontally. Changing the column count
    // changes how many rows each section takes, so the layout model (section
    // offsets, total height) must be rebuilt to stay in sync.
    const int cellPitch = EmojiGridMetrics::kCellSize + kCellSpacing;
    const int newColumns = qMax(1, viewportWidth / cellPitch);
    if (newColumns != m_columns) {
        m_columns = newColumns;
        rebuildLayoutModel();
    }

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

    const int columns = m_columns;

    int buttonIndex = 0;
    int headerIndex = 0;

    for (int sectionIndex = 0; sectionIndex < m_sections.size(); ++sectionIndex) {
        const EmojiGridSection &section = m_sections.at(sectionIndex);
        // If the layout model failed to allocate (OOM degraded it to empty),
        // m_sectionTopY is out of sync with m_sections — skip sections instead
        // of indexing out of bounds.
        if (sectionIndex >= m_sectionTopY.size())
            continue;
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
    const int columns = m_columns;
    const int cellPitch = EmojiGridMetrics::kCellSize + kCellSpacing;

    for (int sectionIndex = 0; sectionIndex < m_sections.size(); ++sectionIndex) {
        const EmojiGridSection &section = m_sections.at(sectionIndex);
        if (sectionIndex >= m_sectionTopY.size())
            continue;
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
