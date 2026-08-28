#include "ImageViewer.hpp"

#include <QCache>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QApplication>

namespace Acheron {
namespace UI {

namespace {
// Bounded session cache of decoded full-resolution images, keyed by the fetch
// URL (proxy URL + format/quality query) plus the device pixel ratio the
// pixmap was decoded at. Opening the same image twice (or navigating back to
// it in a gallery) no longer re-downloads or re-decodes it.
constexpr qsizetype kImageCacheMaxBytes = 160 * 1024 * 1024; // ~32 full-res images

QCache<QString, QPixmap> &fullImageCache()
{
    static QCache<QString, QPixmap> cache(static_cast<int>(kImageCacheMaxBytes / 1024));
    return cache;
}

void cacheFullImage(const QString &key, const QPixmap &pixmap)
{
    const qsizetype cost = qMax<qsizetype>(1, pixmap.sizeInBytes() / 1024);
    if (cost <= kImageCacheMaxBytes / 1024)
        fullImageCache().insert(key, new QPixmap(pixmap), static_cast<int>(cost));
}
} // namespace

ImageViewer::ImageViewer(QWidget *parent) : QWidget(parent)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    setCursor(Qt::OpenHandCursor);
    setFocusPolicy(Qt::StrongFocus);

    networkManager = new QNetworkAccessManager(this);
}

void ImageViewer::showImage(const QUrl &proxyUrl, const QPixmap &preview)
{
    currentUrl = proxyUrl;
    currentImage = preview;
    fullImage = QPixmap();
    displayPixmap = QPixmap(); // stale pre-scale from a previous image must not be painted
    isLoadingFull = true;

    if (parentWidget()) {
        trackedWindow = parentWidget()->window();
        trackedWindow->installEventFilter(this);
    }

    updateGeometryToParent();

    show();
    raise();
    activateWindow();
    setFocus();

    resetView();
    fetchFullImage(proxyUrl);
}

void ImageViewer::updateGeometryToParent()
{
    if (trackedWindow)
        setGeometry(trackedWindow->geometry());
    else
        showFullScreen();
}

void ImageViewer::fetchFullImage(const QUrl &proxyUrl)
{
    QUrl fetchUrl = proxyUrl;
    QUrlQuery query(fetchUrl);
    query.addQueryItem("format", "webp");
    query.addQueryItem("quality", "lossless");
    fetchUrl.setQuery(query);

    const qreal dpr = qApp->devicePixelRatio();
    const QString cacheKey = fetchUrl.toString() + QLatin1Char('@') + QString::number(dpr);

    if (const QPixmap *cached = fullImageCache().object(cacheKey)) {
        applyFullImage(*cached);
        return;
    }

    QNetworkRequest request(fetchUrl);
    QNetworkReply *reply = networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, cacheKey]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            isLoadingFull = false;
            update();
            return;
        }

        QByteArray data = reply->readAll();
        QPixmap pixmap;
        if (pixmap.loadFromData(data)) {
            const qreal dpr = qApp->devicePixelRatio();
            pixmap.setDevicePixelRatio(dpr);
            cacheFullImage(cacheKey, pixmap);
            applyFullImage(pixmap);
            return;
        }

        isLoadingFull = false;
        update();
    });
}

void ImageViewer::applyFullImage(const QPixmap &pixmap)
{
    if (!currentImage.isNull()) {
        QSizeF oldLogicalSize = currentImage.size() / currentImage.devicePixelRatio();
        QSizeF newLogicalSize = pixmap.size() / pixmap.devicePixelRatio();

        qreal scaleRatio = oldLogicalSize.width() / newLogicalSize.width();
        zoomLevel *= scaleRatio;
    }

    fullImage = pixmap;
    currentImage = fullImage;
    isLoadingFull = false;
    updateDisplayPixmap();
    update();
}

void ImageViewer::updateDisplayPixmap()
{
    if (currentImage.isNull()) {
        displayPixmap = QPixmap();
        return;
    }

    const qreal dpr = currentImage.devicePixelRatio();
    const QSize target(qMax(1, qRound(size().width() * dpr)),
                       qMax(1, qRound(size().height() * dpr)));
    const QSize sourceSize = currentImage.size();

    // Only pre-scale when it is a real downscale; upscaling the source into
    // the cache would soften detail without saving any per-repaint work.
    if (target.width() >= sourceSize.width() && target.height() >= sourceSize.height()) {
        displayPixmap = QPixmap();
        return;
    }

    displayPixmap = currentImage.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    displayPixmap.setDevicePixelRatio(dpr);
}

const QPixmap &ImageViewer::displayPixmapFor(const QRectF &destRect) const
{
    if (!displayPixmap.isNull()) {
        const QSizeF displayLogical =
                QSizeF(displayPixmap.size()) / displayPixmap.devicePixelRatio();
        // Use the pre-scaled copy only when it already covers the target
        // resolution, so zooming in past it falls back to the full source.
        if (destRect.width() <= displayLogical.width() && destRect.height() <= displayLogical.height())
            return displayPixmap;
    }
    return currentImage;
}

void ImageViewer::resetView()
{
    zoomLevel = 1.0;
    panOffset = QPointF(0, 0);

    if (!currentImage.isNull()) {
        QSizeF imageSize = currentImage.size() / currentImage.devicePixelRatio();
        QSizeF windowSize = size();

        qreal scaleX = windowSize.width() / imageSize.width();
        qreal scaleY = windowSize.height() / imageSize.height();
        zoomLevel = qMin(scaleX, scaleY) * 0.8;
    }
}

void ImageViewer::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    painter.fillRect(rect(), QColor(0, 0, 0, 100));

    if (currentImage.isNull())
        return;

    QSizeF imageSize = currentImage.size() / currentImage.devicePixelRatio();
    QSizeF scaledSize = imageSize * zoomLevel;

    QPointF center = QPointF(width() / 2.0, height() / 2.0) + panOffset;
    QRectF destRect(center.x() - scaledSize.width() / 2.0, center.y() - scaledSize.height() / 2.0,
                    scaledSize.width(), scaledSize.height());

    // Draw the pre-scaled display pixmap when it covers the target resolution
    // (avoids re-smoothing the full-resolution source on every pan/zoom
    // repaint); fall back to the full-resolution source when zoomed in past
    // the cached resolution to keep detail sharp.
    const QPixmap &source = displayPixmapFor(destRect);
    painter.drawPixmap(destRect, source, QRectF(QPointF(0, 0), source.size()));

    if (isLoadingFull) {
        painter.setPen(Qt::white);
        painter.drawText(rect(), Qt::AlignTop | Qt::AlignHCenter,
                         "\n" + tr("Loading full resolution..."));
    }

    // help text
    painter.setPen(QColor(255, 255, 255, 150));
    painter.drawText(
            rect().adjusted(10, 0, -10, -10), Qt::AlignBottom | Qt::AlignHCenter,
            tr("Scroll to zoom  |  Drag to pan  |  Esc or click outside to close  |  R to reset"));
}

void ImageViewer::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        QSizeF imageSize = currentImage.size() / currentImage.devicePixelRatio();
        QSizeF scaledSize = imageSize * zoomLevel;
        QPointF center = QPointF(width() / 2.0, height() / 2.0) + panOffset;
        QRectF imageRect(center.x() - scaledSize.width() / 2.0,
                         center.y() - scaledSize.height() / 2.0, scaledSize.width(),
                         scaledSize.height());

        if (imageRect.contains(event->pos())) {
            isPanning = true;
            lastMousePos = event->pos();
            setCursor(Qt::ClosedHandCursor);
        } else {
            close();
        }
    }
}

void ImageViewer::mouseMoveEvent(QMouseEvent *event)
{
    if (isPanning) {
        QPointF delta = event->pos() - lastMousePos;
        panOffset += delta;
        lastMousePos = event->pos();
        update();
    }
}

void ImageViewer::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isPanning) {
        isPanning = false;
        setCursor(Qt::OpenHandCursor);
    }
}

void ImageViewer::wheelEvent(QWheelEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QPointF mousePos = event->position();
#else
    QPointF mousePos = event->posF();
#endif
    QPointF beforeZoom = widgetToImage(mousePos);

    qreal factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
    qreal newZoom = zoomLevel * factor;

    newZoom = qBound(0.1, newZoom, 20.0);
    zoomLevel = newZoom;

    QPointF afterZoom = widgetToImage(mousePos);
    QPointF correction = (afterZoom - beforeZoom) * zoomLevel;
    panOffset += correction;

    update();
}

void ImageViewer::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        close();
        break;
    case Qt::Key_R:
        resetView();
        update();
        break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoomLevel *= 1.25;
        update();
        break;
    case Qt::Key_Minus:
        zoomLevel /= 1.25;
        update();
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

void ImageViewer::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event);
    // The pre-scaled display pixmap is sized for the current viewport;
    // regenerate it when the viewport changes.
    updateDisplayPixmap();
}

bool ImageViewer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == trackedWindow) {
        if (event->type() == QEvent::Move || event->type() == QEvent::Resize) {
            updateGeometryToParent();
        } else if (event->type() == QEvent::Close) {
            close();
        }
    }
    return QWidget::eventFilter(watched, event);
}

QPointF ImageViewer::imageToWidget(const QPointF &imagePoint) const
{
    QPointF center = QPointF(width() / 2.0, height() / 2.0) + panOffset;
    return center + imagePoint * zoomLevel;
}

QPointF ImageViewer::widgetToImage(const QPointF &widgetPoint) const
{
    QPointF center = QPointF(width() / 2.0, height() / 2.0) + panOffset;
    return (widgetPoint - center) / zoomLevel;
}

} // namespace UI
} // namespace Acheron
