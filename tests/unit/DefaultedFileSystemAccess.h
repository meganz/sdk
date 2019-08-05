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
