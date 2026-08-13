#pragma once

#include <QWidget>

#include "Core/Snowflake.hpp"

class QListView;
class QLabel;
class QPushButton;
class QComboBox;

namespace Acheron {
namespace UI {

using Acheron::Core::Snowflake;

class ForumPostModel;

class ForumBrowser : public QWidget
{
    Q_OBJECT
public:
    explicit ForumBrowser(QWidget *parent = nullptr);

    void setModel(ForumPostModel *model);
    void setLoading(bool loading);
    void setSortMode(int mode);

signals:
    void postActivated(Snowflake threadId, Snowflake guildId);
    void newPostRequested();
    void sortModeChanged(int mode);

private:
    QListView *listView;
    QLabel *loadingLabel;
    QPushButton *newPostButton;
    QComboBox *sortCombo;
};

} // namespace UI
} // namespace Acheron
