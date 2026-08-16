#include "GeneralPage.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include "UI/Widgets/CustomStatusEdit.hpp"
#include "Core/Settings.hpp"

namespace Acheron {
namespace UI {

GeneralPage::GeneralPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    // Edit Profile button at top
    editProfileBtn = new QPushButton(tr("Edit Profile"), this);
    layout->addWidget(editProfileBtn);
    connect(editProfileBtn, &QPushButton::clicked, this, &GeneralPage::editProfileRequested);

    inMemoryCacheCheckbox = new QCheckBox(tr("In-memory cache database (requires restart)"), this);
    inMemoryCacheCheckbox->setChecked(QSettings().value("general/in_memory_cache", false).toBool());
    layout->addWidget(inMemoryCacheCheckbox);

    notificationSoundsCheckbox = new QCheckBox(tr("Play a sound for new messages outside the current channel"), this);
    notificationSoundsCheckbox->setChecked(QSettings().value("notifications/sounds", true).toBool());
    layout->addWidget(notificationSoundsCheckbox);

    developerModeCheckbox = new QCheckBox(tr("Developer Mode"), this);
    developerModeCheckbox->setChecked(Core::Settings::instance().developerMode());
    layout->addWidget(developerModeCheckbox);

    auto *tabsLayout = new QFormLayout();
    newTabBehaviorCombo = new QComboBox(this);
    newTabBehaviorCombo->addItem(tr("Open channel picker"), "picker");
    newTabBehaviorCombo->addItem(tr("Duplicate current channel"), "duplicate");
    const QString newTabBehavior = QSettings().value("ui/newTabBehavior", "picker").toString();
    int newTabIdx = newTabBehaviorCombo->findData(newTabBehavior);
    if (newTabIdx < 0)
        newTabIdx = 0;
    newTabBehaviorCombo->setCurrentIndex(newTabIdx);
    tabsLayout->addRow(tr("When opening a new tab:"), newTabBehaviorCombo);
    layout->addLayout(tabsLayout);

    auto *statusLayout = new QFormLayout();
    customStatusWidget = new CustomStatusEdit(this);
    statusLayout->addRow(tr("Custom status"), customStatusWidget);
    layout->addLayout(statusLayout);

    auto *mediaGroup = new QGroupBox(tr("Media"), this);
    auto *mediaLayout = new QVBoxLayout(mediaGroup);

    autoplayGifsCheckbox = new QCheckBox(tr("Autoplay GIFs"), this);
    autoplayGifsCheckbox->setChecked(QSettings().value("ui/gifAutoplay", true).toBool());
    mediaLayout->addWidget(autoplayGifsCheckbox);

    autoplayVideosCheckbox = new QCheckBox(tr("Autoplay Videos"), this);
    autoplayVideosCheckbox->setChecked(QSettings().value("ui/videoAutoplay", true).toBool());
    mediaLayout->addWidget(autoplayVideosCheckbox);

    layout->addWidget(mediaGroup);

    layout->addStretch();

    connect(inMemoryCacheCheckbox, &QCheckBox::toggled, this, [](bool checked) {
        QSettings settings;
        settings.setValue("general/in_memory_cache", checked);
    });
    connect(notificationSoundsCheckbox, &QCheckBox::toggled, this, [this](bool checked) {
        QSettings settings;
        settings.setValue("notifications/sounds", checked);
        emit notificationSoundsChanged(checked);
    });
    connect(developerModeCheckbox, &QCheckBox::toggled, this, [](bool checked) {
        Core::Settings::instance().setDeveloperMode(checked);
    });
    connect(autoplayGifsCheckbox, &QCheckBox::toggled, this, [](bool checked) {
        QSettings().setValue("ui/gifAutoplay", checked);
    });
    connect(autoplayVideosCheckbox, &QCheckBox::toggled, this, [](bool checked) {
        QSettings().setValue("ui/videoAutoplay", checked);
    });
    connect(newTabBehaviorCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        QSettings().setValue("ui/newTabBehavior", newTabBehaviorCombo->currentData().toString());
        emit newTabBehaviorChanged();
    });
    connect(customStatusWidget, &CustomStatusEdit::statusChanged, this, [this](const QString &text, const QString &, qint64) {
        emit customStatusChanged(text);
    });
    connect(customStatusWidget, &CustomStatusEdit::statusCleared, this, [this]() {
        emit customStatusChanged(QString());
    });
}

void GeneralPage::setClient(Discord::Client *c)
{
    customStatusWidget->setClient(c);
}

} // namespace UI
} // namespace Acheron
