#pragma once

#include <mega/common/node_info_forward.h>
#include <mega/common/type_traits.h>
#include <mega/fuse/common/inode_info_forward.h>
#include <mega/fuse/common/testing/path_forward.h>
#include <mega/fuse/common/testing/utility.h>
#include <mega/fuse/platform/handle_forward.h>
#include <mega/fuse/platform/security_descriptor_forward.h>
#include <mega/fuse/platform/utility.h>
#include <mega/fuse/platform/windows.h>

template<typename T>
using IsNativeInfoLike =
  mega::common::IsOneOf<T,
                        BY_HANDLE_FILE_INFORMATION,
                        WIN32_FILE_ATTRIBUTE_DATA>;

template<typename T>
using IsAnyInfoLike =
  std::integral_constant<bool,
                         mega::fuse::testing::IsInfoLike<T>::value
                         || IsNativeInfoLike<T>::value>;

template<typename T, typename... Ts>
using AreAnyInfoLike =
  mega::common::AllOf<IsAnyInfoLike, T, Ts...>;

template<typename T, typename... Ts>
using AreNativeInfoLike =
  mega::common::AllOf<IsNativeInfoLike, T, Ts...>;

bool operator==(const BY_HANDLE_FILE_INFORMATION& lhs,
                const BY_HANDLE_FILE_INFORMATION& rhs);

bool operator==(const BY_HANDLE_FILE_INFORMATION& lhs,
                const WIN32_FILE_ATTRIBUTE_DATA& rhs);

bool operator==(const WIN32_FILE_ATTRIBUTE_DATA& lhs,
                const BY_HANDLE_FILE_INFORMATION& rhs);

template<typename T, typename U>
auto operator==(const T& lhs, const U& rhs)
  -> typename std::enable_if<IsNativeInfoLike<T>::value
                             && mega::fuse::testing::IsInfoLike<U>::value,
                             bool>::type
{
    return rhs == lhs;
}

template<typename T, typename U>
auto operator!=(const T& lhs, const U& rhs)
  -> typename std::enable_if<AreAnyInfoLike<T, U>::value, bool>::type
{
    return !(lhs == rhs);
}

namespace mega
{
namespace common
{

template<typename T>
auto operator==(const NodeInfo& lhs, const T& rhs)
  -> std::enable_if_t<IsNativeInfoLike<T>::value, bool>;

} // common

namespace fuse
{

template<typename T>
auto operator==(const T& lhs, const BY_HANDLE_FILE_INFORMATION& rhs)
  -> typename std::enable_if<testing::IsInfoLike<T>::value, bool>::type;

template<typename T>
auto operator==(const T& lhs, const WIN32_FILE_ATTRIBUTE_DATA& rhs)
  -> typename std::enable_if<testing::IsInfoLike<T>::value, bool>::type;

namespace testing
{

struct FileTimes
{
    FILETIME mAccessed;
    FILETIME mCreated;
    FILETIME mWritten;
}; // FileTimes

struct FindHandleDeleter
{
    void operator()(HANDLE handle)
    {
        FindClose(handle);
    }
}; // FindHandleDeleter

using FindHandle = platform::Handle<FindHandleDeleter>;

bool operator==(const FileTimes& lhs, const FileTimes& rhs);

bool operator!=(const FileTimes& lhs, const FileTimes& rhs);

BOOL CreateDirectoryP(const Path& path,
                      LPSECURITY_ATTRIBUTES securityAttributes);

platform::Handle<> CreateFileP(const Path& path,
                               DWORD desiredAccess,
                               DWORD shareMode,
                               LPSECURITY_ATTRIBUTES securityAttributes,
                               DWORD creationDisposition,
                               DWORD flagsAndAttributes,
                               const platform::Handle<>& templateFile);

BOOL DeleteFileP(const Path& path);

FindHandle FindFirstFileP(const Path& path, LPWIN32_FIND_DATAW info);

DWORD GetFileAttributesP(const Path& path);

BOOL GetFileAttributesExP(const Path& path,
                          GET_FILEEX_INFO_LEVELS level,
                          LPVOID info);

BOOL GetFileInformationByPath(const Path& path,
                              BY_HANDLE_FILE_INFORMATION& info);

platform::SecurityDescriptor GetFileSecurityP(const Path& path);

long GetLastError();

BOOL MoveFileExP(const Path& source, const Path& target, DWORD flags);

BOOL RemoveDirectoryP(const Path& path);

BOOL SetFileAttributesP(const Path& path, DWORD attributes);

BOOL SetFileSecurityP(const Path& path,
                      const platform::SecurityDescriptor& descriptor);

} // testing
} // fuse
} // mega

