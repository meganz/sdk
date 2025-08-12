#pragma once

#include <mega/file_service/file_event_forward.h>
#include <mega/file_service/file_event_observer.h>
#include <mega/file_service/file_event_observer_id.h>

#include <map>
#include <mutex>

namespace mega
{
namespace file_service
{

class FileEventEmitter
{
    // Convenience.
    using FileEventObserverMap = std::map<FileEventObserverID, FileEventObserver>;

    // Who should we notify when an event is emitted?
    FileEventObserverMap mObservers;

    // Serializes access to mObservers.
    std::recursive_mutex mObserversLock;

protected:
    FileEventEmitter() = default;

    ~FileEventEmitter() = default;

public:
    // Notify observer when a file changes.
    FileEventObserverID addObserver(FileEventObserver observer);

    // Transmit event to all registered observers.
    void notify(const FileEvent& event);

    // Remove a previously added observer.
    void removeObserver(FileEventObserverID id);
}; // FileEventEmitter

} // file_service
} // mega
