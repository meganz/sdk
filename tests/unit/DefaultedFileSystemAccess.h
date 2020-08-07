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

class DefaultedFileSystemAccess : public mega::FileSystemAccess
{
public:
    using FileSystemAccess::getlocalfstype;

    DefaultedFileSystemAccess(const std::string &separator = "/")
    {
        notifyerr = false;
        notifyfailed = true;
        localseparator = separator;
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
    void local2path(const std::string* local, std::string* path) const override
    {
        throw NotImplemented{__func__};
    }
    void tmpnamelocal(mega::LocalPath&) const override
    {
        throw NotImplemented{__func__};
    }
    bool getsname(mega::LocalPath&, mega::LocalPath&) const override
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
    size_t lastpartlocal(const std::string*) const override
    {
        throw NotImplemented{__func__};
    }
    bool getextension(const mega::LocalPath&, char*, size_t) const override
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
};

} // mt
