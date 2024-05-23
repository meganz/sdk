#include <cassert>
#include <stdexcept>
#include <utility>

#include <sqlite3.h>

#include <mega/fuse/common/badge.h>
#include <mega/fuse/common/database.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/query.h>
#include <mega/fuse/common/scoped_query.h>
#include <mega/fuse/common/transaction.h>

namespace mega
{
namespace fuse
{
namespace detail
{

Transaction::Transaction()
  : mDB(nullptr)
{
}

Transaction::Transaction(Badge<Database>, Database& database)
  : mDB(&database)
{
    auto message = mDB->execute({}, "savepoint txn");

    if (!message.empty())
        throw FUSEErrorF("Unable to start transaction: %s",
                         message.c_str());

    FUSEDebug1("Transaction started");
}

Transaction::Transaction(Transaction&& other)
  : mDB(std::move(other.mDB))
{
    other.mDB = nullptr;
}

Transaction::~Transaction()
{
    try
    {
        if (mDB)
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
    if (!mDB)
        throw FUSEError1("Can't commit an inactive transaction");

    auto message = mDB->execute({}, "release savepoint txn");

    if (!message.empty())
        throw FUSEErrorF("Unable to commit transaction: %s",
                         message.c_str());

    FUSEDebug1("Transaction committed");

    mDB = nullptr;
}

Database& Transaction::database() const
{
    assert(mDB);

    return *mDB;
}

ScopedQuery Transaction::query(Query& query)
{
    if (!mDB)
        throw FUSEError1("Queries require an active transaction");

    return ScopedQuery({}, query);
}

void Transaction::rollback()
{
    if (!mDB)
        throw FUSEError1("Can't rollback an inactive transaction");

    auto message = mDB->execute({}, "rollback transaction to savepoint txn");

    if (message.empty())
        message = mDB->execute({}, "release savepoint txn");

    if (!message.empty())
        throw FUSEErrorF("Unable to rollback transaction: %s",
                         message.c_str());

    FUSEDebug1("Transaction rolled back");

    mDB = nullptr;
}

void Transaction::swap(Transaction& other)
{
    using std::swap;

    swap(mDB, other.mDB);
}

} // detail
} // fuse
} // mega

