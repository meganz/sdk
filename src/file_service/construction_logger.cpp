#include <mega/file_service/construction_logger.h>
#include <mega/file_service/logging.h>

namespace mega
{
namespace file_service
{

ConstructionLogger::ConstructionLogger(const char* name):
    mName(name)
{
    FSInfoF("%s constructed", mName);
}

ConstructionLogger::~ConstructionLogger()
{
    FSInfoF("Destroying %s...", mName);
}

} // file_service
} // mega
