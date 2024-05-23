#pragma once

#include <iosfwd>
#include <string>

#include <mega/fuse/common/testing/client_forward.h>
#include <mega/fuse/common/testing/parameters_forward.h>
#include <mega/fuse/common/testing/path_forward.h>

namespace mega
{
namespace fuse
{
namespace testing
{

struct Parameters
{
    struct
    {
        using Accessor = ClientPtr& (*)();

        Accessor mReadOnly;
        Accessor mReadWrite;
    } mClients;

    std::string mName;

    struct
    {
        using Accessor = const Path& (*)();

        Accessor mObserver;
        Accessor mReadOnly;
        Accessor mReadWrite;
    } mPaths;

    bool mUseVersioning;
}; // Parameters

extern const Parameters SHARED_UNVERSIONED;
extern const Parameters SHARED_VERSIONED;
extern const Parameters STANDARD_UNVERSIONED;
extern const Parameters STANDARD_VERSIONED;

std::ostream& operator<<(std::ostream& ostream, const Parameters& parameters);

std::string toString(const Parameters& parameters);

} // testing
} // fuse
} // mega

