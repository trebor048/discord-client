#include "GeneralPage.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include "UI/Widgets/CustomStatusEdit.hpp"
#include "Core/ImageManager.hpp"
#include "Core/Settings.hpp"

namespace Acheron {
namespace UI {

GeneralPage::GeneralPage(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(14);

    // Edit Profile button at top
    auto *profileRow = new QHBoxLayout();
    editProfileBtn = new QPushButton(tr("Edit Profile"), this);
    profileRow->addWidget(editProfileBtn);
    profileRow->addStretch(1);
    layout->addLayout(profileRow);
    connect(editProfileBtn, &QPushButton::clicked, this, &GeneralPage::editProfileRequested);

    auto *generalGroup = new QGroupBox(tr("General"), this);
    auto *generalLayout = new QVBoxLayout(generalGroup);
    generalLayout->setSpacing(12);

    inMemoryCacheCheckbox = new QCheckBox(tr("In-memory cache database (requires restart)"), generalGroup);
    inMemoryCacheCheckbox->setChecked(QSettings().value("general/in_memory_cache", false).toBool());
    generalLayout->addWidget(inMemoryCacheCheckbox);

    notificationSoundsCheckbox = new QCheckBox(tr("Play a sound for new messages outside the current channel"), generalGroup);
    notificationSoundsCheckbox->setChecked(QSettings().value("notifications/sounds", true).toBool());
    generalLayout->addWidget(notificationSoundsCheckbox);

    developerModeCheckbox = new QCheckBox(tr("Developer Mode"), generalGroup);
    developerModeCheckbox->setChecked(Core::Settings::instance().developerMode());
    generalLayout->addWidget(developerModeCheckbox);

    layout->addWidget(generalGroup);

    auto *tabsGroup = new QGroupBox(tr("Tabs & Status"), this);
    auto *tabsGroupLayout = new QVBoxLayout(tabsGroup);
    tabsGroupLayout->setSpacing(12);

    auto *tabsLayout = new QFormLayout();
    tabsLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    tabsLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);
    newTabBehaviorCombo = new QComboBox(tabsGroup);
    newTabBehaviorCombo->addItem(tr("Open channel picker"), "picker");
    newTabBehaviorCombo->addItem(tr("Duplicate current channel"), "duplicate");
    const QString newTabBehavior = QSettings().value("ui/newTabBehavior", "picker").toString();
    int newTabIdx = newTabBehaviorCombo->findData(newTabBehavior);
    if (newTabIdx < 0)
        newTabIdx = 0;
    newTabBehaviorCombo->setCurrentIndex(newTabIdx);
    tabsLayout->addRow(tr("When opening a new tab:"), newTabBehaviorCombo);
    tabsGroupLayout->addLayout(tabsLayout);

    auto *statusLayout = new QFormLayout();
    statusLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    statusLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);
    customStatusWidget = new CustomStatusEdit(tabsGroup);
    statusLayout->addRow(tr("Custom status"), customStatusWidget);
    tabsGroupLayout->addLayout(statusLayout);

    layout->addWidget(tabsGroup);

    auto *mediaGroup = new QGroupBox(tr("Media"), this);
    auto *mediaLayout = new QVBoxLayout(mediaGroup);
    mediaLayout->setSpacing(12);

    autoplayGifsCheckbox = new QCheckBox(tr("Autoplay GIFs"), mediaGroup);
    autoplayGifsCheckbox->setChecked(QSettings().value("ui/gifAutoplay", true).toBool());
    mediaLayout->addWidget(autoplayGifsCheckbox);

    autoplayVideosCheckbox = new QCheckBox(tr("Autoplay Videos"), mediaGroup);
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
        // The running ImageManager caches this flag; invalidate so the toggle
        // applies immediately (new loads + resume-from-pause pick it up).
        Core::ImageManager::invalidateGifAutoplayCache();
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
