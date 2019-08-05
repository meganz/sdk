#pragma once

#include <mega/filesystem.h>

#include "NotImplemented.h"

namespace mt {

class DefaultedFileAccess : public mega::FileAccess
{
public:
    DefaultedFileAccess()
    : mega::FileAccess{nullptr}
    {}

    bool fopen(std::string*, bool, bool) override
    {
        throw NotImplemented{__func__};
    }
    void updatelocalname(std::string*) override
    {
        throw NotImplemented{__func__};
    }
    bool fwrite(const mega::byte *, unsigned, m_off_t) override
    {
        throw NotImplemented{__func__};
    }
    bool sysread(mega::byte *, unsigned, m_off_t) override
    {
        throw NotImplemented{__func__};
    }
    bool sysstat(mega::m_time_t*, m_off_t*) override
    {
        throw NotImplemented{__func__};
    }
    bool sysopen(bool async = false) override
    {
        throw NotImplemented{__func__};
    }
    void sysclose() override
    {
        throw NotImplemented{__func__};
    }
};

} // mt
