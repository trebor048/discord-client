#include "ModerateMemberDialog.hpp"

namespace Acheron {
namespace UI {

ModerateMemberDialog::ModerateMemberDialog(const QString &userName, Action initialAction,
                                           QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Moderate %1").arg(userName));
    resize(380, 220);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    actionCombo = new QComboBox(this);
    actionCombo->addItem(tr("Kick"), static_cast<int>(Action::Kick));
    actionCombo->addItem(tr("Ban"), static_cast<int>(Action::Ban));
    actionCombo->addItem(tr("Temporary Ban"), static_cast<int>(Action::TempBan));
    actionCombo->addItem(tr("Timeout"), static_cast<int>(Action::Timeout));
    actionCombo->setCurrentIndex(actionCombo->findData(static_cast<int>(initialAction)));
    form->addRow(tr("Action"), actionCombo);

    reasonEdit = new QLineEdit(this);
    reasonEdit->setPlaceholderText(tr("Optional reason (shown in the audit log)"));
    form->addRow(tr("Reason"), reasonEdit);

    deleteMessagesCombo = new QComboBox(this);
    deleteMessagesCombo->addItem(tr("Don't delete any messages"), 0);
    deleteMessagesCombo->addItem(tr("Delete last 24 hours"), 86400);
    deleteMessagesCombo->addItem(tr("Delete last 7 days"), 604800);
    form->addRow(tr("Delete messages"), deleteMessagesCombo);

    durationCombo = new QComboBox(this);
    durationCombo->addItem(tr("1 hour"), 3600);
    durationCombo->addItem(tr("1 day"), 86400);
    durationCombo->addItem(tr("7 days"), 604800);
    form->addRow(tr("Duration"), durationCombo);

    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Confirm"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(actionCombo, &QComboBox::currentIndexChanged, this, [this]() { updateUi(); });
    updateUi();
}

ModerateMemberDialog::Result ModerateMemberDialog::result() const
{
    Result r;
    r.action = static_cast<Action>(actionCombo->currentData().toInt());
    r.reason = reasonEdit->text().trimmed();
    if (r.action != Action::Kick)
        r.deleteMessageSeconds = deleteMessagesCombo->currentData().toInt();
    if (r.action == Action::TempBan || r.action == Action::Timeout)
        r.durationSeconds = durationCombo->currentData().toInt();
    return r;
}

void ModerateMemberDialog::updateUi()
{
    const Action action = static_cast<Action>(actionCombo->currentData().toInt());
    deleteMessagesCombo->setEnabled(action != Action::Kick && action != Action::Timeout);
    durationCombo->setEnabled(action == Action::TempBan || action == Action::Timeout);
}

} // namespace UI
} // namespace Acheron
