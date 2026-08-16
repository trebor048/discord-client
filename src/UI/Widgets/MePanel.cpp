#include "MePanel.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

#include "Core/ClientInstance.hpp"
#include "Core/ImageManager.hpp"
#include "Core/Theme/Icons.hpp"
#include "Discord/CdnUrls.hpp"
#include "Discord/Client.hpp"

namespace Acheron {
namespace UI {

namespace {
QColor statusColor(const QString &status)
{
    if (status == QLatin1String("online"))
        return QColor(0x23, 0xA5, 0x5A);
    if (status == QLatin1String("idle"))
        return QColor(0xF0, 0xB2, 0x32);
    if (status == QLatin1String("dnd"))
        return QColor(0xF2, 0x3F, 0x43);
    return QColor(0x80, 0x84, 0x8E); // invisible / offline / unknown
}
} // namespace

MePanel::MePanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("mePanel"));
    setStyleSheet(QStringLiteral(
            "#mePanel { background: palette(window); border-top: 1px solid palette(mid); }"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(8);

    avatarLabel = new QLabel(this);
    avatarLabel->setFixedSize(28, 28);
    layout->addWidget(avatarLabel);

    auto *textCol = new QVBoxLayout;
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(0);

    nameLabel = new QLabel(tr("Not signed in"), this);
    nameLabel->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 12px;"));
    textCol->addWidget(nameLabel);

    auto *presenceRow = new QHBoxLayout;
    presenceRow->setContentsMargins(0, 0, 0, 0);
    presenceRow->setSpacing(4);
    statusDot = new QLabel(this);
    statusDot->setFixedSize(8, 8);
    presenceRow->addWidget(statusDot, 0, Qt::AlignVCenter);
    statusButton = new QToolButton(this);
    statusButton->setText(tr("Set Status"));
    statusButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    statusButton->setAutoRaise(true);
    statusButton->setCursor(Qt::PointingHandCursor);
    presenceRow->addWidget(statusButton, 0, Qt::AlignVCenter);

    settingsButton = new QToolButton(this);
    settingsButton->setIcon(Core::Theme::Icons::icon(Core::Theme::Icons::Name::Settings,
                                                     Core::Theme::Token::PrimaryText));
    settingsButton->setIconSize(QSize(16, 16));
    settingsButton->setToolTip(tr("Settings"));
    settingsButton->setAutoRaise(true);
    settingsButton->setCursor(Qt::PointingHandCursor);
    presenceRow->addWidget(settingsButton, 0, Qt::AlignVCenter);
    presenceRow->addStretch(1);
    textCol->addLayout(presenceRow);

    layout->addLayout(textCol, 1);

    connect(statusButton, &QToolButton::clicked, this, &MePanel::openStatusMenu);
    connect(settingsButton, &QToolButton::clicked, this, &MePanel::openSettingsMenu);
}

void MePanel::setInstance(Core::ClientInstance *instance, Core::ImageManager *images)
{
    m_instance = instance;
    m_images = images;
    refresh();
}

void MePanel::refresh()
{
    if (!m_instance) {
        nameLabel->setText(tr("Not signed in"));
        statusDot->setStyleSheet(QStringLiteral("background: %1; border-radius: 4px;")
                                         .arg(statusColor(QString()).name()));
        avatarLabel->setPixmap(m_images ? m_images->placeholder(QSize(28, 28)) : QPixmap());
        return;
    }

    const QString displayName = m_instance->users()->getDisplayName(m_instance->accountId(),
                                                                    Core::Snowflake::Invalid);
    nameLabel->setText(displayName.isEmpty() ? tr("Unknown") : displayName);

    const QString presence = m_instance->presenceStatus();
    const QColor color = statusColor(presence);
    statusDot->setStyleSheet(QStringLiteral("background: %1; border-radius: 4px;").arg(color.name()));

    if (m_images) {
        if (auto user = m_instance->users()->getUser(m_instance->accountId())) {
            const QString hash = user->avatar.getOr(QString());
            if (!hash.isEmpty()) {
                const QUrl url = Discord::Cdn::userAvatar(m_instance->accountId(), hash, 64);
                m_images->assign(avatarLabel, url, QSize(28, 28));
                return;
            }
        }
        avatarLabel->setPixmap(m_images->placeholder(QSize(28, 28)));
    } else {
        avatarLabel->setPixmap(QPixmap());
    }
}

void MePanel::openStatusMenu()
{
    if (!m_instance)
        return;

    const QString current = m_instance->presenceStatus();
    QMenu menu(this);

    struct Entry {
        const char *label;
        const char *status;
    };
    static const Entry entries[] = {
        { QT_TR_NOOP("Online"), "online" },
        { QT_TR_NOOP("Idle"), "idle" },
        { QT_TR_NOOP("Do Not Disturb"), "dnd" },
        { QT_TR_NOOP("Invisible"), "invisible" },
    };

    for (const auto &e : entries) {
        QAction *action = menu.addAction(tr(e.label));
        action->setCheckable(true);
        const bool isInvisible = current == QLatin1String("offline")
                                 && QLatin1String(e.status) == QLatin1String("invisible");
        action->setChecked(current == QLatin1String(e.status) || isInvisible);
        connect(action, &QAction::triggered, this, [this, status = QString::fromLatin1(e.status)]() {
            if (m_instance && m_instance->discord())
                m_instance->discord()->setPresenceStatus(status);
            refresh();
        });
    }

    menu.exec(statusButton->mapToGlobal(QPoint(0, statusButton->height())));
}

void MePanel::openSettingsMenu()
{
    QMenu menu(this);

    auto *settingsHeader = menu.addAction(tr("Settings"));
    QFont headerFont = settingsHeader->font();
    headerFont.setBold(true);
    settingsHeader->setFont(headerFont);
    settingsHeader->setEnabled(false);

    auto addPageAction = [this, &menu](const QString &label, const QString &page) {
        QAction *action = menu.addAction(label);
        connect(action, &QAction::triggered, this,
                [this, page]() { emit openSettingsPageRequested(page); });
    };
    addPageAction(tr("General"), tr("General"));
    addPageAction(tr("Appearance"), tr("Appearance"));
    addPageAction(tr("Voice & Audio"), tr("Voice & Audio"));
    addPageAction(tr("Notifications"), tr("Notifications"));
    addPageAction(tr("Privacy & Safety"), tr("Privacy & Safety"));
    addPageAction(tr("Connections"), tr("Connections"));
    addPageAction(tr("Authorized Apps"), tr("Authorized Apps"));
    addPageAction(tr("Streamer Mode"), tr("Streamer Mode"));
#ifdef ACHERON_HAVE_MINIAUDIO
    addPageAction(tr("Audio"), tr("Audio"));
#endif

    menu.addSeparator();

    auto *togglesHeader = menu.addAction(tr("Quick toggles"));
    togglesHeader->setFont(headerFont);
    togglesHeader->setEnabled(false);

    QSettings settings;
    auto addToggle = [this, &menu, &settings](const QString &label, const QString &key,
                                              bool defaultValue, void (MePanel::*signal)(bool)) {
        QAction *action = menu.addAction(label);
        action->setCheckable(true);
        action->setChecked(settings.value(key, defaultValue).toBool());
        connect(action, &QAction::toggled, this, [this, key, signal](bool checked) {
            QSettings().setValue(key, checked);
            Q_EMIT (this->*signal)(checked);
        });
    };
    addToggle(tr("Compact mode"), QStringLiteral("ui/compactMessages"), false,
              &MePanel::compactModeChanged);
    addToggle(tr("Always show timestamps"), QStringLiteral("ui/showTimestamps"), false,
              &MePanel::showTimestampsChanged);
    addToggle(tr("Compact input bar"), QStringLiteral("ui/compactInput"), false,
              &MePanel::compactInputChanged);
    addToggle(tr("Streamer mode"), QStringLiteral("streamer/enabled"), false,
              &MePanel::streamerModeChanged);
    addToggle(tr("Notification sounds"), QStringLiteral("notifications/sounds"), true,
              &MePanel::notificationSoundsChanged);

    menu.exec(settingsButton->mapToGlobal(QPoint(0, settingsButton->height())));
}

} // namespace UI
} // namespace Acheron
