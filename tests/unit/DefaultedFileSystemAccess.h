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
#include <megaapi.h>

#include "NotImplemented.h"

namespace mt {

class DefaultedFileSystemAccess: public mega::FileSystemAccess
{
public:
    using FileSystemAccess::getlocalfstype;

    bool issyncsupported(const mega::LocalPath&,
                         bool& b,
                         mega::SyncError& se,
                         mega::SyncWarning& sw) override
    {
        b = false;
        se = mega::NO_SYNC_ERROR;
        sw = mega::NO_SYNC_WARNING;
        return true;
    }

    DefaultedFileSystemAccess() {}

    std::unique_ptr<mega::FileAccess> newfileaccess(bool /*followSymLinks*/ = true) override
    {
        throw NotImplemented{__func__};
    }
    std::unique_ptr<mega::DirAccess> newdiraccess() override
    {
        throw NotImplemented{__func__};
    }
    bool getlocalfstype(const mega::LocalPath&, mega::FileSystemType& type) const override
    {
        return type = mega::FS_UNKNOWN, false;
    }
    bool getsname(const mega::LocalPath&, mega::LocalPath&) const override
    {
        throw NotImplemented{__func__};
    }
    bool renamelocal(const mega::LocalPath&, const mega::LocalPath&, bool = true) override
    {
        throw NotImplemented{__func__};
    }
    bool copylocal(const mega::LocalPath&, const mega::LocalPath&, mega::m_time_t) override
    {
        throw NotImplemented{__func__};
    }
    bool unlinklocal(const mega::LocalPath&) override
    {
        throw NotImplemented{__func__};
    }
    bool rmdirlocal(const mega::LocalPath&) override
    {
        throw NotImplemented{__func__};
    }
    bool mkdirlocal(const mega::LocalPath&, bool, bool) override
    {
        throw NotImplemented{__func__};
    }
    bool setmtimelocal(const mega::LocalPath&, mega::m_time_t) override
    {
        throw NotImplemented{__func__};
    }
    bool chdirlocal(mega::LocalPath&) const override
    {
        throw NotImplemented{__func__};
    }

    bool expanselocalpath(const mega::LocalPath& /*path*/,
                          mega::LocalPath& /*absolutepath*/) override
    {
        throw NotImplemented{__func__};
    }
    void addevents(mega::Waiter*, int) override
    {
        throw NotImplemented{__func__};
    }

    bool cwd(mega::LocalPath&) const override
    {
        throw NotImplemented{__func__};
    }
};

} // mt
