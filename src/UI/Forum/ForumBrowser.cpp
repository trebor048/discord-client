#include "ForumBrowser.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include "Core/ForumManager.hpp"
#include "ForumPostDelegate.hpp"
#include "ForumPostModel.hpp"

namespace Acheron {
namespace UI {

ForumBrowser::ForumBrowser(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QWidget(this);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(12, 8, 12, 8);
    sortCombo = new QComboBox(header);
    sortCombo->addItem(tr("Recently Active"), static_cast<int>(Core::ForumSortMode::LATEST_ACTIVITY));
    sortCombo->addItem(tr("Date Posted"), static_cast<int>(Core::ForumSortMode::CREATION_DATE));
    newPostButton = new QPushButton(tr("New Post"), header);
    headerLayout->addWidget(sortCombo, 0);
    headerLayout->addStretch(1);
    headerLayout->addWidget(newPostButton, 0);

    loadingLabel = new QLabel(tr("Loading posts…"), this);
    loadingLabel->setContentsMargins(12, 6, 12, 6);
    loadingLabel->setVisible(false);

    listView = new QListView(this);
    listView->setUniformItemSizes(false);
    listView->setMouseTracking(true);
    listView->setSelectionMode(QAbstractItemView::SingleSelection);
    listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    listView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    listView->setFrameShape(QFrame::NoFrame);
    listView->setItemDelegate(new ForumPostDelegate(listView));

    layout->addWidget(header, 0);
    layout->addWidget(loadingLabel, 0);
    layout->addWidget(listView, 1);

    connect(newPostButton, &QPushButton::clicked, this, &ForumBrowser::newPostRequested);
    connect(sortCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) { emit sortModeChanged(sortCombo->currentData().toInt()); });
    connect(listView, &QListView::clicked, this, [this](const QModelIndex &idx) {
        if (!idx.isValid())
            return;
        Snowflake threadId = idx.data(ForumPostModel::ThreadIdRole).toULongLong();
        Snowflake guildId = idx.data(ForumPostModel::GuildIdRole).toULongLong();
        emit postActivated(threadId, guildId);
    });

    auto *timeRefresh = new QTimer(this);
    timeRefresh->setInterval(60 * 1000);
    connect(timeRefresh, &QTimer::timeout, listView->viewport(), qOverload<>(&QWidget::update));
    timeRefresh->start();
}

void ForumBrowser::setModel(ForumPostModel *m)
{
    listView->setModel(m);
}

void ForumBrowser::setLoading(bool loading)
{
    loadingLabel->setVisible(loading);
}

void ForumBrowser::setSortMode(int mode)
{
    QSignalBlocker blocker(sortCombo);
    sortCombo->setCurrentIndex(sortCombo->findData(mode));
}

} // namespace UI
} // namespace Acheron
