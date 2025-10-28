#pragma once

#include <mega/common/expected_forward.h>
#include <mega/common/testing/utility.h>
#include <mega/file_service/file_event_observer.h>
#include <mega/file_service/file_event_observer_id.h>
#include <mega/file_service/file_event_observer_result.h>
#include <mega/file_service/file_event_vector.h>
#include <mega/file_service/type_traits.h>

#include <type_traits>

namespace mega
{
namespace file_service
{
namespace testing
{
namespace detail
{

// Convenience.
using common::Expected;

template<typename T>
struct IsFileEventObserverID: std::false_type
{}; // IsFileEventObserverID<T>

template<>
struct IsFileEventObserverID<FileEventObserverID>: std::true_type
{}; // IsFileEventObserverID<FileEventObserverID>

template<typename E>
struct IsFileEventObserverID<Expected<E, FileEventObserverID>>: std::true_type
{}; // IsFileEventObserverID<Expected<E, FileEventObserverID>>

template<typename T>
constexpr auto IsFileEventObserverIDV = IsFileEventObserverID<T>::value;

template<typename T>
using DetectAddObserver =
    decltype(std::declval<T>().addObserver(std::declval<FileEventObserver>()));

template<typename T>
using DetectRemoveObserver =
    decltype(std::declval<T>().removeObserver(std::declval<FileEventObserverID>()));

template<typename T>
using HasAddObserver = IsFileEventObserverID<DetectedT<DetectAddObserver, T>>;

template<typename T>
using HasRemoveObserver = IsNotNoneSuch<DetectedT<DetectRemoveObserver, T>>;

template<typename T>
using IsFileEventSource = std::conjunction<HasAddObserver<T>, HasRemoveObserver<T>>;

template<typename T>
constexpr auto IsFileEventSourceV = IsFileEventSource<T>::value;

} // detail

// Convenience.
using detail::IsFileEventSource;
using detail::IsFileEventSourceV;

template<typename Source>
class ScopedFileEventObserver
{
    template<typename E>
    auto extract(common::Expected<E, FileEventObserverID> id)
    {
        return id.value();
    }

    auto extract(FileEventObserverID id)
    {
        return id;
    }

    template<typename S>
    friend auto observe(S& source)
        -> std::enable_if_t<IsFileEventSourceV<S>, ScopedFileEventObserver<S>>;

    ScopedFileEventObserver(Source& source):
        mID(),
        mSource(&source)
    {
        mID = extract(source.addObserver(
            [this](auto& event)
            {
                std::lock_guard guard(mEventsLock);

                mEvents.emplace_back(event);

                return FILE_EVENT_OBSERVER_KEEP;
            }));
    }

    // What events has this observer received?
    FileEventVector mEvents;

    // Serializes access to mEvents.
    mutable std::mutex mEventsLock;

    // The ID of our event observer.
    FileEventObserverID mID;

    // The event source that our observer is observing.
    Source* mSource;

public:
    ScopedFileEventObserver(ScopedFileEventObserver&& other) = delete;

    ~ScopedFileEventObserver()
    {
        if (mSource)
            mSource->removeObserver(mID);
    }

    ScopedFileEventObserver& operator=(ScopedFileEventObserver&& rhs) = delete;

    FileEventVector events() const
    {
        std::lock_guard guard(mEventsLock);

        return mEvents;
    }

    template<typename Rep, typename Period>
    bool match(const FileEventVector& expected, std::chrono::duration<Rep, Period> period) const
    {
        return waitFor(
            [&]()
            {
                std::lock_guard guard(mEventsLock);
                return expected == mEvents;
            },
            period);
    }
}; // ScopedFileEventObserver

template<typename Source>
auto observe(Source& source)
    -> std::enable_if_t<IsFileEventSourceV<Source>, ScopedFileEventObserver<Source>>
{
    return ScopedFileEventObserver(source);
}

} // testing
} // file_service
} // mega
