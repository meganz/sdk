#pragma once

#include <mega/common/move_only_function_forward.h>

#include <cassert>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace mega
{
namespace common
{

// Temporary stand in for std::move_only_function<T>.
template<typename ReturnType, typename... ParameterType>
class MoveOnlyFunction<ReturnType(ParameterType...)>
{
    // Provides a uniform interface to invocable objects.
    class Invocable
    {
    protected:
        // Not intended to be instantiated directly.
        Invocable() = default;

    public:
        virtual ~Invocable() = default;

        // Invoke this object with the specified parameters.
        virtual ReturnType invoke(ParameterType... arguments) = 0;
    }; // Invocable

    // Convenience.
    using InvocablePtr = std::unique_ptr<Invocable>;

    // True if:
    // - T is distinct from this class.
    // - T is an invocable compatible with this class.
    template<typename T>
    using IsInvocable = std::conjunction<std::negation<std::is_same<MoveOnlyFunction, T>>,
                                         std::is_invocable_r<ReturnType, T, ParameterType...>>;

    // Convenience.
    template<typename T>
    static constexpr auto IsInvocableV = IsInvocable<T>::value;

    // Wraps an invocable object for later execution.
    template<typename T>
    class InvocableWrapper final: public Invocable
    {
        // The invocable object this instance wraps.
        T mInvocable;

    public:
        template<typename U>
        InvocableWrapper(U&& invocable):
            mInvocable(std::forward<U>(invocable))
        {}

        // Invoke this object with the specified parameters.
        ReturnType invoke(ParameterType... arguments) override
        {
            return std::invoke(mInvocable, std::forward<ParameterType>(arguments)...);
        }
    }; // InvocableWrapper<T>

    // The invocable object this function wraps.
    InvocablePtr mInvocable;

public:
    MoveOnlyFunction():
        mInvocable(nullptr)
    {}

    MoveOnlyFunction(MoveOnlyFunction&& other):
        mInvocable(std::exchange(other.mInvocable, nullptr))
    {}

    template<typename T, std::enable_if_t<IsInvocableV<T>>* = nullptr>
    MoveOnlyFunction(T&& invocable):
        mInvocable(std::make_unique<InvocableWrapper<std::decay_t<T>>>(std::forward<T>(invocable)))
    {}

    MoveOnlyFunction(std::nullptr_t):
        mInvocable(nullptr)
    {}

    explicit operator bool() const
    {
        return *this != nullptr;
    }

    MoveOnlyFunction& operator=(MoveOnlyFunction&& rhs)
    {
        MoveOnlyFunction temp(std::move(rhs));

        swap(temp);

        return *this;
    }

    template<typename T, std::enable_if_t<IsInvocableV<T>>* = nullptr>
    MoveOnlyFunction& operator=(T&& rhs)
    {
        MoveOnlyFunction temp(std::forward<T>(rhs));

        swap(temp);

        return *this;
    }

    MoveOnlyFunction& operator=(std::nullptr_t)
    {
        mInvocable.reset();

        return *this;
    }

    bool operator==(std::nullptr_t) const
    {
        return mInvocable == nullptr;
    }

    bool operator!=(std::nullptr_t) const
    {
        return !(*this == nullptr);
    }

    ReturnType operator()(ParameterType... arguments)
    {
        // Make sure we have an object to invoke.
        assert(mInvocable);

        // Invoke our object.
        return mInvocable->invoke(std::forward<ParameterType>(arguments)...);
    }

    bool operator!() const
    {
        return *this == nullptr;
    }

    void swap(MoveOnlyFunction& other)
    {
        using std::swap;

        swap(mInvocable, other.mInvocable);
    }
}; // MoveOnlyFunction<ReturnType(ParameterType...)>

} // common
} // mega
