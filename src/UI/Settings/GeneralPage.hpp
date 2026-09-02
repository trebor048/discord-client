#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QHBoxLayout;
class QPushButton;

namespace Acheron {
namespace Core {
class ImageManager;
}
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
    void setImageManager(Core::ImageManager *imageManager);

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
    QCheckBox *silentTypingCheckbox;
    QCheckBox *autoplayGifsCheckbox;
    QCheckBox *autoplayVideosCheckbox;
    QComboBox *newTabBehaviorCombo;
    QPushButton *editProfileBtn;
    CustomStatusEdit *customStatusWidget;

    QHBoxLayout *quickReactionRowLayout = nullptr;
    QList<QPushButton *> quickEmojiButtons;

    Core::ImageManager *imageManager = nullptr;
};

} // namespace UI
} // namespace Acheron
