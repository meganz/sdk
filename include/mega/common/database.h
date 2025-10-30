#pragma once

#include <cstddef>
#include <string>
#include <mutex>

#include <mega/common/badge_forward.h>
#include <mega/common/database_forward.h>
#include <mega/common/lockable.h>
#include <mega/common/logger_forward.h>
#include <mega/common/query_forward.h>
#include <mega/common/transaction_forward.h>

#include <mega/types.h>

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
    // See execute below.
    std::string execute(const char* statement);

    // The database's SQL context.
    sqlite3* mDB;

    // The databse's logger instance.
    Logger* mLogger;

    // The database file this instance is accessing.
    std::string mPath;

public:
    Database(Logger& logger, const LocalPath& path);

    Database(Database&& other);

    ~Database();

    // Directly execute an SQL statement on this database.
    //
    // If the statement caused an error, a message describing that error is
    // returned to the caller.
    //
    // If the statement executed successfully, an empty string is returned
    // to the caller.
    std::string execute(Badge<Transaction> badge, const char* statement);

    // Retrieve a reference to this database's SQL context.
    sqlite3* get(Badge<Query> badge);
    sqlite3* get(Badge<Transaction> badge);

    // Retrieve a reference to this database's logger.
    Logger& logger() const;

    // Return a new query that references this database.
    Query query();

    // Return a new transaction that references this database.
    Transaction transaction();
}; // Database

} // common
} // mega

