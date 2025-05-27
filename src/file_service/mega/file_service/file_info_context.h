#include <mega/common/activity_monitor.h>
#include <mega/common/shared_mutex.h>
#include <mega/file_service/file_id.h>
#include <mega/file_service/file_info_context_forward.h>
#include <mega/file_service/file_service_context_forward.h>
#include <mega/types.h>

namespace mega
{
namespace file_service
{

class FileInfoContext
{
    template<typename T>
    auto get(T FileInfoContext::* const property) const;

    common::Activity mActivity;
    NodeHandle mHandle;
    const FileID mID;
    mutable common::SharedMutex mLock;
    std::int64_t mModified;
    FileServiceContext& mService;
    std::uint64_t mSize;

public:
    FileInfoContext(common::Activity activity,
                    NodeHandle handle,
                    FileID id,
                    std::int64_t modified,
                    FileServiceContext& service,
                    std::uint64_t size);

    ~FileInfoContext();

    auto handle() const -> NodeHandle;

    auto id() const -> FileID;

    auto modified() const -> std::int64_t;

    auto size() const -> std::uint64_t;
}; // FileInfoContext

} // file_service
} // mega
