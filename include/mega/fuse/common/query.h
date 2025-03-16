#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

#include <mega/fuse/common/badge_forward.h>
#include <mega/fuse/common/bind_handle_forward.h>
#include <mega/fuse/common/database_forward.h>
#include <mega/fuse/common/inode_id_forward.h>
#include <mega/fuse/common/query_forward.h>

#include <mega/types.h>

struct sqlite3;
struct sqlite3_stmt;

namespace mega
{
namespace fuse
{
namespace detail
{

class Field
{
    void match(const int expected) const;

    int type() const;

    const int mIndex;
    Query& mQuery;

public:
    Field(const int index, Query& query);

    Field(const Field& other) = default;

    operator BindHandle() const;

    operator bool() const;

    operator NodeHandle() const;

    operator InodeID() const;

    operator LocalPath() const;

    operator std::int64_t() const;

    operator std::string() const;

    operator std::uint64_t() const;

    BindHandle bindHandle() const;

    bool boolean() const;

    NodeHandle handle() const;

    InodeID inode() const;

    std::int64_t int64() const;

    bool null() const;

    LocalPath path() const;

    std::string string() const;

    std::uint64_t uint64() const;
}; // Field

class Parameter
{
    const int mIndex;
    Query& mQuery;

public:
    Parameter(const int index, Query& query);

    Parameter(const Parameter& other) = default;

    auto operator=(const BindHandle& value) -> Parameter&;

    auto operator=(const bool value) -> Parameter&;

    auto operator=(const NodeHandle& value) -> Parameter&;

    auto operator=(const InodeID& value) -> Parameter&;

    auto operator=(const LocalPath& value) -> Parameter&;

    auto operator=(const std::int64_t value) -> Parameter&;

    auto operator=(const std::nullptr_t) -> Parameter&;

    auto operator=(const std::string& value) -> Parameter&;

    auto operator=(const char* value) -> Parameter&;

    auto operator=(const std::uint64_t value) -> Parameter&;

    auto boolean(const bool value) -> Parameter&;

    auto bindHandle(const BindHandle& value) -> Parameter&;

    auto handle(const NodeHandle& value) -> Parameter&;

    auto inode(const InodeID& value) -> Parameter&;

    auto int64(const std::int64_t value) -> Parameter&;

    auto null() -> Parameter&;

    auto path(const LocalPath& value) -> Parameter&;

    auto string(const std::string& value) -> Parameter&;

    auto string(const char* value) -> Parameter&;

    auto uint64(const std::uint64_t value) -> Parameter&;
}; // Parameter

struct Query
{
    Query(Badge<Database> badge, sqlite3& db);

    Query(Query&& other);

    ~Query();

    Query& operator=(Query&& rhs);

    Query& operator=(const std::string& rhs);

    Query& operator=(const char* rhs);

    operator bool() const;

    Query& operator++();

    bool operator!() const;

    std::uint64_t changed() const;

    void clear();

    void execute();

    auto field(const std::string& name) -> Field;

    auto field(const char* name) -> Field;

    std::uint64_t lastID() const;

    auto param(const std::string& name) -> Parameter;

    auto param(const char* name) -> Parameter;

    void reset();

    void swap(Query& other);

private:
    friend class Field;
    friend class Parameter;

    sqlite3* mDB;
    bool mHasNext;
    std::map<std::string, int> mFields;
    std::map<std::string, int> mParameters;
    sqlite3_stmt* mStatement;
}; // Query

} // detail
} // fuse
} // mega

