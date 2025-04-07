#include <mega/file_service/construction_logger.h>
#include <mega/file_service/logging.h>

namespace mega
{
namespace file_service
{

ConstructionLogger::ConstructionLogger(const std::string& name):
    mName(name)
{
    FSInfoF("%s constructed", mName.c_str());
}

ConstructionLogger::~ConstructionLogger()
{
    FSInfoF("Destroying %s...", mName.c_str());
}

} // file_service
} // mega
