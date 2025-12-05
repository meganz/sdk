#include <cassert>
#include <stdexcept>
#include <utility>

#include <sqlite3.h>

#include <mega/common/badge.h>
#include <mega/common/database.h>
#include <mega/common/logging.h>
#include <mega/common/query.h>
#include <mega/common/scoped_query.h>
#include <mega/common/transaction.h>

namespace mega
{
namespace common
{

Transaction::Transaction()
  : mDB(nullptr)
  , mInProgress(false)
{
}

Transaction::Transaction(Badge<Database>, Database& database)
  : mDB(&database)
  , mInProgress(false)
{
    try
    {
        // Instantiate a query so we can manipulate the database.
        auto query = database.query();

        // Try and start a new transaction.
        query = "savepoint txn";
        query.execute();

        // Transaction's now in progress.
        mInProgress = true;

        // Let debuggers know the transaction was started.
        LogDebug1(logger(), "Transaction started");
    }
    catch (std::runtime_error& exception)
    {
        // Log the reason why we couldn't start a transaction.
        throw LogErrorF(logger(), "Unable to start transaction: %s", exception.what());
    }
}

Transaction::Transaction(Transaction&& other):
    mDB(std::exchange(other.mDB, nullptr)),
    mInProgress(std::exchange(other.mInProgress, false))
{}

Transaction::~Transaction()
{
    try
    {
        // Try and roll back any transction in progress.
        if (mInProgress)
            rollback();
    }
    catch (std::runtime_error&)
    {
        // Any exception would've been logged in rollback().
    }
}

Transaction& Transaction::operator=(Transaction&& rhs)
{
    Transaction temp(std::move(rhs));

    swap(temp);

    return *this;
}

void Transaction::commit()
try
{
    // Sanity.
    assert(mDB);

    // Can't commit a transaction that hasn't been started.
    if (!mInProgress)
        throw LogError1(logger(), "Can't commit an inactive transaction");

    // So we can manipulate the database.
    auto query = mDB->query();

    // Try and release the transaction.
    query = "release savepoint txn";
    query.execute();

    // Transaction's been committed.
    mInProgress = false;

    // Let debuggers know the transaction was committed.
    LogDebug1(logger(), "Transaction committed");
}

catch (std::runtime_error& exception)
{
    // Let debuggers know why we couldn't commit the transaction.
    throw LogErrorF(logger(), "Unable to commit transaction: %s", exception.what());
}

bool Transaction::inProgress() const
{
    return mInProgress;
}

Logger& Transaction::logger() const
{
    assert(mDB);

    return mDB->logger();
}

Query Transaction::Transaction::query()
{
    // Sanity.
    assert(mDB);

    // Queries must be guarded by a transaction.
    if (!mInProgress)
        throw LogError1(logger(), "Queries require an active transaction");

    return mDB->query();
}

ScopedQuery Transaction::query(Query& query)
{
    // Sanity.
    assert(mDB);

    // Queries must be guarded by a transaction.
    if (!mInProgress)
        throw LogError1(logger(), "Queries require an active transaction");

    return ScopedQuery({}, query);
}

void Transaction::rollback()
try
{
    // Sanity.
    assert(mDB);

    // Can't roll back a transaction that never began.
    if (!mInProgress)
        throw LogError1(logger(), "Can't rollback an inactive transaction");

    auto query = mDB->query();

    // Try and roll back the transaction.
    query = "rollback transaction to savepoint txn";
    query.execute();

    // Try and release the transaction.
    query = "release savepoint txn";
    query.execute();

    // Transaction's been rolled back.
    mInProgress = false;

    // Let debuggers know the transaction was rolled back.
    LogDebug1(logger(), "Transaction rolled back");
}

catch (std::runtime_error& exception)
{
    // Let debuggers know why we couldn't roll back the transaction.
    throw LogErrorF(logger(), "Unable to rollback transaction: %s", exception.what());
}

void Transaction::swap(Transaction& other)
{
    using std::swap;

    swap(mDB, other.mDB);
    swap(mInProgress, other.mInProgress);
}

} // common
} // mega

