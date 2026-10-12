#pragma once

#include <mega/scoped_helpers.h>
#include <mega/types.h>

#include <iosfwd>
#include <mutex>
#include <vector>

namespace mega
{
namespace file_service
{
namespace testing
{

// Records partial download calls for testing purposes.
class PartialDownloadRecorder
{
public:
    struct Call
    {
        NodeHandle handle;
        std::uint64_t length;
        std::uint64_t offset;

        bool operator==(const Call& rhs) const
        {
            return handle == rhs.handle && length == rhs.length && offset == rhs.offset;
        }
    };

    // Get the singleton instance of the recorder.
    static PartialDownloadRecorder& instance();

    // Get a snapshot of the calls that have been recorded since a recording began.
    std::vector<Call> snapshot() const;

private:
    friend ScopedDestructor scopedPartialDownloadRecord();

    void clear();

    void record(NodeHandle handle, std::uint64_t length, std::uint64_t offset);

    PartialDownloadRecorder() = default;

    PartialDownloadRecorder(const PartialDownloadRecorder&) = delete;
    PartialDownloadRecorder& operator=(const PartialDownloadRecorder&) = delete;

    PartialDownloadRecorder(PartialDownloadRecorder&&) = delete;
    PartialDownloadRecorder& operator=(PartialDownloadRecorder&&) = delete;

    mutable std::mutex mMutex;

    std::vector<Call> mCalls;
};

void PrintTo(const PartialDownloadRecorder::Call& call, std::ostream* ostream);

// Start recording partial download calls for testing purposes.
// Returns a ScopedDestructor that will stop recording when it goes out of scope.
// It is process wide, so only one ScopedDestructor should be active at a time.
ScopedDestructor scopedPartialDownloadRecord();

} // testing
} // file_service
} // mega
