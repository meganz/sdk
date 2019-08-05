#pragma once

#include <mega/filesystem.h>

#include "NotImplemented.h"

namespace mt {

class DefaultedDirAccess : public mega::DirAccess
{
public:
    bool dopen(std::string*, mega::FileAccess*, bool) override
    {
        throw NotImplemented{__func__};
    }
    bool dnext(std::string* localpath, std::string* localname, bool followsymlinks = true, mega::nodetype_t* = NULL) override
    {
        throw NotImplemented{__func__};
    }
};

} // mt
