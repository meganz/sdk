#pragma once

#include <cstdint>
#include <string>

#include <mega/fuse/common/badge_forward.h>
#include <mega/fuse/common/query_forward.h>
#include <mega/fuse/common/scoped_query_forward.h>
#include <mega/fuse/common/transaction_forward.h>

namespace mega
{
namespace fuse
{
namespace detail
{

class ScopedQuery
{
    Query* mQuery;

public:
    ScopedQuery();

    ScopedQuery(Badge<Transaction> badge, Query& query);

    ScopedQuery(ScopedQuery&& other);

    ~ScopedQuery();

    ScopedQuery& operator=(ScopedQuery&& rhs);

    operator bool() const;

    ScopedQuery& operator++();

    bool operator!() const;

    std::uint64_t changed() const;

    void clear();

    void execute();

    Field field(const std::string& name);
    Field field(const char* name);

    std::uint64_t lastID() const;

    Parameter param(const std::string& name);
    Parameter param(const char* name);

    void reset();

    void swap(ScopedQuery& other);
}; // ScopedQuery

void swap(ScopedQuery& lhs, ScopedQuery& rhs);

} // detail
} // fuse
} // mega

