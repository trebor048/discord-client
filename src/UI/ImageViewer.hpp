#pragma once

#include <QWidget>
#include <QPixmap>
#include <QUrl>
#include <QPointF>

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
    void resetView();
    void updateGeometryToParent();
    QPointF imageToWidget(const QPointF &imagePoint) const;
    QPointF widgetToImage(const QPointF &widgetPoint) const;

    QPixmap currentImage;
    QPixmap fullImage;
    QUrl currentUrl;
    bool isLoadingFull = false;

    qreal zoomLevel = 1.0;
    QPointF panOffset;
    QPointF lastMousePos;
    bool isPanning = false;

    QWidget *trackedWindow = nullptr;
    QNetworkAccessManager *networkManager;
};

} // namespace UI
} // namespace Acheron
