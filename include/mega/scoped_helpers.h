#pragma once

#include <mega/traits.h>

#include <functional>
#include <memory>

namespace mega
{

// Executes a user-provided function when destroyed.
template<typename Destructor>
class ScopedDestructor
{
    Destructor mDestructor;

public:
    ScopedDestructor(Destructor destructor):
        mDestructor(std::move(destructor))
    {}

    ScopedDestructor(ScopedDestructor&& other) = default;

    ~ScopedDestructor()
    {
        mDestructor();
    }

    ScopedDestructor& operator=(ScopedDestructor&& rhs) = default;
}; // ScopedDestructor

// Returns an object that executes function when destroyed.
template<typename Function, typename = std::enable_if_t<std::is_invocable_v<Function>>>
auto makeScopedDestructor(Function function)
{
    return ScopedDestructor(std::move(function));
}

// Returns an object that executes function when destroyed.
//
// This specialization differs from the above as it allows you to execute a
// function that requires bound arguments.
template<typename Function,
         typename... Arguments,
         typename = std::enable_if_t<std::is_invocable_v<Function, Arguments...>>>
auto makeScopedDestructor(Function function, Arguments&&... arguments)
{
    auto destructor = std::bind(std::move(function), std::forward<Arguments>(arguments)...);

    return makeScopedDestructor(std::move(destructor));
}

// Changes the length (size) of a sequence and returns an object that will
// restore that sequence's original length when destroyed.
template<typename T>
auto makeScopedSizeRestorer(T& instance, std::size_t newSize)
{
    auto oldSize = SizeTraits<T>::size(instance);

    ResizeTraits<T>::resize(instance, newSize);

    return makeScopedDestructor(
        [&instance, oldSize]()
        {
            ResizeTraits<T>::resize(instance, oldSize);
        });
}

// Convenience specialization of the above.
template<typename T>
auto makeScopedSizeRestorer(T& instance)
{
    return makeScopedSizeRestorer(instance, SizeTraits<T>::size(instance));
}

// Changes the value of a specified location and returns an object that will
// restore that location's original value when destroyed.
template<typename T, typename U, typename = std::enable_if_t<std::is_convertible_v<U, T>>>
auto makeScopedValue(T& location, U value)
{
    auto destructor = [oldValue = std::move(location), &location]()
    {
        location = std::move(oldValue);
    }; // destructor

    location = std::forward<U>(value);

    return makeScopedDestructor(std::move(destructor));
}

// Instantiates a shared_ptr from an existing raw pointer.
template<typename Tp,
         typename = std::enable_if_t<std::is_pointer_v<Tp>>,
         typename Tr = std::remove_pointer_t<Tp>,
         typename Deleter = std::default_delete<Tr>>
auto makeSharedFrom(Tp pointer, Deleter deleter = Deleter())
{
    return std::shared_ptr<Tr>(pointer, std::move(deleter));
}

// Instantiates a unique_ptr from an existing raw pointer.
template<typename Tp,
         typename = std::enable_if_t<std::is_pointer_v<Tp>>,
         typename Tr = std::remove_pointer_t<Tp>,
         typename Deleter = std::default_delete<Tr>>
auto makeUniqueFrom(Tp pointer, Deleter deleter = Deleter())
{
    return std::unique_ptr<Tr, Deleter>(pointer, std::move(deleter));
}

} // mega
