#pragma once

#include <QObject>
#include <QPoint>
#include <QString>

#include "Core/Snowflake.hpp"

namespace Acheron {
namespace UI {

class MainWindow;

// Builds the user context menu (and its avatar/role-chips header) that used to
// be assembled inline in MainWindow. Stateless: it reaches session/user data
// and the message input through its MainWindow back-reference.
class ContextMenuFactory : public QObject
{
    Q_OBJECT
public:
    explicit ContextMenuFactory(MainWindow *window);

    void showUserContextMenu(Core::Snowflake userId, Core::Snowflake guildId, QPoint globalPos);

private:
    MainWindow *m_window = nullptr;

    friend class MainWindow;
};

} // namespace UI
} // namespace Acheron
