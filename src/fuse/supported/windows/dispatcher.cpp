#include <cassert>
#include <cstring>

#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/normalized_path.h>
#include <mega/fuse/platform/constants.h>
#include <mega/fuse/platform/dispatcher.h>
#include <mega/fuse/platform/mount.h>
#include <mega/fuse/platform/mount_db.h>
#include <mega/fuse/platform/service_context.h>
#include <mega/fuse/platform/utility.h>

namespace mega
{
namespace fuse
{
namespace platform
{

static Dispatcher& dispatcher(FSP_FILE_SYSTEM& filesystem);

static void logAccess(const char* function, UINT32 mask);

static void logAttributes(const char* function, UINT32 mask);

static void logOptions(const char* function, UINT32 mask);

const FSP_FILE_SYSTEM_INTERFACE Dispatcher::mOperations = {
    /*        GetVolumeInfo */ &Dispatcher::getVolumeInfo,
    /*       SetVolumeLabel */ nullptr,
    /*    GetSecurityByName */ &Dispatcher::getSecurityByName,
    /*               Create */ &Dispatcher::create,
    /*                 Open */ &Dispatcher::open,
    /*            Overwrite */ &Dispatcher::overwrite,
    /*              Cleanup */ &Dispatcher::cleanup,
    /*                Close */ &Dispatcher::close,
    /*                 Read */ &Dispatcher::read,
    /*                Write */ &Dispatcher::write,
    /*                Flush */ &Dispatcher::flush,
    /*          GetFileInfo */ &Dispatcher::getFileInfo,
    /*         SetBasicInfo */ &Dispatcher::setBasicInfo,
    /*          SetFileSize */ &Dispatcher::setFileSize,
    /*            CanDelete */ &Dispatcher::canDelete,
    /*               Rename */ &Dispatcher::rename,
    /*          GetSecurity */ &Dispatcher::getSecurity,
    /*          SetSecurity */ &Dispatcher::setSecurity,
    /*        ReadDirectory */ &Dispatcher::readDirectory,
    /* ResolveReparsePoints */ nullptr,
    /*      GetReparsePoint */ nullptr,
    /*      SetReparsePoint */ nullptr,
    /*   DeleteReparsePoint */ nullptr,
    /*        GetStreamInfo */ nullptr,
    /*     GetDirInfoByName */ &Dispatcher::getDirInfoByName,
    /*              Control */ nullptr,
    /*            SetDelete */ nullptr,
    /*             CreateEx */ nullptr,
    /*          OverwriteEx */ nullptr,
    /*                GetEa */ nullptr,
    /*                SetEa */ nullptr,
    /*            Obsolete0 */ nullptr,
    /*    DispatcherStopped */ nullptr,
    /*           Reserved00 */ nullptr,
    /*           Reserved01 */ nullptr,
    /*           Reserved02 */ nullptr,
    /*           Reserved03 */ nullptr,
    /*           Reserved04 */ nullptr,
    /*           Reserved05 */ nullptr,
    /*           Reserved06 */ nullptr,
    /*           Reserved07 */ nullptr,
    /*           Reserved08 */ nullptr,
    /*           Reserved09 */ nullptr,
    /*           Reserved0a */ nullptr,
    /*           Reserved0b */ nullptr,
    /*           Reserved0c */ nullptr,
    /*           Reserved0d */ nullptr,
    /*           Reserved0e */ nullptr,
    /*           Reserved0f */ nullptr,
    /*           Reserved10 */ nullptr,
    /*           Reserved11 */ nullptr,
    /*           Reserved12 */ nullptr,
    /*           Reserved13 */ nullptr,
    /*           Reserved14 */ nullptr,
    /*           Reserved15 */ nullptr,
    /*           Reserved16 */ nullptr,
    /*           Reserved17 */ nullptr,
    /*           Reserved18 */ nullptr,
    /*           Reserved19 */ nullptr,
    /*           Reserved1a */ nullptr,
    /*           Reserved1b */ nullptr,
    /*           Reserved1c */ nullptr,
    /*           Reserved1d */ nullptr,
    /*           Reserved1e */ nullptr
}; // mOperations

NTSTATUS Dispatcher::canDelete(FSP_FILE_SYSTEM* filesystem,
                               PVOID context,
                               PWSTR path)
{
    assert(filesystem);
    assert(context);
    assert(path);

    auto& dispatcher = platform::dispatcher(*filesystem);
    auto path_ = normalize(path);

    FUSEDebugF("canDelete: context: %p, path: %s",
               context,
               fromWideString(path_).c_str());

    auto result = dispatcher.mMount.canDelete(context);

    FUSEDebugF("canDelete: context: %p, result: %lu", context, result);

    return result;
}

void Dispatcher::cleanup(FSP_FILE_SYSTEM* filesystem,
                         PVOID context,
                         PWSTR path,
                         ULONG flags)
{
    assert(filesystem);
    assert(context);

    auto& dispatcher = platform::dispatcher(*filesystem);
    std::wstring path_;

    if (path)
        path_ = normalize(&path[1]);

    FUSEDebugF("cleanup: context: %p, path: %s, flags: %lx",
               context,
               path_.empty() ? "NULL" : fromWideString(path_).c_str(),
               flags);

    dispatcher.mMount.cleanup(context, path_, flags);
}

void Dispatcher::close(FSP_FILE_SYSTEM* filesystem,
                       PVOID context)
{
    assert(filesystem);
    assert(context);

    auto& dispatcher = platform::dispatcher(*filesystem);

    FUSEDebugF("close: context: %p", context);

    dispatcher.mMount.close(context);
}

NTSTATUS Dispatcher::create(FSP_FILE_SYSTEM* filesystem,
                            PWSTR path,
                            UINT32 options,
                            UINT32 access,
                            UINT32 attributes,
                            [[maybe_unused]] PSECURITY_DESCRIPTOR descriptor,
                            UINT64,
                            PVOID* context,
                            FSP_FSCTL_FILE_INFO* info)
{
    assert(filesystem);
    assert(path);
    assert(descriptor);
    assert(context);
    assert(info);

    auto& dispatcher = platform::dispatcher(*filesystem);
    auto path_ = normalize(&path[1]);

    FUSEDebugF("create: path: %s", fromWideString(path_).c_str());

    logAccess("create", access);
    logAttributes("create", attributes);
    logOptions("create", options);

    auto result = dispatcher.mMount.create(path_,
                                           options,
                                           access,
                                           *context,
                                           *info);

    FUSEDebugF("create: result: %lu", result);

    return result;
}

NTSTATUS Dispatcher::flush(FSP_FILE_SYSTEM* filesystem,
                           PVOID context,
                           FSP_FSCTL_FILE_INFO* info)
{
    assert(filesystem);
    assert(context);
    assert(info);

    auto& dispatcher = platform::dispatcher(*filesystem);

    FUSEDebugF("flush: context: %p, info: %p",
               context,
               info);

    auto result = dispatcher.mMount.flush(context, *info);

    FUSEDebugF("flush: context: %p, result: %lu", context, result);

    return result;
}

NTSTATUS Dispatcher::getDirInfoByName(FSP_FILE_SYSTEM* filesystem,
                                      PVOID context,
                                      PWSTR path,
                                      FSP_FSCTL_DIR_INFO* info)
{
    assert(filesystem);
    assert(context);
    assert(path);
    assert(info);

    auto& dispatcher = platform::dispatcher(*filesystem);
    auto path_ = normalize(&path[1]);

    FUSEDebugF("getDirInfoByName: context: %p, path: %s, info: %p",
                context,
                fromWideString(path_).c_str(),
                info);

    auto result = dispatcher.mMount.getDirInfoByName(context, path_, *info);

    FUSEDebugF("getDirInfoByName: context: %p, result: %lu", context, result);

    return result;
}

NTSTATUS Dispatcher::getFileInfo(FSP_FILE_SYSTEM* filesystem,
                                 PVOID context,
                                 FSP_FSCTL_FILE_INFO* info)
{
    assert(filesystem);
    assert(context);
    assert(info);

    auto& dispatcher = platform::dispatcher(*filesystem);

    FUSEDebugF("getFileInfo: context: %p, info: %p",
               context,
               info);

    auto result = dispatcher.mMount.getFileInfo(context, *info);

    FUSEDebugF("getFileInfo: context: %p, result: %lu", context, result);

    return result;
}

NTSTATUS Dispatcher::getSecurity(FSP_FILE_SYSTEM* filesystem,
                                 PVOID context,
                                 PSECURITY_DESCRIPTOR descriptor,
                                 SIZE_T* descriptorLength)
{
    assert(filesystem);
    assert(context);
    assert(descriptor);
    assert(descriptorLength);

    auto& dispatcher = platform::dispatcher(*filesystem);

    FUSEDebugF("getSecurity: context: %p", context);

    auto result = dispatcher.mMount.getSecurity(context,
                                                descriptor,
                                                *descriptorLength);

    FUSEDebugF("getSecurity: context: %p, result: %lu", context, result);

    return result;
}

NTSTATUS Dispatcher::getSecurityByName(FSP_FILE_SYSTEM* filesystem,
                                       PWSTR path,
                                       PUINT32 attributes,
                                       PSECURITY_DESCRIPTOR descriptor,
                                       SIZE_T* descriptorLength)
{
    assert(filesystem);
    assert(path);
    assert(!!descriptor == !!descriptorLength);

    auto& dispatcher = platform::dispatcher(*filesystem);
    auto path_ = normalize(&path[1]);

    FUSEDebugF("getSecurityByName: path: %s", fromWideString(path_).c_str());

    auto result = dispatcher.mMount.getSecurityByName(path_,
                                                      attributes,
                                                      descriptor,
                                                      descriptorLength);

    FUSEDebugF("getSecurityByName: path: %s, result: %lu",
               fromWideString(path_).c_str(),
               result);

    return result;
}

NTSTATUS Dispatcher::getVolumeInfo(FSP_FILE_SYSTEM* filesystem,
                                   FSP_FSCTL_VOLUME_INFO* info)
{
    assert(filesystem);
    assert(info);

    auto& dispatcher = platform::dispatcher(*filesystem);

    FUSEDebug1("getVolumeInfo");

    return dispatcher.mMount.getVolumeInfo(*info);
}

NTSTATUS Dispatcher::open(FSP_FILE_SYSTEM* filesystem,
                          PWSTR path,
                          UINT32 options,
                          UINT32 access,
                          PVOID* context,
                          FSP_FSCTL_FILE_INFO* info)
{
    assert(filesystem);
    assert(path);
    assert(context);
    assert(info);

    auto& dispatcher = platform::dispatcher(*filesystem);
    auto path_ = normalize(&path[1]);
    
    FUSEDebugF("open: path: %s", fromWideString(path_).c_str());

    logAccess("open", access);
    logOptions("open", options);

    auto result = dispatcher.mMount.open(path_,
                                         options,
                                         access,
                                         *context,
                                         *info);

    FUSEDebugF("open: context: %p, path: %s, result: %lu",
               *context,
               fromWideString(path_).c_str(),
               result);

    return result;
}

NTSTATUS Dispatcher::overwrite(FSP_FILE_SYSTEM* filesystem,
                               PVOID context,
                               UINT32 attributes,
                               BOOLEAN replaceAttributes,
                               UINT64 allocation,
                               FSP_FSCTL_FILE_INFO* info)
{
    assert(filesystem);
    assert(context);
    assert(info);

    auto& dispatcher = platform::dispatcher(*filesystem);

    FUSEDebugF("overwrite: allocation: %lu, attributes: %lx, context: %p, replace: %u",
               allocation,
               attributes,
               context,
               replaceAttributes);

    auto result = dispatcher.mMount.overwrite(context, *info);

    FUSEDebugF("overwrite: context: %p, result: %lu", context, result);

    return result;
}

NTSTATUS Dispatcher::read(FSP_FILE_SYSTEM* filesystem,
                          PVOID context,
                          PVOID buffer,
                          UINT64 offset,
                          ULONG length,
                          PULONG numRead)
{
    assert(filesystem);
    assert(context);
    assert(buffer);
    assert(numRead);

    auto& dispatcher = platform::dispatcher(*filesystem);

    FUSEDebugF("read: buffer: %p, context: %p, offset: %lu, size: %lu",
               buffer,
               context,
               offset,
               length);

    auto result = dispatcher.mMount.read(context,
                                         buffer,
                                         offset,
                                         length,
                                         *numRead);

    FUSEDebugF("read: context: %p, result: %lu", context, result);

    return result;
}

NTSTATUS Dispatcher::readDirectory(FSP_FILE_SYSTEM* filesystem,
                                   PVOID context,
                                   PWSTR pattern,
                                   PWSTR marker,
                                   PVOID buffer,
                                   ULONG length,
                                   PULONG numWritten)
{
    assert(filesystem);
    assert(context);
    assert(buffer);
    assert(numWritten);

    auto& dispatcher = platform::dispatcher(*filesystem);
    std::string marker_;
    std::string pattern_ = "*";

    if (marker)
        marker_ = fromWideString(marker);

    if (pattern)
        pattern_ = fromWideString(pattern);

    FUSEDebugF("readDirectory: context: %p, length: %lu marker: %s, pattern: %s",
               context,
               length,
               marker_.c_str(),
               pattern_.c_str());

    auto result = dispatcher.mMount.readDirectory(context,
                                                  pattern_,
                                                  marker_,
                                                  buffer,
                                                  length,
                                                  *numWritten);

    FUSEDebugF("readDirectory: context: %p, result: %lu", context, result);

    return result;
}

NTSTATUS Dispatcher::rename(FSP_FILE_SYSTEM* filesystem,
                            PVOID context,
                            PWSTR sourcePath,
                            PWSTR targetPath,
                            BOOLEAN replace)
{
    assert(filesystem);
    assert(context);
    assert(sourcePath);
    assert(targetPath);

    auto& dispatcher = platform::dispatcher(*filesystem);
    auto source_ = normalize(&sourcePath[1]);
    auto target_ = normalize(&targetPath[1]);

    FUSEDebugF("rename: context: %p, replace: %u, source: %s, target: %s",
               context,
               replace,
               fromWideString(source_).c_str(),
               fromWideString(target_).c_str());

    auto result = dispatcher.mMount.rename(context, target_, replace);

    FUSEDebugF("rename: context: %p, result: %lu", context, result);

    return result;
}

NTSTATUS Dispatcher::setBasicInfo(FSP_FILE_SYSTEM* filesystem,
                                  PVOID context,
                                  UINT32 attributes,
                                  UINT64 created,
                                  UINT64 accessed,
                                  UINT64 written,
                                  UINT64 changed,
                                  FSP_FSCTL_FILE_INFO* info)
{
    assert(filesystem);
    assert(context);
    assert(info);

    auto& dispatcher = platform::dispatcher(*filesystem);

    FUSEDebugF("setBasicInfo: context: %p", context);

    auto result = dispatcher.mMount.setBasicInfo(context,
                                                 attributes,
                                                 created,
                                                 accessed,
                                                 written,
                                                 changed,
                                                 *info);

    FUSEDebugF("setBasicInfo: context: %p, result: %lu", context, result);

    return result;
}

NTSTATUS Dispatcher::setFileSize(FSP_FILE_SYSTEM* filesystem,
                                 PVOID context,
                                 UINT64 size,
                                 BOOLEAN allocated,
                                 FSP_FSCTL_FILE_INFO* info)
{
    assert(filesystem);
    assert(context);
    assert(info);

    auto& dispatcher = platform::dispatcher(*filesystem);

    FUSEDebugF("setFileSize: allocated: %u, context: %p, size: %lu",
               allocated,
               context,
               size);

    auto result = dispatcher.mMount.setFileSize(context, size, allocated, *info);

    FUSEDebugF("setFileSize: context: %p, result: %lu", context, result);

    return result;
}
    
NTSTATUS Dispatcher::setSecurity(FSP_FILE_SYSTEM* filesystem,
                                 PVOID context,
                                 SECURITY_INFORMATION security,
                                 PSECURITY_DESCRIPTOR descriptor)
{
    assert(filesystem);
    assert(context);
    assert(descriptor);

    auto& dispatcher = platform::dispatcher(*filesystem);

    FUSEDebugF("setSecurity: context: %p, security: %lx",
               context,
               security);

    auto result = dispatcher.mMount.setSecurity(context, security, descriptor);

    FUSEDebugF("setSecurity: context: %p, result: %lu", context, result);

    return result;
}

void Dispatcher::stopped(FSP_FILE_SYSTEM* filesystem,
                         BOOLEAN normally)
{
    assert(filesystem);

    dispatcher(*filesystem).mMount.stopped(normally);
}

NTSTATUS Dispatcher::write(FSP_FILE_SYSTEM* filesystem,
                           PVOID context,
                           PVOID buffer,
                           UINT64 offset,
                           ULONG length,
                           BOOLEAN append,
                           BOOLEAN noGrow,
                           PULONG numWritten,
                           FSP_FSCTL_FILE_INFO* info)
{
    assert(filesystem);
    assert(context);
    assert(buffer);
    assert(numWritten);
    assert(info);

    auto& dispatcher = platform::dispatcher(*filesystem);

    FUSEDebugF("write: buffer: %p, context: %p, offset: %lu, size: %lu",
               buffer,
               context,
               offset,
               length);

    auto result = dispatcher.mMount.write(context,
                                          buffer,
                                          offset,
                                          length,
                                          append,
                                          noGrow,
                                          *numWritten,
                                          *info);

    FUSEDebugF("write: context: %p, result: %lu", context, result);

    return result;
}

Dispatcher::Dispatcher(Mount& mount,
                       const NormalizedPath& path)
  : mFilesystem(nullptr)
  , mMount(mount)
  , mPath()
{
    FSP_FSCTL_VOLUME_PARAMS parameters;

    // Make sure parameters are in a well-defined state.
    std::memset(&parameters, 0, sizeof(parameters));

    // Populate parameters.
    parameters.CasePreservedNames = true;
    parameters.CaseSensitiveSearch = true;
    parameters.FlushAndPurgeOnCleanup = true;
    parameters.MaxComponentLength = MaxNameLength;
    parameters.PersistentAcls = true;
    parameters.ReadOnlyVolume = !mount.writable();
    parameters.SectorSize = 512;
    parameters.FileInfoTimeout = 128;
    parameters.SectorsPerAllocationUnit = BlockSize / parameters.SectorSize;
    parameters.UmFileContextIsUserContext2 = true;
    parameters.UnicodeOnDisk = true;

    // Assume we're mounting as as "disk-based" filesystem.
    std::wstring type = L"" FSP_FSCTL_DISK_DEVICE_NAME;

    // Actually mounting as a "network" filesystem.
    if (path.empty() || path.isRootPath())
    {
        // Compute UNC prefix.
        auto prefix = UNCPrefix + toWideString(mount.name());

        // Sanity.
        assert(prefix.size() <= MaxVolumePrefixLength);

        // Populate UNC prefix.
        std::wmemcpy(parameters.Prefix, prefix.data(), prefix.size());

        // Mount as a "network filesystem."
        type = L"" FSP_FSCTL_NET_DEVICE_NAME;
    }

    // Try and create the filesystem.
    auto result = FspFileSystemCreate(&type[0],
                                      &parameters,
                                      &mOperations,
                                      &mFilesystem);

    // Couldn't create filesystem.
    if (!NT_SUCCESS(result))
        throw FUSEErrorF("Couldn't create dispatcher: %lx", result);

    // Ask WinFSP to log *everything*.
    FspFileSystemSetDebugLog(mFilesystem, std::numeric_limits<UINT32>::max());

    // Allow concurrent file operations.
    FspFileSystemSetOperationGuardStrategy(
      mFilesystem, FSP_FILE_SYSTEM_OPERATION_GUARD_STRATEGY_FINE);

    // Make sure WinFSP logs everything.
    mFilesystem->DebugLog = std::numeric_limits<UINT32>::max();

    // Make sure we know which dispatcher is processing what requests.
    mFilesystem->UserContext = this;

    FUSEDebugF("Dispatcher constructed: %s", mMount.name().c_str());
}

Dispatcher::~Dispatcher()
{
    FspFileSystemDelete(mFilesystem);

    FUSEDebugF("Dispatcher destructed: %s", mMount.name().c_str());
}

const NormalizedPath& Dispatcher::path() const
{
    return mPath;
}

void Dispatcher::reply(FSP_FSCTL_TRANSACT_RSP& response, Error result)
{
    reply(response, translate(result));
}

void Dispatcher::reply(FSP_FSCTL_TRANSACT_RSP& response, NTSTATUS result)
{
    response.IoStatus.Status = result;

    FspFileSystemSendResponse(mFilesystem, &response);
}

FSP_FSCTL_TRANSACT_REQ& Dispatcher::request() const
{
    auto* context = FspFileSystemGetOperationContext();

    assert(context);

    return *context->Request;
}

void Dispatcher::start(const NormalizedPath& path)
{
    // Try and start the dispatcher.
    auto result = FspFileSystemStartDispatcher(mFilesystem, 0);

    // Couldn't start the dispatcher.
    if (!NT_SUCCESS(result))
        throw FUSEErrorF("Couldn't start dispatcher: %s: %lx",
                         mMount.name().c_str(),
                         result);

    // Assume the mount's writable.
    auto* descriptor = &mMount.mMountDB.mReadWriteSecurityDescriptor;

    // Mount's actually raed-only.
    if (!mMount.writable())
        descriptor = &mMount.mMountDB.mReadOnlySecurityDescriptor;

    // Assume the user wants us to allocate a drive letter.
    auto path_ = path.asPlatformEncoded(true);

    // Try and make the mount visible on the local filesystem.
    result = FspFileSystemSetMountPointEx(mFilesystem,
                                          path_.empty() ? nullptr : path_.data(),
                                          descriptor->get());

    // Couldn't make the mount visible.
    if (!NT_SUCCESS(result))
        throw FUSEErrorF("Couldn't set volume mount point: %s: %lx",
                         mMount.name().c_str(),
                         result);

    // Latch the mount's actual path.
    mPath = [this]() {
        std::wstring path = FspFileSystemMountPoint(mFilesystem);
        return LocalPath::fromPlatformEncodedAbsolute(std::move(path));
    }();
}

void Dispatcher::stop()
{
    FspFileSystemStopDispatcher(mFilesystem);
}

Dispatcher& dispatcher(FSP_FILE_SYSTEM& filesystem)
{
    auto* context = filesystem.UserContext;

    assert(context);

    return *reinterpret_cast<Dispatcher*>(context);
}

#define ENTRY(enumerant) {#enumerant, enumerant}

void logAccess(const char* function, UINT32 mask)
{
    static const std::map<const char*, UINT32> names = {
        ENTRY(DELETE),
        ENTRY(FILE_APPEND_DATA),
        ENTRY(FILE_EXECUTE),
        ENTRY(FILE_LIST_DIRECTORY),
        ENTRY(FILE_READ_ATTRIBUTES),
        ENTRY(FILE_READ_DATA),
        ENTRY(FILE_READ_EA),
        ENTRY(FILE_TRAVERSE),
        ENTRY(FILE_WRITE_ATTRIBUTES),
        ENTRY(FILE_WRITE_DATA),
        ENTRY(FILE_WRITE_EA),
        ENTRY(READ_CONTROL),
        ENTRY(SYNCHRONIZE),
        ENTRY(WRITE_DAC),
        ENTRY(WRITE_OWNER)
    }; // names

    for (auto& n : names)
    {
        if ((mask & n.second))
            FUSEDebugF("%s: access: %s", function, n.first);
    }
}

void logAttributes(const char* function, UINT32 mask)
{
    static const std::map<const char*, UINT32> names = {
        ENTRY(FILE_ATTRIBUTE_ARCHIVE),
        ENTRY(FILE_ATTRIBUTE_COMPRESSED),
        ENTRY(FILE_ATTRIBUTE_DEVICE),
        ENTRY(FILE_ATTRIBUTE_DIRECTORY),
        ENTRY(FILE_ATTRIBUTE_EA),
        ENTRY(FILE_ATTRIBUTE_ENCRYPTED),
        ENTRY(FILE_ATTRIBUTE_HIDDEN),
        ENTRY(FILE_ATTRIBUTE_INTEGRITY_STREAM),
        ENTRY(FILE_ATTRIBUTE_NORMAL),
        ENTRY(FILE_ATTRIBUTE_NOT_CONTENT_INDEXED),
        ENTRY(FILE_ATTRIBUTE_NO_SCRUB_DATA),
        ENTRY(FILE_ATTRIBUTE_OFFLINE),
        ENTRY(FILE_ATTRIBUTE_PINNED),
        ENTRY(FILE_ATTRIBUTE_READONLY),
        ENTRY(FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS),
        ENTRY(FILE_ATTRIBUTE_RECALL_ON_OPEN),
        ENTRY(FILE_ATTRIBUTE_REPARSE_POINT),
        ENTRY(FILE_ATTRIBUTE_SPARSE_FILE),
        ENTRY(FILE_ATTRIBUTE_SYSTEM),
        ENTRY(FILE_ATTRIBUTE_TEMPORARY),
        ENTRY(FILE_ATTRIBUTE_UNPINNED),
        ENTRY(FILE_ATTRIBUTE_VIRTUAL)
    }; // names

    for (auto& n : names)
    {
        if ((mask & n.second))
            FUSEDebugF("%s: attribute: %s", function, n.first);
    }
}

void logOptions(const char* function, UINT32 mask)
{
    static const std::map<const char*, UINT32> names = {
        ENTRY(FILE_COMPLETE_IF_OPLOCKED),
        ENTRY(FILE_CREATE_TREE_CONNECTION),
        ENTRY(FILE_DELETE_ON_CLOSE),
        ENTRY(FILE_DIRECTORY_FILE),
        ENTRY(FILE_NON_DIRECTORY_FILE),
        ENTRY(FILE_NO_EA_KNOWLEDGE),
        ENTRY(FILE_NO_INTERMEDIATE_BUFFERING),
        ENTRY(FILE_OPEN_BY_FILE_ID),
        ENTRY(FILE_OPEN_FOR_BACKUP_INTENT),
        ENTRY(FILE_OPEN_REPARSE_POINT),
        ENTRY(FILE_OPEN_REQUIRING_OPLOCK),
        ENTRY(FILE_RANDOM_ACCESS),
        ENTRY(FILE_RESERVE_OPFILTER),
        ENTRY(FILE_SEQUENTIAL_ONLY),
        ENTRY(FILE_SYNCHRONOUS_IO_ALERT),
        ENTRY(FILE_SYNCHRONOUS_IO_NONALERT),
        ENTRY(FILE_WRITE_THROUGH)
    }; // names

    for (auto& n : names)
    {
        if ((mask & n.second))
            FUSEDebugF("%s: option: %s", function, n.first);
    }
}

#undef ENTRY

} // platform
} // fuse
} // mega

