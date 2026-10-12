#pragma once

#include <mega/common/deciseconds.h>
#include <mega/file_service/file_service_options_forward.h>

#include <chrono>
#include <cstdint>
#include <optional>

namespace mega
{
namespace file_service
{

struct ReclaimOptions
{
    // How long shouldn't we access a file before we can reclaim it?
    std::chrono::minutes mAgeThreshold = std::chrono::hours(3 * 24);

    // How many files should we reclaim at a time?
    std::size_t mBatchSize = 4u;

    // How long should we wait before reclaiming space after login
    // or after the reclaim options are configured?
    std::chrono::seconds mDelay = std::chrono::minutes(30);

    // How often should we try to reclaim space?
    std::chrono::seconds mPeriod = std::chrono::hours(2);

    // How many bytes can the service store before it needs to reclaim space?
    std::optional<std::uint64_t> mReclaimThreshold{};

    // How many bytes should the service target when reclaiming space starts?
    std::uint64_t mReclaimTarget = 0u;
};

struct ServiceOptions
{
    // How fast do we think the user's download speed is for streaming?
    std::uint64_t mEstimatedDownloadBitrate = 50 * 1000 * 8;

    // How fast do we think the user's download speed is for normal read?
    std::uint64_t mEstimatedNormalDownloadBitrate = 5 * 1024 * 1024 * 8;

    // How long do we think it'll take before a download yields its first byte of data?
    std::chrono::milliseconds mEstimatedTimeToFirstByte = std::chrono::seconds(1);

    // How long should we wait before we remove a file context from memory?
    std::chrono::seconds mFileContextReleaseDelay{3};

    // Flush delay is computed as follows:
    //   FlushDelay = FileSize / FileFlushDivisor
    //
    // FlushDelay is then clamped so
    //   MinimumFlushDelay <= FlushDelay
    //   MaximumFlushDelay >= FlushDelay
    std::uint64_t mFileFlushDivisor = UINT64_C(1) << 20;

    // How long should we wait before retrying a flush?
    common::deciseconds mFileFlushRetryBackoff{20};

    // Gaps less distant than this value can be filled by a single download.
    std::uint64_t mMaximumGapDistance = UINT64_C(1) << 20;

    // The maximum amount of time we'll wait before flushing modifications.
    std::chrono::seconds mMaximumFileFlushDelay = std::chrono::minutes(5);

    // How many times should we try and flush a file before we give up?
    std::size_t mMaximumFileFlushRetries = 5u;

    // The minimum amount of time we'll wait before flushing modifications.
    std::chrono::seconds mMinimumFileFlushDelay = std::chrono::seconds(30);

    // How long should we wait before considering normal reads are inactive?
    std::chrono::seconds mNormalReadInactivityDelay = std::chrono::seconds(10);

    // Reads less than or equal to this value will be downloaded immediately.
    std::uint64_t mImmediateDownloadThreshold = 1ul << 20;

    // What boundary should new ranges be aligned on?
    //
    // Note that this value is expressed as a power of two.
    //
    // I.e. An value of 10 here means align at a 1K boundary.
    std::uint64_t mJumpBackwardAlignment = 20;

    // How far should we extend the beginning of a new range?
    std::chrono::milliseconds mJumpBackwardDistance{2000};

    // How far forward in milliseconds does a read have to be for us to consider it a jump?
    std::chrono::milliseconds mJumpForwardDistance{3000};

    // How many times will we try to download a range before we give up.
    std::uint64_t mMaximumRangeRetries = 5u;

    // What is the minimum amount of data we should request from the cloud?
    std::uint64_t mMinimumRangeSize = UINT64_C(1) << 16;

    // What is the minimum length of the range to update the speed statistics, 5MB
    std::uint64_t mSpeedStatThreshold = UINT64_C(5) << 20;

    // How long should we wait between retries?
    common::deciseconds mRangeRetryBackoff{20};

    // How long can we wait on an existing download before we split its range?
    std::chrono::seconds mRangeSplitThreshold{2};

    // How long should we wait before persisting a file's access time again on the read path?
    std::chrono::seconds mAccessTimeUpdateInterval = std::chrono::minutes(1);
}; // ServiceOptions

} // file_service
} // mega
