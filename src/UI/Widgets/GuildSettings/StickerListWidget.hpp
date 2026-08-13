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

struct StickerManagementEntry
{
    Core::Snowflake stickerId;
    QString name;
    QString description;
    QString tags;
    Discord::StickerFormatType formatType;
};

/*
 * Widget for managing guild stickers: view with preview, upload (PNG/APNG,
 * size limit), rename, and delete.  Visible only when the user has
 * MANAGE_EXPRESSIONS permission.
 */
class StickerListWidget : public QWidget
{
    Q_OBJECT
public:
    explicit StickerListWidget(QWidget *parent = nullptr);
    ~StickerListWidget() override;

    void setGuildId(Core::Snowflake guildId);
    void setDiscordClient(Discord::Client *client);
    void setHttpClient(Discord::HttpClient *http);
    void setEnabled(bool enabled);
    void setStickers(const QList<Discord::Sticker> &stickers);

signals:
    void stickerModified();

private slots:
    void onUploadClicked();
    void onRenameClicked(Core::Snowflake stickerId, const QString &oldName);
    void onDeleteClicked(Core::Snowflake stickerId, const QString &name);

private:
    struct StickerWidget
    {
        Core::Snowflake stickerId;
        QString name;
        Discord::StickerFormatType formatType;
        QToolButton *previewButton = nullptr;
        QLabel *nameLabel = nullptr;
        QLabel *formatLabel = nullptr;
        QPushButton *renameButton = nullptr;
        QPushButton *deleteButton = nullptr;
        QMovie *movie = nullptr;
        QBuffer *buffer = nullptr;
    };

    void rebuildStickerList();
    void clearStickerWidgets();
    void loadStickerPreview(StickerWidget &entry, const Discord::Sticker &sticker);
    void uploadStickerFile(const QString &filePath, const QString &name,
                           const QString &description, const QString &tags);
    static QString formatTypeString(Discord::StickerFormatType type);

    Core::Snowflake guildId_;
    Discord::Client *client_ = nullptr;
    Discord::HttpClient *http_ = nullptr;
    QNetworkAccessManager *nam_ = nullptr;

    QList<Discord::Sticker> stickers_;
    QList<StickerWidget> stickerWidgets_;

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
