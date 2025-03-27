#include <cassert>
#include <tuple>
#include <utility>

#include <sqlite3.h>

#include <mega/common/badge.h>
#include <mega/common/logging.h>
#include <mega/common/query.h>
#include <mega/common/database.h>

#include <mega/filesystem.h>
#include <mega/types.h>

namespace mega
{
namespace common
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

    throw LogErrorF(mQuery.logger(),
                    "Field type mismatch: Requested an %s, got a %s",
                    names.at(requested),
                    names.at(computed));
}

std::string Field::string() const
{
    match(SQLITE_TEXT);

    auto value = sqlite3_column_text(mQuery.mStatement, mIndex);

    if (!value)
        throw LogErrorF(mQuery.logger(),
                        "Unable to extract field value: %s",
                        sqlite3_errmsg(mQuery.database()));

    auto length = sqlite3_column_bytes(mQuery.mStatement, mIndex);

    return std::string(reinterpret_cast<const char*>(value),
                       static_cast<std::size_t>(length));
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

bool Field::null() const
{
    return type() == SQLITE_NULL;
}

std::uint64_t Field::uint64() const
{
    match(SQLITE_INTEGER);

    auto value = sqlite3_column_int64(mQuery.mStatement, mIndex);

    return static_cast<std::uint64_t>(value);
}

auto Parameter::null() -> Parameter&
{
    auto result = sqlite3_bind_null(mQuery.mStatement, mIndex);

    if (result == SQLITE_OK)
        return *this;

    throw LogErrorF(mQuery.logger(),
                    "Unable to bind parameter: %s",
                    sqlite3_errmsg(mQuery.database()));
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

    throw LogErrorF(mQuery.logger(),
                    "Unable to bind parameter: %s",
                    sqlite3_errmsg(mQuery.database()));
}

auto Parameter::uint64(const std::uint64_t value) -> Parameter&
{
    auto result = sqlite3_bind_int64(mQuery.mStatement,
                                     mIndex,
                                     static_cast<sqlite3_int64>(value));

    if (result == SQLITE_OK)
        return *this;

    throw LogErrorF(mQuery.logger(),
                    "Unable to bind parameter: %s",
                    sqlite3_errmsg(mQuery.database()));
}

Parameter::Parameter(const int index, Query& query)
  : mIndex(index)
  , mQuery(query)
{
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

    auto result = sqlite3_prepare_v2(database(),
                                     rhs,
                                     -1,
                                     &statement,
                                     nullptr);

    if (result != SQLITE_OK)
    {
        auto exception = LogErrorF(logger(),
                                   "Unable to prepare query: %s",
                                   sqlite3_errmsg(database()));

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
            throw LogError1(logger(),
                            "Unable to prepare query: "
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
            throw LogError1(logger(),
                            "Unable to prepare query: "
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
        throw LogErrorF(logger(), "%s: No further rows available", prefix);

    if (!mStatement)
        throw LogErrorF(logger(), "%s: No statement has been executed", prefix);

    auto result = sqlite3_step(mStatement);

    mHasNext = result == SQLITE_ROW;

    if (result == SQLITE_DONE || result == SQLITE_ROW)
        return *this;

    throw LogErrorF(logger(), "%s: %s", prefix, sqlite3_errmsg(database()));
}

bool Query::operator!() const
{
    return !mHasNext;
}

std::uint64_t Query::changed() const
{
    if (mStatement)
        return static_cast<std::uint64_t>(sqlite3_changes(database()));

    return 0;
}

void Query::clear()
{
    auto* prefix = "Unable to clear query parameters";

    if (!mStatement)
        throw LogErrorF(logger(), "%s: No statement has been prepared", prefix);

    auto result = sqlite3_clear_bindings(mStatement);

    if (result != SQLITE_OK)
        throw LogErrorF(logger(), "%s: %s", prefix, sqlite3_errmsg(database()));
}

void Query::execute()
{
    auto* prefix = "Unable to execute query";

    if (!mStatement)
        throw LogErrorF(logger(), "%s: No statement has been prepared", prefix);

    auto result = sqlite3_step(mStatement);

    mHasNext = result == SQLITE_ROW;

    if (result == SQLITE_DONE || result == SQLITE_ROW)
        return;

    throw LogErrorF(logger(), "%s: %s", prefix, sqlite3_errmsg(database()));
}

auto Query::field(const std::string& name) -> Field
{
    auto i = mFields.find(name);

    if (i != mFields.end())
        return Field(i->second, *this);

    throw LogErrorF(logger(), "Query has no field named \"%s\"", name.c_str());
}

auto Query::field(const char* name) -> Field
{
    return field(std::string(name));
}

std::uint64_t Query::lastID() const
{
    assert(mDB);

    return static_cast<std::uint64_t>(sqlite3_last_insert_rowid(database()));
}

Logger& Query::logger() const
{
    assert(mDB);

    return mDB->logger();
}

auto Query::param(const std::string& name) -> Parameter
{
    auto i = mParameters.find(name);

    if (i != mParameters.end())
        return Parameter(i->second, *this);

    auto exception = LogErrorF(logger(),
                               "Query has no parameter named \"%s\"",
                               name.c_str());

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
        throw LogErrorF(logger(), "%s: No statement has been prepared", prefix);

    auto result = sqlite3_reset(mStatement);

    if (result != SQLITE_OK)
        throw LogErrorF(logger(), "%s: %s", prefix, sqlite3_errmsg(database()));

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

sqlite3* Query::database() const
{
    assert(mDB);

    return mDB->get(Badge<Query>());
}

Query::Query(Badge<Database>, Database& db)
  : mDB(&db)
  , mHasNext(false)
  , mFields()
  , mParameters()
  , mStatement(nullptr)
{
}

LocalPath SerializationTraits<LocalPath>::from(const Field& field)
{
    return LocalPath::fromAbsolutePath(field.get<std::string>());
}

void SerializationTraits<LocalPath>::to(Parameter& parameter,
                                         const LocalPath& value)
{
    parameter.set(value.toPath(false));
}

NodeHandle SerializationTraits<NodeHandle>::from(const Field& field)
{
    return NodeHandle().set6byte(field.get<std::uint64_t>());
}

void SerializationTraits<NodeHandle>::to(Parameter& parameter,
                                         const NodeHandle& value)
{
    parameter.set(value.as8byte());
}

} // common
} // mega

