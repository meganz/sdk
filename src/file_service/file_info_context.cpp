#include <mega/common/lock.h>
#include <mega/common/utility.h>
#include <mega/file_service/file_info_context.h>
#include <mega/file_service/file_info_context_badge.h>
#include <mega/file_service/file_service_context.h>
#include <mega/filesystem.h>

namespace mega
{
namespace file_service
{

using namespace common;

template<typename T>
auto FileInfoContext::get(T FileInfoContext::* const property) const
{
    SharedLock<SharedMutex> guard(mLock);

    return this->*property;
}

FileInfoContext::FileInfoContext(Activity activity,
                                 FileAccess& file,
                                 NodeHandle handle,
                                 FileID id,
                                 FileServiceContext& service):
    mActivity(std::move(activity)),
    mHandle(handle),
    mID(id),
    mLock(),
    mModified(file.mtime),
    mService(service),
    mSize(static_cast<std::uint64_t>(file.size))
{}

FileInfoContext::~FileInfoContext()
{
    mService.removeFromIndex(FileInfoContextBadge(), mID);
}

auto FileInfoContext::handle() const -> NodeHandle
{
    return get(&FileInfoContext::mHandle);
}

auto FileInfoContext::id() const -> FileID
{
    return mID;
}

auto FileInfoContext::modified() const -> std::int64_t
{
    return get(&FileInfoContext::mModified);
}

auto FileInfoContext::size() const -> std::uint64_t
{
    return get(&FileInfoContext::mSize);
}

} // file_service
} // mega
