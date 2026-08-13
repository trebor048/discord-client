#pragma once

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QVBoxLayout>
#include <QWidget>

#include "Core/Snowflake.hpp"
#include "Discord/Entities.hpp"
#include "Discord/Enums.hpp"

namespace Acheron {
namespace Core {
class ClientInstance;
}
}

class QGroupBox;

namespace Acheron {
namespace UI {
namespace Widgets {

class RoleEditorWidget : public QWidget
{
    Q_OBJECT
public:
    explicit RoleEditorWidget(Core::ClientInstance *instance, Core::Snowflake guildId,
                              QWidget *parent = nullptr);

    void loadRole(Core::Snowflake roleId);
    void clearRole();

signals:
    void roleModified();

private slots:
    void onSave();
    void onColorPicker();

private:
    void setupUi();
    void buildPermissionCheckboxes();
    void applyRoleData(const Discord::Role &role);
    Discord::Permissions collectPermissions() const;

    struct PermissionEntry
    {
        Discord::Permission flag;
        QString label;
        QString category;
    };

    Core::ClientInstance *m_instance;
    Core::Snowflake m_guildId;
    Core::Snowflake m_roleId;
    Discord::Role m_currentRole;

    QLabel *m_roleNameTitle = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QPushButton *m_colorButton = nullptr;
    QLabel *m_colorPreview = nullptr;
    QCheckBox *m_hoistCheck = nullptr;
    QCheckBox *m_mentionableCheck = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_cancelButton = nullptr;

    QScrollArea *m_permissionsScroll = nullptr;
    QVBoxLayout *m_permissionsLayout = nullptr;
    QHash<Discord::Permission, QCheckBox *> m_permissionCheckboxes;

    QColor m_currentColor;
    bool m_loading = false;

    static const QList<PermissionEntry> &permissionEntries();
};

} // namespace Widgets
} // namespace UI
} // namespace Acheron
