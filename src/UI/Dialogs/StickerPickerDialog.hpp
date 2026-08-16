#pragma once

#include <QDialog>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <QBuffer>
#include <QMovie>
#include <QPointer>
#include <QTimer>

#include <functional>

#include "Core/Snowflake.hpp"
#include "Discord/Entities.hpp"

class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QScrollArea;
class QTabWidget;
class QToolButton;
class QVBoxLayout;
class QWidget;

namespace Acheron {
namespace UI {

struct StickerPackGroup
{
    Core::Snowflake guildId;
    QString guildName;
    QString guildIconHash;
    QList<Discord::Sticker> stickers;
};

class StickerPickerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit StickerPickerDialog(QWidget *parent = nullptr);
    ~StickerPickerDialog() override;

    void setStickerPacks(const QList<StickerPackGroup> &packs);
    void setGuildIconProvider(std::function<QUrl(Core::Snowflake, const QString &)> provider);

    [[nodiscard]] Core::Snowflake selectedStickerId() const { return currentStickerId; }

signals:
    void stickerSelected(Core::Snowflake stickerId);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void rebuildGrid();
    void onSearchChanged(const QString &query);
    void onStickerClicked(Core::Snowflake stickerId);

private:
    struct StickerGridEntry
    {
        Core::Snowflake stickerId;
        QString name;
        Discord::StickerFormatType formatType;
        QUrl cdnUrl;
        Core::Snowflake guildId;
        QString guildName;
        QPointer<QToolButton> button;
    };

    void buildAllTab();
    void buildPackTabs();
    void buildPackTab(QWidget *tab, const StickerPackGroup &group);
    void buildRecentsTab();
    QWidget *buildStickerGrid(const QList<StickerPackGroup> &packs);
    void loadStickerThumbnail(QToolButton *button, const StickerGridEntry &entry, int size);
    void startHoverAnimation(QToolButton *button, const StickerGridEntry &entry);
    void stopHoverAnimation(QToolButton *button);
    QList<StickerGridEntry> filterEntries(const QString &query) const;

    QLineEdit *searchEdit = nullptr;
    QTabWidget *packTabs = nullptr;
    QScrollArea *scrollArea = nullptr;
    QWidget *allTab = nullptr;
    QVBoxLayout *allTabLayout = nullptr;
    QNetworkAccessManager *nam = nullptr;
    QTimer *searchDebounce = nullptr;
    QSet<QNetworkReply *> pendingRequests;
    QHash<QToolButton *, QMovie *> hoveredMovies;
    QHash<Core::Snowflake, QByteArray> stickerDataCache;
    QHash<QString, QPointer<QMovie>> sharedMovies;

    QList<StickerPackGroup> packs;
    QList<StickerGridEntry> allEntries;
    Core::Snowflake currentStickerId;

    std::function<QUrl(Core::Snowflake, const QString &)> guildIconProvider;

    static constexpr int kStickerGridColumns = 6;
    static constexpr int kStickerPreviewSize = 64;
};

} // namespace UI
} // namespace Acheron
