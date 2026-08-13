#include "WebhooksWidget.hpp"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

#include "Core/ClientInstance.hpp"
#include "Discord/Client.hpp"
#include "Discord/Enums.hpp"
#include "Storage/ChannelRepository.hpp"

namespace Acheron {
namespace UI {
namespace Widgets {

using Core::Snowflake;

WebhooksWidget::WebhooksWidget(Core::ClientInstance *instance, Core::Snowflake guildId,
                               QWidget *parent)
    : GuildSettingsPage(instance, guildId, parent)
{
    setupUi();

    auto *client = m_instance->discord();
    connect(client, &Discord::Client::webhooksUpdated, this, &WebhooksWidget::onWebhooksUpdated);
    connect(client, &Discord::Client::guildWebhooksFetched, this, &WebhooksWidget::onWebhooksFetched);
}

void WebhooksWidget::setupUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(12);

    auto *title = new QLabel(QStringLiteral("Webhooks"), this);
    title->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: bold;"));
    layout->addWidget(title);

    m_webhookList = new QListWidget(this);
    m_webhookList->setAlternatingRowColors(true);
    m_webhookList->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_webhookList, 1);

    auto *btnLayout = new QHBoxLayout();
    m_createButton = new QPushButton(QStringLiteral("Create Webhook"), this);
    m_editButton = new QPushButton(QStringLiteral("Edit"), this);
    m_editButton->setEnabled(false);
    m_deleteButton = new QPushButton(QStringLiteral("Delete"), this);
    m_deleteButton->setEnabled(false);
    btnLayout->addWidget(m_createButton);
    btnLayout->addStretch();
    btnLayout->addWidget(m_editButton);
    btnLayout->addWidget(m_deleteButton);
    layout->addLayout(btnLayout);

    connect(m_createButton, &QPushButton::clicked, this, &WebhooksWidget::onCreateWebhook);
    connect(m_editButton, &QPushButton::clicked, this, &WebhooksWidget::onEditWebhook);
    connect(m_deleteButton, &QPushButton::clicked, this, &WebhooksWidget::onDeleteWebhook);
    connect(m_webhookList, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *current) {
                const bool has = current != nullptr;
                m_editButton->setEnabled(has);
                m_deleteButton->setEnabled(has);
            });
}

void WebhooksWidget::load()
{
    m_webhookList->clear();
    auto *item = new QListWidgetItem(m_webhookList);
    item->setText(QStringLiteral("Loading webhooks..."));
    item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);

    m_instance->discord()->fetchGuildWebhooks(m_guildId, [](const auto &) {});
}

void WebhooksWidget::onWebhooksFetched(Core::Snowflake guildId,
                                       const QList<Discord::WebhookData> &webhooks)
{
    if (guildId != m_guildId)
        return;
    m_webhooks = webhooks;
    populateList(webhooks);
}

void WebhooksWidget::populateList(const QList<Discord::WebhookData> &webhooks)
{
    m_webhookList->clear();

    for (const auto &wh : webhooks) {
        const QString name = wh.name.getOr(QStringLiteral("Unnamed"));
        const QString channelStr =
            wh.channelId.hasValue() ? QString::number(static_cast<quint64>(wh.channelId.get())) : QStringLiteral("-");
        const QString creatorName =
            wh.creator.hasValue() ? wh.creator.get().getDisplayName() : QStringLiteral("Unknown");

        auto *item = new QListWidgetItem(m_webhookList);
        item->setText(QStringLiteral("%1 — Channel: %2 — Created by: %3")
                          .arg(name, channelStr, creatorName));
        item->setData(Qt::UserRole, static_cast<quint64>(wh.id.get()));
    }

    if (m_webhookList->count() == 0) {
        auto *item = new QListWidgetItem(m_webhookList);
        item->setText(QStringLiteral("No webhooks."));
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
    }
}

void WebhooksWidget::onCreateWebhook()
{
    // Build a dialog with channel picker and name input
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Create Webhook"));

    auto *layout = new QFormLayout(&dialog);

    auto *channelCombo = new QComboBox(&dialog);
    channelCombo->setMinimumWidth(250);
    channelCombo->setEditable(true);
    channelCombo->setPlaceholderText(QStringLiteral("Enter channel ID..."));

    layout->addRow(QStringLiteral("Channel ID:"), channelCombo);

    auto *nameEdit = new QLineEdit(QStringLiteral("New Webhook"), &dialog);
    nameEdit->setMaxLength(80);
    layout->addRow(QStringLiteral("Webhook name:"), nameEdit);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const QString name = nameEdit->text().trimmed();
    if (name.isEmpty())
        return;

    const quint64 channelId = channelCombo->currentText().toULongLong();
    if (channelId == 0) {
        emit statusMessage(QStringLiteral("Select a valid channel ID before creating a webhook."));
        return;
    }
    m_instance->discord()->createWebhook(Snowflake(channelId), name, QString());
    emit statusMessage(QStringLiteral("Creating webhook..."));
}

void WebhooksWidget::onEditWebhook()
{
    auto *current = m_webhookList->currentItem();
    if (!current)
        return;

    const quint64 webhookId = current->data(Qt::UserRole).toULongLong();

    bool ok = false;
    const QString name =
        QInputDialog::getText(this, QStringLiteral("Edit Webhook"), QStringLiteral("Webhook name:"),
                              QLineEdit::Normal, current->text().section(" — ", 0, 0), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    m_instance->discord()->modifyWebhook(Snowflake(webhookId), name.trimmed(),
                                           QString());
    emit statusMessage(QStringLiteral("Updating webhook..."));
}

void WebhooksWidget::onDeleteWebhook()
{
    auto *current = m_webhookList->currentItem();
    if (!current)
        return;

    const quint64 webhookId = current->data(Qt::UserRole).toULongLong();
    m_instance->discord()->deleteWebhook(Snowflake(webhookId));
    emit statusMessage(QStringLiteral("Deleting webhook..."));
}

void WebhooksWidget::onWebhooksUpdated(const Discord::WebhooksUpdate &event)
{
    if (event.guildId.get() != m_guildId)
        return;
    load();
}

} // namespace Widgets
} // namespace UI
} // namespace Acheron
