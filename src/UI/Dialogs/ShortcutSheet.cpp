#include "ShortcutSheet.hpp"

#include "Core/AnimationUtils.hpp"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace Acheron {
namespace UI {

namespace {

QLabel *makeKeyChip(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setAlignment(Qt::AlignCenter);
    label->setMinimumHeight(24);
    label->setStyleSheet(QStringLiteral(
            "QLabel {"
            "  background: palette(base);"
            "  color: palette(text);"
            "  border: 1px solid palette(mid);"
            "  border-radius: 6px;"
            "  padding: 0 8px;"
            "  font-weight: 600;"
            "}"));
    return label;
}

QWidget *makeShortcutRow(const QStringList &keys, const QString &description, QWidget *parent)
{
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *keysWrap = new QWidget(row);
    auto *keysLayout = new QHBoxLayout(keysWrap);
    keysLayout->setContentsMargins(0, 0, 0, 0);
    keysLayout->setSpacing(6);
    for (int i = 0; i < keys.size(); ++i) {
        if (i > 0)
            keysLayout->addWidget(makeKeyChip(QStringLiteral("+"), keysWrap));
        keysLayout->addWidget(makeKeyChip(keys.at(i), keysWrap));
    }
    layout->addWidget(keysWrap, 0, Qt::AlignTop);

    auto *label = new QLabel(description, row);
    label->setWordWrap(true);
    label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    layout->addWidget(label, 1);

    return row;
}

QFrame *makeSection(const QString &title, QWidget *content, QWidget *parent)
{
    auto *frame = new QFrame(parent);
    frame->setStyleSheet(QStringLiteral(
            "QFrame { background: palette(window); border: 1px solid palette(mid); border-radius: 10px; }"));
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(10);

    auto *heading = new QLabel(title, frame);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    headingFont.setPointSize(headingFont.pointSize() + 1);
    heading->setFont(headingFont);
    layout->addWidget(heading);
    layout->addWidget(content);
    return frame;
}

} // namespace

ShortcutSheet::ShortcutSheet(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Keyboard Shortcuts"));
    setModal(true);
    setWindowFlag(Qt::FramelessWindowHint, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMinimumSize(680, 520);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 16);

    auto *panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("shortcutSheetPanel"));
    panel->setStyleSheet(QStringLiteral(
            "#shortcutSheetPanel { background: palette(window); border: 1px solid palette(mid); border-radius: 12px; }"));
    auto *panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(16, 16, 16, 16);
    panelLayout->setSpacing(12);

    auto *title = new QLabel(tr("Keyboard Shortcuts"), panel);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 4);
    title->setFont(titleFont);
    panelLayout->addWidget(title);

    auto *subtitle = new QLabel(tr("Quick access to the main navigation and chat actions."), panel);
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    panelLayout->addWidget(subtitle);

    auto *content = new QWidget(panel);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);

    auto *navigationRows = new QWidget(content);
    auto *navigationLayout = new QVBoxLayout(navigationRows);
    navigationLayout->setContentsMargins(0, 0, 0, 0);
    navigationLayout->setSpacing(8);
    navigationLayout->addWidget(makeShortcutRow({tr("Ctrl"), tr("K")}, tr("Open quick switch"), navigationRows));
    navigationLayout->addWidget(makeShortcutRow({tr("Ctrl"), tr("/")}, tr("Open this shortcut sheet"), navigationRows));
    navigationLayout->addWidget(makeShortcutRow({tr("Ctrl"), tr("N")}, tr("Open a new window"), navigationRows));
    navigationLayout->addWidget(makeShortcutRow({tr("Ctrl"), tr("Shift"), tr("N")},
                                                tr("Tile a new window beside the current one"),
                                                navigationRows));
    navigationLayout->addWidget(makeShortcutRow({tr("Ctrl"), tr("T")}, tr("Open a new tab"), navigationRows));
    contentLayout->addWidget(makeSection(tr("Navigation"), navigationRows, content));

    auto *chatRows = new QWidget(content);
    auto *chatLayout = new QVBoxLayout(chatRows);
    chatLayout->setContentsMargins(0, 0, 0, 0);
    chatLayout->setSpacing(8);
    chatLayout->addWidget(makeShortcutRow({tr("Back Mouse Button")}, tr("Navigate back in tab history"), chatRows));
    chatLayout->addWidget(makeShortcutRow({tr("Forward Mouse Button")}, tr("Navigate forward in tab history"), chatRows));
    chatLayout->addWidget(makeShortcutRow({tr("Ctrl"), tr("K")}, tr("Open channel quick switch"), chatRows));
    contentLayout->addWidget(makeSection(tr("Chat"), chatRows, content));

    auto *miscRows = new QWidget(content);
    auto *miscLayout = new QVBoxLayout(miscRows);
    miscLayout->setContentsMargins(0, 0, 0, 0);
    miscLayout->setSpacing(8);
    miscLayout->addWidget(makeShortcutRow({tr("Ctrl"), tr("Shift"), tr("R")},
                                          tr("Debug: force a gateway reconnect"),
                                          miscRows));
    contentLayout->addWidget(makeSection(tr("Misc"), miscRows, content));

    auto *scroll = new QScrollArea(panel);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    scroll->setWidget(content);
    panelLayout->addWidget(scroll, 1);

    outer->addWidget(panel, 1);

    Acheron::Core::AnimationUtils::fadeIn(panel, 180);
}

void ShortcutSheet::done(int r)
{
    Acheron::Core::AnimationUtils::fadeOut(this, 100);
    QTimer::singleShot(110, this, [this, r]() {
        QDialog::done(r);
    });
}

} // namespace UI
} // namespace Acheron
