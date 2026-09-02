#pragma once

#include <QWidget>
#include <QPixmap>
#include <QUrl>
#include <QPointF>
#include <QRectF>

class QNetworkAccessManager;
class QNetworkReply;

namespace Acheron {
namespace UI {

class ImageViewer : public QWidget
{
    Q_OBJECT
public:
    explicit ImageViewer(QWidget *parent = nullptr);

    void showImage(const QUrl &proxyUrl, const QPixmap &preview);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void fetchFullImage(const QUrl &proxyUrl);
    void applyFullImage(const QPixmap &pixmap);
    void updateDisplayPixmap();
    const QPixmap &displayPixmapFor(const QRectF &destRect) const;
    void resetView();
    void updateGeometryToParent();
    QPointF imageToWidget(const QPointF &imagePoint) const;
    QPointF widgetToImage(const QPointF &widgetPoint) const;

    QPixmap currentImage;
    QPixmap fullImage;
    // currentImage pre-scaled once to the current viewport resolution so
    // paintEvent stops re-smoothing the full-resolution source on every
    // repaint; regenerated on resize / when a new full image arrives.
    QPixmap displayPixmap;
    QUrl currentUrl;
    bool isLoadingFull = false;

    qreal zoomLevel = 1.0;
    QPointF panOffset;
    QPointF lastMousePos;
    bool isPanning = false;

    QWidget *trackedWindow = nullptr;
    QNetworkAccessManager *networkManager;
    // In-flight full-image fetch; aborted and replaced when a new image is
    // requested so a stale response can never overwrite the current view.
    QNetworkReply *m_activeFullReply = nullptr;
};

} // namespace UI
} // namespace Acheron
