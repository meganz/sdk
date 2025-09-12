#pragma once

#include <mega/common/testing/path_forward.h>
#include <mega/common/testing/real_client.h>
#include <mega/file_service/file_service_forward.h>
#include <mega/file_service/testing/integration/client.h>

#include <string>

namespace mega
{
namespace file_service
{
namespace testing
{

class RealClient: public Client, public common::testing::RealClient
{
public:
    RealClient(const std::string& clientName,
               const common::testing::Path& databasePath,
               const common::testing::Path& storagePath);

    ~RealClient();

    // Get our hands on the client's File Service interface.
    FileService& fileService() const override;
}; // RealClient

} // testing
} // file_service
} // mega
