#include <mega/file_service/file_read_write_state.h>

#include <cassert>
#include <limits>

namespace mega
{
namespace file_service
{

bool FileReadWriteState::read()
{
    std::lock_guard guard(mLock);

    // Convenience.
    auto min = std::numeric_limits<long>::min();

    // Check if we can perform the read.
    auto adjustment = static_cast<long>(mState <= 0 && mState > min);

    // Adjust state.
    mState -= adjustment;

    // Let the caller know if the read can be performed.
    return adjustment;
}

void FileReadWriteState::readCompleted()
{
    std::lock_guard guard(mLock);

    // Sanity.
    assert(mState < 0);

    // Adjust state.
    ++mState;
}

bool FileReadWriteState::write()
{
    std::lock_guard guard(mLock);

    // Check if we can perform the write.
    auto adjustment = static_cast<long>(mState == 0);

    // Adjust state.
    mState += adjustment;

    // Let the caller know if the write can be performed.
    return adjustment;
}

void FileReadWriteState::writeCompleted()
{
    std::lock_guard guard(mLock);

    // Sanity.
    assert(mState == 1);

    // Adjust state.
    --mState;
}

} // file_service
} // mega
