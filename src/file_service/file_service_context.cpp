#include <mega/file_service/file_service_context.h>

namespace mega
{
namespace file_service
{

using namespace common;

FileServiceContext::FileServiceContext(Client& client):
    mClient(client)
{}

} // file_service
} // mega
