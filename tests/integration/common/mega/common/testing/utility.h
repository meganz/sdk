#pragma once

#include <mega/common/date_time_forward.h>
#include <mega/common/error_or_forward.h>
#include <mega/common/testing/path_forward.h>
#include <mega/common/type_traits.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <stdfs.h>
#include <string>
#include <thread>

namespace mega
{

struct FileFingerprint;

namespace common
{
namespace testing
{

template<typename Container, typename Predicate>
bool allOf(Container&& container, Predicate predicate)
{
    return std::all_of(std::begin(container), std::end(container), std::move(predicate));
}

template<typename Container, typename Predicate>
bool anyOf(Container&& container, Predicate predicate)
{
    return std::any_of(std::begin(container), std::end(container), std::move(predicate));
}

ErrorOr<FileFingerprint> fingerprint(const std::string& content,
                                     std::chrono::system_clock::time_point modified);

ErrorOr<FileFingerprint> fingerprint(const Path& path);

template<typename Container, typename Function>
void forEach(Container&& container, Function function)
{
    std::for_each(std::begin(container), std::end(container), std::move(function));
}

DateTime lastWriteTime(const Path& path, std::error_code& result);
DateTime lastWriteTime(const Path& path);

void lastWriteTime(const Path path, const DateTime& modified, std::error_code& result);

void lastWriteTime(const Path& path, const DateTime& modified);

std::string randomBytes(std::size_t length);

std::string randomName();

template<typename Predicate, typename Result = decltype(std::declval<Predicate>()())>
auto waitUntil(Predicate&& predicate,
               std::chrono::steady_clock::time_point when,
               Result defaultValue = Result()) -> decltype(predicate())
{
    // How long should we wait between tests?
    constexpr auto step = std::chrono::milliseconds(256);

    // Wait until when for predicate to be satisifed.
    while (true)
    {
        // Convenience.
        auto now = std::chrono::steady_clock::now();

        // Predicate is satisfied.
        if (auto result = predicate())
            return result;

        // Predicate's taken too long to be satisfied.
        if (now >= when)
            return defaultValue;

        // When should we test the predicate again?
        auto next = std::min(now + step, when);

        // Sleep until then.
        std::this_thread::sleep_until(next);
    }
}

template<typename Predicate,
         typename Period,
         typename Rep,
         typename Result = decltype(std::declval<Predicate>()())>
auto waitFor(Predicate&& predicate,
             std::chrono::duration<Rep, Period> timeout,
             Result defaultValue = Result()) -> decltype(predicate())
{
    return waitUntil(std::move(predicate),
                     std::chrono::steady_clock::now() + timeout,
                     defaultValue);
}

} // testing
} // fuse
} // mega
