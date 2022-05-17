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

//class DefaultedFileAccess : public mega::FileAccess
//{
//public:
//    DefaultedFileAccess()
//    : mega::FileAccess{nullptr}
//    {}
//
//    bool fopen(const mega::LocalPath&, bool, bool, mega::DirAccess* iteratingDir = nullptr, bool = false) override
//    {
//        throw NotImplemented{__func__};
//    }
//    void updatelocalname(const mega::LocalPath&, bool force) override
//    {
//        throw NotImplemented{__func__};
//    }
//    bool fwrite(const mega::byte *, unsigned, m_off_t) override
//    {
//        throw NotImplemented{__func__};
//    }
//    bool ftruncate() override
//    {
//        throw NotImplemented{__func__};
//    }
//    bool sysread(mega::byte *, unsigned, m_off_t) override
//    {
//        throw NotImplemented{__func__};
//    }
//    bool sysstat(mega::m_time_t*, m_off_t*) override
//    {
//        throw NotImplemented{__func__};
//    }
//    bool sysopen(bool async = false) override
//    {
//        throw NotImplemented{__func__};
//    }
//    void sysclose() override
//    {
//        throw NotImplemented{__func__};
//    }
//};

} // mt
