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

void Database::SqliteDeleter::operator()(sqlite3* db) const
{
    sqlite3_close(db);
}

std::string Database::execute(const char* statement)
{
    assert(mDB);
    assert(statement);

    char* message = nullptr;

    auto result = sqlite3_exec(mDB.get(), statement, nullptr, nullptr, &message);

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
    // Log a suitable error message and return an exception. On throw, mDB's
    // unique_ptr destructor closes the connection automatically, so there's no
    // need to close it here.
    auto failed = [this](const std::string& message)
    {
        // Log the failure and return a suitable exception.
        return LogErrorF(*mLogger,
                         "Unable to open database: %s: %s",
                         mPath.c_str(),
                         message.c_str());
    }; // failed

    // Keeps the open call itself simple.
    constexpr auto flags = SQLITE_OPEN_CREATE
                           | SQLITE_OPEN_FULLMUTEX
                           | SQLITE_OPEN_READWRITE;

    // Try and open the database. sqlite3_open_v2 may allocate a handle even on
    // failure, so take ownership of whatever it produces before checking the
    // result — that way the unique_ptr closes it regardless.
    sqlite3* db = nullptr;
    auto result = sqlite3_open_v2(mPath.c_str(), &db, flags, nullptr);
    mDB.reset(db);

    // Couldn't open the database.
    if (result != SQLITE_OK)
        throw failed(sqlite3_errstr(result));

    // Try and enable write-ahead logging.
    auto message = execute("pragma journal_mode = WAL");

    // Relax durability to NORMAL. Combined with WAL this is crash-safe (the
    // database is never corrupted; at most the last few uncheckpointed
    // transactions are lost on power failure) but avoids an fsync on every
    // commit. These are local cache databases that can be re-derived from the
    // cloud, so full per-commit durability isn't required and the per-commit
    // fsync dominates metadata operations such as create/write/truncate.
    if (message.empty())
        message = execute("pragma synchronous = NORMAL");

    // Try and enable foreign keys.
    if (message.empty())
        message = execute("pragma foreign_keys = ON");

    // Couldn't configure the database.
    if (!message.empty())
        throw failed(sqlite3_errmsg(mDB.get()));

    // Prepare the cached transaction-control statements now that the
    // connection is open and configured. They stay engaged for the rest of
    // this object's life and are reset and reused on every transaction.
    mBeginQuery = std::make_unique<Query>(query());
    *mBeginQuery = "savepoint txn";

    mCommitQuery = std::make_unique<Query>(query());
    *mCommitQuery = "release savepoint txn";

    mRollbackQuery = std::make_unique<Query>(query());
    *mRollbackQuery = "rollback transaction to savepoint txn";

    // Database has been opened successfully.
    LogDebugF(*mLogger, "Database opened: %s", mPath.c_str());
}

Database::Database(Database&& other)
  : mDB()
  , mLogger()
  , mPath()
{
    DatabaseLock guard(other);

    mDB = std::move(other.mDB);
    mLogger = other.mLogger;
    mPath = std::move(other.mPath);

    // Take over the cached queries, then re-point them at this database: each
    // still holds a Database* to other, which is about to be left empty.
    mBeginQuery = std::move(other.mBeginQuery);
    mCommitQuery = std::move(other.mCommitQuery);
    mRollbackQuery = std::move(other.mRollbackQuery);

    if (mBeginQuery)
        mBeginQuery->updateDatabase({}, *this);
    if (mCommitQuery)
        mCommitQuery->updateDatabase({}, *this);
    if (mRollbackQuery)
        mRollbackQuery->updateDatabase({}, *this);

    other.mLogger = nullptr;
}

Database::~Database()
{
    if (mLogger)
        LogDebugF(*mLogger, "Database closed: %s", mPath.c_str());
}

sqlite3* Database::get(Badge<Query>)
{
    return mDB.get();
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

void Database::begin(Badge<Transaction>)
{
    DatabaseLock guard(*this);

    // Reset before reuse: a prior execute() leaves the statement at
    // SQLITE_DONE without resetting it.
    mBeginQuery->reset();
    mBeginQuery->execute();
}

void Database::commit(Badge<Transaction>)
{
    DatabaseLock guard(*this);

    mCommitQuery->reset();
    mCommitQuery->execute();
}

void Database::rollback(Badge<Transaction>)
{
    DatabaseLock guard(*this);

    // Roll back to the savepoint, then release it (as the original ad-hoc
    // sequence did).
    mRollbackQuery->reset();
    mRollbackQuery->execute();

    mCommitQuery->reset();
    mCommitQuery->execute();
}

} // common
} // mega

