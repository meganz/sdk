#pragma once

#include <mega/file_service/file_event_observer.h>
#include <mega/file_service/file_event_observer_id.h>
#include <mega/file_service/type_traits.h>

#include <type_traits>

namespace mega
{
namespace file_service
{
namespace detail
{

template<typename T>
using DetectAddObserver =
    decltype(std::declval<T>().addObserver(std::declval<FileEventObserver>()));

template<typename T>
using DetectRemoveObserver =
    decltype(std::declval<T>().removeObserver(std::declval<FileEventObserverID>()));

template<typename T>
using HasAddObserver = std::is_same<DetectedT<DetectAddObserver, T>, FileEventObserverID>;

template<typename T>
using HasRemoveObserver = std::is_same<DetectedT<DetectRemoveObserver, T>, void>;

template<typename T>
using IsFileEventObserver = std::is_invocable_r<void, T, const FileEvent&>;

template<typename T>
using IsFileEventSource = std::conjunction<HasAddObserver<T>, HasRemoveObserver<T>>;

} // detail

// Convenience.
using detail::IsFileEventObserver;
using detail::IsFileEventSource;

template<typename Source>
class ScopedFileEventObserver
{
    template<typename O, typename S>
    friend auto observe(O&& observer, S& source)
        -> std::enable_if_t<std::conjunction_v<IsFileEventObserver<O>, IsFileEventSource<S>>,
                            ScopedFileEventObserver<S>>;

    ScopedFileEventObserver(FileEventObserverID id, Source& source):
        mID(id),
        mSource(source)
    {}

    // The ID of our event observer.
    FileEventObserverID mID;

    // The event source that our observer is observing.
    Source& mSource;

public:
    ScopedFileEventObserver(ScopedFileEventObserver& other) = delete;

    ~ScopedFileEventObserver()
    {
        mSource.removeObserver(mID);
    }
}; // ScopedFileEventObserver

template<typename Observer, typename Source>
auto observe(Observer&& observer, Source& source) -> std::enable_if_t<
    std::conjunction_v<IsFileEventObserver<Observer>, IsFileEventSource<Source>>,
    ScopedFileEventObserver<Source>>
{
    auto id = source.addObserver(std::forward<Observer>(observer));

    return ScopedFileEventObserver(id, source);
}

} // file_service
} // mega
