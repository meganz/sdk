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
    auto message = mDB->execute({}, "savepoint txn");

    if (!message.empty())
        throw LogErrorF(logger(),
                        "Unable to start transaction: %s",
                         message.c_str());

    mInProgress = true;

    LogDebug1(logger(), "Transaction started");
}

Transaction::Transaction(Transaction&& other)
  : mDB(std::move(other.mDB))
  , mInProgress(other.mInProgress)
{
    other.mDB = nullptr;
    other.mInProgress = false;
}

Transaction::~Transaction()
{
    try
    {
        if (mInProgress)
            rollback();
    }
    catch (std::runtime_error&)
    {
    }
}

Transaction& Transaction::operator=(Transaction&& rhs)
{
    Transaction temp(std::move(rhs));

    swap(temp);

    return *this;
}

void Transaction::commit()
{
    assert(mDB);

    if (!mInProgress)
        throw LogError1(logger(), "Can't commit an inactive transaction");

    auto message = mDB->execute({}, "release savepoint txn");

    if (!message.empty())
        throw LogErrorF(logger(),
                        "Unable to commit transaction: %s",
                         message.c_str());

    LogDebug1(logger(), "Transaction committed");

    mInProgress = false;
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
    assert(mDB);

    if (!mInProgress)
        throw LogError1(logger(), "Queries require an active transaction");

    return mDB->query();
}

ScopedQuery Transaction::query(Query& query)
{
    assert(mDB);

    if (!mInProgress)
        throw LogError1(logger(), "Queries require an active transaction");

    return ScopedQuery({}, query);
}

void Transaction::rollback()
{
    assert(mDB);

    if (!mInProgress)
        throw LogError1(logger(), "Can't rollback an inactive transaction");

    auto message = mDB->execute({}, "rollback transaction to savepoint txn");

    if (message.empty())
        message = mDB->execute({}, "release savepoint txn");

    if (!message.empty())
        throw LogErrorF(logger(),
                        "Unable to rollback transaction: %s",
                        message.c_str());

    LogDebug1(logger(), "Transaction rolled back");

    mInProgress = false;
}

void Transaction::swap(Transaction& other)
{
    using std::swap;

    swap(mDB, other.mDB);
    swap(mInProgress, other.mInProgress);
}

} // common
} // mega

