#include <mega/file_service/file.h>
#include <mega/file_service/file_append_request.h>
#include <mega/file_service/file_context.h>
#include <mega/file_service/file_fetch_request.h>
#include <mega/file_service/file_flush_request.h>
#include <mega/file_service/file_info.h>
#include <mega/file_service/file_range.h>
#include <mega/file_service/file_read_request.h>
#include <mega/file_service/file_read_result.h>
#include <mega/file_service/file_remove_request.h>
#include <mega/file_service/file_result.h>
#include <mega/file_service/file_result_or.h>
#include <mega/file_service/file_service_context_badge.h>
#include <mega/file_service/file_touch_request.h>
#include <mega/file_service/file_truncate_request.h>
#include <mega/file_service/file_write_request.h>
#include <mega/file_service/logger.h>

#include <utility>

namespace mega
{
namespace file_service
{

File::File(FileServiceContextBadge, FileContextPtr context):
    mInstanceLogger("File", *this, logger()),
    mContext(std::move(context))
{}

File::~File() = default;

File::File(const File& other):
    mInstanceLogger("File", *this, logger()),
    mContext(other.mContext)
{}

File::File(File&& other):
    mInstanceLogger("File", *this, logger()),
    mContext(std::exchange(other.mContext, nullptr))
{}

File& File::operator=(const File& rhs)
{
    if (this != &rhs)
        mContext = rhs.mContext;

    return *this;
}

File& File::operator=(File&& rhs)
{
    using std::swap;

    if (this != &rhs)
        swap(mContext, rhs.mContext);

    return *this;
}

FileEventObserverID File::addObserver(FileEventObserver observer)
{
    assert(mContext);

    return mContext->addObserver(std::move(observer));
}

void File::append(const void* buffer, FileAppendCallback callback, std::uint64_t length)
{
    assert(buffer || !length);
    assert(callback);
    assert(mContext);

    return mContext->append(FileAppendRequest{buffer, std::move(callback), length});
}

void File::fetch(FileFetchCallback callback)
{
    assert(callback);
    assert(mContext);

    mContext->fetch(FileFetchRequest{std::move(callback)});
}

void File::fetchBarrier(FileFetchBarrierCallback callback)
{
    assert(callback);
    assert(mContext);

    mContext->fetchBarrier(std::move(callback));
}

void File::flush(FileFlushCallback callback)
{
    assert(callback);
    assert(mContext);

    return mContext->flush(FileFlushRequest{std::move(callback)});
}

FileInfo File::info() const
{
    assert(mContext);

    return mContext->info();
}

void File::purge(FilePurgeCallback callback)
{
    assert(callback);
    assert(mContext);

    return mContext->remove(FileRemoveRequest{std::move(callback), false, true});
}

FileRangeVector File::ranges() const
{
    assert(mContext);

    return mContext->ranges();
}

void File::read(FileReadCallback callback, std::uint64_t offset, std::uint64_t length)
{
    assert(callback);
    assert(mContext);

    read(std::move(callback), FileRange(offset, offset + length));
}

void File::read(FileReadCallback callback, const FileRange& range)
{
    assert(callback);
    assert(mContext);

    mContext->read(FileReadRequest{std::move(callback), range});
}

void File::reclaim(FileReclaimCallback callback)
{
    assert(callback);
    assert(mContext);

    mContext->reclaim(std::move(callback));
}

void File::remove(FileRemoveCallback callback, bool replaced)
{
    assert(callback);
    assert(mContext);

    mContext->remove(FileRemoveRequest{std::move(callback), replaced, false});
}

void File::removeObserver(FileEventObserverID id)
{
    assert(mContext);

    mContext->removeObserver(id);
}

void File::touch(FileTouchCallback callback, std::int64_t modified)
{
    assert(callback);
    assert(mContext);

    mContext->touch(FileTouchRequest{std::move(callback), modified});
}

void File::truncate(FileTruncateCallback callback, std::uint64_t newSize)
{
    assert(callback);
    assert(mContext);

    mContext->truncate(FileTruncateRequest{std::move(callback), newSize});
}

void File::write(const void* buffer,
                 FileWriteCallback callback,
                 std::uint64_t offset,
                 std::uint64_t length)
{
    write(buffer, std::move(callback), FileRange(offset, offset + length));
}

void File::write(const void* buffer, FileWriteCallback callback, const FileRange& range)
{
    assert(buffer || range.mEnd - range.mBegin == 0);
    assert(callback);
    assert(mContext);

    mContext->write(FileWriteRequest{buffer, std::move(callback), range});
}

} // file_service
} // mega
