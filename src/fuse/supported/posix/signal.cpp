#include <cassert>
#include <cstring>
#include <utility>

#include <mega/fuse/common/logging.h>
#include <mega/fuse/platform/signal.h>
#include <mega/fuse/platform/utility.h>

namespace mega
{
namespace fuse
{
namespace platform
{

Signal::Signal(const std::string& name)
try
  : mName(name)
  , mReader()
  , mWriter()
{
    // Create pipe.
    std::tie(mReader, mWriter) = pipe(true, true);

    // Make sure writer is closed on fork.
    mWriter.closeOnFork(true);
}
catch (std::runtime_error& exception)
{
    throw FUSEErrorF("Unable to create signal: %s", exception.what());
}

void Signal::clear()
{
    char dummy;

    FUSEDebugF("Clearing signal %s", mName.c_str());

    mReader.read(&dummy, 1);
}

int Signal::descriptor() const
{
    return mReader.get();
}

void Signal::raise()
{
    FUSEDebugF("Raising signal %s", mName.c_str()); 

    mWriter.write("", 1);
}

void Signal::swap(Signal& other)
{
    using std::swap;

    swap(mReader, other.mReader);
    swap(mWriter, other.mWriter);
}

} // platform
} // fuse
} // mega

