#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <string>
#include <thread>

#include <mega/common/error_or_forward.h>
#include <mega/common/node_info_forward.h>
#include <mega/common/type_traits.h>
#include <mega/fuse/common/date_time_forward.h>
#include <mega/fuse/common/inode_id_forward.h>
#include <mega/fuse/common/inode_info_forward.h>
#include <mega/fuse/common/testing/client_forward.h>
#include <mega/fuse/common/testing/path_forward.h>

#include <mega/types.h>

#include <tests/stdfs.h>

namespace mega
{
namespace fuse
{
namespace testing
{

// Convenience.
template<typename T>
using IsInfoLike =
  common::IsOneOf<T, InodeInfo, common::NodeInfo>;

template<typename I, typename T>
using EnableIfInfoLike =
  std::enable_if<IsInfoLike<I>::value, T>;

template<typename Container, typename Predicate>
bool allOf(Container&& container, Predicate predicate)
{
    return std::all_of(std::begin(container),
                       std::end(container),
                       std::move(predicate));
}

template<typename Container, typename Predicate>
bool anyOf(Container&& container, Predicate predicate)
{
    return std::any_of(std::begin(container),
                       std::end(container),
                       std::move(predicate));
}

Error befriend(Client& client0, Client& client1);

common::ErrorOr<FileFingerprint> fingerprint(const std::string& content,
                                             std::chrono::system_clock::time_point modified);

common::ErrorOr<FileFingerprint> fingerprint(const Path& path);

template<typename Container, typename Function>
void forEach(Container&& container, Function function)
{
    std::for_each(std::begin(container),
                  std::end(container),
                  std::move(function));
}

NodeHandle id(const common::NodeInfo& info);
InodeID id(const InodeInfo& info);

DateTime lastWriteTime(const Path& path, std::error_code& result);
DateTime lastWriteTime(const Path& path);

void lastWriteTime(const Path path,
                   const DateTime& modified,
                   std::error_code& result);

void lastWriteTime(const Path& path, const DateTime& modified);

NodeHandle parentID(const common::NodeInfo& info);
InodeID parentID(const InodeInfo& info);

std::string randomBytes(std::size_t length);

std::string randomName();

std::string toString(NodeHandle handle);

std::uint64_t toUint64(InodeID id);
std::uint64_t toUint64(NodeHandle handle);

template<typename Predicate,
         typename Result = decltype(std::declval<Predicate>()())>
auto waitUntil(Predicate&& predicate,
               std::chrono::steady_clock::time_point when,
               Result defaultValue = Result())
  -> decltype(predicate())
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
             Result defaultValue = Result())
  -> decltype(predicate())
{
    return waitUntil(std::move(predicate),
                     std::chrono::steady_clock::now() + timeout,
                     defaultValue);
}

} // testing
} // fuse
} // mega

