#include <mega/file_service/testing/integration/partial_download_recorder.h>
#include <mega/testhooks.h>
#include <mega/utils.h>

#include <cassert>
#include <ostream>

namespace mega
{
namespace file_service
{
namespace testing
{

PartialDownloadRecorder& PartialDownloadRecorder::instance()
{
    static PartialDownloadRecorder recorder;
    return recorder;
}

void PartialDownloadRecorder::clear()
{
    std::lock_guard<std::mutex> lock(mMutex);
    mCalls.clear();
}

void PartialDownloadRecorder::record(NodeHandle handle, std::uint64_t length, std::uint64_t offset)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mCalls.push_back({handle, length, offset});
}

std::vector<PartialDownloadRecorder::Call> PartialDownloadRecorder::snapshot() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mCalls;
}

void PrintTo(const PartialDownloadRecorder::Call& call, std::ostream* ostream)
{
    // Sanity.
    assert(ostream);

    *ostream << "{handle: " << toNodeHandle(call.handle) << ", length: " << call.length
             << ", offset: " << call.offset << "}";
}

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED

ScopedDestructor scopedPartialDownloadRecord()
{
    PartialDownloadRecorder::instance().clear();

    globalMegaTestHooks.onPartialDownload =
        [](NodeHandle handle, std::uint64_t length, std::uint64_t offset)
    {
        PartialDownloadRecorder::instance().record(handle, length, offset);
    };

    return ScopedDestructor{[]()
                            {
                                globalMegaTestHooks.onPartialDownload = nullptr;
                            }};
}

#endif // MEGASDK_DEBUG_TEST_HOOKS_ENABLED

} // testing
} // common
} // mega
