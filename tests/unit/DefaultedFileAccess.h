/**
 * (c) 2019 by Mega Limited, Wellsford, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#pragma once

#include <mega/filesystem.h>

#include "NotImplemented.h"

namespace mt {

using ::mega::FSLogging;

class DefaultedFileAccess: public mega::FileAccess
{
public:
    DefaultedFileAccess():
        mega::FileAccess{nullptr}
    {}

    virtual bool fopen(const ::mega::LocalPath&,
                       bool,
                       bool,
                       FSLogging,
                       ::mega::DirAccess* = nullptr,
                       bool = false,
                       bool = false,
                       ::mega::LocalPath* = nullptr) override
    {
        throw NotImplemented{__func__};
        return false;
    }

    void updatelocalname(const ::mega::LocalPath&, bool) override
    {
        throw NotImplemented{__func__};
    }

    virtual bool frawread(void*, unsigned long, m_off_t, bool, FSLogging, bool* = nullptr) override
    {
        throw NotImplemented{__func__};
        return false;
    }

    virtual bool openf(FSLogging) override
    {
        throw NotImplemented{__func__};
        return false;
    }

    virtual void fclose() override
    {
        throw NotImplemented{__func__};
    }

    virtual bool fwrite(const void*,
                        unsigned long,
                        m_off_t,
                        unsigned long* = nullptr,
                        bool* = nullptr) override
    {
        throw NotImplemented{__func__};
        return false;
    }

    virtual bool fstat(::mega::m_time_t&, m_off_t&) override
    {
        throw NotImplemented{__func__};
        return false;
    }

    virtual bool ftruncate(m_off_t = 0) override
    {
        throw NotImplemented{__func__};
        return false;
    }

    virtual bool setSparse() override
    {
        throw NotImplemented{__func__};
        return false;
    }

    virtual std::optional<std::pair<std::uint64_t, std::uint64_t>> getFileSize() const override
    {
        throw NotImplemented{__func__};
        return std::nullopt;
    }

    virtual bool sysread(void*, unsigned long, m_off_t, bool*) override
    {
        throw NotImplemented{__func__};
        return false;
    }

    virtual bool sysstat(::mega::m_time_t*, m_off_t*, FSLogging) override
    {
        throw NotImplemented{__func__};
    }

    virtual bool sysopen(bool, FSLogging) override
    {
        throw NotImplemented{__func__};
        return false;
    }

    virtual void sysclose() override
    {
        throw NotImplemented{__func__};
    }
};

} // mt
