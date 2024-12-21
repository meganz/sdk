#include <mega/fuse/common/testing/mock_client.h>

namespace mega
{
namespace fuse
{
namespace testing
{

MockClient::MockClient(const std::string& clientName,
                       const Path& databasePath,
                       const Path& storagePath):
    Client(clientName, databasePath, storagePath)
{
}

MockClient::~MockClient()
{
}

} // testing
} // fuse
} // mega

