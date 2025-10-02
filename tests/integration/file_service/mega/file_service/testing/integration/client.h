#pragma once

#include <mega/common/testing/client.h>
#include <mega/common/testing/cloud_path_forward.h>
#include <mega/common/testing/path_forward.h>
#include <mega/file_service/file_id_forward.h>
#include <mega/file_service/file_info_forward.h>
#include <mega/file_service/file_service_forward.h>
#include <mega/file_service/file_service_result_or_forward.h>
#include <mega/file_service/testing/integration/client_forward.h>

#include <string>

namespace mega
{

class NodeHandle;

namespace file_service
{
namespace testing
{

class Client: public virtual common::testing::Client
{
protected:
    Client(const std::string& clientName,
           const common::testing::Path& databasePath,
           const common::testing::Path& storagePath);

public:
    virtual ~Client();

    // Create a new file that is to be managed by the File Service.
    auto fileCreate(NodeHandle parent, const std::string& name) -> FileServiceResultOr<File>;

    // Retrieve information about a file managed by the File Service.
    auto fileInfo(FileID id) const -> FileServiceResultOr<FileInfo>;

    auto fileInfo(common::testing::CloudPath path) const -> FileServiceResultOr<FileInfo>;

    // Open a file managed by the File Service.
    auto fileOpen(FileID id) const -> FileServiceResultOr<File>;

    auto fileOpen(common::testing::CloudPath parentPath, const std::string& name) const
        -> FileServiceResultOr<File>;

    auto fileOpen(common::testing::CloudPath path) const -> FileServiceResultOr<File>;

    // Get our hands on the client's File Service interface.
    virtual FileService& fileService() const = 0;
}; // Client

} // testing
} // common
} // mega
