#pragma once

namespace mega
{
namespace common
{

enum StatusFlag : unsigned int
{
    // The operation can be cancelled.
    SF_CANCELLABLE = 0x1,
    // The operation has been cancelled.
    SF_CANCELLED = 0x2,
    // The operation has completed.
    SF_COMPLETED = 0x4,
    // The operation is in progress.
    SF_IN_PROGRESS = 0x8,
}; // StatusFlag

// A combination of status flags.
using StatusFlags = unsigned int;

} // common
} // mega
