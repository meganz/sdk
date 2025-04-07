#include <mega/file_service/file_service_context.h>

namespace mega
{
namespace file_service
{

using namespace common;

static const char* kName = "FileServiceContext";

FileServiceContext::FileServiceContext(Client& client):
    DestructionLogger(kName),
    mClient(client),
    mConstructionLogger(kName)
{}

} // file_service
} // mega
