#include <mega/file_service/file_event.h>
#include <mega/file_service/file_event_emitter.h>

#include <atomic>

namespace mega
{
namespace file_service
{

FileEventObserverID FileEventEmitter::addObserver(FileEventObserver observer)
{
    // So each observer has a unique process-wide identifier.
    std::atomic<FileEventObserverID> next{0u};

    // Make sure no one else is modifying our map of observers.
    std::lock_guard guard(mObserversLock);

    // Add the observer to our map.
    auto [iterator, _] = mObservers.emplace(next.fetch_add(1), std::move(observer));

    // Return the observer's ID to our caller.
    return iterator->first;
}

void FileEventEmitter::notify(const FileEvent& event)
{
    // Make sure no threads are modifying our map of observers.
    std::lock_guard guard(mObserversLock);

    // Transmit event to each observer.
    for (auto i = mObservers.begin(); i != mObservers.end();)
    {
        // Just in case the observer removes itself.
        auto j = i++;

        // Transmit event to our observer.
        j->second(event);
    }
}

void FileEventEmitter::removeObserver(FileEventObserverID id)
{
    // Make sure no one else is modifying our map of observer.
    std::lock_guard guard(mObserversLock);

    // Remove the the observer from the map.
    mObservers.erase(id);
}

} // file_service
} // mega
