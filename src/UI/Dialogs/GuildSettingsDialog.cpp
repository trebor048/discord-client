#include "GuildSettingsDialog.hpp"
#include "GuildSettingsPage.hpp"

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "Core/PermissionComputer.hpp"
#include "Core/Theme/Manager.hpp"
#include "Core/Theme/Tokens.hpp"
#include "Core/UserManager.hpp"

#include "Discord/CdnUrls.hpp"
#include "UI/Views/GuildSettings/OverviewPage.hpp"
#include "UI/Views/GuildSettings/RolesPage.hpp"
#include "UI/Views/GuildSettings/EmojiPage.hpp"
#include "UI/Views/GuildSettings/StickersPage.hpp"
#include "UI/Widgets/GuildSettings/BanListWidget.hpp"
#include "UI/Widgets/GuildSettings/InvitesListWidget.hpp"
#include "UI/Widgets/GuildSettings/AuditLogWidget.hpp"
#include "UI/Widgets/GuildSettings/WebhooksWidget.hpp"
#include "UI/Widgets/GuildSettings/IntegrationsWidget.hpp"

namespace Acheron {
namespace UI {

using Core::Snowflake;

namespace {
QString hex(const QColor &c)
{
    return c.name(QColor::HexRgb);
}
} // namespace

GuildSettingsDialog::GuildSettingsDialog(Core::ClientInstance *instance, Core::Snowflake guildId,
                                         QWidget *parent)
    : QDialog(parent)
    , m_instance(instance)
    , m_guildId(guildId)
{
    setWindowTitle(QStringLiteral("Server Settings"));
    setMinimumSize(800, 560);
    resize(900, 640);
    setAttribute(Qt::WA_DeleteOnClose);

    // Compute our permissions for the guild
    auto guild = m_instance->getGuild(m_guildId);
    const Snowflake myUserId = m_instance->discord()->getMe().id.get();
    if (guild) {
        QList<Snowflake> memberRoleIds;
        auto roles = m_instance->users()->getMemberRoles(m_guildId, myUserId);
        if (roles)
            memberRoleIds = *roles;
        const auto guildRoles = m_instance->getRolesForGuild(m_guildId);
        m_myPermissions =
            Core::PermissionComputer::computeBasePermissions(guild->ownerId.get(), myUserId,
                                                              m_guildId, memberRoleIds,
                                                              guildRoles);
    }

    setupUi();
    applyPermissions();
    setCurrentPage(Page::Overview);
}

void GuildSettingsDialog::setupUi()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Header
    auto *headerWidget = new QWidget(this);
    headerWidget->setFixedHeight(48);
    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(16, 0, 16, 0);
    headerLayout->setSpacing(8);

    m_guildIconLabel = new QLabel(headerWidget);
    m_guildIconLabel->setFixedSize(28, 28);
    headerLayout->addWidget(m_guildIconLabel);

    m_guildNameLabel = new QLabel(headerWidget);
    m_guildNameLabel->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: bold;"));
    headerLayout->addWidget(m_guildNameLabel);
    headerLayout->addStretch();

    m_closeButton = new QPushButton(QStringLiteral("Close"), headerWidget);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    headerLayout->addWidget(m_closeButton);

    mainLayout->addWidget(headerWidget);

    // Splitter with nav + content
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(1);
    m_splitter->setChildrenCollapsible(false);

    // Left navigation
    m_navList = new QListWidget(m_splitter);
    m_navList->setObjectName(QStringLiteral("guildSettingsNav"));
    m_navList->setFixedWidth(200);
    m_navList->setIconSize(QSize(16, 16));
    m_navList->setUniformItemSizes(true);
    m_navList->setCursor(Qt::PointingHandCursor);

    // Right content
    m_pageStack = new QStackedWidget(m_splitter);
    m_pageStack->setContentsMargins(0, 0, 0, 0);

    m_splitter->addWidget(m_navList);
    m_splitter->addWidget(m_pageStack);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);

    mainLayout->addWidget(m_splitter, 1);

    // Populate guild info
    auto guild = m_instance->getGuild(m_guildId);
    if (guild) {
        m_guildNameLabel->setText(guild->name.get());
        if (guild->icon.hasValue() && !guild->icon.get().isEmpty()) {
            m_iconLoader = new QNetworkAccessManager(this);
            const QUrl iconUrl = Discord::Cdn::guildIcon(m_guildId, guild->icon.get(), 64);
            QNetworkRequest request(iconUrl);
            request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Acheron/1.0"));
            QNetworkReply *reply = m_iconLoader->get(request);
            const QString guildName = guild->name.get();
            connect(reply, &QNetworkReply::finished, this, [this, reply, guildName]() {
                reply->deleteLater();
                auto setFallback = [this, guildName]() {
                    m_guildIconLabel->clear();
                    m_guildIconLabel->setText(guildName.left(1).toUpper());
                    m_guildIconLabel->setAlignment(Qt::AlignCenter);
                    m_guildIconLabel->setStyleSheet(
                        QStringLiteral("QLabel { background: %1; color: white; border-radius: 14px; "
                                       "font-weight: bold; font-size: 14px; }")
                            .arg(QColor(0x58, 0x65, 0xF2).name()));
                };

                if (reply->error() != QNetworkReply::NoError) {
                    setFallback();
                    return;
                }

                QPixmap pixmap;
                if (!pixmap.loadFromData(reply->readAll())) {
                    setFallback();
                    return;
                }

                pixmap = pixmap.scaled(m_guildIconLabel->size(), Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
                m_guildIconLabel->setPixmap(pixmap);
                m_guildIconLabel->setText({});
                m_guildIconLabel->setStyleSheet(QStringLiteral("QLabel { border-radius: 14px; }"));
            });
        } else {
            m_guildIconLabel->setText(guild->name.get().left(1).toUpper());
            m_guildIconLabel->setAlignment(Qt::AlignCenter);
            m_guildIconLabel->setStyleSheet(
                QStringLiteral("QLabel { background: %1; color: white; border-radius: 14px; "
                               "font-weight: bold; font-size: 14px; }")
                    .arg(QColor(0x58, 0x65, 0xF2).name()));
        }
    } else {
        m_guildNameLabel->setText(QStringLiteral("Server Settings"));
    }

    // Build pages
    addPage(Page::Overview, QStringLiteral("Overview"),
            new Views::OverviewPage(m_instance, m_guildId, m_pageStack));
    addPage(Page::Roles, QStringLiteral("Roles"),
            new Views::RolesPage(m_instance, m_guildId, m_pageStack));
    addPage(Page::Emoji, QStringLiteral("Emoji"),
            new Views::EmojiPage(m_instance, m_guildId, m_pageStack));
    addPage(Page::Stickers, QStringLiteral("Stickers"),
            new Views::StickersPage(m_instance, m_guildId, m_pageStack));
    addPage(Page::Bans, QStringLiteral("Bans"),
            new Widgets::BanListWidget(m_instance, m_guildId, m_pageStack));
    addPage(Page::Invites, QStringLiteral("Invites"),
            new Widgets::InvitesListWidget(m_instance, m_guildId, m_pageStack));
    addPage(Page::AuditLog, QStringLiteral("Audit Log"),
            new Widgets::AuditLogWidget(m_instance, m_guildId, m_pageStack));
    addPage(Page::Webhooks, QStringLiteral("Webhooks"),
            new Widgets::WebhooksWidget(m_instance, m_guildId, m_pageStack));
    addPage(Page::Integrations, QStringLiteral("Integrations"),
            new Widgets::IntegrationsWidget(m_instance, m_guildId, m_pageStack));

    connect(m_navList, &QListWidget::currentRowChanged, this, &GuildSettingsDialog::onPageChanged);

    // Apply stylesheet
    auto &tm = Core::Theme::Manager::instance();
    const auto windowBg = tm.color(Core::Theme::Token::WindowBg);
    const auto baseBg = tm.color(Core::Theme::Token::BaseBg);
    const auto text = tm.color(Core::Theme::Token::PrimaryText);
    const auto accent = tm.color(Core::Theme::Token::Highlight);
    const auto divider = tm.color(Core::Theme::Token::Divider);

    setStyleSheet(QStringLiteral(
        "QDialog { background: %1; }"
        "#guildSettingsNav { background: %2; border: none; border-right: 1px solid %5; "
        "  outline: none; font-size: 13px; }"
        "#guildSettingsNav::item { padding: 8px 16px; border: none; color: %4; }"
        "#guildSettingsNav::item:selected { background: %1; color: %3; "
        "  border-left: 3px solid %6; }"
        "#guildSettingsNav::item:hover { background: %2; }"
        "QPushButton { padding: 6px 16px; border-radius: 4px; border: 1px solid %5; "
        "  background: %2; color: %4; font-size: 12px; }"
        "QPushButton:hover { background: %1; }"
        "QStackedWidget { background: %1; border: none; }"
        "QScrollArea { border: none; background: %1; }"
        "QLabel { color: %4; }"
    ).arg(hex(windowBg), hex(baseBg), hex(accent), hex(text), hex(divider), hex(accent)));
}

void GuildSettingsDialog::addPage(Page page, const QString &title, GuildSettingsPage *widget)
{
    const int index = static_cast<int>(page);
    // Ensure nav item exists
    while (m_navList->count() <= index)
        m_navList->addItem(QString());

    m_navList->item(index)->setText(title);
    m_pageStack->addWidget(widget);
}

void GuildSettingsDialog::onPageChanged(int index)
{
    if (index < 0 || index >= m_pageStack->count())
        return;

    m_pageStack->setCurrentIndex(index);

    auto *page = qobject_cast<GuildSettingsPage *>(m_pageStack->widget(index));
    if (page)
        page->load();
}

void GuildSettingsDialog::setCurrentPage(Page page)
{
    const int index = static_cast<int>(page);
    m_navList->setCurrentRow(index);
}

void GuildSettingsDialog::applyPermissions()
{
    const bool canManage = m_myPermissions.testFlag(Discord::Permission::MANAGE_GUILD) ||
                           m_myPermissions.testFlag(Discord::Permission::ADMINISTRATOR);
    const bool canManageRoles = m_myPermissions.testFlag(Discord::Permission::MANAGE_ROLES) ||
                                canManage;
    const bool canBan = m_myPermissions.testFlag(Discord::Permission::BAN_MEMBERS) || canManage;
    const bool canViewAuditLog =
        m_myPermissions.testFlag(Discord::Permission::VIEW_AUDIT_LOG) || canManage;
    const bool canManageWebhooks =
        m_myPermissions.testFlag(Discord::Permission::MANAGE_WEBHOOKS) || canManage;
    const bool canManageExpressions =
        m_myPermissions.testFlag(Discord::Permission::MANAGE_EXPRESSIONS) || canManage;

    // Disable pages the user can't access
    auto setRowEnabled = [this](Page p, bool enabled) {
        const int idx = static_cast<int>(p);
        if (idx < m_navList->count()) {
            auto *item = m_navList->item(idx);
            if (item) {
                item->setFlags(enabled ? Qt::ItemIsEnabled | Qt::ItemIsSelectable
                                       : Qt::NoItemFlags);
            }
        }
    };

    setRowEnabled(Page::Roles, canManageRoles || canManage);
    setRowEnabled(Page::Emoji, canManageExpressions);
    setRowEnabled(Page::Stickers, canManageExpressions);
    setRowEnabled(Page::Bans, canBan);
    setRowEnabled(Page::AuditLog, canViewAuditLog);
    setRowEnabled(Page::Webhooks, canManageWebhooks);
}

void GuildSettingsDialog::closeEvent(QCloseEvent *event)
{
    QDialog::closeEvent(event);
}

} // namespace UI
} // namespace Acheron
