#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;

namespace Acheron {
namespace UI {

class StreamerModePage : public QWidget
{
    Q_OBJECT
public:
    explicit StreamerModePage(QWidget *parent = nullptr);

signals:
    void streamerModeChanged(bool enabled);

private:
    void checkForStreamingSoftware();

    QCheckBox *streamerModeCheckbox;
    QCheckBox *hidePersonalInfoCheckbox;
    QCheckBox *hideInviteLinksCheckbox;
    QCheckBox *muteNotificationSoundsCheckbox;
    QCheckBox *autoDetectCheckbox;
    QPushButton *checkNowBtn;
    QLabel *detectionLabel;
};

} // namespace UI
} // namespace Acheron
