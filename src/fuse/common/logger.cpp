#include <mega/fuse/common/logger.h>

namespace mega
{
namespace fuse
{

using namespace common;

SubsystemLogger& logger()
{
    static SubsystemLogger logger("FUSE");

    return logger;
}

} // fuse
} // mega

