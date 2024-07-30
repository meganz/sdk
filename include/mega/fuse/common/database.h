#pragma once

#include <cstddef>
#include <string>
#include <mutex>

#include <mega/fuse/common/badge_forward.h>
#include <mega/fuse/common/database_forward.h>
#include <mega/fuse/common/lockable.h>
#include <mega/fuse/common/query_forward.h>
#include <mega/fuse/common/transaction_forward.h>

#include <mega/types.h>

struct sqlite3;

namespace mega
{
namespace fuse
{

template<>
struct LockableTraits<Database>
  : public LockableTraitsCommon<Database, std::recursive_mutex>
{
}; // LockableTraits<Database>

namespace detail
{

class Database
  : public Lockable<Database>
{
    std::string execute(const char* statement);

    sqlite3* mDB;
    std::string mPath;

public:
    Database(const LocalPath& path);

    Database(Database&& other);

    ~Database();

    std::string execute(Badge<Transaction> badge, const char* statement);

    Query query();

    Transaction transaction();
}; // Database

} // detail
} // fuse
} // mega

