#include "RoleEditorWidget.hpp"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

#include "Core/ClientInstance.hpp"
#include "Discord/Client.hpp"

namespace Acheron {
namespace UI {
namespace Widgets {

using Core::Snowflake;

const QList<RoleEditorWidget::PermissionEntry> &RoleEditorWidget::permissionEntries()
{
    static const QList<PermissionEntry> entries = {
        { Discord::Permission::ADMINISTRATOR, QStringLiteral("Administrator"), QStringLiteral("Guild") },
        { Discord::Permission::VIEW_AUDIT_LOG, QStringLiteral("View Audit Log"), QStringLiteral("Guild") },
        { Discord::Permission::MANAGE_GUILD, QStringLiteral("Manage Server"), QStringLiteral("Guild") },
        { Discord::Permission::MANAGE_ROLES, QStringLiteral("Manage Roles"), QStringLiteral("Guild") },
        { Discord::Permission::MANAGE_CHANNELS, QStringLiteral("Manage Channels"), QStringLiteral("Guild") },
        { Discord::Permission::KICK_MEMBERS, QStringLiteral("Kick Members"), QStringLiteral("Guild") },
        { Discord::Permission::BAN_MEMBERS, QStringLiteral("Ban Members"), QStringLiteral("Guild") },
        { Discord::Permission::CHANGE_NICKNAME, QStringLiteral("Change Nickname"), QStringLiteral("Guild") },
        { Discord::Permission::MANAGE_NICKNAMES, QStringLiteral("Manage Nicknames"), QStringLiteral("Guild") },
        { Discord::Permission::MANAGE_EXPRESSIONS, QStringLiteral("Manage Expressions"), QStringLiteral("Guild") },
        { Discord::Permission::CREATE_EXPRESSIONS, QStringLiteral("Create Expressions"), QStringLiteral("Guild") },
        { Discord::Permission::VIEW_GUILD_INSIGHTS, QStringLiteral("View Guild Insights"), QStringLiteral("Guild") },
        { Discord::Permission::MANAGE_WEBHOOKS, QStringLiteral("Manage Webhooks"), QStringLiteral("Guild") },
        { Discord::Permission::MANAGE_EVENTS, QStringLiteral("Manage Events"), QStringLiteral("Guild") },
        { Discord::Permission::CREATE_EVENTS, QStringLiteral("Create Events"), QStringLiteral("Guild") },

        { Discord::Permission::CREATE_INSTANT_INVITE, QStringLiteral("Create Invite"), QStringLiteral("Text") },
        { Discord::Permission::VIEW_CHANNEL, QStringLiteral("View Channels"), QStringLiteral("Text") },
        { Discord::Permission::SEND_MESSAGES, QStringLiteral("Send Messages"), QStringLiteral("Text") },
        { Discord::Permission::MANAGE_MESSAGES, QStringLiteral("Manage Messages"), QStringLiteral("Text") },
        { Discord::Permission::EMBED_LINKS, QStringLiteral("Embed Links"), QStringLiteral("Text") },
        { Discord::Permission::ATTACH_FILES, QStringLiteral("Attach Files"), QStringLiteral("Text") },
        { Discord::Permission::READ_MESSAGE_HISTORY, QStringLiteral("Read Message History"), QStringLiteral("Text") },
        { Discord::Permission::MENTION_EVERYONE, QStringLiteral("Mention @everyone/@here"), QStringLiteral("Text") },
        { Discord::Permission::USE_EXTERNAL_EMOJIS, QStringLiteral("Use External Emoji"), QStringLiteral("Text") },
        { Discord::Permission::ADD_REACTIONS, QStringLiteral("Add Reactions"), QStringLiteral("Text") },
        { Discord::Permission::SEND_TTS_MESSAGES, QStringLiteral("Send Text-to-Speech Messages"), QStringLiteral("Text") },
        { Discord::Permission::MANAGE_THREADS, QStringLiteral("Manage Threads"), QStringLiteral("Text") },
        { Discord::Permission::CREATE_PUBLIC_THREADS, QStringLiteral("Create Public Threads"), QStringLiteral("Text") },
        { Discord::Permission::CREATE_PRIVATE_THREADS, QStringLiteral("Create Private Threads"), QStringLiteral("Text") },
        { Discord::Permission::SEND_MESSAGES_IN_THREADS, QStringLiteral("Send Messages in Threads"), QStringLiteral("Text") },
        { Discord::Permission::USE_EXTERNAL_STICKERS, QStringLiteral("Use External Stickers"), QStringLiteral("Text") },
        { Discord::Permission::PIN_MESSAGES, QStringLiteral("Pin Messages"), QStringLiteral("Text") },
        { Discord::Permission::BYPASS_SLOWMODE, QStringLiteral("Bypass Slowmode"), QStringLiteral("Text") },
        { Discord::Permission::USE_EMBEDDED_ACTIVITIES, QStringLiteral("Use Embedded Activities"), QStringLiteral("Text") },
        { Discord::Permission::SEND_POLLS, QStringLiteral("Send Polls"), QStringLiteral("Text") },
        { Discord::Permission::SEND_VOICE_MESSAGES, QStringLiteral("Send Voice Messages"), QStringLiteral("Text") },

        { Discord::Permission::CONNECT, QStringLiteral("Connect"), QStringLiteral("Voice") },
        { Discord::Permission::SPEAK, QStringLiteral("Speak"), QStringLiteral("Voice") },
        { Discord::Permission::MUTE_MEMBERS, QStringLiteral("Mute Members"), QStringLiteral("Voice") },
        { Discord::Permission::DEAFEN_MEMBERS, QStringLiteral("Deafen Members"), QStringLiteral("Voice") },
        { Discord::Permission::MOVE_MEMBERS, QStringLiteral("Move Members"), QStringLiteral("Voice") },
        { Discord::Permission::USE_VAD, QStringLiteral("Use Voice Activity"), QStringLiteral("Voice") },
        { Discord::Permission::PRIORITY_SPEAKER, QStringLiteral("Priority Speaker"), QStringLiteral("Voice") },
        { Discord::Permission::STREAM, QStringLiteral("Stream / Video"), QStringLiteral("Voice") },
        { Discord::Permission::REQUEST_TO_SPEAK, QStringLiteral("Request to Speak"), QStringLiteral("Voice") },
        { Discord::Permission::USE_SOUNDBOARD, QStringLiteral("Use Soundboard"), QStringLiteral("Voice") },
        { Discord::Permission::USE_EXTERNAL_SOUNDS, QStringLiteral("Use External Sounds"), QStringLiteral("Voice") },
        { Discord::Permission::SET_VOICE_CHANNEL_STATUS, QStringLiteral("Set Voice Channel Status"), QStringLiteral("Voice") },
        { Discord::Permission::MODERATE_MEMBERS, QStringLiteral("Moderate Members (Timeout)"), QStringLiteral("Voice") },

        { Discord::Permission::USE_APPLICATION_COMMANDS, QStringLiteral("Use Application Commands"), QStringLiteral("Other") },
        { Discord::Permission::USE_CLYDE_AI, QStringLiteral("Use Clyde AI"), QStringLiteral("Other") },
        { Discord::Permission::USE_EXTERNAL_APPS, QStringLiteral("Use External Apps"), QStringLiteral("Other") },
        { Discord::Permission::VIEW_CREATOR_MONETIZATION_ANALYTICS, QStringLiteral("View Creator Monetization Analytics"), QStringLiteral("Other") },
    };
    return entries;
}

RoleEditorWidget::RoleEditorWidget(Core::ClientInstance *instance, Core::Snowflake guildId,
                                   QWidget *parent)
    : QWidget(parent)
    , m_instance(instance)
    , m_guildId(guildId)
{
    setupUi();
    buildPermissionCheckboxes();
}

void RoleEditorWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(16);

    m_roleNameTitle = new QLabel(QStringLiteral("No Role Selected"), this);
    m_roleNameTitle->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold;"));
    layout->addWidget(m_roleNameTitle);

    // Role settings form
    auto *formGroup = new QGroupBox(QStringLiteral("Role Settings"), this);
    auto *formLayout = new QFormLayout(formGroup);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setMaxLength(100);
    formLayout->addRow(QStringLiteral("Name"), m_nameEdit);

    auto *colorLayout = new QHBoxLayout();
    m_colorPreview = new QLabel(this);
    m_colorPreview->setFixedSize(24, 24);
    m_colorPreview->setStyleSheet(QStringLiteral("background: transparent; border: 1px solid palette(mid); border-radius: 4px;"));
    colorLayout->addWidget(m_colorPreview);

    m_colorButton = new QPushButton(QStringLiteral("Choose Color"), this);
    connect(m_colorButton, &QPushButton::clicked, this, &RoleEditorWidget::onColorPicker);
    colorLayout->addWidget(m_colorButton);
    colorLayout->addStretch();
    formLayout->addRow(QStringLiteral("Color"), colorLayout);

    m_hoistCheck = new QCheckBox(QStringLiteral("Display role members separately"), this);
    formLayout->addRow(QStringLiteral("Hoist"), m_hoistCheck);

    m_mentionableCheck = new QCheckBox(QStringLiteral("Allow anyone to @mention this role"), this);
    formLayout->addRow(QStringLiteral("Mentionable"), m_mentionableCheck);

    layout->addWidget(formGroup);

    // Permissions section
    m_permissionsScroll = new QScrollArea(this);
    m_permissionsScroll->setWidgetResizable(true);
    m_permissionsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *permContainer = new QWidget();
    m_permissionsLayout = new QVBoxLayout(permContainer);
    m_permissionsLayout->setSpacing(4);

    m_permissionsScroll->setWidget(permContainer);
    layout->addWidget(m_permissionsScroll, 1);

    // Buttons
    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    m_cancelButton = new QPushButton(QStringLiteral("Reset"), this);
    btnLayout->addWidget(m_cancelButton);

    m_saveButton = new QPushButton(QStringLiteral("Save Changes"), this);
    m_saveButton->setEnabled(false);
    btnLayout->addWidget(m_saveButton);

    layout->addLayout(btnLayout);

    connect(m_saveButton, &QPushButton::clicked, this, &RoleEditorWidget::onSave);
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        if (m_roleId.isValid())
            loadRole(m_roleId);
    });

    // Enable save button when anything changes
    connect(m_nameEdit, &QLineEdit::textChanged, this, [this]() { m_saveButton->setEnabled(!m_loading); });
    connect(m_hoistCheck, &QCheckBox::toggled, this, [this]() { m_saveButton->setEnabled(!m_loading); });
    connect(m_mentionableCheck, &QCheckBox::toggled, this, [this]() { m_saveButton->setEnabled(!m_loading); });

    // Disable until a role is loaded
    setEnabled(false);
}

void RoleEditorWidget::buildPermissionCheckboxes()
{
    // Group permissions by category
    const auto &entries = permissionEntries();
    QHash<QString, QVBoxLayout *> categoryLayouts;
    QHash<QString, QGroupBox *> categoryBoxes;

    for (const auto &entry : entries) {
        if (!categoryBoxes.contains(entry.category)) {
            auto *box = new QGroupBox(entry.category);
            auto *boxLayout = new QVBoxLayout(box);
            boxLayout->setSpacing(2);
            categoryBoxes[entry.category] = box;
            categoryLayouts[entry.category] = boxLayout;
        }

        auto *checkbox = new QCheckBox(entry.label);
        connect(checkbox, &QCheckBox::toggled, this,
                [this]() { m_saveButton->setEnabled(!m_loading); });
        m_permissionCheckboxes[entry.flag] = checkbox;
        categoryLayouts[entry.category]->addWidget(checkbox);
    }

    for (auto *box : categoryBoxes)
        m_permissionsLayout->addWidget(box);

    m_permissionsLayout->addStretch();
}

void RoleEditorWidget::loadRole(Core::Snowflake roleId)
{
    m_roleId = roleId;
    m_loading = true;

    auto roles = m_instance->getRolesForGuild(m_guildId);
    for (const auto &role : roles) {
        if (role.id.get() == roleId) {
            applyRoleData(role);
            break;
        }
    }

    m_loading = false;
    m_saveButton->setEnabled(false);
    setEnabled(true);
}

void RoleEditorWidget::clearRole()
{
    m_roleId = Snowflake::Invalid;
    m_nameEdit->clear();
    m_hoistCheck->setChecked(false);
    m_mentionableCheck->setChecked(false);
    m_colorPreview->setStyleSheet(
        QStringLiteral("background: transparent; border: 1px solid palette(mid); border-radius: 4px;"));
    for (auto *cb : m_permissionCheckboxes)
        cb->setChecked(false);
    m_roleNameTitle->setText(QStringLiteral("No Role Selected"));
    setEnabled(false);
}

void RoleEditorWidget::applyRoleData(const Discord::Role &role)
{
    m_currentRole = role;
    m_roleNameTitle->setText(role.name.getOr(QStringLiteral("new role")));
    m_nameEdit->setText(role.name.getOr(QStringLiteral("")));

    if (role.hasColor()) {
        m_currentColor = role.getColor();
        m_colorPreview->setStyleSheet(
            QStringLiteral("background: %1; border: 1px solid palette(mid); border-radius: 4px;")
                .arg(m_currentColor.name()));
    } else {
        m_currentColor = QColor();
        m_colorPreview->setStyleSheet(
            QStringLiteral("background: transparent; border: 1px solid palette(mid); border-radius: 4px;"));
    }

    m_hoistCheck->setChecked(role.hoist.getOr(false));
    m_mentionableCheck->setChecked(role.mentionable.getOr(false));

    // Update permission checkboxes
    const Discord::Permissions perms = role.permissions.getOr(Discord::NO_PERMISSIONS);
    for (auto it = m_permissionCheckboxes.constBegin(); it != m_permissionCheckboxes.constEnd(); ++it) {
        it.value()->setChecked(perms.testFlag(it.key()));
    }
}

void RoleEditorWidget::onSave()
{
    if (!m_roleId.isValid())
        return;

    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty())
        return;

    Discord::Role updated = m_currentRole;
    updated.name = name;
    // Only set color when a valid color was explicitly chosen
    if (m_currentColor.isValid() && m_currentColor.alpha() > 0) {
        updated.color = m_currentColor.rgb() & 0xFFFFFF;
    }
    updated.hoist = m_hoistCheck->isChecked();
    updated.mentionable = m_mentionableCheck->isChecked();
    updated.permissions = collectPermissions();

    m_instance->discord()->modifyRole(m_guildId, m_roleId, updated);
    m_saveButton->setEnabled(false);
    emit roleModified();
}

void RoleEditorWidget::onColorPicker()
{
    auto color = QColorDialog::getColor(m_currentColor.isValid() ? m_currentColor : Qt::transparent,
                                        this, QStringLiteral("Select Role Color"));
    if (color.isValid()) {
        m_currentColor = color;
        m_colorPreview->setStyleSheet(
            QStringLiteral("background: %1; border: 1px solid palette(mid); border-radius: 4px;")
                .arg(color.name()));
        m_saveButton->setEnabled(true);
    }
}

Discord::Permissions RoleEditorWidget::collectPermissions() const
{
    Discord::Permissions::Storage flags = 0;
    for (auto it = m_permissionCheckboxes.constBegin(); it != m_permissionCheckboxes.constEnd(); ++it) {
        if (it.value()->isChecked())
            flags |= static_cast<Discord::Permissions::Storage>(it.key());
    }
    return Discord::Permissions::fromInt(flags);
}

} // namespace Widgets
} // namespace UI
} // namespace Acheron
