#pragma once

#include <string>
#include <mutex>

#include <mega/fuse/common/badge_forward.h>
#include <mega/fuse/common/database_forward.h>
#include <mega/fuse/common/query_forward.h>
#include <mega/fuse/common/scoped_query_forward.h>
#include <mega/fuse/common/transaction_forward.h>

struct sqlite3;

namespace mega
{
namespace fuse
{
namespace detail
{

class Transaction
{
    // What database is this transaction operating on?
    Database* mDB;

public:
    Transaction();

    Transaction(Badge<Database> badge, Database& database);

    Transaction(Transaction&& other);

    ~Transaction();

    Transaction& operator=(Transaction&& rhs);

    // Commit the transaction.
    void commit();

    // What database is associated with this transaction?
    Database& database() const;

    // Rollback the transaction.
    void rollback();

    // Start a query under this transaction.
    ScopedQuery query(Query& query);

    // Swap this transaction with another.
    void swap(Transaction& other);
}; // Transaction

} // detail
} // fuse
} // mega

