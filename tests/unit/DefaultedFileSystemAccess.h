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
    mega::FileAccess* newfileaccess() override
    {
        throw NotImplemented{__func__};
    }
    mega::DirAccess* newdiraccess() override
    {
        throw NotImplemented{__func__};
    }
    void path2local(std::string*, std::string*) const override
    {
        throw NotImplemented{__func__};
    }
    void local2path(std::string* local, std::string* path) const override
    {
        *path = *local;
    }
    void tmpnamelocal(std::string*) const override
    {
        throw NotImplemented{__func__};
    }
    bool getsname(std::string*, std::string*) const override
    {
        throw NotImplemented{__func__};
    }
    bool renamelocal(std::string*, std::string*, bool = true) override
    {
        throw NotImplemented{__func__};
    }
    bool copylocal(std::string*, std::string*, mega::m_time_t) override
    {
        throw NotImplemented{__func__};
    }
    bool unlinklocal(std::string*) override
    {
        throw NotImplemented{__func__};
    }
    bool rmdirlocal(std::string*) override
    {
        throw NotImplemented{__func__};
    }
    bool mkdirlocal(std::string*, bool = false) override
    {
        throw NotImplemented{__func__};
    }
    bool setmtimelocal(std::string *, mega::m_time_t) override
    {
        throw NotImplemented{__func__};
    }
    bool chdirlocal(std::string*) const override
    {
        throw NotImplemented{__func__};
    }
    size_t lastpartlocal(std::string*) const override
    {
        throw NotImplemented{__func__};
    }
    bool getextension(std::string*, char*, size_t) const override
    {
        throw NotImplemented{__func__};
    }
    bool expanselocalpath(std::string *path, std::string *absolutepath) override
    {
        throw NotImplemented{__func__};
    }
    void addevents(mega::Waiter*, int) override
    {
        throw NotImplemented{__func__};
    }
};

} // mt
