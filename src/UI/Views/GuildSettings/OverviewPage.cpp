#include "OverviewPage.hpp"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "Core/ClientInstance.hpp"

namespace Acheron {
namespace UI {
namespace Views {

OverviewPage::OverviewPage(Core::ClientInstance *instance, Core::Snowflake guildId,
                           QWidget *parent)
    : GuildSettingsPage(instance, guildId, parent)
{
    setupUi();
}

void OverviewPage::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(16);

    auto *title = new QLabel(QStringLiteral("Server Overview"), this);
    title->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold;"));
    layout->addWidget(title);

    // Guild icon preview
    auto *iconLayout = new QHBoxLayout();
    iconLayout->setSpacing(12);
    m_iconLabel = new QLabel(this);
    m_iconLabel->setFixedSize(64, 64);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setStyleSheet(QStringLiteral(
        "QLabel { background: #5865F2; color: white; border-radius: 32px; "
        "font-weight: bold; font-size: 28px; }"));
    iconLayout->addWidget(m_iconLabel);

    auto *iconText = new QLabel(QStringLiteral("We can change the server icon here."), this);
    iconText->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 12px;"));
    iconText->setWordWrap(true);
    iconLayout->addWidget(iconText, 1);
    iconLayout->addStretch();
    layout->addLayout(iconLayout);

    // Form
    auto *form = new QFormLayout();
    form->setSpacing(8);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setMaxLength(100);
    form->addRow(QStringLiteral("Server Name"), m_nameEdit);

    m_guildIdLabel = new QLabel(this);
    m_guildIdLabel->setStyleSheet(QStringLiteral("font-family: monospace; color: palette(mid);"));
    m_guildIdLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QStringLiteral("Server ID"), m_guildIdLabel);

    m_ownerLabel = new QLabel(this);
    form->addRow(QStringLiteral("Owner"), m_ownerLabel);

    m_premiumTierLabel = new QLabel(this);
    form->addRow(QStringLiteral("Boost Level"), m_premiumTierLabel);

    m_memberCountLabel = new QLabel(this);
    form->addRow(QStringLiteral("Members"), m_memberCountLabel);

    layout->addLayout(form);

    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    m_saveButton = new QPushButton(QStringLiteral("Save Changes"), this);
    m_saveButton->setEnabled(false);
    btnLayout->addWidget(m_saveButton);
    layout->addLayout(btnLayout);

    connect(m_saveButton, &QPushButton::clicked, this, &OverviewPage::onSaveName);
    connect(m_nameEdit, &QLineEdit::textChanged, this, [this]() {
        m_saveButton->setEnabled(true);
    });

    layout->addStretch();
}

void OverviewPage::load()
{
    auto guild = m_instance->getGuild(m_guildId);
    if (!guild)
        return;

    m_nameEdit->setText(guild->name.get());
    m_saveButton->setEnabled(false);
    m_guildIdLabel->setText(QString::number(guild->id.get()));
    m_iconLabel->setText(guild->name.get().left(1).toUpper());

    m_premiumTierLabel->setText(
        QStringLiteral("Level %1").arg(static_cast<int>(guild->premiumTier.getOr(Discord::PremiumTier::NONE))));

    // Owner display
    if (guild->ownerId.get().isValid())
        m_ownerLabel->setText(QString::number(static_cast<quint64>(guild->ownerId.get())));
}

void OverviewPage::onSaveName()
{
    const QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty())
        return;

    m_instance->discord()->modifyGuild(m_guildId, name);
    m_saveButton->setEnabled(false);
    emit statusMessage(QStringLiteral("Saving server name..."));
}

} // namespace Views
} // namespace UI
} // namespace Acheron
