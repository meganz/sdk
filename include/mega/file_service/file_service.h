#pragma once

#include <mega/common/client_forward.h>
#include <mega/common/instance_logger.h>
#include <mega/common/node_key_data_forward.h>
#include <mega/common/shared_mutex.h>
#include <mega/file_service/file_event_observer.h>
#include <mega/file_service/file_event_observer_id.h>
#include <mega/file_service/file_forward.h>
#include <mega/file_service/file_id_forward.h>
#include <mega/file_service/file_info_forward.h>
#include <mega/file_service/file_service_callbacks.h>
#include <mega/file_service/file_service_context_pointer.h>
#include <mega/file_service/file_service_forward.h>
#include <mega/file_service/file_service_options.h>
#include <mega/file_service/file_service_result_forward.h>
#include <mega/file_service/file_service_result_or_forward.h>
#include <mega/file_service/storage_info.h>
#include <mega/localpath.h>
#include <mega/types.h>

#include <cstdint>
#include <string>

namespace mega
{
namespace file_service
{

class FileService
{
    // Logs instance lifetime.
    common::InstanceLogger<FileService> mInstanceLogger;

    // What context does this instance wrap?
    FileServiceContextPtr mContext;

    // Serializes access to mContext;
    mutable common::SharedMutex mContextLock;

    // Specifies how the service should perform storage reclamation.
    ReclaimOptions mReclaimOptions;

    // Serializes access to mReclaimOptions.
    common::SharedMutex mReclaimOptionsLock;

    // Specifies various metrics that control how the service behaves.
    ServiceOptions mServiceOptions;

    // Serializes access to mServiceOptions.
    common::SharedMutex mServiceOptionsLock;

    // A client that is used when SDK is not logged in, when there is no session id
    common::Client& mPublicClient;

    // Whether the initialize() has been called
    bool mInitialized{false};

    // Db root path when SDK is not logged in
    LocalPath mPublicDbRootPath;

    // A user storage name when SDK is not logged in, allocated once during logged in/out cycles
    std::string mPublicStorageName;

    // Construct the file service using mPublicClient
    auto construct() -> FileServiceResult;

public:
    FileService(common::Client& publicClient);

    ~FileService();

    // Add a foreign file to the service.
    auto add(NodeHandle handle,
             const common::NodeKeyData& keyData,
             std::uint64_t size) -> FileServiceResultOr<FileID>;

    // Notify observer when a file changes.
    auto addObserver(FileEventObserver observer) -> FileServiceResultOr<FileEventObserverID>;

    // Create a new file.
    auto create(NodeHandle parent, const std::string& name) -> FileServiceResultOr<File>;

    // Where is the service storing its database?
    auto databasePath() const -> FileServiceResultOr<LocalPath>;

    // Deinitialize the file service.
    void deinitialize(bool cleanCache, bool isDestructing);

    // Retrieve information about a file managed by the file service.
    auto info(FileID id) -> FileServiceResultOr<FileInfo>;

    // Initialize the file service.
    auto initialize(common::Client& client) -> FileServiceResult;

    // Open a file for reading or writing.
    auto open(NodeHandle parent, const std::string& name) -> FileServiceResultOr<File>;
    auto open(FileID id) -> FileServiceResultOr<File>;

    // Update the file service's options.
    void serviceOptions(const ServiceOptions& serviceOptions);

    // Retrieve the file service's current options.
    auto serviceOptions() -> ServiceOptions;

    // Purge all files from storage.
    //
    // This function is intended to be used by integration tests.
    //
    // If you do happen to call it in a different context, be aware that
    // this function will block the caller until all file (or file info)
    // references have been dropped.
    auto purge() -> FileServiceResult;

    // Start a background task to reclaim storage space immediately
    auto reclaim(ReclaimCallback callback,
                 std::optional<ReclaimOptions> reclaimOptions = std::nullopt) -> FileServiceResult;

    // Update the file service's reclaim options.
    void reclaimOptions(const ReclaimOptions& options);

    // Retrieve the file service's current reclaim options.
    auto reclaimOptions() -> ReclaimOptions;

    // Remove a previously added file observer.
    auto removeObserver(FileEventObserverID id) -> FileServiceResult;

    // Get storage size information in detail such as reclaimable storage size
    auto storageInfo(const ReclaimOptions* options = nullptr) -> FileServiceResultOr<StorageInfo>;

    // How much storage space is the service using? Better performance than storageInfo but with
    // less information
    auto storageUsed() -> FileServiceResultOr<std::uint64_t>;

    // Find out where the service is storing a particular file.
    auto userFilePath(FileID id) const -> FileServiceResultOr<LocalPath>;
}; // FileService

} // file_service
} // mega
