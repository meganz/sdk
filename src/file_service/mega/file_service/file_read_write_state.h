#pragma once

#include <mutex>

namespace mega
{
namespace file_service
{

class FileReadWriteState
{
    // Serializes access to mState.
    std::mutex mLock;

    // Tracks whether we have any active read or write operations.
    //
    // Value is interpreted as follows:
    // <0 - One or more read operations are in progress.
    //  0 - No operations are in progress.
    // =1 - A write operation is in progress.
    // >1 - Invalid state.
    long mState{0};

public:
    // Try and begin a read operation.
    //
    // Returns false if the read must be queued.
    bool read();

    // Signal that a read operation has completed.
    void readCompleted();

    // Try and begin a write operation.
    //
    // Returns false if the write must be queued.
    bool write();

    // Signal that a write operation has completed.
    void writeCompleted();
}; // FileReadWriteState

} // file_service
} // mega
