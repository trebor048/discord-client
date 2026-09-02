#include "SettingsWindow.hpp"

#include "Core/AnimationUtils.hpp"

#include <QAbstractItemView>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

#include "AppearancePage.hpp"
#include "AuthorizedAppsPage.hpp"
#include "ConnectionsPage.hpp"
#include "GeneralPage.hpp"
#include "NotificationsPage.hpp"
#include "PrivacySettingsPage.hpp"
#include "StreamerModePage.hpp"
#include "VoicePage.hpp"

#include "Core/Theme/Manager.hpp"

#include "Discord/Client.hpp"
#include "Core/Notification/NotificationManager.hpp"
#ifndef ACHERON_NO_VOICE
#  include "Core/AV/VoiceManager.hpp"
#endif

#ifdef ACHERON_HAVE_MINIAUDIO
#  include "AudioPage.hpp"
#endif

namespace Acheron {
namespace UI {

namespace {

// Re-pins a settings page's minimum height to its preferred content height so
// the wrapping scroll area never compresses rows below their text (which
// overlaps at large UI fonts). Re-runs on LayoutRequest — the event Qt emits
// whenever any child layout invalidates — so tab switches, dynamic content
// (loaded app lists, sound-override widgets) and font changes all re-pin.
// The pin is guarded against no-ops so the min-height set here cannot retrigger
// an endless LayoutRequest loop.
class MinHeightRefresher : public QObject
{
public:
    MinHeightRefresher(QWidget *page, QObject *parent = nullptr)
        : QObject(parent), page(page)
    {
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == page) {
            switch (event->type()) {
            case QEvent::ApplicationFontChange:
            case QEvent::FontChange:
            case QEvent::LayoutRequest:
                refresh();
                break;
            default:
                break;
            }
        }
        return QObject::eventFilter(watched, event);
    }

    void refresh()
    {
        if (!page)
            return;
        // QWidget::sizeHint() returns layout()->totalSizeHint(), which for a
        // page whose layout holds a QTabWidget can be far smaller than the
        // layout's real preferred size (the tab widget sizes to its tallest
        // page). Take the larger of the two so the pin never crushes content.
        int preferred = page->sizeHint().height();
        if (QLayout *lay = page->layout())
            preferred = std::max(preferred, lay->sizeHint().height());
        if (page->minimumHeight() != preferred)
            page->setMinimumHeight(preferred);
    }

private:
    QWidget *page;
};

} // namespace

SettingsWindow::SettingsWindow(QWidget *parent)
    : BasePopup(parent)
{
    setWindowTitle(tr("Settings"));

    setupUi();
}

void SettingsWindow::setClient(Discord::Client *c)
{
    client = c;

    // Propagate to pages that need the client. Pages live inside scroll-area
    // wrappers, so locate them by type anywhere in the page stack.
    if (auto *generalPage = findChild<GeneralPage *>())
        generalPage->setClient(c);
    if (auto *privacyPage = findChild<PrivacySettingsPage *>())
        privacyPage->setClient(c);
    if (auto *connPage = findChild<ConnectionsPage *>())
        connPage->setClient(c);
    if (auto *appsPage = findChild<AuthorizedAppsPage *>())
        appsPage->setClient(c);
}

void SettingsWindow::setImageManager(Core::ImageManager *mgr)
{
    if (auto *page = findChild<GeneralPage *>())
        page->setImageManager(mgr);
    if (auto *page = findChild<AuthorizedAppsPage *>())
        page->setImageManager(mgr);
}

void SettingsWindow::setNotificationManager(Core::NotificationManager *mgr)
{
    if (auto *page = findChild<NotificationsPage *>())
        page->setNotificationManager(mgr);
}

void SettingsWindow::setVoiceManager(Core::AV::VoiceManager *mgr)
{
#ifndef ACHERON_NO_VOICE
    if (auto *page = findChild<VoicePage *>())
        page->setVoiceManager(mgr);
#else
    Q_UNUSED(mgr);
#endif
}

void SettingsWindow::selectPage(const QString &name)
{
    if (!categoryList || !pages || name.isEmpty())
        return;
    for (int i = 0; i < categoryList->count(); ++i) {
        if (categoryList->item(i)->text().compare(name, Qt::CaseInsensitive) == 0) {
            categoryList->setCurrentRow(i);
            pages->setCurrentIndex(i);
            if (auto *page = pages->currentWidget())
                Acheron::Core::AnimationUtils::fadeIn(page, 150);
            return;
        }
    }
}

QStringList SettingsWindow::pageNames() const
{
    QStringList names;
    if (!categoryList)
        return names;
    for (int i = 0; i < categoryList->count(); ++i)
        names.append(categoryList->item(i)->text());
    return names;
}

void SettingsWindow::setupUi()
{
    auto *container = getContainer();
    container->setMinimumSize(1040, 640);
    container->setMaximumWidth(1500);
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *outerLayout = new QVBoxLayout(container);
    outerLayout->setContentsMargins(28, 22, 28, 26);
    outerLayout->setSpacing(0);

    auto *headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(12);
    auto *titleLabel = new QLabel(tr("Settings"), container);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 3);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    auto *closeButton = new QToolButton(container);
    closeButton->setText(QStringLiteral("\u00D7"));
    closeButton->setToolTip(tr("Close settings"));
    closeButton->setAutoRaise(true);
    closeButton->setFixedSize(32, 32);
    closeButton->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; border-radius: 16px; font-size: 22px; "
        "color: palette(placeholder-text); }"
        "QToolButton:hover { background: palette(mid); color: palette(window-text); }"));
    connect(closeButton, &QToolButton::clicked, this, &SettingsWindow::accept);

    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(closeButton);
    outerLayout->addLayout(headerLayout);

    // Header divider
    auto *divider = new QFrame(container);
    divider->setObjectName(QStringLiteral("settingsHeaderDivider"));
    divider->setFixedHeight(1);
    divider->setStyleSheet(QStringLiteral(
            "#settingsHeaderDivider { background: palette(mid); border: none; }"));
    outerLayout->addSpacing(12);
    outerLayout->addWidget(divider);
    outerLayout->addSpacing(16);

    auto *mainLayout = new QHBoxLayout();
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(24);

    categoryList = new QListWidget(container);
    categoryList->setObjectName(QStringLiteral("settingsCategoryList"));
    categoryList->setFixedWidth(190);
    categoryList->setFrameShape(QFrame::NoFrame);
    categoryList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    categoryList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    categoryList->setSpacing(2);
    const int r = Core::Theme::Manager::instance().roundness();
    const int rSmall = std::max(2, r / 2);
    categoryList->setStyleSheet(QStringLiteral(
            "QListWidget#settingsCategoryList { background: palette(alternate-base); "
            "border: 1px solid palette(mid); border-radius: %1px; padding: 4px; }"
            "QListWidget#settingsCategoryList::item { border-radius: %2px; "
            "padding: 8px 10px; }"
            "QListWidget#settingsCategoryList::item:hover { background: palette(mid); }"
            "QListWidget#settingsCategoryList::item:selected { background: palette(highlight); "
            "color: palette(highlighted-text); }")
                                       .arg(r)
                                       .arg(rSmall));

    pages = new QStackedWidget(container);
    pages->setObjectName(QStringLiteral("settingsPages"));

    // Wrap every page in a scroll area so large fonts (or a short window)
    // scroll the content instead of letting the stack compress the layouts,
    // which makes text overlap.
    auto addPage = [this](const QString &name, QWidget *page) {
        auto *scroll = new QScrollArea(this);
        scroll->setObjectName(QStringLiteral("settingsPageScroll"));
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setWidget(page);
        // The scroll area resizes its widget toward the viewport size but never
        // below minimumSizeHint — and a layout's minimums are far smaller than
        // the text they contain (especially with a large UI font), so rows got
        // compressed and their text overlapped. Pin the minimum height to the
        // preferred content height so the page scrolls instead of crushing;
        // MinHeightRefresher re-pins on LayoutRequest (tab switches, dynamic
        // content) and font changes.
        auto *refresher = new MinHeightRefresher(page, this);
        page->installEventFilter(refresher);
        refresher->refresh();
        categoryList->addItem(name);
        pages->addWidget(scroll);
    };

    // Existing pages
    auto *general = new GeneralPage(container);
    addPage(tr("General"), general);
    connect(general, &GeneralPage::notificationSoundsChanged,
            this, &SettingsWindow::notificationSoundsChanged);
    connect(general, &GeneralPage::customStatusChanged,
            this, &SettingsWindow::customStatusChanged);
    connect(general, &GeneralPage::editProfileRequested,
            this, &SettingsWindow::editProfileRequested);
    connect(general, &GeneralPage::newTabBehaviorChanged,
            this, &SettingsWindow::newTabBehaviorChanged);

    auto *appearance = new AppearancePage(container);
    addPage(tr("Appearance"), appearance);
    connect(appearance, &AppearancePage::channelListModeChanged, this, &SettingsWindow::channelListModeChanged);
    connect(appearance, &AppearancePage::compactModeChanged, this, &SettingsWindow::compactModeChanged);
    connect(appearance, &AppearancePage::compactInputChanged, this, &SettingsWindow::compactInputChanged);
    connect(appearance, &AppearancePage::showTimestampsChanged, this,
            &SettingsWindow::showTimestampsChanged);

    auto *voice = new VoicePage(container);
    addPage(tr("Voice & Audio"), voice);
    connect(voice, &VoicePage::pushToTalkToggled, this, &SettingsWindow::pushToTalkToggled);
    connect(voice, &VoicePage::pushToTalkKeyChanged, this, &SettingsWindow::pushToTalkKeyChanged);

    auto *notifications = new NotificationsPage(container);
    addPage(tr("Notifications"), notifications);

    // New pages
    auto *privacy = new PrivacySettingsPage(container);
    addPage(tr("Privacy & Safety"), privacy);

    auto *connections = new ConnectionsPage(container);
    addPage(tr("Connections"), connections);

    auto *authorizedApps = new AuthorizedAppsPage(container);
    addPage(tr("Authorized Apps"), authorizedApps);

    auto *streamerMode = new StreamerModePage(container);
    addPage(tr("Streamer Mode"), streamerMode);
    connect(streamerMode, &StreamerModePage::streamerModeChanged,
            this, &SettingsWindow::streamerModeChanged);

#ifdef ACHERON_HAVE_MINIAUDIO
    addPage(tr("Audio"), new AudioPage(this));
#endif

    // Fit the category list to its widest label so long names (e.g. under a
    // large UI font) stay readable instead of clipping into the page area.
    int widestLabel = 0;
    for (int i = 0; i < categoryList->count(); ++i)
        widestLabel = std::max(widestLabel,
                               categoryList->fontMetrics().horizontalAdvance(
                                       categoryList->item(i)->text()));
    // item padding (10px x2) + list padding (4px x2) + borders (1px x2)
    categoryList->setFixedWidth(std::clamp(widestLabel + 30, 190, 420));

    categoryList->setCurrentRow(0);

    connect(categoryList, &QListWidget::currentRowChanged, this, [this](int index) {
        pages->setCurrentIndex(index);
        if (auto *page = pages->currentWidget())
            Acheron::Core::AnimationUtils::fadeIn(page, 180);
    });

    mainLayout->addWidget(categoryList);
    mainLayout->addWidget(pages, 1);
    outerLayout->addLayout(mainLayout, 1);
}

} // namespace UI
} // namespace Acheron
