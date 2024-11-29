#include <cstring>

#include <mega/fuse/common/client.h>
#include <mega/fuse/common/directory_inode.h>
#include <mega/fuse/common/error_or.h>
#include <mega/fuse/common/file_inode.h>
#include <mega/fuse/common/file_open_flag.h>
#include <mega/fuse/common/inode_id.h>
#include <mega/fuse/common/inode_info.h>
#include <mega/fuse/common/inode.h>
#include <mega/fuse/common/logging.h>
#include <mega/fuse/common/mount_inode_id.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/node_info.h>
#include <mega/fuse/common/ref.h>
#include <mega/fuse/platform/context.h>
#include <mega/fuse/platform/date_time.h>
#include <mega/fuse/platform/directory_context.h>
#include <mega/fuse/platform/file_context.h>
#include <mega/fuse/platform/library.h>
#include <mega/fuse/platform/mount_db.h>
#include <mega/fuse/platform/mount.h>
#include <mega/fuse/platform/path_adapter.h>
#include <mega/fuse/platform/service_context.h>
#include <mega/fuse/platform/utility.h>

#include "megafs.h"

namespace mega
{
namespace fuse
{
namespace platform
{

NTSTATUS Mount::canDelete(PVOID context)
{
    // Mount isn't writable.
    if (!writable())
        return STATUS_ACCESS_DENIED;

    // Get our hands on the entity's context.
    auto* context_ = reinterpret_cast<Context*>(context);

    // Sanity.
    assert(context_);

    // Get our hands on the entity's inode.
    auto ref = context_->inode();

    // Inode isn't writable.
    if (ref->permissions() != FULL)
        return STATUS_ACCESS_DENIED;

    // Does the inode represent a directory?
    if (auto directoryRef = ref->directory())
    {
        // Is the directory empty?
        auto hasChildren = directoryRef->hasChildren();

        // Can't determine whether the directory's empty.
        if (!hasChildren)
            return STATUS_UNSUCCESSFUL;

        // Directory isn't empty.
        if (*hasChildren)
            return STATUS_DIRECTORY_NOT_EMPTY;
    }

    // Entity can be deleted.
    return STATUS_SUCCESS;
}

void Mount::cleanup(PVOID context,
                    const std::wstring& path,
                    ULONG flags)
{
    // Nothing to do if we're not deleting the entity.
    if (!(flags & FspCleanupDelete))
        return;

    // Get our hands on the context.
    auto* context_ = reinterpret_cast<Context*>(context);

    // Sanity.
    assert(context_);

    // Try and delete the entity.
    auto result = context_->inode()->unlink();

    // Entity's been deleted.
    if (result == API_OK)
        return;

    // Couldn't delete the entity.
    FUSEWarningF("Couldn't delete entity: %s: %d",
                 fromWideString(path).c_str(),
                 static_cast<int>(result));
}

void Mount::close(PVOID context)
{
    // Get our hands on the context.
    auto* context_ = reinterpret_cast<Context*>(context);

    // Sanity.
    assert(context_);

    // Delete the context.
    delete context_;
}

NTSTATUS Mount::create(const std::wstring& path,
                       UINT32 options,
                       UINT32 access,
                       PVOID& context,
                       FSP_FSCTL_FILE_INFO& info)
{
    // Try and locate the specified node.
    auto name   = std::string();
    auto result = inodeDB().lookup(PathAdapter(path), handle(), &name);

    // Node already exists.
    if (result.second == API_OK)
        return STATUS_OBJECT_NAME_COLLISION;

    // Some parent doesn't exist.
    if (result.second != API_FUSE_ENOTFOUND)
        return translate(result.second);

    auto directoryRef = result.first->directory();

    // Sanity.
    assert(directoryRef);
    
    // Mount isn't writable.
    if (!writable())
        return STATUS_ACCESS_DENIED;

    // Directory isn't writable.
    if (directoryRef->permissions() != FULL)
        return STATUS_ACCESS_DENIED;

    // Assume caller wants to create a new file.
    auto create = &DirectoryInode::makeFile;

    // Caller wants to create a new directory;
    if ((options & FILE_DIRECTORY_FILE))
        create = &DirectoryInode::makeDirectory;

    // Try and create the new node.
    auto created = (*directoryRef.*create)(*this, name);

    // Couldn't create the new node.
    if (!created)
        return translate(created.error());
    
    // Latch the new node's description.
    translate(info, *this, std::get<1>(*created));

    // Get our hands on the newly created node.
    auto ref = std::move(std::get<0>(*created));

    // Caller's created a directory.
    if ((directoryRef = ref->directory()))
    {
        // Create a context to represent this directory.
        auto context_ =
          std::make_unique<DirectoryContext>(directoryRef, *this, false);

        // Caller now owns the context.
        context = context_.release();

        // Directory's been created.
        return STATUS_SUCCESS;
    }

    // Caller's created a file.
    FileOpenFlags flags = 0;

    // Caller wants to append to the file.
    if ((access & FILE_APPEND_DATA))
        flags = FOF_APPEND | FOF_WRITABLE;

    // Caller wants to write data to the file.
    if ((access & FILE_WRITE_DATA))
        flags = FOF_WRITABLE;

    // Try and open the file for writing.
    auto opened = ref->file()->open(*this, flags);

    // Couldn't open the file.
    if (!opened)
        return translate(opened.error());

    // Caller now owns file context.
    context = opened->release();

    // File's been created.
    return STATUS_SUCCESS;
}

NTSTATUS Mount::flush(PVOID context,
                      FSP_FSCTL_FILE_INFO& info)
{
    // Caller wants to flush the entire volume.
    if (!context)
        return STATUS_UNSUCCESSFUL;

    // Get our hands on this file's context.
    auto* context_ = reinterpret_cast<Context*>(context)->file();

    // Sanity.
    assert(context);

    // Try and flush any modifications to the cloud.
    auto result = context_->flush();

    // Latch the file's description.
    translate(info, *this, context_->info());

    // Let the caller know if the flush was successful.
    return translate(result);
}

NTSTATUS Mount::getDirInfoByName(PVOID context,
                                 const std::wstring& name,
                                 FSP_FSCTL_DIR_INFO& info)
{
    // Sanity.
    assert(context);

    // Get our hands on the directory's context.
    auto* context_ = reinterpret_cast<Context*>(context)->directory();

    // Sanity.
    assert(context_);

    // Try and locate the specified child.
    auto ref = context_->get(fromWideString(name));

    // Couldn't locate the child.
    if (!ref)
        return STATUS_OBJECT_NAME_NOT_FOUND;
    
    // Latch the child's description.
    translate(info, *this, ref->info());

    // Let the caller know the request was successful.
    return STATUS_SUCCESS;
}

NTSTATUS Mount::getFileInfo(PVOID context,
                            FSP_FSCTL_FILE_INFO& info)
{
    // Sanity.
    assert(context);

    // Get our hands on the entity's context.
    auto* context_ = static_cast<Context*>(context);

    // Desrcibe the entity to the caller.
    translate(info, *this, context_->info());

    // Let the caller know their request was successful.
    return STATUS_SUCCESS;
}

NTSTATUS Mount::getSecurity(PVOID context,
                            PSECURITY_DESCRIPTOR descriptor,
                            SIZE_T& descriptorLength)
{
    // Get our hands on the file's context.
    auto* context_ = reinterpret_cast<Context*>(context);

    // Sanity.
    assert(context_);
    
    // Retrieve the inode's security descriptor.
    return getSecurity(descriptor,
                       descriptorLength,
                       context_->inode()->info());
}

NTSTATUS Mount::getSecurity(PSECURITY_DESCRIPTOR descriptor,
                            SIZE_T& descriptorLength,
                            InodeInfo info)
{
    // Sanity.
    assert(descriptor);

    // Assume the inode is read-write.
    auto* descriptor_ = &mMountDB.mReadWriteSecurityDescriptor;

    // Mount or inode is read-only.
    if (!writable() || info.mPermissions != FULL)
        descriptor_ = &mMountDB.mReadOnlySecurityDescriptor;

    // Compute the descriptor's length.
    SIZE_T descriptorLength_ = descriptor_->length();

    // Let the caller know the descriptor's length.
    std::swap(descriptorLength, descriptorLength_);

    // Caller hasn't allocated enough buffer space.
    if (descriptorLength > descriptorLength_)
        return STATUS_BUFFER_OVERFLOW;

    // Copy descriptor to the caller's buffer.
    std::memcpy(descriptor, descriptor_->get(), descriptorLength);

    // Let the caller know they've got the descriptor.
    return STATUS_SUCCESS;
}

NTSTATUS Mount::getSecurityByName(const std::wstring& path,
                                  PUINT32 attributes,
                                  PSECURITY_DESCRIPTOR descriptor,
                                  SIZE_T* descriptorLength)
{
    // Try and locate the specified inode.
    auto result = inodeDB().lookup(PathAdapter(path), handle());

    // Couldn't locate the inode.
    if (result.second != API_OK)
        return translate(result.second);

    // Latch this inode's description.
    auto info = result.first->info();

    // Caller wants to know the inode's file attributes.
    if (attributes)
        *attributes = platform::attributes(info, *this);

    // Caller wants the inode's security descriptor.
    if (descriptor)
        return getSecurity(descriptor,
                           *descriptorLength,
                           std::move(info));

    // Let the caller know the request was successful.
    return STATUS_SUCCESS;
}

NTSTATUS Mount::getVolumeInfo(FSP_FSCTL_VOLUME_INFO& info)
{
    // Convenience.
    auto& client = mMountDB.client();

    // Ask the client how much storage we've used.
    auto storageInfo = client.storageInfo();

    // Couldn't determine how much storage we've used.
    if (!storageInfo)
        return STATUS_UNSUCCESSFUL;

    // Get our hands on the mount's name.
    auto name = toWideString(this->name());

    // Populate usage statistics.
    info.FreeSize = static_cast<UINT64>(storageInfo->mAvailable);
    info.TotalSize = static_cast<UINT64>(storageInfo->mCapacity);

    // Populate volume label.
    info.VolumeLabelLength =
      static_cast<UINT16>(name.size() * sizeof(wchar_t));

    std::memcpy(info.VolumeLabel, name.c_str(), info.VolumeLabelLength);

    // Return control to caller.
    return STATUS_SUCCESS;
}

InodeDB& Mount::inodeDB() const
{
    return mMountDB.mContext.mInodeDB;
}

NTSTATUS Mount::open(const std::wstring& path,
                     UINT32 options,
                     UINT32 access,
                     PVOID& context,
                     FSP_FSCTL_FILE_INFO& info)
{
    // Try and locate the specified inode.
    auto result = inodeDB().lookup(PathAdapter(path), handle());

    // Couldn't locate the inode.
    if (result.second != API_OK)
        return translate(result.second);

    auto ref = std::move(result.first);

    // Inode describes a directory.
    if (auto directoryRef = ref->directory())
    {
        // Caller's only interested in files.
        if ((options & FILE_NON_DIRECTORY_FILE))
            return STATUS_FILE_IS_A_DIRECTORY;

        // Latch the directory's description.
        translate(info, *this, directoryRef->info());

        // Create a context to represent this directory.
        auto context_ =
          std::make_unique<DirectoryContext>(directoryRef,
                                              *this,
                                              path.empty());

        // Caller now owns the directory's context.
        context = context_.release();

        // Let the caller know the directory's opened.
        return STATUS_SUCCESS;
    }

    // Inode describes a file but the caller wants a directory.
    if ((options & FILE_DIRECTORY_FILE))
        return STATUS_NOT_A_DIRECTORY;

    FileOpenFlags flags = 0;

    // Compute open flags.
    if ((access & FILE_APPEND_DATA))
        flags = FOF_APPEND | FOF_WRITABLE;

    if ((access & FILE_WRITE_DATA))
        flags = FOF_WRITABLE;

    // Get our hands on the file's inode.
    auto fileRef = ref->file();

    // Try and open the file.
    auto opened = fileRef->open(*this, flags);

    // Couldn't open the file.
    if (!opened)
        return translate(opened.error());

    // Latch the file's description.
    translate(info, *this, fileRef->info());

    // Caller know owns the file's context.
    context = opened->release();

    // Let the caller know the file's opened.
    return STATUS_SUCCESS;
}

NTSTATUS Mount::overwrite(PVOID context,
                          FSP_FSCTL_FILE_INFO& info)
{
    // Sanity.
    assert(context);

    // Get our hands on the file's context.
    auto* context_ = reinterpret_cast<Context*>(context)->file();

    // Sanity.
    assert(context_);

    // Try and truncate the file.
    auto result = context_->truncate(0, false);

    // Can't truncate the file.
    if (result != API_OK)
        return translate(result);

    // Latch the file's description.
    translate(info, *this, context_->info());

    // File's been overwritten.
    return STATUS_SUCCESS;
}

NTSTATUS Mount::read(PVOID context,
                     PVOID buffer,
                     UINT64 offset,
                     ULONG length,
                     ULONG& numRead)
{
    // Sanity.
    assert(context);

    // Get our hands on the context.
    auto* context_ = reinterpret_cast<Context*>(context)->file();

    // Sanity.
    assert(context_);

    // Get our hands on the request's "hint."
    auto hint = mDispatcher.request().Hint;

    // Actually reads the file.
    auto read = [=](Activity&, const Task& task) {
        auto response = std::make_unique<FSP_FSCTL_TRANSACT_RSP>();

        std::memset(response.get(), 0, sizeof(response));

        // Prepare for response.
        response->Hint = hint;
        response->Kind = FspFsctlTransactReadKind;
        response->Size = sizeof(*response);

        // Try and read the file.
        auto result = context_->read(static_cast<m_off_t>(offset), length);

        // Couldn't read the file.
        if (!result)
            return mDispatcher.reply(*response, result.error());

        // Let the caller know how much data was read.
        response->IoStatus.Information =
          static_cast<ULONG>(result->size());

        // Caller's hit the end of the file.
        if (!response->IoStatus.Information)
            return mDispatcher.reply(*response, STATUS_END_OF_FILE);

        // Populate user's buffer.
        std::memcpy(buffer, result->c_str(), result->size());

        // Let the caller know their read has been successful.
        mDispatcher.reply(*response, STATUS_SUCCESS);
    }; // read

    // Schedule the read for execution.
    mExecutor.execute(std::bind(std::move(read),
                                mActivities.begin(),
                                std::placeholders::_1),
                      true);

    // Let the caller know their read is underway.
    return STATUS_PENDING;
}

NTSTATUS Mount::readDirectory(PVOID context,
                              const std::string& pattern,
                              const std::string& marker,
                              PVOID buffer,
                              ULONG length,
                              ULONG& numWritten)
{
    // Get our hands on the directory's context.
    auto* context_ = reinterpret_cast<DirectoryContext*>(context);

    // Get our hands on the request's "hint."
    auto hint = mDispatcher.request().Hint;

    // Actually reads the directory.
    auto read = [=](Activity&, const Task&) {
        auto numWritten = 0ul;
        auto response = std::make_unique<FSP_FSCTL_TRANSACT_RSP>();

        // Populate directory entries.
        context_->get(marker,
                      buffer,
                      length,
                      *this,
                      numWritten);

        // Populate response.
        std::memset(response.get(), 0, sizeof(*response));

        response->Hint = hint;
        response->Kind = FspFsctlTransactQueryDirectoryKind;
        response->Size = sizeof(*response);
        response->IoStatus.Information = static_cast<UINT32>(numWritten);

        // Send response to caller.
        mDispatcher.reply(*response, STATUS_SUCCESS);
    }; // read

    // Schedule the read for execution.
    mExecutor.execute(std::bind(std::move(read),
                                mActivities.begin(),
                                std::placeholders::_1),
                      true);

    // Let the caller know their request is in progress.
    return STATUS_PENDING;
}

NTSTATUS Mount::rename(PVOID context,
                       const std::wstring& targetPath,
                       BOOLEAN replace)
{
    // Get our hands on this inode's context.
    auto* context_ = reinterpret_cast<Context*>(context);

    // Sanity.
    assert(context_);

    // Mount isn't writable.
    if (!writable())
        return STATUS_ACCESS_DENIED;

    // Convenience.
    auto name = std::string();

    // Try and locate the target inode.
    auto located = inodeDB().lookup(PathAdapter(targetPath), handle(), &name);

    // Convenience.
    auto source = context_->inode();
    auto target = std::move(located.first);

    // Target was found.
    if (located.second == API_OK)
    {
        // But the caller doesn't want to replace it.
        if (!replace)
            return STATUS_OBJECT_NAME_COLLISION;

        // Try and replace target with source.
        auto result = source->replace(std::move(target), false);

        // Let the caller know if source replaced target.
        return translate(result);
    }

    // Target directory wasn't found.
    if (located.second != API_FUSE_ENOTFOUND)
        return translate(located.second);

    // Try and move source to target.
    auto result = source->move(std::move(name), target->directory());

    // Let the caller know if source was moved to target.
    return translate(result);
}

NTSTATUS Mount::setBasicInfo(PVOID context,
                             UINT32 attributes,
                             UINT64 created,
                             UINT64 accessed,
                             UINT64 written,
                             UINT64 changed,
                             FSP_FSCTL_FILE_INFO& info)
{
    // Get our hands on this inode's context.
    auto* context_ = reinterpret_cast<Context*>(context);

    // Sanity.
    assert(context_);

    // Get our hands on the inode.
    auto ref = context_->inode();

    // Latch the inode's current description
    translate(info, *this, ref->info());

    // User wants to modify the file's attributes.
    if (attributes != INVALID_FILE_ATTRIBUTES)
    {
        // Normalize attributes for comparison.
        if (!attributes)
            attributes = FILE_ATTRIBUTE_NORMAL;

        // Caller isn't allowed to change attributes.
        if (attributes != info.FileAttributes)
            return STATUS_ACCESS_DENIED;
    }

    // Caller isn't allowed to change creation time.
    if (created && created != info.CreationTime)
        return STATUS_ACCESS_DENIED;

    // Mount isn't writable.
    if (!writable())
        return STATUS_ACCESS_DENIED;

    // Inode isn't writable.
    if (ref->permissions() != FULL)
        return STATUS_ACCESS_DENIED;

    // Caller doesn't want to change the inode's modification time.
    if (!written)
        return STATUS_SUCCESS;

    auto fileRef = ref->file();

    // Directories don't have a mutable modification time.
    if (!fileRef)
        return STATUS_SUCCESS;

    // Try and change the file's modification time.
    auto result = fileRef->touch(*this, DateTime(written));

    // Latch the file's current description.
    translate(info, *this, ref->info());

    // Let the caller know if the modification time was changed.
    return translate(result);
}

NTSTATUS Mount::setFileSize(PVOID context,
                            UINT64 size,
                            BOOLEAN allocated,
                            FSP_FSCTL_FILE_INFO& info)
{
    // Sanity.
    assert(context);

    // Get our hands on the file's context.
    auto* context_ = reinterpret_cast<Context*>(context)->file();

    // Sanity.
    assert(context_);

    // Mount isn't writable.
    if (!writable())
        return STATUS_ACCESS_DENIED;

    // File isn't writable.
    if (context_->inode()->permissions() != FULL)
        return STATUS_ACCESS_DENIED;

    // Try and set the file's size.
    auto result = context_->truncate(static_cast<m_off_t>(size), allocated);

    // Couldn't set the file's size.
    if (result != API_OK)
        return translate(result);

    // Latch the file's description.
    translate(info, *this, context_->info());

    // Let the caller know the file's size has been changed.
    return STATUS_SUCCESS;
}

NTSTATUS Mount::setSecurity(PVOID context,
                            SECURITY_INFORMATION security,
                            PSECURITY_DESCRIPTOR desired)
{
    auto* context_ = reinterpret_cast<Context*>(context);

    // Sanity.
    assert(context_);

    // Mount isn't writable.
    if (!writable())
        return STATUS_ACCESS_DENIED;

    // Inode isn't writable.
    if (context_->inode()->permissions() != FULL)
        return STATUS_ACCESS_DENIED;

    // Create a mutable copy of this inode's security descriptor.
    auto descriptor = mMountDB.mReadWriteSecurityDescriptor;

    // Try and perform the requested updates.
    auto result = descriptor.modify(desired, security);

    // Couldn't update the descriptor.
    if (result != ERROR_SUCCESS)
        return FspNtStatusFromWin32(result);

    // Make sure the descriptor hasn't changed.
    if (descriptor != mMountDB.mReadWriteSecurityDescriptor)
        return STATUS_ACCESS_DENIED;

    return STATUS_SUCCESS;
}

void Mount::stopped(BOOLEAN normally)
{
}

NTSTATUS Mount::write(PVOID context,
                      PVOID buffer,
                      UINT64 offset,
                      ULONG length,
                      BOOLEAN append,
                      BOOLEAN noGrow,
                      ULONG& numWritten,
                      FSP_FSCTL_FILE_INFO& info)
{
    // Sanity.
    assert(context);

    // Get our hands on the file's context.
    auto* context_ = reinterpret_cast<Context*>(context)->file();

    // Sanity.
    assert(context_);

    // Get our hands on the request's "hint."
    auto hint = mDispatcher.request().Hint;

    // Convenience.
    auto length_ = static_cast<m_off_t>(length);
    auto offset_ = static_cast<m_off_t>(offset);

    // Caller wants to write to the end of the file.
    if (append)
        offset_ = -1;

    // Actually perform the write.
    auto write = [=](Activity&, const Task&) {
        auto response = std::make_unique<FSP_FSCTL_TRANSACT_RSP>();

        // Prepare for response.
        std::memset(response.get(), 0, sizeof(*response));

        response->Hint = hint;
        response->Kind = FspFsctlTransactWriteKind;
        response->Size = sizeof(*response);

        // Try and write the data to the file.
        auto result = context_->write(buffer, length_, offset_, noGrow);

        // Couldn't write the data to the file.
        if (!result)
            return mDispatcher.reply(*response, result.error());

        // Let the caller know how much data was written.
        response->IoStatus.Information = static_cast<UINT32>(*result);

        // Let the caller know the write was successful.
        mDispatcher.reply(*response, STATUS_SUCCESS);
    }; // write

    // Schedule write for execution.
    mExecutor.execute(std::bind(std::move(write),
                                mActivities.begin(),
                                std::placeholders::_1),
                      true);

    // Let the caller know the write is underway.
    return STATUS_PENDING;
}

Mount::Mount(const MountInfo& info,
             MountDB& mountDB)
  : fuse::Mount(info, mountDB)
  , mActivities()
  , mDispatcher(*this)
  , mExecutor(mountDB.executorFlags())
{
    mDispatcher.start();

    FUSEDebugF("Mount constructed: %s",
               path().toPath(false).c_str());
}

Mount::~Mount()
{
    // Wait for all outstanding requests to complete.
    mActivities.waitUntilIdle();

    // Shut down the dispatcher.
    mDispatcher.stop();

    FUSEDebugF("Mount destroyed: %s",
               path().toPath(false).c_str());
}

void Mount::invalidateAttributes(InodeID id)
{
}

void Mount::invalidateData(InodeID id,
                           m_off_t offset,
                           m_off_t size)
{
}

void Mount::invalidateData(InodeID id)
{
}

void Mount::invalidateEntry(const std::string& name,
                            InodeID child,
                            InodeID parent)
{
}

void Mount::invalidateEntry(const std::string& name,
                            InodeID parent)
{
}

InodeID Mount::map(MountInodeID id) const
{
    return InodeID(id);
}

MountInodeID Mount::map(InodeID id) const
{
    return MountInodeID(id);
}

MountResult Mount::remove()
{
    // Remove the mount from memory.
    mMountDB.remove(*this);

    // Let the caller know the mount's been removed.
    return MOUNT_SUCCESS;
}

} // platform
} // fuse
} // mega

