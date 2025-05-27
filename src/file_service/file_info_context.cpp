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
    SharedLock guard(mLock);

    return this->*property;
}

FileInfoContext::FileInfoContext(Activity activity,
                                 NodeHandle handle,
                                 FileID id,
                                 std::int64_t modified,
                                 FileServiceContext& service,
                                 std::uint64_t size):
    mActivity(std::move(activity)),
    mHandle(handle),
    mID(id),
    mLock(),
    mModified(modified),
    mService(service),
    mSize(size)
{}

FileInfoContext::~FileInfoContext()
{
    mService.removeFromIndex(FileInfoContextBadge(), mID);
}

NodeHandle FileInfoContext::handle() const
{
    return get(&FileInfoContext::mHandle);
}

FileID FileInfoContext::id() const
{
    return mID;
}

std::int64_t FileInfoContext::modified() const
{
    return get(&FileInfoContext::mModified);
}

std::uint64_t FileInfoContext::size() const
{
    return get(&FileInfoContext::mSize);
}

} // file_service
} // mega
