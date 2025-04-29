#pragma once

#include <string>
#include <mutex>

#include <mega/common/badge_forward.h>
#include <mega/common/database_forward.h>
#include <mega/common/logger_forward.h>
#include <mega/common/query_forward.h>
#include <mega/common/scoped_query_forward.h>
#include <mega/common/transaction_forward.h>

struct sqlite3;

namespace mega
{
namespace common
{

class Transaction
{
    // What database is this transaction operating on?
    Database* mDB;

    // Is this transaction in progress?
    bool mInProgress;

public:
    Transaction();

    Transaction(Badge<Database> badge, Database& database);

    Transaction(Transaction&& other);

    ~Transaction();

    Transaction& operator=(Transaction&& rhs);

    // Commit the transaction.
    void commit();

    // What logger is associated with this transaction?
    Logger& logger() const;

    // Rollback the transaction.
    void rollback();

    // Start a query under this transaction.
    Query query();

    // Start a query under this transaction.
    ScopedQuery query(Query& query);

    // Swap this transaction with another.
    void swap(Transaction& other);
}; // Transaction

} // common
} // mega

