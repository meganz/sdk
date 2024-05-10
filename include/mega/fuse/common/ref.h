#pragma once

#include <cassert>
#include <utility>

#include <mega/fuse/common/ref_forward.h>

namespace mega
{
namespace fuse
{

// Represents a reference to some reference-counted instance.
template<typename T>
class Ref
{
    template<typename U>
    friend class Ref;

    // What instance are we referencing?
    T* mInstance;

public:
    Ref()
      : mInstance(nullptr)
    {
    }

    template<typename U>
    explicit Ref(U* instance)
      : mInstance(instance)
    {
        if (mInstance)
            doRef(RefBadge(), *mInstance);
    }

    template<typename U>
    Ref(U* instance, AdoptRefTag)
      : mInstance(instance)
    {
        assert(instance);
    }

    Ref(const Ref& other)
      : Ref(other.mInstance)
    {
    }

    template<typename U>
    Ref(const Ref<U>& other)
      : Ref(other.mInstance)
    {
    }

    Ref(Ref&& other) noexcept
      : mInstance(std::move(other.mInstance))
    {
        other.mInstance = nullptr;
    }

    template<typename U>
    Ref(Ref<U>&& other) noexcept
      : mInstance(std::move(other.mInstance))
    {
        other.mInstance = nullptr;
    }

    ~Ref()
    {
        if (mInstance)
            doUnref(RefBadge(), *mInstance);
    }

    operator bool() const
    {
        return mInstance;
    }

    Ref& operator=(const Ref& rhs)
    {
        Ref temp(rhs);

        swap(temp);

        return *this;
    }

    template<typename U>
    Ref& operator=(const Ref<U>& rhs)
    {
        Ref temp(rhs);

        swap(temp);

        return *this;
    }

    Ref& operator=(Ref&& rhs) noexcept
    {
        Ref temp(std::move(rhs));

        swap(temp);

        return *this;
    }

    template<typename U>
    Ref& operator=(Ref<U>&& rhs) noexcept
    {
        Ref temp(std::move(rhs));

        swap(temp);

        return *this;
    }

    T& operator*()
    {
        assert(mInstance);

        return *mInstance;
    }

    const T& operator*() const
    {
        assert(mInstance);

        return *mInstance;
    }

    T* operator->()
    {
        assert(mInstance);

        return mInstance;
    }

    const T* operator->() const
    {
        assert(mInstance);

        return mInstance;
    }

    bool operator==(const Ref& rhs) const
    {
        return mInstance == rhs.mInstance;
    }

    template<typename U>
    bool operator==(const Ref<U>& rhs) const
    {
        return mInstance == rhs.mInstance;
    }

    template<typename U>
    bool operator<(const Ref<U>& rhs) const
    {
        return mInstance < rhs.mInstance;
    }

    bool operator!=(const Ref& rhs) const
    {
        return mInstance != rhs.mInstance;
    }

    template<typename U>
    bool operator!=(const Ref<U>& rhs) const
    {
        return mInstance != rhs.mInstance;
    }

    bool operator!() const
    {
        return !mInstance;
    }

    T* get()
    {
        return mInstance;
    }

    T* release()
    {
        auto* instance = mInstance;

        mInstance = nullptr;

        return instance;
    }

    template<typename U>
    void reset(U* instance = nullptr)
    {
        Ref temp(instance);

        swap(temp);
    }

    template<typename U>
    void swap(Ref<U>& other)
    {
        using std::swap;

        swap(mInstance, other.mInstance);
    }
}; // Ref<T>

template<typename T>
void swap(Ref<T>& lhs, Ref<T>& rhs)
{
    lhs.swap(rhs);
}

} // fuse
} // mega

