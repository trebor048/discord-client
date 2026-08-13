#pragma once

#include <QWidget>

#include <functional>

#include "Core/Snowflake.hpp"
#include "Discord/Entities.hpp"

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QNetworkAccessManager;
class QGridLayout;
class QToolButton;
class QMovie;
class QBuffer;

namespace Acheron {
namespace Discord {
class Client;
class HttpClient;
} // namespace Discord

namespace UI {

/*
 * Widget for managing guild emojis: view, upload (PNG/GIF up to 256KB),
 * rename, and delete.  Visible only when the user has
 * MANAGE_EXPRESSIONS permission.
 */
class EmojiListWidget : public QWidget
{
    Q_OBJECT
public:
    explicit EmojiListWidget(QWidget *parent = nullptr);
    ~EmojiListWidget() override;

    void setGuildId(Core::Snowflake guildId);
    void setDiscordClient(Discord::Client *client);
    void setHttpClient(Discord::HttpClient *http);
    void setEnabled(bool enabled);
    void setEmojis(const QList<Discord::Emoji> &emojis);

signals:
    void emojiModified();

private slots:
    void onUploadClicked();
    void onRenameClicked(Core::Snowflake emojiId, const QString &oldName);
    void onDeleteClicked(Core::Snowflake emojiId, const QString &name);

private:
    struct EmojiWidget {
        Core::Snowflake emojiId;
        QString name;
        bool animated = false;
        QToolButton *previewButton = nullptr;
        QLabel *nameLabel = nullptr;
        QPushButton *renameButton = nullptr;
        QPushButton *deleteButton = nullptr;
    };

    void rebuildEmojiList();
    void clearEmojiWidgets();
    void loadEmojiPreview(EmojiWidget &entry, const Discord::Emoji &emoji);
    void uploadEmojiFile(const QString &filePath, const QString &name);

    Core::Snowflake guildId_;
    Discord::Client *client_ = nullptr;
    Discord::HttpClient *http_ = nullptr;
    QNetworkAccessManager *nam_ = nullptr;

    QList<Discord::Emoji> emojis_;
    QList<EmojiWidget> emojiWidgets_;

    QLabel *titleLabel_ = nullptr;
    QLabel *permissionLabel_ = nullptr;
    QPushButton *uploadButton_ = nullptr;
    QScrollArea *scrollArea_ = nullptr;
    QWidget *container_ = nullptr;
    QVBoxLayout *containerLayout_ = nullptr;
    bool enabled_ = false;
};

} // namespace UI
} // namespace Acheron
