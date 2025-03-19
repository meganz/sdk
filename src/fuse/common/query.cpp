#include <cassert>
#include <tuple>
#include <utility>

#include <sqlite3.h>

#include <mega/fuse/common/badge.h>
#include <mega/fuse/common/bind_handle.h>
#include <mega/fuse/common/inode_id.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/query.h>

#include <mega/filesystem.h>
#include <mega/types.h>

namespace mega
{
namespace fuse
{
namespace detail
{

void Field::match(const int requested) const
{
#define NAME(type) {type, #type}
    static const std::map<int, const char*> names = {
        NAME(SQLITE_BLOB),
        NAME(SQLITE_INTEGER),
        NAME(SQLITE_FLOAT),
        NAME(SQLITE_NULL),
        NAME(SQLITE_TEXT)
    }; // names
#undef NAME
    
    auto computed = type();

    if (computed == requested)
        return;

    throw FUSEErrorF("Field type mismatch: Requested an %s, got a %s",
                     names.at(requested),
                     names.at(computed));
}

int Field::type() const
{
    return sqlite3_column_type(mQuery.mStatement, mIndex);
}

Field::Field(const int index, Query& query)
  : mIndex(index)
  , mQuery(query)
{
}

Field::operator BindHandle() const
{
    return bindHandle();
}

Field::operator bool() const
{
    return boolean();
}

Field::operator NodeHandle() const
{
    return handle();
}

Field::operator LocalPath() const
{
    return path();
}

Field::operator InodeID() const
{
    return inode();
}

Field::operator std::int64_t() const
{
    return int64();
}

Field::operator std::string() const
{
    return string();
}

Field::operator std::uint64_t() const
{
    return uint64();
}

BindHandle Field::bindHandle() const
{
    return BindHandle(string());
}

bool Field::boolean() const
{
    return uint64() > 0;
}

NodeHandle Field::handle() const
{
    NodeHandle handle;

    handle.set6byte(uint64());

    return handle;
}

InodeID Field::inode() const
{
    auto value = uint64();

    // Not synthetic.
    if (value < InodeID(0).get())
        return InodeID(NodeHandle().set6byte(value));

    return InodeID(value);
}

std::int64_t Field::int64() const
{
    return static_cast<std::int64_t>(uint64());
}

bool Field::null() const
{
    return type() == SQLITE_NULL;
}

LocalPath Field::path() const
{
    return LocalPath::fromAbsolutePath(string());
}

std::string Field::string() const
{
    match(SQLITE_TEXT);

    auto value = sqlite3_column_text(mQuery.mStatement, mIndex);

    if (!value)
        throw FUSEErrorF("Unable to extract field value: %s",
                         sqlite3_errmsg(mQuery.mDB));

    auto length = sqlite3_column_bytes(mQuery.mStatement, mIndex);

    return std::string(reinterpret_cast<const char*>(value),
                       static_cast<std::size_t>(length));
}

std::uint64_t Field::uint64() const
{
    match(SQLITE_INTEGER);

    auto value = sqlite3_column_int64(mQuery.mStatement, mIndex);

    return static_cast<std::uint64_t>(value);
}

Parameter::Parameter(const int index, Query& query)
  : mIndex(index)
  , mQuery(query)
{
}

auto Parameter::operator=(const BindHandle& value) -> Parameter&
{
    return bindHandle(value);
}

auto Parameter::operator=(const bool value) -> Parameter&
{
    return boolean(value);
}

auto Parameter::operator=(const NodeHandle& value) -> Parameter&
{
    return handle(value);
}

auto Parameter::operator=(const InodeID& value) -> Parameter&
{
    return inode(value);
}

auto Parameter::operator=(const LocalPath& value) -> Parameter&
{
    return path(value);
}

auto Parameter::operator=(const std::int64_t value) -> Parameter&
{
    return int64(value);
}

auto Parameter::operator=(const std::nullptr_t) -> Parameter&
{
    return null();
}

auto Parameter::operator=(const std::string& value) -> Parameter&
{
    return string(value);
}

auto Parameter::operator=(const char* value) -> Parameter&
{
    assert(value);

    return string(std::string(value));
}

auto Parameter::operator=(const std::uint64_t value) -> Parameter&
{
    return uint64(value);
}

auto Parameter::bindHandle(const BindHandle& value) -> Parameter&
{
    return string(value.get());
}

auto Parameter::boolean(const bool value) -> Parameter&
{
    return uint64(value);
}

auto Parameter::handle(const NodeHandle& value) -> Parameter&
{
    return uint64(value.as8byte());
}

auto Parameter::inode(const InodeID& value) -> Parameter&
{
    return uint64(value.get());
}

auto Parameter::int64(const std::int64_t value) -> Parameter&
{
    return uint64(static_cast<std::uint64_t>(value));
}

auto Parameter::null() -> Parameter&
{
    auto result = sqlite3_bind_null(mQuery.mStatement, mIndex);

    if (result == SQLITE_OK)
        return *this;

    throw FUSEErrorF("Unable to bind parameter: %s",
                     sqlite3_errmsg(mQuery.mDB));
}

auto Parameter::path(const LocalPath& value) -> Parameter&
{
    return string(value.toPath(false));
}

auto Parameter::string(const std::string& value) -> Parameter&
{
    auto result = sqlite3_bind_text(mQuery.mStatement,
                                    mIndex,
                                    value.c_str(),
                                    static_cast<int>(value.size()),
                                    SQLITE_TRANSIENT);

    if (result == SQLITE_OK)
        return *this;

    throw FUSEErrorF("Unable to bind parameter: %s",
                     sqlite3_errmsg(mQuery.mDB));
}

auto Parameter::string(const char* value) -> Parameter&
{
    auto result = sqlite3_bind_text(mQuery.mStatement,
                                    mIndex,
                                    value,
                                    -1,
                                    SQLITE_TRANSIENT);

    if (result == SQLITE_OK)
        return *this;

    throw FUSEErrorF("Unable to bind parameter: %s",
                     sqlite3_errmsg(mQuery.mDB));
}

auto Parameter::uint64(const std::uint64_t value) -> Parameter&
{
    auto result = sqlite3_bind_int64(mQuery.mStatement,
                                     mIndex,
                                     static_cast<sqlite3_int64>(value));

    if (result == SQLITE_OK)
        return *this;

    throw FUSEErrorF("Unable to bind parameter: %s",
                     sqlite3_errmsg(mQuery.mDB));
}

Query::Query(Query&& other)
  : mDB(std::move(other.mDB))
  , mHasNext(std::move(other.mHasNext))
  , mFields(std::move(other.mFields))
  , mParameters(std::move(other.mParameters))
  , mStatement(std::move(other.mStatement))
{
    other.mDB = nullptr;
    other.mHasNext = false;
    other.mStatement = nullptr;
}

Query::~Query()
{
    sqlite3_finalize(mStatement);
}

Query& Query::operator=(Query&& rhs)
{
    Query temp(std::move(rhs));

    swap(temp);

    return *this;
}

Query& Query::operator=(const std::string& rhs)
{
    return operator=(rhs.c_str());
}

Query& Query::operator=(const char* rhs)
{
    assert(rhs);

    sqlite3_stmt* statement = nullptr;

    auto result = sqlite3_prepare_v2(mDB,
                                     rhs,
                                     -1,
                                     &statement,
                                     nullptr);

    if (result != SQLITE_OK)
    {
        auto exception = FUSEErrorF("Unable to prepare query: %s",
                                    sqlite3_errmsg(mDB));

        assert(false);

        throw exception;
    }

    std::map<std::string, int> fields;

    for (auto i = 0, j = sqlite3_column_count(statement);
         i < j;
         ++i)
    {
        auto* name = sqlite3_column_name(statement, i);

        if (!name)
            throw FUSEError1("Unable to prepare query: "
                             "Couldn't extract field name");

        fields.emplace(name, i);
    }

    std::map<std::string, int> parameters;

    for (auto i = 0, j = sqlite3_bind_parameter_count(statement);
         i < j;
         ++i)
    {
        auto* name = sqlite3_bind_parameter_name(statement, i + 1);

        if (!name)
            throw FUSEError1("Unable to prepare query: "
                             "Couldn't extract parameter name");

        parameters.emplace(name, i + 1);
    }

    mHasNext = false;
    mFields = std::move(fields);
    mParameters = std::move(parameters);

    sqlite3_finalize(mStatement);

    mStatement = statement;

    return *this;
}

Query::operator bool() const
{
    return mHasNext;
}

Query& Query::operator++()
{
    auto* prefix = "Unable to retrieve row";

    if (!mHasNext)
        throw FUSEErrorF("%s: No further rows available", prefix);

    if (!mStatement)
        throw FUSEErrorF("%s: No statement has been executed", prefix);

    auto result = sqlite3_step(mStatement);

    mHasNext = result == SQLITE_ROW;

    if (result == SQLITE_DONE || result == SQLITE_ROW)
        return *this;

    throw FUSEErrorF("%s: %s", prefix, sqlite3_errmsg(mDB));
}

bool Query::operator!() const
{
    return !mHasNext;
}

std::uint64_t Query::changed() const
{
    if (mStatement)
        return static_cast<std::uint64_t>(sqlite3_changes(mDB));

    return 0;
}

void Query::clear()
{
    auto* prefix = "Unable to clear query parameters";

    if (!mStatement)
        throw FUSEErrorF("%s: No statement has been prepared", prefix);

    auto result = sqlite3_clear_bindings(mStatement);

    if (result != SQLITE_OK)
        throw FUSEErrorF("%s: %s", prefix, sqlite3_errmsg(mDB));
}

void Query::execute()
{
    auto* prefix = "Unable to execute query";

    if (!mStatement)
        throw FUSEErrorF("%s: No statement has been prepared", prefix);

    auto result = sqlite3_step(mStatement);

    mHasNext = result == SQLITE_ROW;

    if (result == SQLITE_DONE || result == SQLITE_ROW)
        return;

    throw FUSEErrorF("%s: %s", prefix, sqlite3_errmsg(mDB));
}

auto Query::field(const std::string& name) -> Field
{
    auto i = mFields.find(name);

    if (i != mFields.end())
        return Field(i->second, *this);

    throw FUSEErrorF("Query has no field named \"%s\"", name.c_str());
}

auto Query::field(const char* name) -> Field
{
    return field(std::string(name));
}

std::uint64_t Query::lastID() const
{
    assert(mDB);

    return static_cast<std::uint64_t>(sqlite3_last_insert_rowid(mDB));
}

auto Query::param(const std::string& name) -> Parameter
{
    auto i = mParameters.find(name);

    if (i != mParameters.end())
        return Parameter(i->second, *this);

    auto exception = FUSEErrorF("Query has no parameter named \"%s\"", name.c_str());

    assert(false);

    throw exception;
}

auto Query::param(const char* name) -> Parameter
{
    return param(std::string(name));
}

void Query::reset()
{
    auto* prefix = "Unable to reset query";

    if (!mStatement)
        throw FUSEErrorF("%s: No statement has been prepared", prefix);

    auto result = sqlite3_reset(mStatement);

    if (result != SQLITE_OK)
        throw FUSEErrorF("%s: %s", prefix, sqlite3_errmsg(mDB));

    mHasNext = false;
}

void Query::swap(Query& other)
{
    using std::swap;

    swap(other.mDB, other.mDB);
    swap(other.mFields, other.mFields);
    swap(other.mParameters, other.mParameters);
    swap(other.mStatement, other.mStatement);
}

Query::Query(Badge<Database>, sqlite3& db)
  : mDB(&db)
  , mHasNext(false)
  , mFields()
  , mParameters()
  , mStatement(nullptr)
{
}

} // detail
} // fuse
} // mega

