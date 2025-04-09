#pragma once

#include <mega/common/database.h>
#include <mega/common/query.h>
#include <mega/common/scoped_query.h>
#include <mega/common/transaction.h>

namespace mega
{
namespace common
{

// Call function with a transaction object.
template<typename Function,
         typename ResultType = std::invoke_result_t<Function, Transaction&>>
auto withTransaction(Database& database, Function&& function)
  -> std::enable_if_t<std::is_same_v<ResultType, void>>
{
    auto lock = DatabaseLock(database);
    auto transaction = database.transaction();

    std::invoke(std::forward<Function>(function), transaction);

    transaction.commit();
}

template<typename Function,
         typename ResultType = std::invoke_result_t<Function, Transaction&>>
auto withTransaction(Database& database, Function&& function)
  -> std::enable_if_t<!std::is_same_v<ResultType, void>>
{
    auto lock = DatabaseLock(database);
    auto transaction = database.transaction();
    auto result = std::invoke(std::forward<Function>(function), transaction);

    transaction.commit();

    return result;
}

// Call function with a query object.
template<typename Function>
auto withQuery(Database& database, Function&& function)
  -> std::invoke_result_t<Function, Query&&>
{
    return withTransaction(database, [&function](Transaction& transaction) {
        return std::invoke(std::forward<Function>(function), transaction.query());
    });
}

// Call function with a scoped query object.
template<typename Function>
auto withQuery(Database& database, Function&& function, Query& query)
  -> std::invoke_result_t<Function, common::ScopedQuery&&>
{
    return withTransaction(database, [&function, &query](Transaction& transaction) {
        return std::invoke(std::forward<Function>(function), transaction.query(query));
    });
}

} // common
} // mega

