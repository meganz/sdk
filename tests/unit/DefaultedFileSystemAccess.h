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

    bool issyncsupported(const mega::LocalPath&, bool& b, mega::SyncError& se, mega::SyncWarning& sw) override { b = false; se = mega::NO_SYNC_ERROR; sw = mega::NO_SYNC_WARNING; return true;}


    DefaultedFileSystemAccess()
    {
        notifyerr = false;
        notifyfailed = true;
    }
    std::unique_ptr<mega::FileAccess> newfileaccess(bool followSymLinks = true) override
    {
        throw NotImplemented{__func__};
    }
    mega::DirAccess* newdiraccess() override
    {
        throw NotImplemented{__func__};
    }
    bool getlocalfstype(const mega::LocalPath&, mega::FileSystemType& type) const override
    {
        return type = mega::FS_UNKNOWN, false;
    }
    void path2local(const std::string*, std::string*) const override
    {
        throw NotImplemented{__func__};
    }

#if defined(_WIN32)
    void path2local(const std::string*, std::wstring*) const
    {
        throw NotImplemented{ __func__ };
    }
#endif

    void local2path(const std::string* local, std::string* path) const override
    {
        throw NotImplemented{ __func__ };
    }

#if defined(_WIN32)
    void local2path(const std::wstring* local, std::string* path) const override
    {
        path->resize((local->size() * sizeof(wchar_t) + 1) * 4 / sizeof(wchar_t) + 1);

        path->resize(WideCharToMultiByte(CP_UTF8, 0, local->data(),
            int(local->size()),
            (char*)path->data(),
            int(path->size()),
            NULL, NULL));

        normalize(path);
    }
#endif

    void tmpnamelocal(mega::LocalPath&) const override
    {
        throw NotImplemented{__func__};
    }
    bool getsname(const mega::LocalPath&, mega::LocalPath&) const override
    {
        throw NotImplemented{__func__};
    }
    bool renamelocal(mega::LocalPath&, mega::LocalPath&, bool = true) override
    {
        throw NotImplemented{__func__};
    }
    bool copylocal(mega::LocalPath&, mega::LocalPath&, mega::m_time_t) override
    {
        throw NotImplemented{__func__};
    }
    bool unlinklocal(mega::LocalPath&) override
    {
        throw NotImplemented{__func__};
    }
    bool rmdirlocal(mega::LocalPath&) override
    {
        throw NotImplemented{__func__};
    }
    bool mkdirlocal(mega::LocalPath&, bool = false) override
    {
        throw NotImplemented{__func__};
    }
    bool setmtimelocal(mega::LocalPath&, mega::m_time_t) override
    {
        throw NotImplemented{__func__};
    }
    bool chdirlocal(mega::LocalPath&) const override
    {
        throw NotImplemented{__func__};
    }
    bool getextension(const mega::LocalPath&, std::string&) const override
    {
        throw NotImplemented{__func__};
    }
    bool expanselocalpath(mega::LocalPath& path, mega::LocalPath& absolutepath) override
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
