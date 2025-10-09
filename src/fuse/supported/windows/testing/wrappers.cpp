#include <mega/common/node_info.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/testing/path.h>
#include <mega/fuse/platform/date_time.h>
#include <mega/fuse/platform/handle.h>
#include <mega/fuse/platform/local_pointer.h>
#include <mega/fuse/platform/security_descriptor.h>
#include <mega/fuse/platform/testing/wrappers.h>

bool operator==(const BY_HANDLE_FILE_INFORMATION& lhs,
                const BY_HANDLE_FILE_INFORMATION& rhs)
{
    return !std::memcmp(&lhs, &rhs, sizeof(lhs));
}

bool operator==(const BY_HANDLE_FILE_INFORMATION& lhs,
                const WIN32_FILE_ATTRIBUTE_DATA& rhs)
{
    using mega::fuse::DateTime;

    return lhs.dwFileAttributes == rhs.dwFileAttributes
           && DateTime(lhs.ftCreationTime) == rhs.ftCreationTime
           && DateTime(lhs.ftLastAccessTime) == rhs.ftLastAccessTime
           && DateTime(lhs.ftLastWriteTime) == rhs.ftLastWriteTime
           && lhs.nFileSizeLow == rhs.nFileSizeLow
           && lhs.nFileSizeHigh == rhs.nFileSizeHigh;
}

bool operator==(const WIN32_FILE_ATTRIBUTE_DATA& lhs,
                const BY_HANDLE_FILE_INFORMATION& rhs)
{
    return rhs == lhs;
}

namespace mega
{
namespace common
{

template<typename T>
auto operator==(const NodeInfo& lhs, const T& rhs)
  -> std::enable_if_t<IsNativeInfoLike<T>::value, bool>
{
    return fuse::operator==(lhs, rhs);
}

template bool operator==(const NodeInfo&,  const BY_HANDLE_FILE_INFORMATION&);
template bool operator==(const NodeInfo&,  const WIN32_FILE_ATTRIBUTE_DATA&);

} // common

namespace fuse
{

using namespace common;
using namespace platform;

template<typename T>
auto operator==(const T& lhs, const BY_HANDLE_FILE_INFORMATION& rhs)
  -> typename std::enable_if<testing::IsInfoLike<T>::value, bool>::type
{
    using testing::id;
    using testing::toUint64;

    DWORD attributes = FILE_ATTRIBUTE_NORMAL;
    auto  handle     = toUint64(id(lhs));
    DWORD handleLo   = static_cast<DWORD>(handle);
    DWORD handleHi   = static_cast<DWORD>(handle >> 32);
    DWORD sizeLo     = static_cast<DWORD>(lhs.mSize);
    DWORD sizeHi     = static_cast<DWORD>(lhs.mSize >> 32);

    if (lhs.mPermissions != FULL)
        attributes = FILE_ATTRIBUTE_READONLY;

    if (lhs.mIsDirectory)
    {
        attributes = FILE_ATTRIBUTE_DIRECTORY;
        sizeLo = 0;
        sizeHi = 0;
    }

    return attributes == rhs.dwFileAttributes
           && handleLo == rhs.nFileIndexLow
           && handleHi == rhs.nFileIndexHigh
           && DateTime(lhs.mModified) == rhs.ftLastWriteTime
           && sizeLo == rhs.nFileSizeLow
           && sizeHi == rhs.nFileSizeHigh;
}

template bool operator==(const InodeInfo&, const BY_HANDLE_FILE_INFORMATION&);
template bool operator==(const NodeInfo&,  const BY_HANDLE_FILE_INFORMATION&);

template<typename T>
auto operator==(const T& lhs, const WIN32_FILE_ATTRIBUTE_DATA& rhs)
  -> typename std::enable_if<testing::IsInfoLike<T>::value, bool>::type
{
    DWORD attributes = FILE_ATTRIBUTE_NORMAL;
    DWORD sizeLo  = static_cast<DWORD>(lhs.mSize);
    DWORD sizeHi  = static_cast<DWORD>(lhs.mSize >> 32);
    auto  written = DateTime(lhs.mModified);

    if (lhs.mPermissions != FULL)
        attributes = FILE_ATTRIBUTE_READONLY;

    if (lhs.mIsDirectory)
    {
        attributes = FILE_ATTRIBUTE_DIRECTORY;
        sizeLo = 0;
        sizeHi = 0;
    }

    return attributes == rhs.dwFileAttributes
           && sizeLo  == rhs.nFileSizeLow
           && sizeHi  == rhs.nFileSizeHigh
           && written == rhs.ftLastWriteTime;
}

template bool operator==(const InodeInfo&, const WIN32_FILE_ATTRIBUTE_DATA&);
template bool operator==(const NodeInfo&,  const WIN32_FILE_ATTRIBUTE_DATA&);

namespace testing
{

bool operator==(const FileTimes& lhs, const FileTimes& rhs)
{
    return !std::memcmp(&lhs, &rhs, sizeof(lhs));
}

bool operator!=(const FileTimes& lhs, const FileTimes& rhs)
{
    return !(lhs == rhs);
}

BOOL CreateDirectoryP(const Path& path,
                      LPSECURITY_ATTRIBUTES securityAttributes)
{
    return CreateDirectoryW(path.path().c_str(), securityAttributes);
}

Handle<> CreateFileP(const Path& path,
                     DWORD desiredAccess,
                     DWORD shareMode,
                     LPSECURITY_ATTRIBUTES securityAttributes,
                     DWORD creationDisposition,
                     DWORD flagsAndAttributes,
                     const Handle<>& templateFile)
{
    return Handle<>(CreateFileW(path.path().c_str(),
                                desiredAccess,
                                shareMode,
                                securityAttributes,
                                creationDisposition,
                                flagsAndAttributes,
                                templateFile.get()));
}

BOOL DeleteFileP(const Path& path)
{
    return DeleteFileW(path.path().c_str());
}

FindHandle FindFirstFileP(const Path& path, LPWIN32_FIND_DATAW info)
{
    return FindHandle(FindFirstFileW(path.path().c_str(), info));
}

bool flushFile(const Path& path)
{
    auto handle = CreateFileP(path,
                              GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr,
                              OPEN_EXISTING,
                              0,
                              Handle<>());

    if (!handle)
        return false;

    return FlushFileBuffers(handle.get());
}

DWORD GetFileAttributesP(const Path& path)
{
    return GetFileAttributesW(path.path().c_str());
}

BOOL GetFileAttributesExP(const Path& path,
                          GET_FILEEX_INFO_LEVELS level,
                          LPVOID info)
{
    return GetFileAttributesExW(path.path().c_str(),
                                level,
                                info);
}

BOOL GetFileInformationByPath(const Path& path,
                              BY_HANDLE_FILE_INFORMATION& info)
{
    auto handle = CreateFileP(path,
                              GENERIC_READ,
                              0,
                              nullptr,
                              OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS,
                              Handle<>());

    if (!handle)
        return false;

    if (!GetFileInformationByHandle(handle.get(), &info))
        return false;

    SetLastError(ERROR_SUCCESS);

    return true;
}

SecurityDescriptor GetFileSecurityP(const Path& path)
{
    constexpr auto requested = DACL_SECURITY_INFORMATION
                               | GROUP_SECURITY_INFORMATION
                               | OWNER_SECURITY_INFORMATION;

    auto required = 0ul;

    GetFileSecurityW(path.path().c_str(),
                     requested,
                     nullptr,
                     required,
                     &required);

    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return SecurityDescriptor();

    LocalPtr<void> descriptor(LocalAlloc(LMEM_FIXED, required));

    if (!descriptor)
        return SecurityDescriptor();

    SetLastError(ERROR_SUCCESS);

    if (!GetFileSecurityW(path.path().c_str(),
                          requested,
                          descriptor.get(),
                          required,
                          &required))
        return SecurityDescriptor();

    return SecurityDescriptor(std::move(descriptor));
}

long GetLastError()
{
    return static_cast<long>(::GetLastError());
}

std::optional<VolumeInfo> GetVolumeInformationByPath(const Path& path)
{
    // Add trailing separator as necessary.
    auto path_ = toWideString(path.string());

    if (!path_.empty() && path_.back() != L'\\')
        path_.push_back(L'\\');

    // Preallocate maximum necessary space for names.
    WCHAR filesystemName[MAX_PATH + 1];
    WCHAR volumeName[MAX_PATH + 1];

    // So we know how long the names actually are.
    DWORD filesystemNameLength = 0u;
    DWORD volumeNameLength = 0u;

    // Couldn't get information about the specified volume.
    if (!GetVolumeInformationW(path_.c_str(),
                               volumeName,
                               std::size(volumeName),
                               nullptr,
                               nullptr,
                               nullptr,
                               filesystemName,
                               std::size(filesystemName)))
        return std::nullopt;

    // Make sure the names are null terminated.
    filesystemName[MAX_PATH] = L'\0';
    volumeName[MAX_PATH] = L'\0';

    VolumeInfo info;

    // Populate info instance.
    info.mFilesystemName = fromWideString(filesystemName);
    info.mVolumeName = fromWideString(volumeName);

    // Return volume info to our caller.
    return info;
}

BOOL MoveFileExP(const Path& source, const Path& target, DWORD flags)
{
    return MoveFileExW(source.path().c_str(),
                       target.path().c_str(),
                       flags);
}

BOOL RemoveDirectoryP(const Path& path)
{
    return RemoveDirectoryW(path.path().c_str());
}

BOOL SetFileAttributesP(const Path& path, DWORD attributes)
{
    return SetFileAttributesW(path.path().c_str(), attributes);
}

BOOL SetFileSecurityP(const Path& path, const SecurityDescriptor& descriptor)
{
    constexpr auto flags = DACL_SECURITY_INFORMATION
                           | GROUP_SECURITY_INFORMATION;

    return SetFileSecurityW(path.path().c_str(), flags, descriptor.get());
}

} // testing
} // fuse
} // mega

