#include <cassert>
#include <cstddef>
#include <mutex>
#include <utility>

#include <sqlite3.h>

#include <mega/common/badge.h>
#include <mega/common/database.h>
#include <mega/common/logger.h>
#include <mega/common/logging.h>
#include <mega/common/query.h>
#include <mega/common/transaction.h>

#include <mega/filesystem.h>

namespace mega
{
namespace common
{

std::string Database::execute(const char* statement)
{
    assert(mDB);
    assert(statement);

    char* message = nullptr;

    auto result = sqlite3_exec(mDB,
                               statement,
                               nullptr,
                               nullptr,
                               &message);

    if (result == SQLITE_OK)
        return std::string();

    std::string temp = message;

    sqlite3_free(message);

    return temp;
}

Database::Database(Logger& logger, const LocalPath& path)
  : Lockable()
  , mDB(nullptr)
  , mLogger(&logger)
  , mPath(path.toPath(false))
{
    constexpr auto flags = SQLITE_OPEN_CREATE
                           | SQLITE_OPEN_FULLMUTEX
                           | SQLITE_OPEN_READWRITE;

    std::string message;

    auto result = sqlite3_open_v2(mPath.c_str(),
                                  &mDB,
                                  flags,
                                  nullptr);

    if (result == SQLITE_OK)
        message = execute("pragma journal_mode = WAL");

    if (message.empty())
        message = execute("pragma foreign_keys = ON");

    if (message.empty())
    {
        LogDebugF(*mLogger, "Database opened: %s", mPath.c_str());
        return;
    }

    if (message.empty() && mDB)
        message = sqlite3_errmsg(mDB);

    if (message.empty())
        message = sqlite3_errstr(result);

    sqlite3_close(mDB);

    throw LogErrorF(*mLogger,
                    "Unable to open database: %s: %s",
                    mPath.c_str(),
                    message.c_str());
}

Database::Database(Database&& other)
  : mDB()
  , mLogger()
  , mPath()
{
    DatabaseLock guard(other);

    mDB = other.mDB;
    mLogger = other.mLogger;
    mPath = std::move(other.mPath);

    other.mDB = nullptr;
    other.mLogger = nullptr;
}

Database::~Database()
{
    sqlite3_close(mDB);

    if (mLogger)
        LogDebugF(*mLogger, "Database closed: %s", mPath.c_str());
}

std::string Database::execute(Badge<Transaction>, const char* statement)
{
    return execute(statement);
}

sqlite3* Database::get(Badge<Query>)
{
    return mDB;
}

sqlite3* Database::get(Badge<Transaction>)
{
    return mDB;
}

Logger& Database::logger() const
{
    assert(mLogger);

    return *mLogger;
}

Query Database::query()
{
    return Query({}, *this);
}

Transaction Database::transaction()
{
    return Transaction({}, *this);
}

} // common
} // mega

