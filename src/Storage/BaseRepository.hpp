#pragma once

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>

namespace Acheron {
namespace Storage {

class BaseRepository
{
public:
    BaseRepository(const QString &connName);

protected:
    QSqlDatabase getDb() const;

    // execute and log on error. true = success
    static bool execLogged(QSqlQuery &q, const char *context);

    // bind or NULL
    template <typename F>
    static void bindOptional(QSqlQuery &q, const QString &key, const F &field)
    {
        if (field.hasValue())
            q.bindValue(key, field);
        else
            q.bindValue(key, QVariant());
    }

    QString connName;
};

} // namespace Storage
} // namespace Acheron
