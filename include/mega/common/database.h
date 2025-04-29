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
    std::string execute(const char* statement);

    sqlite3* mDB;
    Logger* mLogger;
    std::string mPath;

public:
    Database(Logger& logger, const LocalPath& path);

    Database(Database&& other);

    ~Database();

    std::string execute(Badge<Transaction> badge, const char* statement);

    sqlite3* get(Badge<Query> badge);
    sqlite3* get(Badge<Transaction> badge);

    Logger& logger() const;

    Query query();

    Transaction transaction();
}; // Database

} // common
} // mega

