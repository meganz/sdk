#include <mega/common/testing/path.h>
#include <mega/file_service/file_service.h>
#include <mega/file_service/testing/integration/real_client.h>

namespace mega
{
namespace file_service
{
namespace testing
{

using common::testing::Path;

RealClient::RealClient(const std::string& clientName,
                       const Path& databasePath,
                       const Path& storagePath):
    common::testing::Client(clientName, databasePath, storagePath),
    Client(clientName, databasePath, storagePath),
    common::testing::RealClient(clientName, databasePath, storagePath)
{}

RealClient::~RealClient() {}

FileService& RealClient::fileService() const
{
    return mClient->mFileService;
}

} // testing
} // file_service
} // mega
