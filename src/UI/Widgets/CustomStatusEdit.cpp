#include "CustomStatusEdit.hpp"

#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStringList>
#include <QToolButton>

#include "Core/Result.hpp"
#include "Discord/Client.hpp"
#include "UI/Dialogs/EmojiPickerDialog.hpp"

namespace Acheron {
namespace UI {

CustomStatusEdit::CustomStatusEdit(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    emojiButton = new QToolButton(this);
    emojiButton->setText(QStringLiteral("\xF0\x9F\x98\x8A"));
    emojiButton->setToolTip(tr("Pick an emoji"));
    emojiButton->setFixedSize(32, 28);
    layout->addWidget(emojiButton);

    recentStatusCombo = new QComboBox(this);
    recentStatusCombo->setEditable(false);
    recentStatusCombo->setPlaceholderText(tr("Recent statuses"));
    layout->addWidget(recentStatusCombo);

    textEdit = new QLineEdit(this);
    textEdit->setPlaceholderText(tr("What's on your mind?"));
    textEdit->setMaxLength(128);
    layout->addWidget(textEdit, 1);

    expiryCombo = new QComboBox(this);
    expiryCombo->addItem(tr("Don't clear"), 0);
    expiryCombo->addItem(tr("30 minutes"), 30 * 60);
    expiryCombo->addItem(tr("1 hour"), 60 * 60);
    expiryCombo->addItem(tr("4 hours"), 4 * 60 * 60);
    expiryCombo->addItem(tr("Today"), -1);
    expiryCombo->addItem(tr("Tomorrow"), -2);
    layout->addWidget(expiryCombo);

    saveBtn = new QPushButton(tr("Save"), this);
    layout->addWidget(saveBtn);

    clearBtn = new QPushButton(tr("Clear"), this);
    layout->addWidget(clearBtn);

    // Load saved status
    QSettings settings;
    textEdit->setText(settings.value("general/custom_status").toString());
    selectedEmoji = settings.value("general/custom_status_emoji").toString();
    if (!selectedEmoji.isEmpty())
        emojiButton->setText(selectedEmoji);

    connect(emojiButton, &QToolButton::clicked, this, &CustomStatusEdit::onEmojiPicked);
    connect(recentStatusCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                if (index < 0)
                    return;
                const QString entry = recentStatusCombo->itemData(index).toString();
                const int split = entry.indexOf(' ');
                if (split > 0) {
                    selectedEmoji = entry.left(split);
                    textEdit->setText(entry.mid(split + 1));
                    emojiButton->setText(selectedEmoji);
                } else {
                    selectedEmoji.clear();
                    textEdit->setText(entry);
                    emojiButton->setText(QStringLiteral("\xF0\x9F\x98\x8A"));
                }
            });
    connect(saveBtn, &QPushButton::clicked, this, &CustomStatusEdit::onSaveStatus);
    connect(clearBtn, &QPushButton::clicked, this, &CustomStatusEdit::onClearStatus);

    loadRecentStatuses();
}

void CustomStatusEdit::setClient(Discord::Client *c)
{
    client = c;
}

QString CustomStatusEdit::statusText() const
{
    return textEdit->text();
}

QString CustomStatusEdit::emojiName() const
{
    return selectedEmoji;
}

qint64 CustomStatusEdit::expiresAt() const
{
    int data = expiryCombo->currentData().toInt(0);
    if (data == 0)
        return 0;

    if (data > 0) {
        // Relative expiry in seconds
        return QDateTime::currentSecsSinceEpoch() + data;
    }

    // Special values: -1 = end of today, -2 = end of tomorrow
    QDateTime now = QDateTime::currentDateTime();
    if (data == -1) {
        return QDateTime(now.date().addDays(1), QTime(0, 0)).toSecsSinceEpoch();
    } else if (data == -2) {
        return QDateTime(now.date().addDays(2), QTime(0, 0)).toSecsSinceEpoch();
    }
    return 0;
}

void CustomStatusEdit::onEmojiPicked()
{
    auto *picker = new EmojiPickerDialog(this);
    picker->setAttribute(Qt::WA_DeleteOnClose);
    picker->setWindowModality(Qt::ApplicationModal);

    connect(picker, &EmojiPickerDialog::emojiSelected, this, [this, picker](const QString &emoji) {
        selectedEmoji = emoji;
        emojiButton->setText(emoji);
        picker->close();
    });

    picker->exec();
}

void CustomStatusEdit::onClearStatus()
{
    textEdit->clear();
    selectedEmoji.clear();
    emojiButton->setText(QStringLiteral("\xF0\x9F\x98\x8A"));

    QSettings settings;
    settings.remove("general/custom_status");
    settings.remove("general/custom_status_emoji");

    if (client) {
        client->clearCustomStatus([](const Core::Result<QJsonObject> &) {});
    }

    emit statusCleared();
}

void CustomStatusEdit::onSaveStatus()
{
    QString text = textEdit->text();
    QString emoji = selectedEmoji;
    qint64 expAt = expiresAt();

    QSettings settings;
    settings.setValue("general/custom_status", text);
    settings.setValue("general/custom_status_emoji", emoji);

    // Save to recent statuses
    QSettings recentSettings;
    QStringList recent = recentSettings.value("general/recent_statuses").toStringList();
    QString entry = emoji.isEmpty() ? text : emoji + " " + text;
    if (!entry.isEmpty()) {
        recent.removeAll(entry);
        recent.prepend(entry);
        if (recent.size() > 5)
            recent = recent.mid(0, 5);
        recentSettings.setValue("general/recent_statuses", recent);
    }

    loadRecentStatuses();

    if (client) {
        client->updateCustomStatus(text, emoji, expAt, [](const Core::Result<QJsonObject> &) {});
    }

    emit statusChanged(text, emoji, expAt);
}

void CustomStatusEdit::loadRecentStatuses()
{
    QSettings settings;
    const QStringList recent = settings.value("general/recent_statuses").toStringList();

    recentStatusCombo->blockSignals(true);
    recentStatusCombo->clear();
    for (const auto &entry : recent) {
        recentStatusCombo->addItem(entry, entry);
    }
    recentStatusCombo->setCurrentIndex(-1);
    recentStatusCombo->blockSignals(false);
}

} // namespace UI
} // namespace Acheron
