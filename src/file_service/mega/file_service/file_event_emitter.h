#pragma once

#include <mega/common/instance_logger.h>
#include <mega/file_service/file_event_forward.h>
#include <mega/file_service/file_event_observer.h>
#include <mega/file_service/file_event_observer_id.h>
#include <mega/file_service/file_id_forward.h>

#include <atomic>
#include <condition_variable>
#include <list>
#include <map>
#include <mutex>
#include <thread>
#include <type_traits>

namespace mega
{
namespace file_service
{

class FileEventEmitter
{
    // Convenience.
    using FileEventObserverMap = std::map<FileEventObserverID, FileEventObserver>;

    struct TransmitContext;

    // Adds an observer to an observer map.
    FileEventObserverID addObserver(FileEventObserver observer, FileEventObserverMap& observers);

    // Notifies pending events.
    void loop();

    // True if we should defer the removal of this observer.
    bool shouldDeferRemove(FileEventObserverID id);

    // True if we should defer the removal of this observer map.
    bool shouldDeferRemoveAll(FileEventObserverMap& map);

    // Transmit an event to each observer in the specified map.
    void transmit(const FileEvent& event, FileEventObserverMap& map);

    // Transmit an event to our observers.
    void transmit(const FileEvent& event);

    // Logs instance lifetime.
    common::InstanceLogger<FileEventEmitter> mInstanceLogger;

    // Signalled when we want to wake up our worker.
    std::condition_variable mCV;

    // Serializes access to mFileObservers and mServiceObservers.
    std::recursive_mutex mObserversLock;

    // Tracks events waiting to be notified.
    std::list<FileEvent> mPendingEvents;

    // Serializes access to mPendingEvents.
    std::mutex mPendingEventsLock;

    // Tracks service-wide observers.
    FileEventObserverMap mServiceObservers;

    // Tells mWorker when it should terminate.
    bool mTerminate;

    // Tracks state while an event is being transmitted.
    static thread_local TransmitContext* mTransmitContext;

    // The thread responsible for notifying our observers.
    std::thread mWorker;

public:
    FileEventEmitter();

    FileEventEmitter(const FileEventEmitter& other) = delete;

    ~FileEventEmitter();

    FileEventEmitter& operator=(const FileEventEmitter& rhs) = delete;

    // Add a service-wide observer.
    FileEventObserverID addObserver(FileEventObserver observer);

    // Transmit event to our observers.
    void notify(FileEvent event);

    // Remove a service-wide observer.
    void removeObserver(FileEventObserverID observerID);

    // Remove all service-wide observers.
    void removeObservers();
}; // FileEventEmitter

} // file_service
} // mega
