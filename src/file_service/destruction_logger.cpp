#include <mega/file_service/destruction_logger.h>
#include <mega/file_service/logging.h>

namespace mega
{
namespace file_service
{

DestructionLogger::DestructionLogger(const char* name):
    mName(name)
{
    FSInfoF("Constructing %s...", mName);
}

DestructionLogger::~DestructionLogger()
{
    FSInfoF("%s destroyed", mName);
}

} // file_service
} // mega
