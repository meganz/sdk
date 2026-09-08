#pragma once

#include <mega/common/badge_forward.h>
#include <mega/common/database_forward.h>
#include <mega/common/lockable.h>
#include <mega/common/logger_forward.h>
#include <mega/common/query_forward.h>
#include <mega/common/transaction_forward.h>
#include <mega/types.h>

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>

struct sqlite3;

namespace mega
{
namespace common
{

template<>
struct LockableTraits<Database>
  : public LockableTraitsCommon<Database, std::recursive_mutex>
{
}; // LockableTraits<Database>

class Database
  : public Lockable<Database>
{
    // Closes a SQLite connection. Used as the deleter for mDB so the
    // connection is closed automatically on destruction.
    struct SqliteDeleter
    {
        void operator()(sqlite3* db) const;
    };

    // See execute below.
    std::string execute(const char* statement);

    // The database's SQL context. Declared before the cached queries below so
    // that it is destroyed after them: members destruct in reverse
    // declaration order, and sqlite3_close fails if any prepared statement is
    // still open on the connection. The unique_ptr closes the connection
    // automatically, so neither the destructor nor the move constructor have
    // to do it by hand.
    std::unique_ptr<sqlite3, SqliteDeleter> mDB;

    // The databse's logger instance.
    Logger* mLogger;

    // The database file this instance is accessing.
    std::string mPath;

    // Cached transaction-control statements. These run on every transaction,
    // so they're prepared once and reused (via Transaction) rather than
    // recompiled each time. Prepared at construction. Guarded by the
    // database lock, like all other access to mDB.
    std::unique_ptr<Query> mBeginQuery;
    std::unique_ptr<Query> mCommitQuery;
    std::unique_ptr<Query> mRollbackQuery;

public:
    Database(Logger& logger, const LocalPath& path);

    Database(Database&& other);

    ~Database();

    // Retrieve a reference to this database's SQL context.
    sqlite3* get(Badge<Query> badge);

    // Retrieve a reference to this database's logger.
    Logger& logger() const;

    // Return a new query that references this database.
    Query query();

    // Return a new transaction that references this database.
    Transaction transaction();

    // Transaction control, used by Transaction only. Each runs a cached,
    // pre-prepared savepoint statement under the database lock.
    void begin(Badge<Transaction> badge);
    void commit(Badge<Transaction> badge);
    void rollback(Badge<Transaction> badge);
}; // Database

} // common
} // mega

