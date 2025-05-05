#pragma once

namespace mega
{
namespace common
{

enum StatusFlag : unsigned int
{
    // The upload can be cancelled.
    SF_CANCELLABLE = 0x1,
    // The upload has been cancelled.
    SF_CANCELLED = 0x2,
    // The upload hsa completed.
    SF_COMPLETED = 0x4
}; // StatusFlag

// A combination of status flags.
using StatusFlags = unsigned int;

} // common
} // mega
