#pragma once

#include <mega/fuse/platform/windows.h>

#include <utility>

#include <mega/fuse/platform/handle_forward.h>

namespace mega
{
namespace fuse
{
namespace platform
{

struct DefaultHandleDeleter
{
    void operator()(HANDLE handle)
    {
        CloseHandle(handle);
    }
}; // DefaultHandleDeleter

template<typename Deleter>
class Handle
{
    Deleter mDeleter;
    HANDLE mHandle;

public:
    Handle(const Deleter& deleter = Deleter())
      : Handle(INVALID_HANDLE_VALUE, deleter)
    {
    }

    explicit Handle(HANDLE handle, const Deleter& deleter = Deleter())
      : mDeleter(deleter)
      , mHandle(handle)
    {
    }

    Handle(Handle&& other)
      : mDeleter(std::move(other.mDeleter))
      , mHandle(std::move(other.mHandle))
    {
        other.mDeleter = Deleter();
        other.mHandle = INVALID_HANDLE_VALUE;
    }

    ~Handle()
    {
        if (mHandle != INVALID_HANDLE_VALUE)
            mDeleter(mHandle);
    }

    Handle& operator=(Handle&& rhs)
    {
        if (this == &rhs)
            return *this;

        Handle temp(std::move(rhs));

        swap(temp);

        return *this;
    }

    operator bool() const
    {
        return mHandle != INVALID_HANDLE_VALUE;
    }

    bool operator==(const Handle& rhs) const
    {
        return mHandle == rhs.mHandle;
    }

    bool operator!() const
    {
        return mHandle == INVALID_HANDLE_VALUE;
    }

    bool operator!=(const Handle& rhs) const
    {
        return mHandle != rhs.mHandle;
    }

    HANDLE get() const
    {
        return mHandle;
    }

    HANDLE release()
    {
        auto handle = mHandle;

        mHandle = INVALID_HANDLE_VALUE;

        return handle;
    }

    void reset(HANDLE other = INVALID_HANDLE_VALUE)
    {
        if (mHandle != INVALID_HANDLE_VALUE)
            mDeleter(mHandle);

        mHandle = other;
    }

    void swap(Handle& other)
    {
        using std::swap;

        swap(mDeleter, other.mDeleter);
        swap(mHandle, other.mHandle);
    }
}; // Handle

template<typename Deleter>
void swap(Handle<Deleter>& lhs, Handle<Deleter>& rhs)
{
    lhs.swap(rhs);
}

} // platform
} // fuse
} // mega

