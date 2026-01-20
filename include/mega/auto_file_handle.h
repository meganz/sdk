#pragma once

#ifdef WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace mega
{

class AutoFileHandle
{
#ifdef WIN32
    using HandleType = HANDLE;
    const HandleType UNSET = INVALID_HANDLE_VALUE;
#else // _WIN32
    using HandleType = int;
    const HandleType UNSET = -1;
#endif // ! _WIN32

    HandleType h = UNSET;

public:
    AutoFileHandle() {}

    AutoFileHandle(HandleType ih):
        h(ih)
    {}

    ~AutoFileHandle()
    {
        close();
    }

    void close()
    {
        if (h != UNSET)
        {
#ifdef WIN32
            ::CloseHandle(h);
#else
            ::close(h);
#endif
        }

        h = UNSET;
    }

    AutoFileHandle& operator=(HandleType ih)
    {
        // avoid to leak a handle if changed
        if (ih != h)
            close();

        h = ih;
        return *this;
    }

    bool isSet() const
    {
        return h != UNSET;
    }

    // implicit conversion, so can pass into OS API
    operator HandleType() const
    {
        return h;
    }

    HandleType* ptr()
    {
        return &h;
    }

    HandleType get() const
    {
        return h;
    }

    // Prevent copying to avoid double-close issues
    AutoFileHandle(const AutoFileHandle&) = delete;
    AutoFileHandle& operator=(const AutoFileHandle&) = delete;

    // Allow moving
    AutoFileHandle(AutoFileHandle&& other) noexcept:
        h(other.h)
    {
        other.h = UNSET;
    }

    AutoFileHandle& operator=(AutoFileHandle&& other) noexcept
    {
        if (this != &other)
        {
            close();
            h = other.h;
            other.h = UNSET;
        }
        return *this;
    }
};

} // namespace mega
