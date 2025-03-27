#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

#include <mega/common/badge_forward.h>
#include <mega/common/database_forward.h>
#include <mega/common/logger_forward.h>
#include <mega/common/query_forward.h>
#include <mega/common/serialization_traits.h>

#include <mega/types.h>

struct sqlite3;
struct sqlite3_stmt;

namespace mega
{
namespace common
{

class Field
{
    void match(const int expected) const;

    std::string string() const;

    int type() const;

    std::uint64_t uint64() const;

    const int mIndex;
    Query& mQuery;

public:
    Field(const int index, Query& query);

    Field(const Field& other) = default;

    template<typename T>
    auto get() const
      -> std::enable_if_t<HasSerializationTraitsV<T>, T>
    {
        return SerializationTraits<T>::from(*this);
    }

    template<typename T,
             typename U = std::is_integral<T>,
             typename V = HasSerializationTraits<T>>
    auto get() const
      -> std::enable_if_t<U::value && !V::value, T>
    {
        return static_cast<T>(uint64());
    }

    template<typename T>
    auto get() const
      -> std::enable_if_t<std::is_same_v<std::string, T>, T>
    {
        return string();
    }

    bool null() const;
}; // Field

class Parameter
{
    auto null() -> Parameter&;

    auto string(const char* value) -> Parameter&;

    auto uint64(const std::uint64_t value) -> Parameter&;

    const int mIndex;
    Query& mQuery;

public:
    Parameter(const int index, Query& query);

    Parameter(const Parameter& other) = default;

    template<typename T>
    auto set(const T& value)
      -> std::enable_if_t<HasSerializationTraitsV<T>, Parameter&>
    {
        return SerializationTraits<T>::to(*this, value), *this;
    }

    template<typename T,
             typename U = std::is_integral<T>,
             typename V = HasSerializationTraits<T>>
    auto set(T value)
      -> std::enable_if_t<U::value && !V::value, Parameter&>
    {
        return uint64(static_cast<std::uint64_t>(value));
    }

    template<typename T>
    auto set(const T& value)
      -> std::enable_if_t<std::is_same_v<std::string, T>, Parameter&>
    {
        return string(value.c_str());
    }

    template<typename T>
    auto set(const T* value)
      -> std::enable_if_t<std::is_same_v<char, T>, Parameter&>
    {
        return string(value);
    }

    template<typename T>
    auto set(T)
      -> std::enable_if_t<std::is_same_v<std::nullptr_t, T>, Parameter&>
    {
        return null();
    }
}; // Parameter

struct Query
{
    Query(Badge<Database> badge, Database& database);

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

    Logger& logger() const;

    auto param(const std::string& name) -> Parameter;

    auto param(const char* name) -> Parameter;

    void reset();

    void swap(Query& other);

private:
    friend class Field;
    friend class Parameter;

    sqlite3* database() const;

    Database* mDB;
    bool mHasNext;
    std::map<std::string, int> mFields;
    std::map<std::string, int> mParameters;
    sqlite3_stmt* mStatement;
}; // Query

template<>
struct SerializationTraits<LocalPath>
{
    static LocalPath from(const Field& field);

    static void to(Parameter& parameter, const LocalPath& value);
}; // SerializationTraits<LocalPath>

template<>
struct SerializationTraits<NodeHandle>
{
    static NodeHandle from(const Field& field);

    static void to(Parameter& parameter, const NodeHandle& value);
}; // SerializationTraits<NodeHandle>

} // common
} // mega

