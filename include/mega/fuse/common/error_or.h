#pragma once

#include <cassert>
#include <type_traits>
#include <utility>

#include <mega/fuse/common/error_or_forward.h>

#include <mega/types.h>

namespace mega
{
namespace fuse
{
namespace detail
{

// Check if T is an error type.
template<typename T>
struct IsError
  : std::false_type
{
}; // IsError<T>

template<>
struct IsError<Error>
  : std::true_type
{
}; // IsError<Error>

template<>
struct IsError<ErrorCodes>
  : std::true_type
{
}; // IsError<ErrorCodes>

// Checks if T is an ErrorOr type.
template<typename T>
struct IsErrorOr
  : std::false_type
{
}; // IsErrorOr<T>

template<typename T>
struct IsErrorOr<ErrorOr<T>>
  : std::true_type
{
}; // IsErrorOr<ErrorOr<T>>

}; // detail

// Checks if T is the Error type.
template<typename T>
struct IsError
  : detail::IsError<typename std::decay<T>::type>
{
}; // IsError<T>

// Checks if T is an ErrorOr type.
template<typename T>
struct IsErrorOr
  : detail::IsErrorOr<typename std::decay<T>::type>
{
}; // IsErrorOr<T>

// Combines above checks.
template<typename T>
struct IsErrorLike
  : std::integral_constant<bool,
                           IsError<T>::value || IsErrorOr<T>::value>
{
}; // IsErrorLike<T>

// Represents an error or a value.
template<typename T>
class ErrorOr
{
    template<typename U>
    friend class ErrorOr;

    // Sanity.
    static_assert(!IsErrorLike<T>::value, "");

    union
    {
        Error mError;
        T mValue;
    };

    bool mHasValue;

public:
    ErrorOr()
      : mError()
      , mHasValue(false)
    {
    }

    ErrorOr(const ErrorOr& other)
      : mError()
      , mHasValue(other.mHasValue)
    {
        if (mHasValue)
            new (&mValue) T(other.mValue);
        else
            mError = other.mError;
    }

    template<typename U>
    ErrorOr(const ErrorOr<U>& other)
      : mError()
      , mHasValue(other.mHasValue)
    {
        if (mHasValue)
            new (&mValue) T(other.mValue);
        else
            mError = other.mError;
    }

    ErrorOr(ErrorOr&& other)
      : mError()
      , mHasValue(other.mHasValue)
    {
        if (other.mHasValue)
            new (&mValue) T(std::move(other.mValue));
        else
            mError = std::move(other.mError);
    }

    template<typename U>
    ErrorOr(ErrorOr<U>&& other)
      : mError()
      , mHasValue(other.mHasValue)
    {
        if (other.mHasValue)
            new (&mValue) T(std::move(other.mValue));
        else
            mError = std::move(other.mError);
    }

    ErrorOr(Error error)
      : mError(error)
      , mHasValue(false)
    {
    }

    ErrorOr(ErrorCodes error)
      : mError(error)
      , mHasValue(false)
    {
    }

    ErrorOr(T&& value)
      : mValue(std::move(value))
      , mHasValue(true)
    {
    }

    template<typename U,
             typename V = IsErrorLike<U>,
             typename W = typename std::enable_if<!V::value>::type>
    ErrorOr(U&& value)
      : mValue(std::forward<U>(value))
      , mHasValue(true)
    {
    }

    ErrorOr(const T& value)
      : mValue(value)
      , mHasValue(true)
    {
    }

    template<typename U>
    ErrorOr(const U& value)
      : mValue(value)
      , mHasValue(true)
    {
    }

    ~ErrorOr()
    {
        if (mHasValue)
            mValue.~T();
    }

    operator bool() const
    {
        return mHasValue;
    }

    T& operator*()
    {
        return value();
    }

    const T& operator*() const
    {
        return value();
    }

    T* operator->() 
    {
        assert(mHasValue);

        return &mValue;
    }

    const T* operator->() const
    {
        assert(mHasValue);

        return &mValue;
    }

    ErrorOr& operator=(const ErrorOr& rhs)
    {
        ErrorOr temp(rhs);

        swap(temp);

        return *this;
    }

    template<typename U>
    ErrorOr& operator=(const ErrorOr<U>& rhs)
    {
        ErrorOr temp(rhs);

        swap(temp);

        return *this;
    }

    template<typename U>
    ErrorOr& operator=(const U& rhs)
    {
        ErrorOr temp(rhs);

        swap(temp);

        return *this;
    }

    ErrorOr& operator=(Error rhs)
    {
        ErrorOr temp(rhs);

        swap(temp);

        return *this;
    }

    ErrorOr& operator=(ErrorOr&& rhs)
    {
        ErrorOr temp(std::move(rhs));

        swap(temp);

        return *this;
    }

    template<typename U>
    ErrorOr& operator=(ErrorOr<U>&& rhs)
    {
        ErrorOr temp(std::move(rhs));

        swap(temp);

        return *this;
    }

    template<typename U>
    ErrorOr& operator=(U&& rhs)
    {
        ErrorOr temp(std::move(rhs));

        swap(temp);

        return *this;
    }

    template<typename U>
    auto operator==(U rhs) const
      -> typename std::enable_if<IsError<U>::value, bool>::type
    {
        return error() == rhs;
    }

    template<typename U>
    auto operator==(const U& rhs) const
      -> typename std::enable_if<!IsError<U>::value, bool>::type
    {
        return value() == rhs;
    }

    template<typename U>
    bool operator!=(const U& rhs) const
    {
        return !(*this == rhs);
    }

    bool operator!() const
    {
        return !mHasValue;
    }

    Error error() const
    {
        if (mHasValue)
            return API_OK;

        return mError;
    }

    bool hasValue() const
    {
        return mHasValue;
    }

    void swap(ErrorOr& other)
    {
        using std::swap;

        if (mHasValue == other.mHasValue)
        {
            if (mHasValue)
                return swap(mValue, other.mValue);

            return swap(mError, other.mError);
        }

        auto* e = this;
        auto* v = &other;

        if (e->mHasValue)
            swap(e, v);

        auto result = e->mError;

        new (&e->mValue) T(std::move(v->mValue));

        v->mValue.~T();
        v->mError = result;

        swap(mHasValue, other.mHasValue);
    }

    T& value()
    {
        assert(mHasValue);

        return mValue;
    }

    const T& value() const
    {
        assert(mHasValue);

        return mValue;
    }
}; // ErrorOr<T>

} // fuse
} // mega

