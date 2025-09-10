#include <cstring>

#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/platform/date_time.h>
#include <mega/fuse/platform/mount.h>
#include <mega/fuse/platform/utility.h>

namespace mega
{
namespace fuse
{
namespace platform
{

static FSP_FSCTL_DIR_INFO* translate(FSP_FSCTL_DIR_INFO& destination,
                                     const Mount& mount,
                                     const std::wstring& name,
                                     std::size_t size,
                                     const InodeInfo& source);

DWORD attributes(const InodeInfo& info, const Mount& mount)
{
    // Inode's a directory.
    if (info.mIsDirectory)
        return FILE_ATTRIBUTE_DIRECTORY;

    // Inode's a read-only file.
    if (!mount.writable() || info.mPermissions != FULL)
        return FILE_ATTRIBUTE_READONLY;

    // Inode's a writable file.
    return FILE_ATTRIBUTE_NORMAL;
}

std::string fromWideString(const wchar_t* value, std::size_t length)
{
    // String's empty.
    if (!length)
        return std::string();

    // How much buffer space do we need?
    auto required = WideCharToMultiByte(CP_UTF8,
                                        0,
                                        value,
                                        static_cast<int>(length),
                                        nullptr,
                                        0,
                                        nullptr,
                                        nullptr);

    // Couldn't determine buffer requirements.
    if (!required)
        throw FUSEErrorF("Couldn't compute length of wide-string: %u",
                         GetLastError());

    std::string buffer(required, '\x0');

    // Translate the wide-string to UTF-8.
    if (!WideCharToMultiByte(CP_UTF8,
                             0,
                             value,
                             static_cast<int>(length),
                             &buffer[0],
                             static_cast<int>(required),
                             nullptr,
                             nullptr))
        throw FUSEErrorF("Couldn't translate wide-string to UTF-8: %u",
                         GetLastError());

    // Return buffer to caller.
    return buffer;
}

std::string fromWideString(const std::wstring& value)
{
    return fromWideString(value.c_str(), value.size());
}

std::wstring normalize(const std::wstring& value)
{
    // String's empty.
    if (value.empty())
        return std::wstring();

    // How much space does our buffer need?
    auto required = NormalizeString(NormalizationC,
                                    value.c_str(),
                                    static_cast<int>(value.size()),
                                    nullptr,
                                    0);

    // Couldn't determine length of buffer.
    if (required <= 0)
        throw FUSEErrorF("Couldn't compute length of normalized wide-string: %u",
                         GetLastError());

    std::wstring buffer(required, L'\x0');

    // Try and normalize the string.
    required = NormalizeString(NormalizationC,
                               value.c_str(),
                               static_cast<int>(value.size()),
                               &buffer[0],
                               static_cast<int>(buffer.size()));

    if (required <= 0)
        throw FUSEErrorF("Couldn't normalize wide-string: %u",
                         GetLastError());

    // Trim the buffer down to size.
    buffer.resize(required);

    // Return normalized string to caller.
    return buffer;
}

std::wstring toWideString(const std::string& value)
{
    // String's empty.
    if (value.empty())
        return std::wstring();

    // How large does our buffer need to be?
    auto required = MultiByteToWideChar(CP_UTF8,
                                        0,
                                        value.c_str(),
                                        static_cast<int>(value.size()),
                                        nullptr,
                                        0);

    // Couldn't determine buffer length.
    if (!required)
        throw FUSEErrorF("Couldn't compute length of UTF-8 string: %u",
                         GetLastError());

    std::wstring buffer(required, L'\x0');

    // Translate our UTF-8 string into a wide-string.
    if (!MultiByteToWideChar(CP_UTF8,
                             0,
                             value.c_str(),
                             static_cast<int>(value.size()),
                             &buffer[0],
                             static_cast<int>(buffer.size())))
        throw FUSEErrorF("Couldn't translate UTF-8 string to wide-string: %u",
                         GetLastError());

    // Return wide-string to caller.
    return buffer;
}

void translate(FSP_FSCTL_FILE_INFO& destination,
               const Mount& mount,
               const InodeInfo& source)
{
    // Make sure destination's in a well-defined state.
    std::memset(&destination, 0, sizeof(destination));

    destination.ChangeTime = DateTime(source.mModified);
    destination.CreationTime = destination.ChangeTime;
    destination.FileAttributes = attributes(source, mount);
    destination.IndexNumber = source.mID.get();
    destination.LastAccessTime = destination.ChangeTime;
    destination.LastWriteTime = destination.ChangeTime;

    if (source.mIsDirectory)
        return;

    destination.AllocationSize = static_cast<UINT64>(source.mSize);
    destination.FileSize = destination.AllocationSize;
}

FSP_FSCTL_DIR_INFO* translate(FSP_FSCTL_DIR_INFO& destination,
                              const Mount& mount,
                              const InodeInfo& source)
{
    // Translate name into a wide string.
    auto name = toWideString(source.mName);

    // Compute buffer's length.
    auto size = sizeof(destination) + sizeof(wchar_t) * name.size();

    // Populate buffer and return reference to caller.
    return translate(destination, mount, name, size, source);
}

FSP_FSCTL_DIR_INFO* translate(std::vector<uint8_t>& destination,
                              const Mount& mount,
                              const std::string& name,
                              const InodeInfo& source)
{
    // Convenience.
    FSP_FSCTL_DIR_INFO* buffer;

    // Translate the inode's name into a wide-string.
    auto name_ = toWideString(name);

    // Make sure our buffer is large enough.
    destination.resize(sizeof(*buffer) + sizeof(wchar_t) * name_.size());

    // So we can easily populate our buffer.
    buffer = reinterpret_cast<FSP_FSCTL_DIR_INFO*>(&destination[0]);

    // Populate buffer and return reference to caller.
    return translate(*buffer,
                     mount,
                     name_,
                     destination.size(),
                     source);
}

NTSTATUS translate(Error result)
{
    switch (result)
    {
    case API_EEXIST:
        return STATUS_OBJECT_NAME_COLLISION;
    case API_ENOENT:
        return STATUS_OBJECT_PATH_NOT_FOUND;
    case API_FUSE_ENOTDIR:
        return STATUS_NOT_A_DIRECTORY;
    case API_FUSE_EISDIR:
        return STATUS_FILE_IS_A_DIRECTORY;
    case API_FUSE_ENOTFOUND:
        return STATUS_OBJECT_NAME_NOT_FOUND;
    case API_OK:
        return STATUS_SUCCESS;
    default:
        return STATUS_UNSUCCESSFUL;
    }
}

FSP_FSCTL_DIR_INFO* translate(FSP_FSCTL_DIR_INFO& destination,
                              const Mount& mount,
                              const std::wstring& name,
                              std::size_t size,
                              const InodeInfo& source)
{
    // Specify buffer's length.
    destination.Size = static_cast<UINT16>(size);

    // Make sure reserved bytes are zero.
    std::memset(destination.Padding, 0, sizeof(destination.Padding));

    // Populate file name.
    std::memcpy(destination.FileNameBuf,
                name.c_str(),
                destination.Size - sizeof(destination));

    // Populate file attributes.
    translate(destination.FileInfo, mount, source);

    // Return buffer's address to caller.
    return &destination;
}

FolderLocker::FolderLocker(const std::wstring& path)
{
    mHandle = CreateFile(path.c_str(), // folder path
                         GENERIC_READ, // desired access (must request at least read)
                         0, // share mode = 0 (deny all sharing)
                         NULL, // security attributes
                         OPEN_EXISTING, // must exist
                         FILE_FLAG_BACKUP_SEMANTICS, // required for opening directories
                         NULL);

    if (mHandle == INVALID_HANDLE_VALUE)
        FUSEInfoF("Exclusive open folder failed %d", GetLastError());
    else
        FUSEInfoF("Exclusive open folder OK");
}

FolderLocker& FolderLocker::operator=(FolderLocker&& other)
{
    using std::swap;
    swap(mHandle, other.mHandle);
    return *this;
}

FolderLocker::~FolderLocker()
{
    release();
}

void FolderLocker::release()
{
    if (mHandle == INVALID_HANDLE_VALUE)
        return;

    CloseHandle(mHandle);
    mHandle = INVALID_HANDLE_VALUE;
}

} // platform
} // fuse
} // mega

