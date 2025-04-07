#include <mega/file_service/destruction_logger.h>
#include <mega/file_service/logging.h>

namespace mega
{
namespace file_service
{

DestructionLogger::DestructionLogger(const std::string& name):
    mName(name)
{
    FSInfoF("Constructing %s...", mName.c_str());
}

DestructionLogger::~DestructionLogger()
{
    FSInfoF("%s destroyed", mName.c_str());
}

} // file_service
} // mega
