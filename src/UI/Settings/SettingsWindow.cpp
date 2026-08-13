#include "SettingsWindow.hpp"

#include "Core/AnimationUtils.hpp"

#include <QAbstractItemView>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

#include "AppearancePage.hpp"
#include "AuthorizedAppsPage.hpp"
#include "ConnectionsPage.hpp"
#include "GeneralPage.hpp"
#include "NotificationsPage.hpp"
#include "PrivacySettingsPage.hpp"
#include "StreamerModePage.hpp"
#include "VoicePage.hpp"

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

SettingsWindow::SettingsWindow(QWidget *parent)
    : BasePopup(parent)
{
    setWindowTitle(tr("Settings"));

    setupUi();
}

void SettingsWindow::setClient(Discord::Client *c)
{
    client = c;

    // Propagate to pages that need the client
    for (int i = 0; i < pages->count(); ++i) {
        auto *page = pages->widget(i);
        if (auto *generalPage = qobject_cast<GeneralPage *>(page))
            generalPage->setClient(c);
        else if (auto *privacyPage = qobject_cast<PrivacySettingsPage *>(page))
            privacyPage->setClient(c);
        else if (auto *connPage = qobject_cast<ConnectionsPage *>(page))
            connPage->setClient(c);
        else if (auto *appsPage = qobject_cast<AuthorizedAppsPage *>(page))
            appsPage->setClient(c);
    }
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

void SettingsWindow::setupUi()
{
    auto *container = getContainer();
    container->setMinimumSize(900, 600);
    container->setMaximumWidth(1100);
    container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *outerLayout = new QVBoxLayout(container);
    outerLayout->setContentsMargins(24, 20, 24, 24);
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
    mainLayout->setSpacing(20);

    categoryList = new QListWidget(container);
    categoryList->setObjectName(QStringLiteral("settingsCategoryList"));
    categoryList->setFixedWidth(190);
    categoryList->setFrameShape(QFrame::NoFrame);
    categoryList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    categoryList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    categoryList->setSpacing(2);
    categoryList->setStyleSheet(QStringLiteral(
            "QListWidget#settingsCategoryList { background: palette(alternate-base); "
            "border: 1px solid palette(mid); border-radius: 8px; padding: 4px; }"
            "QListWidget#settingsCategoryList::item { border-radius: 6px; "
            "padding: 8px 10px; }"
            "QListWidget#settingsCategoryList::item:hover { background: palette(mid); }"
            "QListWidget#settingsCategoryList::item:selected { background: palette(highlight); "
            "color: palette(highlighted-text); }"));

    pages = new QStackedWidget(container);
    pages->setObjectName(QStringLiteral("settingsPages"));

    auto addPage = [this](const QString &name, QWidget *page) {
        categoryList->addItem(name);
        pages->addWidget(page);
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

    categoryList->setCurrentRow(0);

    connect(categoryList, &QListWidget::currentRowChanged, this, [this](int index) {
        pages->setCurrentIndex(index);
        if (auto *page = pages->currentWidget())
            Acheron::Core::AnimationUtils::fadeIn(page, 150);
    });

    mainLayout->addWidget(categoryList);
    mainLayout->addWidget(pages, 1);
    outerLayout->addLayout(mainLayout, 1);
}

} // namespace UI
} // namespace Acheron
