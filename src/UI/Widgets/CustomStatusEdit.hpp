#pragma once

#include <QWidget>

class QLineEdit;
class QPushButton;
class QComboBox;
class QToolButton;

namespace Acheron {
namespace Discord {
class Client;
}
namespace UI {

class EmojiPickerDialog;

class CustomStatusEdit : public QWidget
{
    Q_OBJECT
public:
    explicit CustomStatusEdit(QWidget *parent = nullptr);

    void setClient(Discord::Client *client);

    QString statusText() const;
    QString emojiName() const;
    qint64 expiresAt() const;

signals:
    void statusChanged(const QString &text, const QString &emoji, qint64 expiresAt);
    void statusCleared();

private:
    void onEmojiPicked();
    void onClearStatus();
    void onSaveStatus();
    void loadRecentStatuses();

    Discord::Client *client = nullptr;

    QToolButton *emojiButton;
    QComboBox *recentStatusCombo;
    QLineEdit *textEdit;
    QComboBox *expiryCombo;
    QPushButton *saveBtn;
    QPushButton *clearBtn;

    QString selectedEmoji;
};

} // namespace UI
} // namespace Acheron
