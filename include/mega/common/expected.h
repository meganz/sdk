#pragma once

#include <mega/common/expected_forward.h>
#include <mega/common/unexpected.h>

#include <cassert>
#include <type_traits>
#include <variant>

namespace mega
{
namespace common
{
namespace detail
{

template<typename T>
struct IsExpected: std::false_type
{}; // IsExpected<T>

template<typename E, typename T>
struct IsExpected<Expected<E, T>>: std::true_type
{}; // IsExpected<Expected<E, T>>

} // detail

template<typename E, typename T>
class Expected
{
    template<typename F, typename U>
    friend class Expected;

    template<typename U, typename V>
    static constexpr auto IsConstructibleV =
        std::is_constructible_v<U, V> || std::is_convertible_v<V, U>;

    template<typename U>
    static constexpr auto IsCompatibleValueV = !IsExpectedV<U> && IsConstructibleV<T, U>;

    std::variant<E, T> mValue;

public:
    Expected():
        mValue()
    {}

    template<typename F, typename U>
    Expected(Expected<F, U>&& other):
        mValue(std::visit(
            [](auto&& value)
            {
                return std::variant<E, T>(std::move(value));
            },
            other.mValue))
    {}

    template<typename F, typename U>
    Expected(const Expected<F, U>& other):
        mValue(std::visit(
            [](const auto& value)
            {
                return std::variant<E, T>(value);
            },
            other.mValue))
    {}

    template<typename F>
    Expected(Unexpected<F>&& other):
        mValue(std::in_place_type_t<E>(), other.value())
    {}

    template<typename F>
    Expected(const Unexpected<F>& other):
        mValue(std::in_place_type_t<E>(), other.value())
    {}

    template<typename U, std::enable_if_t<IsCompatibleValueV<U>>* = nullptr>
    Expected(U&& other):
        mValue(std::in_place_type_t<T>(), std::forward<U>(other))
    {}

    operator bool() const
    {
        return hasValue();
    }

    template<typename F, typename U>
    Expected& operator=(Expected<F, U>&& rhs)
    {
        Expected temp(std::move(rhs));

        swap(temp);

        return *this;
    }

    template<typename F, typename U>
    Expected& operator=(const Expected<F, U>& rhs)
    {
        Expected temp(rhs);

        swap(temp);

        return *this;
    }

    template<typename U, std::enable_if_t<IsCompatibleValueV<U>>* = nullptr>
    Expected& operator=(U&& rhs)
    {
        Expected temp(std::forward<U>(rhs));

        swap(temp);

        return *this;
    }

    T* operator->()
    {
        return &value();
    }

    const T* operator->() const
    {
        return &value();
    }

    T& operator*() &
    {
        return value();
    }

    T&& operator*() &&
    {
        return std::move(*this).value();
    }

    const T& operator*() const&
    {
        return value();
    }

    const T&& operator*() const&&
    {
        return std::move(*this).value();
    }

    template<typename F, typename U>
    bool operator==(const Expected<F, U>& rhs) const
    {
        if (hasValue())
            return rhs.hasValue() && std::get<T>(mValue) == std::get<U>(rhs.mValue);

        return rhs.hasError() && std::get<E>(mValue) == std::get<F>(rhs.mValue);
    }

    template<typename F>
    bool operator==(const Unexpected<F>& rhs) const
    {
        return hasError() && error() == rhs.value();
    }

    template<typename U, std::enable_if_t<IsCompatibleValueV<U>>* = nullptr>
    bool operator==(const U& rhs) const
    {
        return hasValue() && value() == rhs;
    }

    bool operator!() const
    {
        return hasError();
    }

    template<typename F, typename U>
    bool operator!=(const Expected<F, U>& rhs) const
    {
        return !(*this == rhs);
    }

    template<typename F>
    bool operator!=(const Unexpected<F>& rhs) const
    {
        return !(*this == rhs);
    }

    template<typename U, std::enable_if_t<IsCompatibleValueV<U>>* = nullptr>
    bool operator!=(const U& rhs) const
    {
        return !(*this == rhs);
    }

    bool hasError() const
    {
        return std::holds_alternative<E>(mValue);
    }

    bool hasValue() const
    {
        return std::holds_alternative<T>(mValue);
    }

    E& error() &
    {
        assert(hasError());

        return std::get<E>(mValue);
    }

    E&& error() &&
    {
        assert(hasError());

        return std::get<E>(std::move(mValue));
    }

    const E& error() const&
    {
        assert(hasError());

        return std::get<E>(mValue);
    }

    const E&& error() const&&
    {
        assert(hasError());

        return std::get<E>(std::move(mValue));
    }

    template<typename U>
    auto errorOr(U&& defaultValue) && -> std::enable_if_t<std::is_convertible_v<U, E>, E>
    {
        if (hasError())
            return std::get<E>(std::move(mValue));

        return std::forward<U>(defaultValue);
    }

    template<typename U>
    auto errorOr(U&& defaultValue) const& -> std::enable_if_t<std::is_convertible_v<U, E>, E>
    {
        if (hasError())
            return std::get<E>(mValue);

        return std::forward<U>(defaultValue);
    }

    void swap(Expected& other)
    {
        using std::swap;

        swap(mValue, other.mValue);
    }

    T& value() &
    {
        assert(hasValue());

        return std::get<T>(mValue);
    }

    T&& value() &&
    {
        assert(hasValue());

        return std::get<T>(std::move(mValue));
    }

    const T& value() const&
    {
        assert(hasValue());

        return std::get<T>(mValue);
    }

    const T&& value() const&&
    {
        assert(hasValue());

        return std::get<T>(std::move(mValue));
    }

    T valueOr(T&& defaultValue) &&
    {
        if (hasValue())
            return std::get<T>(std::move(mValue));

        return std::move(defaultValue);
    }

    T valueOr(T&& defaultValue) const&
    {
        if (hasValue())
            return std::get<T>(mValue);

        return std::move(defaultValue);
    }
}; // Expected<E, T>

// Sanity.
template<typename E, typename TE, typename TT>
class Expected<E, Expected<TE, TT>>;

template<typename E, typename T>
class Expected<E, Unexpected<T>>;

template<typename T>
struct IsExpected: detail::IsExpected<std::decay_t<T>>
{}; // IsExpected<T>

} // common
} // mega
