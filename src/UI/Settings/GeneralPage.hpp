#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QHBoxLayout;
class QPushButton;

namespace Acheron {
namespace Discord {
class Client;
}
namespace UI {

class CustomStatusEdit;

class GeneralPage : public QWidget
{
    Q_OBJECT
public:
    explicit GeneralPage(QWidget *parent = nullptr);

    void setClient(Discord::Client *client);

signals:
    void notificationSoundsChanged(bool enabled);
    void customStatusChanged(const QString &status);
    void editProfileRequested();
    void newTabBehaviorChanged();

private:
    // Quick-reaction bar customization
    void rebuildQuickReactionRow();
    void changeQuickReaction(int index);
    void removeQuickReaction(int index);
    void addQuickReaction();

    QCheckBox *inMemoryCacheCheckbox;
    QCheckBox *notificationSoundsCheckbox;
    QCheckBox *developerModeCheckbox;
    QCheckBox *autoplayGifsCheckbox;
    QCheckBox *autoplayVideosCheckbox;
    QComboBox *newTabBehaviorCombo;
    QPushButton *editProfileBtn;
    CustomStatusEdit *customStatusWidget;

    QHBoxLayout *quickReactionRowLayout = nullptr;
    QList<QPushButton *> quickEmojiButtons;
};

} // namespace UI
} // namespace Acheron
