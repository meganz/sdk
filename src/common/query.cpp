#include <mega/common/badge.h>
#include <mega/common/database.h>
#include <mega/common/logging.h>
#include <mega/common/query.h>
#include <mega/filesystem.h>
#include <mega/types.h>

#include <cassert>
#include <chrono>
#include <sqlite3.h>
#include <tuple>
#include <utility>

namespace mega
{
namespace common
{

class RetryTimer
{
    // How much time should we wait, maximum, between retries?
    static constexpr auto MaxRetryInterval = std::chrono::seconds(2);

    // How much time should we spend trying to retry?
    static constexpr auto MaxRetryTime = std::chrono::minutes(1);

    // Current interval between retries.
    std::chrono::milliseconds mRetryInterval;

    // Total time taken trying to retry.
    std::chrono::milliseconds mRetryTime;

public:
    RetryTimer():
        mRetryInterval(8),
        mRetryTime(0)
    {}

    // Wait until we can perform another retry.
    //
    // Returns false if no further retries should be attempted.
    bool wait()
    {
        // Convenience.
        using std::chrono::duration_cast;
        using std::chrono::milliseconds;

        // Avoid having to cast many times.
        constexpr auto MaxInterval = duration_cast<milliseconds>(MaxRetryInterval);
        constexpr auto MaxTime = duration_cast<milliseconds>(MaxRetryTime);

        // We've already waited long enough.
        if (mRetryTime == MaxTime)
            return false;

        // Wait for a little while.
        std::this_thread::sleep_for(mRetryInterval);

        // Remember how long we waited.
        mRetryTime += mRetryInterval;

        // Exponentially increase retry interval.
        mRetryInterval = std::min(mRetryInterval * 2, MaxInterval);

        // Make sure our wait time never exceeds MaxTime.
        mRetryInterval = std::min(mRetryInterval, MaxTime - mRetryTime);

        // Let the caller know they should retry the operation.
        return true;
    }
}; // RetryTimer

// How long should we wait, maximum, before retrying an operation?
static constexpr auto MaxRetryInterval = std::chrono::seconds(2);

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

auto Parameter::string(const char* data, std::size_t length) -> Parameter&
{
    auto result = sqlite3_bind_text(mQuery.mStatement,
                                    mIndex,
                                    data,
                                    static_cast<int>(length),
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

Query::Query(Query&& other):
    mDB(std::exchange(other.mDB, nullptr)),
    mHasNext(std::exchange(other.mHasNext, false)),
    mFields(std::move(other.mFields)),
    mParameters(std::move(other.mParameters)),
    mStatement(std::exchange(other.mStatement, nullptr))
{}

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

    execute(prefix);

    return *this;
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

bool Query::execute()
{
    return execute("Unable to execute query");
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
    // Convenience.
    auto* prefix = "Unable to reset query";

    // Can't reset a statement that hasn't been prepared.
    if (!mStatement)
        throw LogErrorF(logger(), "%s: No statement has been prepared", prefix);

    // There will never be any results after the statement is reset.
    mHasNext = false;

    // Repeatedly try and reset the query.
    for (RetryTimer timer;;)
    {
        // Try and reset the query.
        auto result = sqlite3_reset(mStatement);

        // Query was reset successfully.
        if (result == SQLITE_OK)
            return;

        // Convenience.
        const auto* reason = sqlite3_errmsg(database());

        // Couldn't reset the query because:
        // - We encountered a nontransient error.
        // - We spent too long retrying.
        if ((result != SQLITE_BUSY && result != SQLITE_LOCKED) || !timer.wait())
            throw LogErrorF(logger(), "%s: %s", prefix, reason);

        // So we know when reset fails due to locks.
        LogWarningF(logger(), "%s: %s", prefix, reason);
    }
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

bool Query::execute(const char* prefix)
{
    // Sanity.
    assert(prefix);

    // Can't execute a query that hasn't been prepared.
    if (!mStatement)
        throw LogErrorF(logger(), "%s: No statement has been prepared", prefix);

    // Repeatedly attempt to execute the query.
    for (RetryTimer timer;;)
    {
        // Try and execute the query.
        auto result = sqlite3_step(mStatement);

        // Does the query have any further rows to return?
        mHasNext = result == SQLITE_ROW;

        // Query executed successfully.
        if (mHasNext || result == SQLITE_DONE)
            return mHasNext;

        // Reset the query.
        //
        // This is necessary for two reasons.
        //
        // First, we will want to retry the query if we were not able to
        // acquire a necessary file or table lock.
        //
        // Second, we want to clear the latest error set on our statement so
        // that later reset() calls do not fail spuriously.
        sqlite3_reset(mStatement);

        // Convenience.
        const auto* reason = sqlite3_errmsg(database());

        // Query failed because:
        // - We encountered a nontransient error.
        // - We spent too long retrying the query.
        if ((result != SQLITE_BUSY && result != SQLITE_LOCKED) || !timer.wait())
            throw LogErrorF(logger(), "%s: %s", prefix, reason);

        // So we know when queries fail due to locks.
        LogWarningF(logger(), "%s: %s", prefix, reason);
    }
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

