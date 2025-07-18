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

struct FileServiceOptions
{
    // How many times will we try to download a range before we give up.
    std::uint64_t mMaximumRangeRetries = 5u;

    // Specifies the minimum distance between ranges before they are merged.
    std::uint64_t mMinimumRangeDistance = 1u << 17;

    // Specifies the unit of transfer from the cloud.
    std::uint64_t mMinimumRangeSize = 1u << 21;

    // How long should we wait between retries?
    common::deciseconds mRangeRetryBackoff{20};

    // How long shouldn't we access a file before we can reclaim it?
    std::chrono::hours mReclaimAgeThreshold{72};

    // How often should we try to reclaim space?
    std::chrono::seconds mReclaimPeriod{7200};

    // How many bytes can the service store before it needs to reclaim space?
    std::optional<std::uint64_t> mReclaimSizeThreshold{};
}; // FileServiceOptions

} // file_service
} // mega
