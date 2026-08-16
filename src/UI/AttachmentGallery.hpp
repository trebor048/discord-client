#pragma once

#include <QDialog>
#include <QList>
#include <QUrl>

#include "UI/Chat/ChatModel.hpp"

class QKeyEvent;
class QShowEvent;
class QGraphicsView;
class QGraphicsScene;
class QGraphicsPixmapItem;
class QNetworkAccessManager;
class QNetworkReply;
class QLabel;
class QPushButton;

namespace Acheron {
namespace UI {

/**
 * Gallery viewer for a message's image attachments.
 *
 * Shows the current image in a zoomable/pannable canvas with a toolbar for
 * previous/next navigation, zoom, save-as, and copy-link. The full-resolution
 * image is fetched from the attachment's proxy URL in the background while the
 * cached chat thumbnail is shown immediately.
 */
class AttachmentGallery : public QDialog
{
    Q_OBJECT
public:
    /// Creates a gallery over the image attachments in @p attachments, starting
    /// at @p startIndex. The list must be non-empty.
    AttachmentGallery(const QList<AttachmentData> &attachments, int startIndex,
                      QWidget *parent = nullptr);

    /// Saves a single attachment to disk via a QFileDialog. Shared by the chat
    /// context menu and the gallery so save-as behavior lives in one place.
    static void saveAttachment(const AttachmentData &att, QWidget *parent);
    /// Saves one attachment to a concrete file path (no dialog).
    static void saveAttachmentTo(const AttachmentData &att, const QString &path, QWidget *parent);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void showIndex(int index);
    void handleKey(int key);
    void zoomBy(qreal factor);
    void fitToWindow();
    void fetchFullImage(const QUrl &proxyUrl);
    void saveCurrent();
    void saveAll();
    void copyLink();
    void updateNavigationState();
    void updateCounter();

    QList<AttachmentData> attachments;
    int currentIndex = 0;

    QGraphicsView *view = nullptr;
    QGraphicsScene *scene = nullptr;
    QGraphicsPixmapItem *imageItem = nullptr;

    QPushButton *prevButton = nullptr;
    QPushButton *nextButton = nullptr;
    QPushButton *zoomInButton = nullptr;
    QPushButton *zoomOutButton = nullptr;
    QPushButton *fitButton = nullptr;
    QPushButton *saveButton = nullptr;
    QPushButton *saveAllButton = nullptr;
    QPushButton *copyLinkButton = nullptr;
    QLabel *counterLabel = nullptr;

    QNetworkAccessManager *networkManager = nullptr;
    QNetworkReply *activeReply = nullptr;
};

} // namespace UI
} // namespace Acheron
