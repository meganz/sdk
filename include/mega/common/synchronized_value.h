#pragma once

#include <mega/common/synchronized_value_forward.h>

#include <mutex>
#include <utility>

namespace mega
{
namespace common
{

template<typename ValueType, bool DefaultConstructible = std::is_default_constructible_v<ValueType>>
class SynchronizedValue
{
    // ValueType must be copyable.
    static_assert(std::is_copy_constructible_v<ValueType>);

    // The data we want to protect.
    ValueType mData;

    // Serializes access to mData.
    mutable std::mutex mMutex;

public:
    template<typename... Arguments,
             typename = std::enable_if_t<std::is_constructible_v<ValueType, Arguments...>>>
    SynchronizedValue(Arguments&&... arguments):
        mData(std::forward<Arguments>(arguments)...)
    {}

    template<typename OtherValueType,
             typename = std::enable_if_t<std::is_constructible_v<ValueType, OtherValueType>>>
    SynchronizedValue(OtherValueType&& other):
        mData(std::forward<OtherValueType>(other))
    {}

    // Return a copy of this instance's data.
    operator ValueType() const
    {
        return get();
    }

    // Update this instance's data.
    template<typename OtherValueType,
             typename = std::enable_if_t<std::is_assignable_v<ValueType&, OtherValueType>>>
    SynchronizedValue& operator=(OtherValueType&& rhs)
    {
        set(std::forward<OtherValueType>(rhs));

        return *this;
    }

    // Return a copy of this instance's data.
    ValueType get() const
    {
        std::lock_guard guard(mMutex);

        return mData;
    }

    // Update this instance's data.
    template<typename OtherValueType,
             typename = std::enable_if_t<std::is_assignable_v<ValueType&, OtherValueType>>>
    void set(OtherValueType&& other)
    {
        std::lock_guard guard(mMutex);

        mData = std::forward<OtherValueType>(other);
    }
}; // SynchronizedValue<ValueType, false>

template<typename ValueType>
class SynchronizedValue<ValueType, true>: public SynchronizedValue<ValueType, false>
{
public:
    using SynchronizedValue<ValueType, false>::SynchronizedValue;

    SynchronizedValue() = default;
}; // SynchronizedValue<ValueType, true>

} // common
} // mega
