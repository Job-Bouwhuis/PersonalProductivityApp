#pragma once

#include <QSqlDatabase>
#include <QString>

namespace productivity::infrastructure {

class ApplicationDatabase final
{
public:
    ApplicationDatabase() = default;
    ~ApplicationDatabase();

    ApplicationDatabase(const ApplicationDatabase&) = delete;
    ApplicationDatabase& operator=(const ApplicationDatabase&) = delete;

    bool open();
    void close();
    QSqlDatabase connection() const;

private:
    QString connectionName_;
    QSqlDatabase database_;
};

} // namespace productivity::infrastructure
