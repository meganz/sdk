#include <mega/file_service/file_service_options.h>
#include <mega/file_service/flush_utilities.h>

#include <algorithm>
#include <cstdint>

namespace mega
{
namespace file_service
{

using namespace std::chrono;

seconds computeFlushDelay(const ServiceOptions& options, std::uint64_t size)
{
    // Convenience.
    const auto divisor = options.mFileFlushDivisor;
    const auto maxDelay = options.mMaximumFileFlushDelay;
    const auto minDelay = options.mMinimumFileFlushDelay;

    // Compute flush delay.
    auto delay = size / std::max(divisor, UINT64_C(1));

    // Clamp delay to ensure it is representable by seconds.
    delay = std::min(delay, static_cast<std::uint64_t>(maxDelay.count()));

    // Return clamped delay to our caller.
    return std::max(minDelay, seconds(delay));
}

} // file_service
} // mega
