#pragma once

#include <QtWidgets>

namespace Acheron {
namespace UI {

class ModerateMemberDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Action {
        Kick,
        Ban,
        TempBan,
        Timeout,
    };

    struct Result
    {
        Action action = Action::Ban;
        QString reason;
        int deleteMessageSeconds = 0; // Ban / TempBan
        int durationSeconds = 0;      // TempBan
    };

    explicit ModerateMemberDialog(const QString &userName, Action initialAction = Action::Ban,
                                  QWidget *parent = nullptr);
    Result result() const;

private:
    void updateUi();

    QComboBox *actionCombo;
    QLineEdit *reasonEdit;
    QComboBox *deleteMessagesCombo;
    QComboBox *durationCombo;
};

} // namespace UI
} // namespace Acheron
