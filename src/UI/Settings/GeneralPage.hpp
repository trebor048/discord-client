#pragma once

#include <QString>
#include <QWidget>

class QCheckBox;
class QComboBox;
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
    QCheckBox *inMemoryCacheCheckbox;
    QCheckBox *notificationSoundsCheckbox;
    QComboBox *newTabBehaviorCombo;
    QPushButton *editProfileBtn;
    CustomStatusEdit *customStatusWidget;
};

} // namespace UI
} // namespace Acheron
