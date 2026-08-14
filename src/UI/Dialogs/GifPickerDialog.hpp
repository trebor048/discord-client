#pragma once

#include <QDialog>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include "Discord/GifProvider.hpp"

class QGridLayout;
class QLineEdit;
class QMovie;
class QScrollArea;
class QPushButton;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QComboBox;
class QToolButton;
class QVBoxLayout;
class QWidget;

namespace Acheron {
namespace UI {

/**
 * Dialog for searching and selecting GIFs from multiple providers.
 *
 * Features:
 *  - Search bar with debounced auto-search
 *  - Trending GIFs / GIPHY categories
 *  - Grid of GIF thumbnails (click to select)
 *  - Hover-to-play animated previews
 *  - Infinite scroll pagination
 *  - API key configuration detection + warning
 */
class GifPickerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit GifPickerDialog(QWidget *parent = nullptr);

    /// Returns the selected GIF item.
    [[nodiscard]] const Discord::GifItem &selectedGif() const { return m_selected; }

signals:
    void gifSelected(const Discord::GifItem &gif);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class Mode {
        Trending,
        Search,
    };

    void loadTrending();
    void loadCategories();
    void loadSearch(const QString &query, int offset);
    void loadMore();
    void appendGifs(const QList<Discord::GifItem> &items, bool hasMore);
    void clearGrid();
    void clearCategoryBar();
    void buildCategoryBar(const QList<Discord::GifCategory> &cats);
    void updateProviderUi();
    void handleProviderChanged();
    void onGifWidgetClicked(const Discord::GifItem &item);
    void onScrollChanged(int value);
    void selectGif(const Discord::GifItem &item);

    QWidget *createGifWidget(const Discord::GifItem &item);
    void fetchThumbnail(QToolButton *btn, const QUrl &url);
    void startHoverMovie(QToolButton *btn, const QUrl &url);
    void stopHoverMovie(QToolButton *btn);

    Discord::GifProvider *m_provider;
    QNetworkAccessManager *m_thumbNam;

    QLineEdit *m_searchEdit = nullptr;
    QComboBox *m_providerCombo = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_gridContainer = nullptr;
    QGridLayout *m_gridLayout = nullptr;
    QWidget *m_categoryBar = nullptr;
    QScrollArea *m_categoryScroll = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_loadingLabel = nullptr;
    QLabel *m_apiKeyWarning = nullptr;

    QString m_currentQuery;
    int m_offset = 0; // Pagination offset for the active provider
    Mode m_mode = Mode::Trending;
    Discord::GifProvider::Provider m_currentProvider;

    QTimer *m_searchDebounce = nullptr;

    bool m_loading = false;
    bool m_hasMore = false;
    bool m_autoLoadPaused = false; // set past kAutoLoadGifLimit; cleared by "Load more"
    int m_loadedCount = 0;
    QPushButton *m_loadMoreButton = nullptr;
    int m_nextRow = 0;
    int m_nextCol = 0;
    int m_requestGeneration = 0; // Bumped on every clear; stale callbacks are discarded
    int m_categoriesGeneration = 0; // Bumped on provider switch; stale category responses discarded

    static constexpr int kGridColumns = 3;
    static constexpr int kGridCellWidth = 180;
    static constexpr int kGridCellHeight = 130;
    // Infinite scroll pauses past this many loaded results; the user resumes
    // it with an explicit "Load more" click so grids can't grow without bound.
    static constexpr int kAutoLoadGifLimit = 500;

    Discord::GifItem m_selected;
    QSet<QNetworkReply *> m_pendingThumbs;
    QHash<QToolButton *, QMovie *> m_hoverMovies;
    QHash<QToolButton *, QUrl> m_hoverUrls;
};

} // namespace UI
} // namespace Acheron
