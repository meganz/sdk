#include <mega/file_service/logger.h>
#include <mega/log_level.h>

namespace mega
{
namespace file_service
{

using namespace common;

SubsystemLogger& logger()
{
    static SubsystemLogger logger("FileService");

    logger.logLevel(logDebug);

    return logger;
}

} // file_service
} // mega
