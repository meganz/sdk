#include <cassert>
#include <cstddef>
#include <mutex>
#include <utility>

#include <sqlite3.h>

#include <mega/fuse/common/badge.h>
#include <mega/fuse/common/database.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/query.h>
#include <mega/fuse/common/transaction.h>

#include <mega/filesystem.h>

namespace mega
{
namespace fuse
{
namespace detail
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

Database::Database(const LocalPath& path)
  : Lockable()
  , mDB(nullptr)
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
        FUSEDebugF("Database opened: %s", mPath.c_str());
        return;
    }

    if (message.empty() && mDB)
        message = sqlite3_errmsg(mDB);

    if (message.empty())
        message = sqlite3_errstr(result);

    sqlite3_close(mDB);

    throw FUSEErrorF("Unable to open database: %s: %s",
                     mPath.c_str(),
                     message.c_str());
}

Database::Database(Database&& other)
  : mDB()
  , mPath()
{
    DatabaseLock guard(other);

    mDB = other.mDB;
    mPath = std::move(other.mPath);

    other.mDB = nullptr;
}

Database::~Database()
{
    sqlite3_close(mDB);

    FUSEDebugF("Database closed: %s", mPath.c_str());
}

std::string Database::execute(Badge<Transaction>, const char* statement)
{
    return execute(statement);
}

Query Database::query()
{
    return Query({}, *mDB);
}

Transaction Database::transaction()
{
    return Transaction({}, *this);
}

} // detail
} // fuse
} // mega

