#include <sstream>

#include <mega/fuse/common/testing/parameters.h>
#include <mega/fuse/common/testing/test.h>

namespace mega
{
namespace fuse
{
namespace testing
{

static Parameters sharedSuite(bool useVersioning);
static Parameters standardSuite(bool useVersioning);

const Parameters SHARED_UNVERSIONED   = sharedSuite(false);
const Parameters SHARED_VERSIONED     = sharedSuite(true);
const Parameters STANDARD_UNVERSIONED = standardSuite(false);
const Parameters STANDARD_VERSIONED   = standardSuite(true);

std::ostream& operator<<(std::ostream& ostream, const Parameters& parameters)
{
    return ostream << toString(parameters);
}

std::string toString(const Parameters& parameters)
{
    std::ostringstream ostream;

    ostream << parameters.mName
            << (parameters.mUseVersioning ? "_" : "_un")
            << "versioned";

    return ostream.str();
}

Parameters sharedSuite(bool useVersioning)
{
    return {
        {
            &Test::ClientS,
            &Test::ClientS
        },
        "shared",
        {
            &Test::MountPathOS,
            &Test::MountPathRS,
            &Test::MountPathWS
        },
        useVersioning
    };
}

Parameters standardSuite(bool useVersioning)
{
    return {
        {
            &Test::ClientW,
            &Test::ClientW
        },
        "standard",
        {
            &Test::MountPathO,
            &Test::MountPathR,
            &Test::MountPathW
        },
        useVersioning
    };
}

} // testing
} // fuse
} // mega

