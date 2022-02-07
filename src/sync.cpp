/**
 * @file sync.cpp
 * @brief Class for synchronizing local and remote trees
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */
#include <cctype>
#include <type_traits>
#include <unordered_set>
#include <future>

#include "mega.h"

#ifdef ENABLE_SYNC
#include "mega/sync.h"
#include "mega/megaapp.h"
#include "mega/transfer.h"
#include "mega/megaclient.h"
#include "mega/base64.h"
#include "mega/heartbeats.h"

namespace mega {

const int Sync::SCANNING_DELAY_DS = 5;
const int Sync::EXTRA_SCANNING_DELAY_DS = 150;
const int Sync::FILE_UPDATE_DELAY_DS = 30;
const int Sync::FILE_UPDATE_MAX_DELAY_SECS = 60;
const dstime Sync::RECENT_VERSION_INTERVAL_SECS = 10800;

#define SYNC_verbose if (syncs.mDetailedSyncLogging) LOG_verbose

std::atomic<size_t> ScanService::mNumServices(0);
std::unique_ptr<ScanService::Worker> ScanService::mWorker;
std::mutex ScanService::mWorkerLock;

ChangeDetectionMethod changeDetectionMethodFromString(const string& method)
{
    static const auto notifications =
      changeDetectionMethodToString(CDM_NOTIFICATIONS);
    static const auto scanning =
      changeDetectionMethodToString(CDM_PERIODIC_SCANNING);

    if (method == notifications)
        return CDM_NOTIFICATIONS;

    if (method == scanning)
        return CDM_PERIODIC_SCANNING;

    return CDM_UNKNOWN;
}

string changeDetectionMethodToString(const ChangeDetectionMethod method)
{
    switch (method)
    {
    case CDM_NOTIFICATIONS:
        return "notifications";
    case CDM_PERIODIC_SCANNING:
        return "scanning";
    default:
        return "unknown";
    }
}

ScanService::ScanService(Waiter& waiter)
  : mWaiter(waiter)
{
    // Locking here, rather than in the if statement, ensures that the
    // worker is fully constructed when control leaves the constructor.
    std::lock_guard<std::mutex> lock(mWorkerLock);

    if (++mNumServices == 1)
    {
        mWorker.reset(new Worker());
    }
}

ScanService::~ScanService()
{
    if (--mNumServices == 0)
    {
        std::lock_guard<std::mutex> lock(mWorkerLock);
        mWorker.reset();
    }
}

auto ScanService::queueScan(LocalPath targetPath, handle expectedFsid, bool followSymlinks, map<LocalPath, FSNode>&& priorScanChildren) -> RequestPtr
{
    // Create a request to represent the scan.
    auto request = std::make_shared<ScanRequest>(mWaiter, followSymlinks, targetPath, expectedFsid, move(priorScanChildren));

    // Queue request for processing.
    mWorker->queue(request);

    return request;
}

ScanService::ScanRequest::ScanRequest(Waiter& waiter,
                                      bool followSymLinks,
                                      LocalPath targetPath,
                                      handle expectedFsid,
                                      map<LocalPath, FSNode>&& priorScanChildren)
  : mWaiter(waiter)
  , mScanResult(SCAN_INPROGRESS)
  , mFollowSymLinks(followSymLinks)
  , mKnown(move(priorScanChildren))
  , mResults()
  , mTargetPath(std::move(targetPath))
  , mExpectedFsid(expectedFsid)
{
}

ScanService::Worker::Worker(size_t numThreads)
  : mFsAccess(new FSACCESS_CLASS())
  , mPending()
  , mPendingLock()
  , mPendingNotifier()
  , mThreads()
{
    // Always at least one thread.
    assert(numThreads > 0);

    LOG_debug << "Starting ScanService worker...";

    // Start the threads.
    while (numThreads--)
    {
        try
        {
            mThreads.emplace_back([this]() { loop(); });
        }
        catch (std::system_error& e)
        {
            LOG_err << "Failed to start worker thread: " << e.what();
        }
    }

    LOG_debug << mThreads.size() << " worker thread(s) started.";
    LOG_debug << "ScanService worker started.";
}

ScanService::Worker::~Worker()
{
    LOG_debug << "Stopping ScanService worker...";

    // Queue the 'terminate' sentinel.
    {
        std::unique_lock<std::mutex> lock(mPendingLock);
        mPending.emplace_back();
    }

    // Wake any sleeping threads.
    mPendingNotifier.notify_all();

    LOG_debug << "Waiting for worker thread(s) to terminate...";

    // Wait for the threads to terminate.
    for (auto& thread : mThreads)
    {
        thread.join();
    }

    LOG_debug << "ScanService worker stopped.";
}

void ScanService::Worker::queue(ScanRequestPtr request)
{
    // Queue the request.
    {
        std::unique_lock<std::mutex> lock(mPendingLock);
        mPending.emplace_back(std::move(request));
    }

    // Tell the lucky thread it has something to do.
    mPendingNotifier.notify_one();
}

void ScanService::Worker::loop()
{
    // We're ready when we have some work to do.
    auto ready = [this]() { return mPending.size(); };

    for ( ; ; )
    {
        ScanRequestPtr request;

        {
            // Wait for something to do.
            std::unique_lock<std::mutex> lock(mPendingLock);
            mPendingNotifier.wait(lock, ready);

            // Are we being told to terminate?
            if (!mPending.front())
            {
                // Bail, don't deque the sentinel.
                return;
            }

            request = std::move(mPending.front());
            mPending.pop_front();
        }

        const auto targetPath =
          request->mTargetPath.toPath();

        LOG_verbose << "Directory scan begins: " << targetPath;
        using namespace std::chrono;
        auto scanStart = high_resolution_clock::now();

        // Process the request.
        unsigned nFingerprinted = 0;
        auto result = scan(request, nFingerprinted);
        auto scanEnd = high_resolution_clock::now();

        if (result == SCAN_SUCCESS)
        {
            LOG_verbose << "Directory scan complete for: " << targetPath
                        << " entries: " << request->mResults.size()
                        << " taking " << duration_cast<milliseconds>(scanEnd - scanStart).count() << "ms"
                        << " fingerprinted: " << nFingerprinted;
        }
        else
        {
            LOG_verbose << "Directory scan FAILED (" << result << "): " << targetPath;
        }

        request->mScanResult = result;
        request->mWaiter.notify();
    }
}

FSNode ScanService::Worker::interrogate(DirAccess& iterator,
                                        const LocalPath& name,
                                        LocalPath& path,
                                        ScanRequest& request,
                                        unsigned& nFingerprinted)
{
    auto reuseFingerprint =
      [](const FSNode& lhs, const FSNode& rhs)
      {
          // it might look like fingerprint.crc comparison has been missed out here
          // but that is intentional.  The point is to avoid re-fingerprinting files
          // when we rescan a folder, if we already have good info about them and
          // nothing we can see from outside the file had changed,
          // mtime is the same, and no notifications arrived
          // for this particular file.
          return lhs.type == rhs.type
                 && lhs.fsid == rhs.fsid
                 && lhs.fingerprint.mtime == rhs.fingerprint.mtime
                 && lhs.fingerprint.size == rhs.fingerprint.size;
      };

    FSNode result;
    auto& known = request.mKnown;

    // Always record the name.
    result.localname = name;
    //result.name = name.toName(*mFsAccess);

    // Can we open the file?
    auto fileAccess = mFsAccess->newfileaccess(false);

    if (fileAccess->fopen(path, true, false, &iterator))
    {
        // Populate result.
        result.fsid = fileAccess->fsidvalid ? fileAccess->fsid : UNDEF;
        result.isSymlink = fileAccess->mIsSymLink;
        result.fingerprint.mtime = fileAccess->mtime;
        result.fingerprint.size = fileAccess->size;
        result.shortname = mFsAccess->fsShortname(path);
        result.type = fileAccess->type;

        if (result.shortname &&
           *result.shortname == result.localname)
        {
            result.shortname.reset();
        }

        // Warn about symlinks.
        if (result.isSymlink)
        {
            LOG_debug << "Interrogated path is a symlink: "
                      << path.toPath();
        }

        // No need to fingerprint directories.
        if (result.type == FOLDERNODE)
        {
            return result;
        }

        // Do we already know about this child?
        auto it = known.find(name);

        // Can we reuse an existing fingerprint?
        if (it != known.end() && reuseFingerprint(it->second, result))
        {
            // Yep as fsid/mtime/size/type match.
            result.fingerprint = std::move(it->second.fingerprint);
        }
        else
        {
            // Child has changed, need a new fingerprint.
            result.fingerprint.genfingerprint(fileAccess.get());
            nFingerprinted += 1;
        }

        return result;
    }

    // Couldn't open the file.
    LOG_warn << "Error opening directory scan entry: " << path.toPath();

    // File's blocked if the error is transient.
    result.isBlocked = fileAccess->retry;

    // Warn about the blocked file.
    if (result.isBlocked)
    {
        LOG_warn << "File/Folder blocked during directory scan: " << path.toPath();
    }

    return result;
}

// Really we only have one worker despite the vector of threads - maybe we should just have one
// regardless of multiple clients too - there is only one filesystem after all (but not singleton!!)
CodeCounter::ScopeStats ScanService::syncScanTime = { "folderScan" };

auto ScanService::Worker::scan(ScanRequestPtr request, unsigned& nFingerprinted) -> ScanResult
{
    CodeCounter::ScopeTimer rst(syncScanTime);

#ifdef WIN32

    auto r = static_cast<WinFileSystemAccess*>(mFsAccess.get())->directoryScan(request->mTargetPath,
            request->mExpectedFsid, request->mKnown, request->mResults, nFingerprinted);

    // No need to keep this data around anymore.
    request->mKnown.clear();

    return r;

#else

    // Have we been passed a valid target path?
    auto fileAccess = mFsAccess->newfileaccess();
    auto path = request->mTargetPath;

    if (!fileAccess->fopen(path, true, false))
    {
        LOG_debug << "Scan target does not exist or is not openable: "
                  << path.toPath();
        return SCAN_INACCESSIBLE;
    }

    // Does the path denote a directory?
    if (fileAccess->type != FOLDERNODE)
    {
        LOG_debug << "Scan target is not a directory: "
                  << path.toPath();
        return SCAN_INACCESSIBLE;
    }

    if (fileAccess->fsid != request->mExpectedFsid)
    {
        LOG_debug << "Scan target at this path has been replaced, fsid is different: "
            << path.toPath();
        return SCAN_FSID_MISMATCH;
    }

    std::unique_ptr<DirAccess> dirAccess(mFsAccess->newdiraccess());
    LocalPath name;

    // Can we open the directory?
    if (!dirAccess->dopen(&path, fileAccess.get(), false))
    {
        LOG_debug << "Scan target is not iteratable: "
                  << path.toPath();
        return SCAN_INACCESSIBLE;
    }

    std::vector<FSNode> results;

    // Process each file in the target.
    while (dirAccess->dnext(path, name, request->mFollowSymLinks))
    {
        ScopedLengthRestore restorer(path);
        path.appendWithSeparator(name, false);

        // Learn everything we can about the file.
        auto info = interrogate(*dirAccess, name, path, *request, nFingerprinted);
        results.emplace_back(std::move(info));
    }

    // No need to keep this data around anymore.
    request->mKnown.clear();

    // Publish the results.
    request->mResults = std::move(results);
    return SCAN_SUCCESS;
#endif
}

ScopedSyncPathRestore::ScopedSyncPathRestore(SyncPath& p)
    : path(p)
    , length1(p.localPath.localpath.size())
    , length2(p.syncPath.size())
    , length3(p.cloudPath.size())
{
}
ScopedSyncPathRestore::~ScopedSyncPathRestore()
{
    path.localPath.localpath.resize(length1);
    path.syncPath.resize(length2);
    path.cloudPath.resize(length3);
};

bool SyncTransferCount::operator==(const SyncTransferCount& rhs) const
{
    return mCompleted == rhs.mCompleted
           && mCompletedBytes == rhs.mCompletedBytes
           && mPending == rhs.mPending
           && mPendingBytes == rhs.mPendingBytes;
}

bool SyncTransferCount::operator!=(const SyncTransferCount& rhs) const
{
    return !(*this == rhs);
}

bool SyncTransferCounts::operator==(const SyncTransferCounts& rhs) const
{
    return mDownloads == rhs.mDownloads && mUploads == rhs.mUploads;
}

bool SyncTransferCounts::operator!=(const SyncTransferCounts& rhs) const
{
    return !(*this == rhs);
}

bool SyncTransferCounts::completed() const
{
    auto pending = mDownloads.mPendingBytes + mUploads.mPendingBytes;

    if (!pending)
        return true;

    auto completed = mDownloads.mCompletedBytes + mUploads.mCompletedBytes;

    return completed >= pending;
}

double SyncTransferCounts::progress() const
{
    auto pending = mDownloads.mPendingBytes + mUploads.mPendingBytes;

    if (!pending)
        return 1.0;

    auto completed = mDownloads.mCompletedBytes + mUploads.mCompletedBytes;
    auto progress = static_cast<double>(completed) / static_cast<double>(pending);

    return std::min(1.0, progress);
}

string SyncPath::localPath_utf8() const
{
    return localPath.toPath();
}

bool SyncPath::appendRowNames(const syncRow& row, FileSystemType filesystemType)
{
    // add to localPath
    if (row.fsNode)
    {
        localPath.appendWithSeparator(row.fsNode->localname, true);
    }
    else if (row.syncNode)
    {
        localPath.appendWithSeparator(row.syncNode->localname, true);
    }
    else if (row.cloudNode)
    {
        // this is the local name used when downsyncing a cloud name, if previously unmatched
        localPath.appendWithSeparator(LocalPath::fromRelativeName(row.cloudNode->name, *syncs.fsaccess, filesystemType), true);
    }
    else if (!row.cloudClashingNames.empty() || !row.fsClashingNames.empty())
    {
        // so as not to mislead in logs etc
        localPath.appendWithSeparator(LocalPath::fromRelativeName("<<<clashing>>>", *syncs.fsaccess, filesystemType), true);
    }
    else
    {
        // this is a legitimate case; eg. we only had a syncNode and it is removed in resolve_delSyncNode
        return false;
    }

    // add to cloudPath
    cloudPath += "/";
    CloudNode cn;
    if (row.cloudNode)
    {
        cloudPath += row.cloudNode->name;
    }
    else if (row.syncNode && syncs.lookupCloudNode(row.syncNode->syncedCloudNodeHandle, cn,
        nullptr, nullptr, nullptr, nullptr, Syncs::LATEST_VERSION))
    {
        cloudPath += cn.name;
    }
    else if (row.syncNode)
    {
        cloudPath += row.syncNode->localname.toName(*syncs.fsaccess);
    }
    else if (row.fsNode)
    {
        cloudPath += row.fsNode->localname.toName(*syncs.fsaccess);
    }
    else if (!row.cloudClashingNames.empty() || !row.fsClashingNames.empty())
    {
        // so as not to mislead in logs etc
        cloudPath += "<<<clashing>>>";
    }
    else
    {
        // this is a legitimate case; eg. we only had a syncNode and it is removed in resolve_delSyncNode
        return false;
    }

    // add to syncPath
    syncPath += "/";
    if (row.cloudNode)
    {
        syncPath += row.cloudNode->name;
    }
    else if (row.syncNode)
    {
        syncPath += row.syncNode->localname.toName(*syncs.fsaccess);
    }
    else if (row.fsNode)
    {
        syncPath += row.fsNode->localname.toName(*syncs.fsaccess);
    }
    else if (!row.cloudClashingNames.empty() || !row.fsClashingNames.empty())
    {
        // so as not to mislead in logs etc
        syncPath += "<<<clashing>>>";
    }
    else
    {
        // this is a legitimate case; eg. we only had a syncNode and it is removed in resolve_delSyncNode
        return false;
    }

    return true;
}

LocalPath SyncThreadsafeState::syncTmpFolder() const
{
    lock_guard<mutex> g(mMutex);
    return mSyncTmpFolder;
}

void SyncThreadsafeState::setSyncTmpFolder(const LocalPath& tmpFolder)
{
    lock_guard<mutex> g(mMutex);
    mSyncTmpFolder = tmpFolder;
}

void SyncThreadsafeState::addExpectedUpload(NodeHandle parentHandle, const string& name, weak_ptr<SyncUpload_inClient> wp)
{
    lock_guard<mutex> g(mMutex);
    mExpectedUploads[toNodeHandle(parentHandle) + ":" + name] = wp;
    LOG_verbose << "Expecting upload-putnode " << (toNodeHandle(parentHandle) + ":" + name);
}

void SyncThreadsafeState::removeExpectedUpload(NodeHandle parentHandle, const string& name)
{
    lock_guard<mutex> g(mMutex);
    mExpectedUploads.erase(toNodeHandle(parentHandle) + ":" + name);
    LOG_verbose << "Unexpecting upload-putnode " << (toNodeHandle(parentHandle) + ":" + name);
}

shared_ptr<SyncUpload_inClient> SyncThreadsafeState::isNodeAnExpectedUpload(NodeHandle parentHandle, const string& name)
{
    lock_guard<mutex> g(mMutex);
    auto it = mExpectedUploads.find(toNodeHandle(parentHandle) + ":" + name);
    return it == mExpectedUploads.end() ? nullptr : it->second.lock();
}


void SyncThreadsafeState::adjustTransferCounts(bool upload, int32_t adjustQueued, int32_t adjustCompleted, m_off_t adjustQueuedBytes, m_off_t adjustCompletedBytes)
{
    lock_guard<mutex> g(mMutex);
    auto& tc = upload ? mTransferCounts.mUploads : mTransferCounts.mDownloads;

    assert(adjustQueued >= 0 || tc.mPending);
    assert(adjustCompleted >= 0 || tc.mCompleted);

    assert(adjustQueuedBytes >= 0 || tc.mPendingBytes);
    assert(adjustCompletedBytes >= 0 || tc.mCompletedBytes);

    tc.mPending += adjustQueued;
    tc.mCompleted += adjustCompleted;
    tc.mPendingBytes += adjustQueuedBytes;
    tc.mCompletedBytes += adjustCompletedBytes;

    if (!tc.mPending && tc.mCompletedBytes == tc.mPendingBytes)
    {
        tc.mCompletedBytes = 0;
        tc.mPendingBytes = 0;
    }
}

void SyncThreadsafeState::transferBegin(direction_t direction, m_off_t numBytes)
{
    adjustTransferCounts(direction == PUT, 1, 0, numBytes, 0);
}

void SyncThreadsafeState::transferComplete(direction_t direction, m_off_t numBytes)
{
    adjustTransferCounts(direction == PUT, -1, 1, 0, numBytes);
}

void SyncThreadsafeState::transferFailed(direction_t direction, m_off_t numBytes)
{
    adjustTransferCounts(direction == PUT, -1, 1, -numBytes, 0);
}

SyncTransferCounts SyncThreadsafeState::transferCounts() const
{
    lock_guard<mutex> guard(mMutex);

    return mTransferCounts;
}

SyncConfig::SyncConfig(LocalPath localPath,
                       std::string name,
                       NodeHandle remoteNode,
                       const std::string &remotePath,
                       const fsfp_t localFingerprint,
                       const LocalPath& externalDrivePath,
                       const bool enabled,
                       const SyncConfig::Type syncType,
                       const SyncError error,
                       const SyncWarning warning,
                       mega::handle hearBeatID)
    : mEnabled(enabled)
    , mLocalPath(std::move(localPath))
    , mName(std::move(name))
    , mRemoteNode(remoteNode)
    , mOriginalPathOfRemoteRootNode(remotePath)
    , mFilesystemFingerprint(localFingerprint)
    , mSyncType(syncType)
    , mError(error)
    , mWarning(warning)
    , mBackupId(hearBeatID)
    , mExternalDrivePath(externalDrivePath)
    , mBackupState(SYNC_BACKUP_NONE)
{}

bool SyncConfig::operator==(const SyncConfig& rhs) const
{
    return mEnabled == rhs.mEnabled
           && mExternalDrivePath == rhs.mExternalDrivePath
           && mLocalPath == rhs.mLocalPath
           && mName == rhs.mName
           && mRemoteNode == rhs.mRemoteNode
           && mOriginalPathOfRemoteRootNode == rhs.mOriginalPathOfRemoteRootNode
           && mFilesystemFingerprint == rhs.mFilesystemFingerprint
           && mSyncType == rhs.mSyncType
           && mError == rhs.mError
           && mBackupId == rhs.mBackupId
           && mWarning == rhs.mWarning
           && mBackupState == rhs.mBackupState;
}

bool SyncConfig::operator!=(const SyncConfig& rhs) const
{
    return !(*this == rhs);
}

bool SyncConfig::getEnabled() const
{
    return mEnabled;
}

void SyncConfig::setEnabled(bool enabled)
{
    mEnabled = enabled;
}

const LocalPath& SyncConfig::getLocalPath() const
{
    return mLocalPath;
}

SyncConfig::Type SyncConfig::getType() const
{
    return mSyncType;
}

bool SyncConfig::isBackup() const
{
    return mSyncType == TYPE_BACKUP;
}

bool SyncConfig::isExternal() const
{
    return !mExternalDrivePath.empty();
}

bool SyncConfig::isInternal() const
{
    return mExternalDrivePath.empty();
}

bool SyncConfig::stateFieldsChanged()
{
    bool changed = mError != mKnownError ||
                   mEnabled != mKnownEnabled ||
                   mKnownRunState != mRunState;

    if (changed)
    {
        mKnownError = mError;
        mKnownEnabled = mEnabled;
        mKnownRunState = mRunState;
    }
    return changed;
}

std::string SyncConfig::syncErrorToStr()
{
    return syncErrorToStr(mError);
}

std::string SyncConfig::syncErrorToStr(SyncError errorCode)
{
    switch(errorCode)
    {
    case NO_SYNC_ERROR:
        return "No error";
    case UNKNOWN_ERROR:
        return "Unknown error";
    case UNSUPPORTED_FILE_SYSTEM:
        return "File system not supported";
    case INVALID_REMOTE_TYPE:
        return "Remote node is not valid";
    case INVALID_LOCAL_TYPE:
        return "Local path is not valid";
    case INITIAL_SCAN_FAILED:
        return "Initial scan failed";
    case LOCAL_PATH_TEMPORARY_UNAVAILABLE:
        return "Local path temporarily unavailable";
    case LOCAL_PATH_UNAVAILABLE:
        return "Local path not available";
    case REMOTE_NODE_NOT_FOUND:
        return "Remote node not found";
    case STORAGE_OVERQUOTA:
        return "Reached storage quota limit";
    case BUSINESS_EXPIRED:
        return "Business account expired";
    case FOREIGN_TARGET_OVERSTORAGE:
        return "Foreign target storage quota reached";
    case REMOTE_PATH_HAS_CHANGED:
        return "Remote path has changed";
    case REMOTE_NODE_MOVED_TO_RUBBISH:
        return "Remote node moved to Rubbish Bin";
    case SHARE_NON_FULL_ACCESS:
        return "Share without full access";
    case LOCAL_FILESYSTEM_MISMATCH:
        return "Local filesystem mismatch";
    case PUT_NODES_ERROR:
        return "Put nodes error";
    case ACTIVE_SYNC_BELOW_PATH:
        return "Active sync below path";
    case ACTIVE_SYNC_ABOVE_PATH:
        return "Active sync above path";
    case REMOTE_PATH_DELETED:
        assert(false);  // obsolete, should not happen
        return "Remote node has been deleted";
    case REMOTE_NODE_INSIDE_RUBBISH:
        return "Remote node is inside Rubbish Bin";
    case VBOXSHAREDFOLDER_UNSUPPORTED:
        return "Unsupported VBoxSharedFolderFS filesystem";
    case LOCAL_PATH_SYNC_COLLISION:
        return "Local path collides with an existing sync";
    case ACCOUNT_BLOCKED:
        return "Your account is blocked";
    case UNKNOWN_TEMPORARY_ERROR:
        return "Unknown temporary error";
    case TOO_MANY_ACTION_PACKETS:
        return "Too many changes in account, local state invalid";
    case LOGGED_OUT:
        return "Session closed";
    case WHOLE_ACCOUNT_REFETCHED:
        return "The whole account was reloaded, missed updates could not have been applied in an orderly fashion";
    case MISSING_PARENT_NODE:
        return "Unable to figure out some node correspondence";
    case BACKUP_MODIFIED:
        return "Backup externally modified";
    case BACKUP_SOURCE_NOT_BELOW_DRIVE:
        return "Backup source path not below drive path.";
    case SYNC_CONFIG_WRITE_FAILURE:
        return "Unable to write sync config to disk.";
    case COULD_NOT_MOVE_CLOUD_NODES:
        return "Unable to move cloud nodes.";
    case COULD_NOT_CREATE_IGNORE_FILE:
        return "Unable to create initial ignore file.";
    case SYNC_CONFIG_READ_FAILURE:
        return "Unable to read sync configs from disk.";
    case UNKNOWN_DRIVE_PATH:
        return "Unknown drive path.";
    case INVALID_SCAN_INTERVAL:
        return "Invalid scan interval specified.";
    case NOTIFICATION_SYSTEM_UNAVAILABLE:
        return "Filesystem notification subsystem unavailable.";
    case UNABLE_TO_ADD_WATCH:
        return "Unable to add filesystem watch.";
    case UNABLE_TO_RETRIEVE_ROOT_FSID:
        return "Unable to retrieve sync root FSID.";
    default:
        return "Undefined error";
    }
}

void SyncConfig::setBackupState(SyncBackupState state)
{
    assert(isBackup());

    mBackupState = state;
}

SyncBackupState SyncConfig::getBackupState() const
{
    return mBackupState;
}

const char* SyncConfig::synctypename(const SyncConfig::Type type)
{
    switch (type)
    {
    case SyncConfig::TYPE_BACKUP:
        return "BACKUP";
    case SyncConfig::TYPE_DOWN:
        return "DOWN";
    case SyncConfig::TYPE_UP:
        return "UP";
    case SyncConfig::TYPE_TWOWAY:
        return "TWOWAY";
    default:
        return "UNKNOWN";
    }
}

bool SyncConfig::synctypefromname(const string& name, Type& type)
{
    if (name == "BACKUP")
    {
        return type = TYPE_BACKUP, true;
    }
    if (name == "DOWN")
    {
        return type = TYPE_DOWN, true;
    }
    else if (name == "UP")
    {
        return type = TYPE_UP, true;
    }
    else if (name == "TWOWAY")
    {
        return type = TYPE_TWOWAY, true;
    }

    assert(!"Unknown sync type name.");

    return false;
}

SyncError SyncConfig::knownError() const
{
    return mKnownError;
}

bool SyncConfig::isScanOnly() const
{
    return mChangeDetectionMethod == CDM_PERIODIC_SCANNING;
}

string SyncConfig::getSyncDbStateCacheName(handle fsid, NodeHandle nh, handle userId) const
{
    handle tableid[3];
    tableid[0] = fsid;
    tableid[1] = nh.as8byte();
    tableid[2] = userId;

    string dbname;
    dbname.resize(sizeof tableid * 4 / 3 + 3);
    dbname.resize(Base64::btoa((byte*)tableid, sizeof tableid, (char*)dbname.c_str()));
    return dbname;
}

// new Syncs are automatically inserted into the session's syncs list
// and a full read of the subtree is initiated
Sync::Sync(UnifiedSync& us, const string& cdebris,
           const LocalPath& clocaldebris, bool cinshare, const string& logname)
: syncs(us.syncs)
, localroot(nullptr)
, mUnifiedSync(us)
, syncscanbt(us.syncs.rng)
, threadSafeState(new SyncThreadsafeState(us.mConfig.mBackupId, &syncs.mClient))
{
    assert(syncs.onSyncThread());
    assert(cdebris.empty() || clocaldebris.empty());
    assert(!cdebris.empty() || !clocaldebris.empty());

    localroot.reset(new LocalNode(this));

    const SyncConfig& config = us.mConfig;

    syncs.lookupCloudNode(config.mRemoteNode, cloudRoot, &cloudRootPath, nullptr, nullptr, nullptr, Syncs::FOLDER_ONLY);

    isnetwork = false;
    inshare = cinshare;
    tmpfa = NULL;
    syncname = logname; // can be updated to be more specific in logs
    //initializing = true;

    localnodes[FILENODE] = 0;
    localnodes[FOLDERNODE] = 0;

    mUnifiedSync.mConfig.mRunState = SyncRunState::Loading;

    mLocalPath = mUnifiedSync.mConfig.getLocalPath();

    mFilesystemType = syncs.fsaccess->getlocalfstype(mLocalPath);

    localroot->init(FOLDERNODE, NULL, mLocalPath, nullptr);  // the root node must have the absolute path.  We don't store shortname, to avoid accidentally using relative paths.
    localroot->setSyncedNodeHandle(config.mRemoteNode);
    localroot->setScanAgain(false, true, true, 0);
    localroot->setCheckMovesAgain(false, true, true);
    localroot->setSyncAgain(false, true, true);

    if (!cdebris.empty())
    {
        debris = cdebris;
        localdebrisname = LocalPath::fromRelativePath(debris);
        localdebris = localdebrisname;
        localdebris.prependWithSeparator(mLocalPath);
    }
    else
    {
        localdebrisname = clocaldebris.leafName();
        localdebris = clocaldebris;
    }

    // Should this sync make use of filesystem notifications?
    if (us.mConfig.mChangeDetectionMethod == CDM_NOTIFICATIONS)
    {
        // Notifications may be queueing from this moment
        dirnotify.reset(syncs.fsaccess->newdirnotify(*localroot, mLocalPath, &syncs.waiter));
    }

    // set specified fsfp or get from fs if none
    const auto cfsfp = mUnifiedSync.mConfig.mFilesystemFingerprint;
    if (cfsfp)
    {
        fsfp = cfsfp;
    }
    else
    {
        fsfp = syncs.fsaccess->fsFingerprint(mLocalPath);
    }

    fsstableids = syncs.fsaccess->fsStableIDs(mLocalPath);
    LOG_info << "Filesystem IDs are stable: " << fsstableids;


    auto fas = syncs.fsaccess->newfileaccess(false);

    if (fas->fopen(mLocalPath, true, false))
    {
        // get the fsid of the synced folder
        localroot->fsid_lastSynced = fas->fsid;

        // load LocalNodes from cache (only for internal syncs)
        // We are using SQLite in the no-mutex mode, so only access a database from a single thread.
        if (syncs.mClient.dbaccess && !us.mConfig.isExternal())
        {
            string dbname = config.getSyncDbStateCacheName(fas->fsid, config.mRemoteNode, syncs.mClient.me);

            // Note, we opened dbaccess in thread-safe mode
            statecachetable.reset(syncs.mClient.dbaccess->open(syncs.rng, *syncs.fsaccess, dbname, DB_OPEN_FLAG_TRANSACTED));
            us.mConfig.mDatabaseExists = true;

            readstatecache();
        }
    }
    us.mConfig.mRunState = us.mConfig.mTemporarilyPaused ? SyncRunState::Pause : SyncRunState::Run;
}

Sync::~Sync()
{
    assert(syncs.onSyncThread());

    // must be set to prevent remote mass deletion while rootlocal destructor runs
    mDestructorRunning = true;
    mUnifiedSync.mConfig.mRunState = mUnifiedSync.mConfig.mDatabaseExists ? SyncRunState::Suspend : SyncRunState::Disable;

    // unlock tmp lock
    tmpfa.reset();

    // Deleting localnodes after this will not remove them from the db.
    statecachetable.reset();

    // This will recursively delete all LocalNodes in the sync.
    // If they have transfers associated, the SyncUpload_inClient and <tbd> will have their wasRequesterAbandoned flag set true
    localroot.reset();
}

//bool Sync::backupModified()
//{
//    assert(syncs.onSyncThread());
//    changestate(SYNC_DISABLED, BACKUP_MODIFIED, false, true);
//    return false;
//}

bool Sync::isBackup() const
{
    assert(syncs.onSyncThread());
    return getConfig().isBackup();
}

bool Sync::isBackupAndMirroring() const
{
    assert(syncs.onSyncThread());
    return isBackup() &&
           getConfig().getBackupState() == SYNC_BACKUP_MIRROR;
}

bool Sync::isBackupMonitoring() const
{
    // only called from tests
    assert(!syncs.onSyncThread());
    return getConfig().getBackupState() == SYNC_BACKUP_MONITOR;
}

void Sync::setBackupMonitoring()
{
    assert(syncs.onSyncThread());
    auto& config = getConfig();

    assert(config.getBackupState() == SYNC_BACKUP_MIRROR);

    LOG_verbose << "Sync "
                << toHandle(config.mBackupId)
                << " transitioning to monitoring mode.";

    config.setBackupState(SYNC_BACKUP_MONITOR);

    syncs.saveSyncConfig(config);
}

SyncTransferCounts Sync::transferCounts() const
{
    return threadSafeState->transferCounts();
}

void Sync::setSyncPaused(bool pause)
{
    assert(syncs.onSyncThread());

    getConfig().mTemporarilyPaused = pause;
    syncs.mSyncFlags->isInitialPass = true;
}

bool Sync::isSyncPaused() {
    assert(syncs.onSyncThread());

    return getConfig().mTemporarilyPaused;
}

void Sync::addstatecachechildren(uint32_t parent_dbid, idlocalnode_map* tmap, LocalPath& localpath, LocalNode *p, int maxdepth)
{
    assert(syncs.onSyncThread());

    auto range = tmap->equal_range(parent_dbid);

    for (auto it = range.first; it != range.second; it++)
    {
        ScopedLengthRestore restoreLen(localpath);

        localpath.appendWithSeparator(it->second->localname, true);

        LocalNode* l = it->second;
        handle fsid = l->fsid_lastSynced;
        m_off_t size = l->syncedFingerprint.size;

        // clear localname to force newnode = true in setnameparent
        l->localname.clear();

        // if we already have the shortname from database, use that, otherwise (db is from old code) look it up
        std::unique_ptr<LocalPath> shortname;
        if (l->slocalname_in_db)
        {
            // null if there is no shortname, or the shortname matches the localname.
            shortname.reset(l->slocalname.release());
        }
        else
        {
            shortname = syncs.fsaccess->fsShortname(localpath);
        }

        l->init(l->type, p, localpath, nullptr);

        l->syncedFingerprint.size = size;
        l->setSyncedFsid(fsid, syncs.localnodeBySyncedFsid, l->localname, std::move(shortname));
        l->setSyncedNodeHandle(l->syncedCloudNodeHandle);
        l->oneTimeUseSyncedFingerprintInScan = true;

        if (!l->slocalname_in_db)
        {
            statecacheadd(l);
            if (insertq.size() > 50000)
            {
                DBTableTransactionCommitter committer(statecachetable);
                cachenodes();  // periodically output updated nodes with shortname updates, so people who restart megasync still make progress towards a fast startup
            }
        }

        if (maxdepth)
        {
            addstatecachechildren(l->dbid, tmap, localpath, l, maxdepth - 1);
        }
    }
}

void Sync::readstatecache()
{
    assert(syncs.onSyncThread());

    string cachedata;
    idlocalnode_map tmap;
    uint32_t cid;
    LocalNode* l;

    LOG_debug << syncname << "Sync " << toHandle(getConfig().mBackupId) << " about to load from db";

    statecachetable->rewind();
    unsigned numLocalNodes = 0;

    // bulk-load cached nodes into tmap
    assert(!memcmp(syncs.syncKey.key, syncs.mClient.key.key, sizeof(syncs.syncKey.key)));
    while (statecachetable->next(&cid, &cachedata, &syncs.syncKey))
    {
        uint32_t parentID = 0;

        if ((l = LocalNode::unserialize(*this, cachedata, parentID).release()))
        {
            l->dbid = cid;
            tmap.emplace(parentID, l);
            numLocalNodes += 1;
        }
    }

    // recursively build LocalNode tree
    LocalPath pathBuffer = localroot->localname; // don't let localname be appended during recurse
    addstatecachechildren(0, &tmap, pathBuffer, localroot.get(), 100);
    cachenodes();

    LOG_debug << syncname << "Sync " << toHandle(getConfig().mBackupId) << " loaded from db with " << numLocalNodes << " sync nodes";

    localroot->setScanAgain(false, true, true, 0);
}

SyncConfig& Sync::getConfig()
{
    return mUnifiedSync.mConfig;
}

const SyncConfig& Sync::getConfig() const
{
    return mUnifiedSync.mConfig;
}

// remove LocalNode from DB cache
void Sync::statecachedel(LocalNode* l)
{
    assert(syncs.onSyncThread());

    if (!statecachetable)
    {
        return;
    }

    if (l->dbid && statecachetable)
    {
        statecachetable->del(l->dbid);
    }
    l->dbid = 0;

    insertq.erase(l);
}

// insert LocalNode into DB cache
void Sync::statecacheadd(LocalNode* l)
{
    assert(syncs.onSyncThread());

    if (!statecachetable)
    {
        return;
    }

    insertq.insert(l);
}

void Sync::cachenodes()
{
    assert(syncs.onSyncThread());

    // Purge the queues if we have no state cache.
    if (!statecachetable)
    {
        insertq.clear();
        return;
    }

    if (insertq.size())
    {
        LOG_debug << syncname << "Saving LocalNode database with " << insertq.size() << " additions";

        DBTableTransactionCommitter committer(statecachetable);

        // additions - we iterate until completion or until we get stuck
        bool added;

        do {
            added = false;

            for (set<LocalNode*>::iterator it = insertq.begin(); it != insertq.end(); )
            {
                if ((*it)->type == TYPE_UNKNOWN)
                {
                    SYNC_verbose << syncname << "Leaving unknown type node out of DB, (likely scan blocked): " << (*it)->getLocalPath().toPath();
                    insertq.erase(it++);
                }
                else if ((*it)->parent->dbid || (*it)->parent == localroot.get())
                {
                    // add once we know the parent dbid so that the parent/child structure is correct in db
                    assert(!memcmp(syncs.syncKey.key, syncs.mClient.key.key, sizeof(syncs.syncKey.key)));
                    statecachetable->put(MegaClient::CACHEDLOCALNODE, *it, &syncs.syncKey);
                    insertq.erase(it++);
                    added = true;
                }
                else it++;
            }
        } while (added);

        if (insertq.size())
        {
            LOG_err << "LocalNode caching did not complete";
            assert(false);
        }
    }
}

void Sync::changestate(SyncError newSyncError, bool newEnableFlag, bool notifyApp, bool keepSyncDb)
{
    mUnifiedSync.changeState(newSyncError, newEnableFlag, notifyApp, keepSyncDb);
}

void UnifiedSync::changeState(SyncError newSyncError, bool newEnableFlag, bool notifyApp, bool keepSyncDb)
{
    assert(syncs.onSyncThread());

    // External backups should not auto-start
    newEnableFlag &= mConfig.isInternal();

    assert(!(newSyncError == DECONFIGURING_SYNC && keepSyncDb));

    if (!keepSyncDb)
    {
        if (mSync && mSync->statecachetable)
        {
            // make sure db is up to date before we close it.
            mSync->cachenodes();

            // remove the LocalNode database files on sync disablement (historic behaviour; sync re-enable with LocalNode state from non-matching SCSN is not supported (yet))
            mSync->statecachetable->remove();
            mSync->statecachetable.reset();
        }
        else
        {
            // delete the database file directly since we don't have an object for it
            auto fas = syncs.fsaccess->newfileaccess(false);
            if (fas->fopen(mConfig.mLocalPath, true, false))
            {
                string dbname = mConfig.getSyncDbStateCacheName(fas->fsid, mConfig.mRemoteNode, syncs.mClient.me);

                LocalPath dbPath;
                syncs.mClient.dbaccess->checkDbFileAndAdjustLegacy(*syncs.fsaccess, dbname, DB_OPEN_FLAG_TRANSACTED, dbPath);

                LOG_debug << "Deleting sync database at: " << dbPath;
                syncs.fsaccess->unlinklocal(dbPath);
            }
        }
        mConfig.mDatabaseExists = false;
    }

    if (newSyncError != NO_SYNC_ERROR && mSync && mSync->statecachetable)
    {
        // if we are keeping the db and unloading the sync,
        // prevent any more changes to it from this point
        mSync->cachenodes();
        mSync->statecachetable.reset();
    }

    mConfig.mError = newSyncError;
    mConfig.setEnabled(newEnableFlag);

    if (newSyncError)
    {
        mConfig.mRunState = mConfig.mDatabaseExists ? SyncRunState::Suspend : SyncRunState::Disable;
    }

    if (newSyncError != DECONFIGURING_SYNC &&
        newSyncError != UNLOADING_SYNC)
    {
        changedConfigState(true, notifyApp);
        mNextHeartbeat->updateSPHBStatus(*this);
    }
}

// walk localpath and return corresponding LocalNode and its parent
// localpath must be relative to l or start with the root prefix if l == NULL
// localpath must be a full sync path, i.e. start with localroot->localname
// NULL: no match, optionally returns residual path
LocalNode* Sync::localnodebypath(LocalNode* l, const LocalPath& localpath, LocalNode** parent, LocalPath* outpath, bool fromOutsideThreadAlreadyLocked)
{
    assert(syncs.onSyncThread() || fromOutsideThreadAlreadyLocked);
    assert(!outpath || outpath->empty());

    size_t subpathIndex = 0;

    if (!l)
    {
        // verify matching localroot prefix - this should always succeed for
        // internal use
        if (!localroot->localname.isContainingPathOf(localpath, &subpathIndex))
        {
            if (parent)
            {
                *parent = NULL;
            }

            return NULL;
        }

        l = localroot.get();
    }

    if (localpath.empty())
    {
        if (outpath) outpath->clear();
        if (parent) *parent = l->parent;
        return l;
    }

    LocalPath component;

    while (localpath.nextPathComponent(subpathIndex, component))
    {
        if (parent)
        {
            *parent = l;
        }

        localnode_map::iterator it;
        if ((it = l->children.find(&component)) == l->children.end()
            && (it = l->schildren.find(&component)) == l->schildren.end())
        {
            // no full match: store residual path, return NULL with the
            // matching component LocalNode in parent
            if (outpath)
            {
                *outpath = std::move(component);
                auto remainder = localpath.subpathFrom(subpathIndex);
                if (!remainder.empty())
                {
                    outpath->appendWithSeparator(remainder, false);
                }
            }

            return NULL;
        }

        l = it->second;
    }

    // full match: no residual path, return corresponding LocalNode
    if (outpath)
    {
        outpath->clear();
    }
    return l;
}

void Sync::createDebrisTmpLockOnce()
{
    assert(syncs.onSyncThread());

    if (!tmpfa)
    {
        tmpfa = syncs.fsaccess->newfileaccess();

        int i = 3;
        while (i--)
        {

            LocalPath localfilename = localdebris;
            if (syncs.fsaccess->mkdirlocal(localfilename, true, false))
            {
                LOG_verbose << syncname << "Created local sync debris folder";
            }

            LocalPath tmpname = LocalPath::fromRelativePath("tmp");
            localfilename.appendWithSeparator(tmpname, true);
            if (syncs.fsaccess->mkdirlocal(localfilename, false, false))
            {
                LOG_verbose << syncname << "Created local sync debris tmp folder";
            }

            tmpfaPath = localfilename;

            // lock it
            LocalPath lockname = LocalPath::fromRelativePath("lock");
            localfilename.appendWithSeparator(lockname, true);

            if (tmpfa->fopen(localfilename, false, true))
            {
                LOG_verbose << syncname << "Locked local sync debris tmp lock file";
                break;
            }
        }

        // if we failed to create the tmp dir three times in a row, fall
        // back to the sync's root
        if (i < 0)
        {
            tmpfa.reset();
            tmpfaPath = getConfig().mLocalPath;
        }

        threadSafeState->setSyncTmpFolder(tmpfaPath);
    }
}

bool SyncStallInfo::empty() const
{
    return cloud.empty() && local.empty();
}

bool SyncStallInfo::waitingCloud(const string& cloudPath1,
                                 const string& cloudPath2,
                                 const LocalPath& localPath,
                                 SyncWaitReason reason)
{
    for (auto i = cloud.begin(); i != cloud.end(); )
    {
        // No need to add a new entry as we've already reported some parent.
        if (IsContainingCloudPathOf(i->first, cloudPath1) && reason == i->second.reason)
            return false;

        // Remove entries that are below cloudPath1.
        if (IsContainingCloudPathOf(cloudPath1, i->first) && reason == i->second.reason)
        {
            i = cloud.erase(i);
            continue;
        }

        // Check the next entry.
        ++i;
    }

    // Add a new entry.
    auto& entry = cloud[cloudPath1];

    entry.involvedCloudPath = cloudPath2;
    entry.involvedLocalPath = localPath;
    entry.reason = reason;

    return true;
}

bool SyncStallInfo::waitingLocal(const LocalPath& localPath1,
                                 const LocalPath& localPath2,
                                 const string& cloudPath,
                                 SyncWaitReason reason)
{
    for (auto i = local.begin(); i != local.end(); )
    {
        if (i->first.isContainingPathOf(localPath1) && reason == i->second.reason)
            return false;

        if (localPath1.isContainingPathOf(i->first) && reason == i->second.reason)
        {
            i = local.erase(i);
            continue;
        }

        ++i;
    }

    auto& entry = local[localPath1];

    entry.involvedCloudPath = cloudPath;
    entry.involvedLocalPath = localPath2;
    entry.reason = reason;

    return true;
}

bool SyncStallInfo::hasImmediateStallReason() const
{
    for (auto& i : cloud)
    {
        if (syncWaitReasonAlwaysNeedsUserIntervention(i.second.reason))
        {
            return true;
        }
    }
    for (auto& i : local)
    {
        if (syncWaitReasonAlwaysNeedsUserIntervention(i.second.reason))
        {
            return true;
        }
    }
    return false;
}

struct ProgressingMonitor
{
    bool resolved = false;
    SyncFlags& sf;
    ProgressingMonitor(Syncs& syncs) : sf(*syncs.mSyncFlags) {}

    bool isContainingNodePath(const string& a, const string& b)
    {
        return a.size() <= b.size() &&
            !memcmp(a.c_str(), b.c_str(), a.size()) &&
            (a.size() == b.size() || b[a.size()] == '/');
    }

    void waitingCloud(const string& cloudPath, const string& cloudPath2, const LocalPath& localpath, SyncWaitReason r)
    {
        // the caller has a path in the cloud that an operation is in progress for, or can't be dealt with yet.
        // update our list of subtree roots containing such paths
        resolved = true;

        sf.stall.waitingCloud(cloudPath, cloudPath2, localpath, r);
    }

    void waitingLocal(const LocalPath& localPath, const LocalPath& localPath2, const string& cloudPath, SyncWaitReason r)
    {
        // the caller has a local path that an operation is in progress for, or can't be dealt with yet.
        // update our list of subtree roots containing such paths
        resolved = true;

        sf.stall.waitingLocal(localPath, localPath2, cloudPath, r);
    }

    void noResult()
    {
        // call this if we are still waiting but for something certain to complete
        // and for which we don't need to report a path
        resolved = true;
    }

    // For brevity in programming, if none of the above occurred,
    // the destructor records that we are progressing (ie, not stalled).
    ~ProgressingMonitor()
    {
        if (!resolved)
        {
            sf.noProgress = false;
            sf.noProgressCount = 0;
        }
    }
};


bool Sync::checkLocalPathForMovesRenames(syncRow& row, syncRow& parentRow, SyncPath& fullPath, bool& rowResult, bool belowRemovedCloudNode)
{
    // We have detected that this LocalNode might be a move/rename target (the moved-to location).
    // Ie, there is a new/different FSItem in this row.
    // This function detects whether this is in fact a move or not, and takes care of performing the corresponding cloud move
    // If we do determine it's a move, then we perform the corresponding move in the cloud.
    // Of course that's not instant, so we need to wait until the move completes (or fails)
    // In order to keep track of that, we put a shared_ptr to the move-tracking object into this LocalNode's rare fields.
    // The client thread actually performing the move will update flags in that object, or it might release its shared_ptr to the move object.
    // In the meantime this thread can continue recursing and iterate over the tree multiple times until the move is resolved.
    // We don't recurse below the moved-from or moved-to node in the meantime though as that would cause incorrect decisions to be made.
    // (well we do but in a very limited capacity, only checking if the cloud side has new nodes to detect crossed-over moves)
    // If the move/rename is successful, then on one of those iterations we will detect it as a synced row in
    // resolve_rowMatched.   That function will update our data structures, moving the sub-LocalNodes from the
    // moved-from LocalNode to the moved-to LocalNode.  Later the moved-from LocalNode will be removed as it has no FSNode or CloudNode.
    // So yes that means we do briefly have two LocalNodes for a the single moved file/folder while the move goes on.
    // That's convenient algorithmicaly for tracking the move, and also it's not safe to delete the old node early
    // as it might have come from a parent folder, which in data structures in the recursion stack are referring to.
    // If the move/rename fails, it's likely because the move is no longer appropriate, eg new parent folder
    // is missing.  In that case, we clean up the data structure and let the algorithm make a new choice.

    assert(syncs.onSyncThread());

    // no cloudNode at this row.  Check if this node is where a filesystem item moved to.

    if (row.fsNode->isSymlink)
    {
        LOG_debug << syncname << "checked path is a symlink, blocked: " << fullPath.localPath_utf8();

        ProgressingMonitor monitor(syncs);
        monitor.waitingLocal(fullPath.localPath, LocalPath(), string(), SyncWaitReason::SymlinksNotSupported);

        rowResult = false;
        return true;
    }
    else if (row.syncNode && row.syncNode->type != row.fsNode->type)
    {
        LOG_debug << syncname << "checked path does not have the same type, blocked: " << fullPath.localPath_utf8();

        ProgressingMonitor monitor(syncs);
        monitor.waitingLocal(fullPath.localPath, LocalPath(), string(), SyncWaitReason::FolderMatchedAgainstFile);

        rowResult = false;
        return true;
    }
    else
    {
        if (auto* s = row.syncNode)
        {
            // Do w have any rare fields?
            if (s->hasRare())
            {
                // Move is (was) in progress?
                if (auto& moveToHere = s->rare().moveToHere)
                {
                    if (moveToHere->failed)
                    {
                        // Move's failed. Try again.
                        moveToHere.reset();
                    }
                    else
                    {
                        // Move's in progress.
                        //
                        // Revisit when the move is complete.
                        // In the mean time, don't recurse below this node.
                        row.suppressRecursion = true;
                        rowResult = false;

                        // When false, we can visit resolve_rowMatched(...).
                        return !moveToHere->succeeded;
                    }
                }

                // Unlink in progress?
                if (!s->rare().unlinkHere.expired())
                {
                    // Don't recurse into our children.
                    row.suppressRecursion = true;

                    // Row isn't synced.
                    rowResult = false;

                    // Move isn't complete.
                    return true;
                }
            }
        }

        // we already checked fsid differs before calling

        // This line can be useful to uncomment for debugging, but it does add a lot to the log.
        //SYNC_verbose << "Is this a local move destination, by fsid " << toHandle(row.fsNode->fsid) << " at " << logTriplet(row, fullPath);

        // was the file overwritten by moving an existing file over it?
        if (LocalNode* sourceSyncNode = syncs.findLocalNodeBySyncedFsid(row.fsNode->fsid, row.fsNode->type, row.fsNode->fingerprint, this, nullptr))   // todo: maybe null for sync* to detect moves between sync?
        {
            assert(parentRow.syncNode);
            ProgressingMonitor monitor(syncs);

            // Are we moving an ignore file?
            if (row.isIgnoreFile() || sourceSyncNode->isIgnoreFile())
            {
                // Then it's not subject to move processing.
                return false;
            }

            // Is the move target excluded?
            if (parentRow.exclusionState(*row.fsNode) != ES_INCLUDED)
            {
                // Then don't perform the move.
                return false;
            }

            auto markSiblingSourceRow = [&]() {
                if (!row.rowSiblings)
                    return;

                if (sourceSyncNode->parent != parentRow.syncNode)
                    return;

                for (auto& sibling : *row.rowSiblings)
                {
                    if (sibling.syncNode == sourceSyncNode)
                    {
                        sibling.itemProcessed = true;
                        sibling.syncNode->setSyncAgain(true, false, false);
                        return;
                    }
                }
            };

            // It turns out that after LocalPath implicitly uses \\?\ and
            // absolute paths, we don't need to worry about reserved names.

            //// Does the move target have a reserved name?
            //if (row.hasReservedName(*syncs.fsaccess))
            //{
            //    // Make sure we don't try and synchronize the source.
            //    // This is meaningful for straight renames.
            //    markSiblingSourceRow();

            //    // Let the engine know why we can't action the move.
            //    monitor.waitingLocal(sourceSyncNode->getLocalPath(),
            //                         fullPath.localPath,
            //                         fullPath.cloudPath,
            //                         SyncWaitReason::MoveTargetHasReservedName);

            //    // Let the engine know that we've detected a move.
            //    parentRow.syncNode->setCheckMovesAgain(false, true, false);

            //    // Don't recurse if the row's a directory.
            //    row.suppressRecursion = true;

            //    // Row isn't synchronized.
            //    rowResult = false;

            //    // Escape immediately from syncItemCheckMove(...).
            //    return true;
            //}

            if (!row.syncNode)
            {
                resolve_makeSyncNode_fromFS(row, parentRow, fullPath, false);
                assert(row.syncNode);
            }

            row.syncNode->setCheckMovesAgain(true, false, false);

            // Is the source's exclusion state well-defined?
            if (sourceSyncNode->exclusionState() == ES_UNKNOWN)
            {
                // Let the engine know why we can't perform the move.
                monitor.waitingLocal(sourceSyncNode->getLocalPath(),
                                     LocalPath(),
                                     string(),
                                     SyncWaitReason::UnknownExclusionState);


                // In some cases the move source may be below the target.
                //
                // This flag is necessary so that we continue to descend down
                // from the move target to the source such that we recompute
                // the source's exclusion state.
                //
                // TODO: Should we only set this flag if the source is below
                //       the target?
                row.recurseBelowRemovedFsNode = true;
                row.suppressRecursion = true;

                // Attempt the move later.
                return rowResult = false, true;
            }

            // Sanity.
            assert(sourceSyncNode->exclusionState() == ES_INCLUDED);

            // logic to detect files being updated in the local computer moving the original file
            // to another location as a temporary backup
            if (sourceSyncNode->type == FILENODE &&
                checkIfFileIsChanging(*row.fsNode, sourceSyncNode->getLocalPath()))
            {
                // Make sure we don't sync our sibling until the move is complete.
                markSiblingSourceRow();

                // if we revist here and the file is still the same after enough time, we'll move it
                monitor.waitingLocal(sourceSyncNode->getLocalPath(), LocalPath(), string(), SyncWaitReason::WatiingForFileToStopChanging);
                rowResult = false;
                return true;
            }

            // Is there something in the way at the move destination?
            string nameOverwritten;
            if (row.cloudNode)
            {
                SYNC_verbose << syncname << "Move detected by fsid " << toHandle(row.fsNode->fsid) << " but something else with that name (" << row.cloudNode->name << ") is already here in the cloud. Type: " << row.cloudNode->type
                    << " new path: " << fullPath.localPath_utf8()
                    << " old localnode: " << sourceSyncNode->getLocalPath()
                    << logTriplet(row, fullPath);

                // but, is it ok to overwrite that thing?  If that's what happened locally to a synced file, and the cloud item was also synced and is still there, then it's legit
                // overwriting a folder like that is not possible so far as I know
                // Note that the original algorithm would overwrite a file or folder, moving the old one to cloud debris
                bool legitOverwrite = //row.syncNode->type == FILENODE &&
                                      //row.syncNode->fsid_lastSynced != UNDEF &&
                                      row.syncNode->syncedCloudNodeHandle == row.cloudNode->handle;

                if (legitOverwrite)
                {
                    SYNC_verbose << syncname << "Move is a legit overwrite of a synced file/folder, so we overwrite that in the cloud also." << logTriplet(row, fullPath);
                    nameOverwritten = row.cloudNode->name;
                    // todo: record the old one as prior version?
                }
                else
                {
                    row.syncNode->setCheckMovesAgain(false, true, false);
                    monitor.waitingLocal(fullPath.localPath, sourceSyncNode->getLocalPath(), fullPath.cloudPath, SyncWaitReason::ApplyMoveIsBlockedByExistingItem);
                    rowResult = false;
                    return true;
                }
            }

            row.suppressRecursion = true;   // wait until we have moved the other LocalNodes below this one

            // we don't want the source LocalNode to be visited until the move completes
            // because it might see a new file with the same name, and start an
            // upload attached to that LocalNode (which would create a wrong version chain in the account)
            // TODO: consider alternative of preventing version on upload completion - probably resulting in much more complicated name matching though
            markSiblingSourceRow();

            // Although we have detected a move locally, there's no guarantee the cloud side
            // is complete, the corresponding parent folders in the cloud may not match yet.
            // ie, either of sourceCloudNode or targetCloudNode could be null here.
            // So, we have a heirarchy of statuses:
            //    If any scan flags anywhere are set then we can't be sure a missing fs node isn't a move
            //    After all scanning is done, we need one clean tree traversal with no moves detected
            //    before we can be sure we can remove nodes or upload/download.
            CloudNode sourceCloudNode, targetCloudNode;
            string sourceCloudNodePath, targetCloudNodePath;
            bool foundSourceCloudNode = syncs.lookupCloudNode(sourceSyncNode->syncedCloudNodeHandle, sourceCloudNode, &sourceCloudNodePath, nullptr, nullptr, nullptr, Syncs::LATEST_VERSION);
            bool foundTargetCloudNode = syncs.lookupCloudNode(parentRow.syncNode->syncedCloudNodeHandle, targetCloudNode, &targetCloudNodePath, nullptr, nullptr, nullptr, Syncs::FOLDER_ONLY);

            if (foundSourceCloudNode && foundTargetCloudNode)
            {
                LOG_debug << syncname << "Move detected by fsid " << toHandle(row.fsNode->fsid) << ". Type: " << sourceSyncNode->type
                    << " new path: " << fullPath.localPath_utf8()
                    << " old localnode: " << sourceSyncNode->getLocalPath()
                    << logTriplet(row, fullPath);

                if (belowRemovedCloudNode)
                {
                    LOG_debug << syncname << "Move destination detected for fsid " << toHandle(row.fsNode->fsid) << " but we are belowRemovedCloudNode, must wait for resolution at: " << fullPath.cloudPath << logTriplet(row, fullPath);;
                    monitor.waitingLocal(fullPath.localPath, sourceSyncNode->getLocalPath(), fullPath.cloudPath, SyncWaitReason::ApplyMoveNeedsOtherSideParentFolderToExist);
                    row.syncNode->setSyncAgain(true, false, false);
                }
                else
                {
                    // movePtr stays alive until the move completes
                    // if it's all successful, we will detect the completed move in resolve_rowMatches
                    // and the details from this shared_ptr will help move sub-LocalNodes.
                    // In the meantime, the shared_ptr reminds us not to start another move
                    auto movePtr = std::make_shared<LocalNode::RareFields::MoveInProgress>();


                    Syncs::QueuedClientFunc simultaneousMoveReplacedNodeToDebris = nullptr;

                    if (row.cloudNode && row.cloudNode->handle != sourceCloudNode.handle)
                    {
                        LOG_debug << syncname << "Moving node to debris for replacement: " << fullPath.cloudPath << logTriplet(row, fullPath);

                        auto deletePtr = std::make_shared<LocalNode::RareFields::DeleteToDebrisInProgress>();
                        sourceSyncNode->rare().removeNodeHere = deletePtr;

                        bool inshareFlag = inshare;
                        auto deleteHandle = row.cloudNode->handle;
                        simultaneousMoveReplacedNodeToDebris = [deleteHandle, inshareFlag, deletePtr](MegaClient& mc, DBTableTransactionCommitter& committer)
                            {
                                if (auto n = mc.nodeByHandle(deleteHandle))
                                {
                                    mc.movetosyncdebris(n, inshareFlag, nullptr);
                                }

                                // deletePtr lives until this moment
                            };

                        syncs.queueClient(move(simultaneousMoveReplacedNodeToDebris));

                        // For the normal move case, we would have made this (empty) row.syncNode specifically for the move
                        // But for this case we are reusing this existing LocalNode and it may be a folder with children
                        // Those children should be removed, should this whole operation succeed.  Make a list
                        // and remove them if the cloud actions succeed.
                        for (auto& c : row.syncNode->children)
                        {
                            movePtr->priorChildrenToRemove[c.second->localname] = c.second;
                        }
                    }


                    // record details so we can look up the source LocalNode again after the move completes:
                    movePtr->sourceFsid = row.fsNode->fsid;
                    movePtr->sourceType = row.fsNode->type;
                    movePtr->sourceFingerprint = row.fsNode->fingerprint;
                    movePtr->sourcePtr = sourceSyncNode;

                    string newName = row.fsNode->localname.toName(*syncs.fsaccess);
                    if (newName == sourceCloudNode.name ||
                        sourceSyncNode->localname == row.fsNode->localname)
                    {
                        // if it wasn't renamed locally, or matches the target anyway
                        // then don't change the name
                        newName.clear();
                    }

                    // If renaming (or move-renaming), check for filename anomalies.
                    // Only report if we really do succeed with the rename
                    std::function<void(MegaClient&)> anomalyReport = nullptr;
                    if (!newName.empty() && newName != nameOverwritten)
                    {
                        auto anomalyType = isFilenameAnomaly(fullPath.localPath.leafName(), newName, sourceCloudNode.type);
                        if (anomalyType != FILENAME_ANOMALY_NONE)
                        {
                            auto local  = fullPath.localPath;
                            auto remote =  targetCloudNodePath + "/" + newName;

                            anomalyReport = [=](MegaClient& mc){
                                assert(!mc.syncs.onSyncThread());
                                mc.filenameAnomalyDetected(anomalyType, local, remote);
                            };
                        }
                    }

                    if (sourceCloudNode.parentHandle == targetCloudNode.handle && !newName.empty())
                    {
                        // send the command to change the node name
                        LOG_debug << syncname
                                  << "Renaming node: " << sourceCloudNodePath
                                  << " to " << newName  << logTriplet(row, fullPath);

                        auto renameHandle = sourceCloudNode.handle;
                        syncs.queueClient([renameHandle, newName, movePtr, anomalyReport, simultaneousMoveReplacedNodeToDebris](MegaClient& mc, DBTableTransactionCommitter& committer)
                            {
                                if (auto n = mc.nodeByHandle(renameHandle))
                                {

                                    // first move the old thing at the target path to debris.
                                    // this should occur in the same batch so it looks simultaneous
                                    if (simultaneousMoveReplacedNodeToDebris)
                                    {
                                        simultaneousMoveReplacedNodeToDebris(mc, committer);
                                    }

                                    mc.setattr(n, attr_map('n', newName), [&mc, movePtr, newName, anomalyReport](NodeHandle, Error err){

                                        movePtr->succeeded = !error(err);
                                        movePtr->failed = !!error(err);

                                        LOG_debug << mc.clientname << "SYNC Rename completed: " << newName << " err:" << err;

                                        if (!err && anomalyReport) anomalyReport(mc);
                                    });
                                }
                            });

                        row.syncNode->rare().moveToHere = movePtr;
                        sourceSyncNode->rare().moveFromHere = movePtr;

                        LOG_debug << syncname << "Sync - local rename/move " << sourceSyncNode->getLocalPath().toPath() << " -> " << fullPath.localPath.toPath();

                        rowResult = false;
                        return true;
                    }
                    else
                    {
                        // send the command to move the node
                        LOG_debug << syncname << "Moving node: " << sourceCloudNodePath
                                  << " into " << targetCloudNodePath
                                  << (newName.empty() ? "" : (" as " + newName).c_str()) << logTriplet(row, fullPath);

                        syncs.queueClient([sourceCloudNode, targetCloudNode, newName, movePtr, anomalyReport, simultaneousMoveReplacedNodeToDebris](MegaClient& mc, DBTableTransactionCommitter& committer)
                        {
                            auto fromNode = mc.nodeByHandle(sourceCloudNode.handle);
                            auto toNode = mc.nodeByHandle(targetCloudNode.handle);

                            if (fromNode && toNode)
                            {

                                // first move the old thing at the target path to debris.
                                // this should occur in the same batch so it looks simultaneous
                                if (simultaneousMoveReplacedNodeToDebris)
                                {
                                    simultaneousMoveReplacedNodeToDebris(mc, committer);
                                }

                                auto err = mc.rename(fromNode, toNode,
                                            SYNCDEL_NONE,
                                            sourceCloudNode.parentHandle,
                                            newName.empty() ? nullptr : newName.c_str(),
                                            [&mc, movePtr, anomalyReport](NodeHandle, Error err){

                                                movePtr->succeeded = !error(err);
                                                movePtr->failed = !!error(err);

                                                LOG_debug << mc.clientname << "SYNC Move completed. err:" << err;

                                                if (!err && anomalyReport) anomalyReport(mc);
                                            });

                                if (err)
                                {
                                    // todo: or should we mark this one as blocked and otherwise continue.

                                    // err could be EACCESS or ECIRCULAR for example
                                    LOG_warn << mc.clientname << "SYNC Rename not permitted due to err " << err << ": " << fromNode->displaypath()
                                        << " to " << toNode->displaypath()
                                        << (newName.empty() ? "" : (" as " + newName).c_str());

                                    // todo: figure out if the problem could be overcome by copying and later deleting the source
                                    // but for now, mark the sync as disabled
                                    // todo: work out the right sync error code

                                    // todo: find another place to detect this condition?   Or, is this something that might happen anyway due to async changes and race conditions, we should be able to reevaluate.
                                    //changestate(COULD_NOT_MOVE_CLOUD_NODES, false, true);
                                }
                            }

                            // movePtr.reset();  // kept alive until completion - then the sync code knows it's finished
                        });

                        // command sent, now we wait for the actinpacket updates, later we will recognise
                        // the row as synced from fsNode, cloudNode and update the syncNode from those

                        LOG_debug << syncname << "Sync - local rename/move " << sourceSyncNode->getLocalPath().toPath() << " -> " << fullPath.localPath.toPath();

                        row.syncNode->rare().moveToHere = movePtr;
                        sourceSyncNode->rare().moveFromHere = movePtr;

                        LOG_verbose << syncname << "Set moveToHere ptr: " << (void*)movePtr.get() << " at " << logTriplet(row, fullPath);

                        row.suppressRecursion = true;

                        row.syncNode->setSyncAgain(true, true, false); // keep visiting this node

                        rowResult = false;
                        return true;
                    }
                }
            }
            else
            {
                if (!foundSourceCloudNode)
                {
                    // eg. upload in progress for this node, and locally renamed in the meantime.
                    // we can still update the LocalNode, and the uploaded node will be renamed later.

                    if (!foundSourceCloudNode) SYNC_verbose << syncname << "Adjusting LN for local move/rename before cloud node exists." << logTriplet(row, fullPath);

                    // remove fsid (and handle) from source node, so we don't detect
                    // that as a move source anymore
                    sourceSyncNode->moveContentTo(row.syncNode, fullPath.localPath, true);

                    assert(sourceSyncNode->fsid_lastSynced == row.fsNode->fsid);
                    sourceSyncNode->setSyncedFsid(UNDEF, syncs.localnodeBySyncedFsid, sourceSyncNode->localname, nullptr); // no longer associted with an fs item
                    sourceSyncNode->sync->statecacheadd(sourceSyncNode);

                    // do not consider this one synced though, or we won't recognize it as a move target when the uploaded node appears
                    //row.syncNode->setSyncedFsid(row.fsNode->fsid, syncs.localnodeBySyncedFsid, row.syncNode->localname, nullptr); // in case of further local moves before upload completes
                    //statecacheadd(row.syncNode);

                    // we know we have orphaned the sourceSyncNode so it can be removed at the first opportunity, no need to wait
                    sourceSyncNode->confirmDeleteCount = 2;
                    sourceSyncNode->certainlyOrphaned = 1;
                }
                else
                {
                    // eg. cloud parent folder not synced yet (maybe Localnode is created, but not handle matched yet)
                    if (!foundTargetCloudNode) SYNC_verbose << syncname << "Target parent cloud node doesn't exist yet" << logTriplet(row, fullPath);

                    monitor.waitingLocal(fullPath.localPath, sourceSyncNode->getLocalPath(), fullPath.cloudPath, SyncWaitReason::ApplyMoveNeedsOtherSideParentFolderToExist);

                    row.suppressRecursion = true;
                    rowResult = false;
                    return true;
                }
            }
        }
    }

    return false;
 }


 #ifndef NDEBUG
 handle debug_getfsid(const LocalPath& p, FileSystemAccess& fsa)
 {
    auto fa = fsa.newfileaccess();
    LocalPath lp = p;
    if (fa->fopen(lp, true, false, nullptr, false)) return fa->fsid;
    else return UNDEF;
 }
 #endif

bool Sync::checkCloudPathForMovesRenames(syncRow& row, syncRow& parentRow, SyncPath& fullPath, bool& rowResult, bool belowRemovedFsNode)
{
    // We have detected that this LocalNode might be a move/rename target (the moved-to location).
    // Ie, there is a new/different CloudNode in this row.
    // This function detects whether this is in fact a move or not, and takes care of performing the corresponding local move/rename
    // If we do determine it's a move/rename, then we perform the corresponding action in the local FS.
    // We perform the local action synchronously so we don't need to track it with shared_ptrs etc.
    // Should it fail for some reason though, we report that in the stall tracking system and continue.
    // In the meantime this thread can continue recursing and iterate over the tree multiple times until the move is resolved.
    // We don't recurse below the moved-from or moved-to node in the meantime though as that would cause incorrect decisions to be made.
    // (well we do but in a very limited capacity, only checking if the local side has new nodes to detect crossed-over moves)
    // If the move/rename is successful, then on the next tree iteration we will detect it as a synced row in
    // resolve_rowMatched.   That function will update our data structures, moving the sub-LocalNodes from the
    // moved-from LocalNode to the moved-to LocalNode.  Later the moved-from LocalNode will be removed as it has no FSNode or CloudNode.
    // So yes that means we do briefly have two LocalNodes for a the single moved file/folder while the move goes on.
    // That's convenient algorithmicaly for tracking the move, and also it's not safe to delete the old node early
    // as it might have come from a parent folder, which in data structures in the recursion stack are referring to.
    // If the move/rename fails, it's likely because the move is no longer appropriate, eg new parent folder
    // is missing.  In that case, we clean up the data structure and let the algorithm make a new choice.

    assert(syncs.onSyncThread());

    // if this cloud move was a sync decision, don't look to make it locally too
    if (row.syncNode && row.syncNode->hasRare() && row.syncNode->rare().moveToHere)
    {
        SYNC_verbose << "Node was our own cloud move so skip possible matching local move. " << logTriplet(row, fullPath);
        rowResult = false;
        return false;  // we need to progress to resolve_rowMatched at this node
    }

    SYNC_verbose << syncname << "checking localnodes for synced handle " << row.cloudNode->handle;

    ProgressingMonitor monitor(syncs);

    if (row.syncNode && row.syncNode->type != row.cloudNode->type)
    {
        LOG_debug << syncname << "checked node does not have the same type, blocked: " << fullPath.cloudPath;

        monitor.waitingCloud(fullPath.cloudPath, string(), LocalPath(), SyncWaitReason::FolderMatchedAgainstFile);

        row.suppressRecursion = true;
        rowResult = false;
        return true;
    }

    unique_ptr<LocalNode> childrenToDeleteOnFunctionExit;

    if (LocalNode* sourceSyncNode = syncs.findLocalNodeByNodeHandle(row.cloudNode->handle))
    {
        if (sourceSyncNode == row.syncNode) return false;

        // Are we moving an ignore file?
        if (row.isIgnoreFile() || sourceSyncNode->isIgnoreFile())
        {
            // Then it's not subject to the usual move procesing.
            return false;
        }

        // Is the move target excluded?
        if (parentRow.exclusionState(*row.cloudNode) != ES_INCLUDED)
        {
            // Then don't perform the move.
            return false;
        }

        // It's a move or rename
        if (isBackup())
        {
            // Backups must not change the local
            changestate(BACKUP_MODIFIED, false, true, false);
            rowResult = false;
            return true;
        }

        assert(parentRow.syncNode);
        if (parentRow.syncNode) parentRow.syncNode->setCheckMovesAgain(false, true, false);
        if (row.syncNode) row.syncNode->setCheckMovesAgain(true, false, false);

        // Is the source's exclusion state well defined?
        if (sourceSyncNode->exclusionState() == ES_UNKNOWN)
        {
            // Let the engine know why we couldn't process this move.
            monitor.waitingLocal(sourceSyncNode->getLocalPath(),
                                 LocalPath(),
                                 string(),
                                 SyncWaitReason::UnknownExclusionState);

            row.recurseBelowRemovedCloudNode = true;
            row.suppressRecursion = true;

            // Complete the move later.
            return rowResult = false, true;
        }

        // Convenience.
        auto markSiblingSourceRow = [&]() {
            if (!row.rowSiblings)
                return;

            if (sourceSyncNode->parent != parentRow.syncNode)
                return;

            for (auto& sibling : *row.rowSiblings)
            {
                if (sibling.syncNode == sourceSyncNode)
                {
                    sibling.itemProcessed = true;
                    return;
                }
            }
        };

        //sourceSyncNode->treestate(TREESTATE_SYNCING);
        //if (row.syncNode) row.syncNode->treestate(TREESTATE_SYNCING);

        // It turns out that after LocalPath implicitly uses \\?\ and
        // absolute paths, we don't need to worry about reserved names.

        //// Does the move target have a reserved name?
        //if (row.hasReservedName(*syncs.fsaccess))
        //{
        //    // Make sure we don't try and synchronize the source.
        //    markSiblingSourceRow();

        //    // Let the engine know why we can't action the move.
        //    monitor.waitingLocal(fullPath.localPath,
        //                         sourceSyncNode->getLocalPath(),
        //                         fullPath.cloudPath,
        //                         SyncWaitReason::MoveTargetHasReservedName);

        //    // Don't recurse if the row's a directory.
        //    row.suppressRecursion = true;

        //    // Row isn't synchronized.
        //    rowResult = false;

        //    // Escape immediately from syncItemCheckMove(...).
        //    return true;
        //}

        LocalPath sourcePath = sourceSyncNode->getLocalPath();

        // True if the move-target exists and we're free to "overwrite" it.
        auto overwrite = false;

        // is there already something else at the target location though?
        if (row.fsNode)
        {
            // todo: should we check if the node that is already here is in fact a match?  in which case we should allow progressing to resolve_rowMatched

            SYNC_verbose << syncname << "Move detected by nodehandle, but something else with that name is already here locally. Type: " << row.fsNode->type
                << " moved node: " << fullPath.cloudPath
                << " old parent correspondence: " << (sourceSyncNode->parent ? sourceSyncNode->parent->getLocalPath().toPath() : "<null>")
                << logTriplet(row, fullPath);

            if (row.syncNode)
            {
                overwrite |= row.syncNode->type == row.fsNode->type;
                overwrite &= row.syncNode->fsid_lastSynced == row.fsNode->fsid;
            }

            if (!overwrite)
            {
                parentRow.syncNode->setCheckMovesAgain(false, true, false);
                monitor.waitingCloud(fullPath.cloudPath, sourceSyncNode->getCloudPath(), fullPath.localPath, SyncWaitReason::ApplyMoveIsBlockedByExistingItem);
                rowResult = false;
                return true;
            }

            SYNC_verbose << syncname << "Move is a legit overwrite of a synced file, so we overwrite that locally too." << logTriplet(row, fullPath);
        }

        if (!sourceSyncNode->moveApplyingToLocal && !belowRemovedFsNode && parentRow.cloudNode)
        {
            LOG_debug << syncname << "Move detected by nodehandle. Type: " << sourceSyncNode->type
                << " moved node: " << fullPath.cloudPath
                << " old parent correspondence: " << (sourceSyncNode->parent ? sourceSyncNode->parent->getLocalPath().toPath() : "<null>")
                << logTriplet(row, fullPath);

            LOG_debug << "Sync - remote move " << fullPath.cloudPath <<
                " from corresponding " << (sourceSyncNode->parent ? sourceSyncNode->parent->getLocalPath().toPath() : "<null>") <<
                " to " << parentRow.cloudNode->name;

            sourceSyncNode->moveApplyingToLocal = true;
        }

        assert(!isBackup());

        // Check for filename anomalies.
        {
            auto type = isFilenameAnomaly(fullPath.localPath, row.cloudNode->name);

            if (type != FILENAME_ANOMALY_NONE)
            {
                auto remotePath = fullPath.cloudPath;
                auto localPath = fullPath.localPath;
                syncs.queueClient([type, localPath, remotePath](MegaClient& mc, DBTableTransactionCommitter& committer)
                    {
                        mc.filenameAnomalyDetected(type, localPath, remotePath);
                    });
            }
        }

        // we don't want the source LocalNode to be visited until after the move completes, and we revisit with rescanned folder data
        // because it might see a new file with the same name, and start a download, keeping the row instead of removing it
        markSiblingSourceRow();

        if (belowRemovedFsNode)
        {
            LOG_debug << syncname << "Move destination detected for node " << row.cloudNode->handle << " but we are belowRemovedFsNode, must wait for resolution at: " << logTriplet(row, fullPath);;
            monitor.waitingCloud(fullPath.cloudPath, sourceSyncNode->getCloudPath(), fullPath.localPath, SyncWaitReason::ApplyMoveNeedsOtherSideParentFolderToExist);
            if (parentRow.syncNode) parentRow.syncNode->setSyncAgain(false, true, false);
            rowResult = false;
            return true;
        }

        // check filesystem is not changing fsids as a result of rename
        assert(sourceSyncNode->fsid_lastSynced == debug_getfsid(sourcePath, *syncs.fsaccess));

        if (overwrite)
        {
            auto path = fullPath.localPath.toPath();

            SYNC_verbose << "Move-target exists and must be moved to local debris: " << path;

            if (!movetolocaldebris(fullPath.localPath))
            {
                // Couldn't move the target to local debris.
                LOG_err << "Couldn't move move-target to local debris: " << path;

                // Sanity: Must exist for overwrite to be true.
                assert(row.syncNode);

                monitor.waitingLocal(fullPath.localPath, LocalPath(), string(), SyncWaitReason::CouldNotMoveToLocalDebrisFolder);

                // Don't recurse as the subtree's fubar.
                row.suppressRecursion = true;

                // Move hasn't completed.
                sourceSyncNode->moveAppliedToLocal = false;

                // Row hasn't been synced.
                rowResult = false;

                return true;
            }

            LOG_debug << syncname << "Move-target moved to local debris: " << path;

            // Rescan the parent if we're operating in periodic scan mode.
            if (!dirnotify)
                parentRow.syncNode->setScanAgain(false, true, false, 0);

            // Therefore there is nothing in the local subfolder anymore
            // And we should delete the localnodes corresponding to the items we moved to debris.
            // BUT what if the move is coming from inside that folder ?!!
            // Therefore move them to an unattached locallnode which will delete them on function exit

            //row.syncNode->deleteChildren();
            childrenToDeleteOnFunctionExit.reset(new LocalNode(this));
            while (!row.syncNode->children.empty())
            {
                auto* child = row.syncNode->children.begin()->second;
                child->setnameparent(childrenToDeleteOnFunctionExit.get(), child->localname, child->cloneShortname());
            }
        }

        if (syncs.fsaccess->renamelocal(sourcePath, fullPath.localPath))
        {
            // todo: move anything at this path to sync debris first?  Old algo didn't though
            // todo: additional consideration: what if there is something here, and it should be moved/renamed to elsewhere in the sync (not the debris) first?
            // todo: additional consideration: what if things to be renamed/moved form a cycle?

            // check filesystem is not changing fsids as a result of rename
            assert(overwrite || sourceSyncNode->fsid_lastSynced == debug_getfsid(fullPath.localPath, *syncs.fsaccess));

            LOG_debug << syncname << "Sync - local rename/move " << sourceSyncNode->getLocalPath().toPath() << " -> " << fullPath.localPath.toPath();

            if (!row.syncNode)
            {
                resolve_makeSyncNode_fromCloud(row, parentRow, fullPath, false);
                assert(row.syncNode);
            }

            // remove fsid (and handle) from source node, so we don't detect
            // that as a move source anymore
            sourceSyncNode->setSyncedFsid(UNDEF, syncs.localnodeBySyncedFsid, sourceSyncNode->localname, nullptr);  // shortname will be updated when rescan
            sourceSyncNode->setSyncedNodeHandle(NodeHandle());
            sourceSyncNode->sync->statecacheadd(sourceSyncNode);

            sourceSyncNode->moveContentTo(row.syncNode, fullPath.localPath, true);

            sourceSyncNode->moveAppliedToLocal = true;

            sourceSyncNode->setScanAgain(true, false, false, 0);
            row.syncNode->setScanAgain(true, true, true, 0);  // scan parent to see this moved fs item, also scan subtree to see if anything new is in there to overcome race conditions with fs notifications from the prior fs subtree paths

            rowResult = false;
            return true;
        }
        else if (syncs.fsaccess->transient_error)
        {
            LOG_warn << "transient error moving folder: " << sourcePath.toPath() << logTriplet(row, fullPath);

            monitor.waitingLocal(fullPath.localPath, sourceSyncNode->getLocalPath(), sourceSyncNode->getCloudPath(), SyncWaitReason::MoveOrRenameFailed);

            row.suppressRecursion = true;
            sourceSyncNode->moveApplyingToLocal = false;
            rowResult = false;
            return true;
        }
        else if (syncs.fsaccess->target_name_too_long)
        {
            LOG_warn << "Unable to move folder as the move target's name is too long: "
                     << sourcePath.toPath()
                     << logTriplet(row, fullPath);

            monitor.waitingLocal(fullPath.localPath,
                                 sourceSyncNode->getLocalPath(),
                                 sourceSyncNode->getCloudPath(),
                                 SyncWaitReason::MoveTargetNameTooLong);

            row.suppressRecursion = true;
            sourceSyncNode->moveApplyingToLocal = false;
            rowResult = false;

            return true;
        }
        else
        {
            SYNC_verbose << "Move to here delayed since local parent doesn't exist yet: " << sourcePath.toPath() << logTriplet(row, fullPath);
            monitor.waitingCloud(fullPath.cloudPath, sourceSyncNode->getCloudPath(), fullPath.localPath, SyncWaitReason::ApplyMoveNeedsOtherSideParentFolderToExist);
            rowResult = false;
            return true;
        }
    }
    else
    {
        monitor.noResult();
    }
    return false;
}

dstime Sync::procextraq()
{
    assert(syncs.onSyncThread());
    assert(dirnotify.get());

    Notification notification;
    NotificationDeque& queue = dirnotify->fsDelayedNetworkEventq;
    dstime delay = NEVER;

    while (queue.popFront(notification))
    {
        LocalNode* node = notification.localnode;

        // Ignore notifications for nodes that no longer exist.
        if (node == (LocalNode*)~0)
        {
            LOG_debug << syncname << "Notification skipped: "
                      << notification.path.toPath();
            continue;
        }

        // How long has it been since the notification was queued?
        auto elapsed = syncs.waiter.ds - notification.timestamp;

        // Is it ready to be processed?
        if (elapsed < EXTRA_SCANNING_DELAY_DS)
        {
            // We'll process the notification later.
            queue.unpopFront(notification);

            return delay;
        }

        LOG_verbose << syncname << "Processing extra fs notification: "
                    << notification.path.toPath();

        LocalPath remainder;
        LocalNode* match;
        LocalNode* nearest;

        match = localnodebypath(node, notification.path, &nearest, &remainder, false);

        // If the node is reachable, notify its parent.
        if (match && match->parent)
        {
            nearest = match->parent;
        }

        // Make sure some parent in the chain actually exists.
        if (!nearest)
        {
            // Should this notification be rescheduled?
            continue;
        }

        // Let the parent know it needs a scan.
#ifdef DEBUG
        if (nearest->scanAgain < TREE_ACTION_HERE)
        {
            SYNC_verbose << "Trigger scan flag by delayed notification on " << nearest->getLocalPath();
        }
#endif
        nearest->setScanAgain(false, true, !remainder.empty(), SCANNING_DELAY_DS);

        // How long the caller should wait before syncing.
        delay = SCANNING_DELAY_DS;
    }

    return delay;
}

//  Just mark the relative LocalNodes as needing to be rescanned.
dstime Sync::procscanq()
{
    assert(syncs.onSyncThread());
    assert(dirnotify.get());

    NotificationDeque& queue = dirnotify->fsEventq;

    if (queue.empty())
    {
        return NEVER;
    }

    LOG_verbose << syncname << "Marking sync tree with filesystem notifications: "
                << queue.size();

    Notification notification;
    dstime delay = NEVER;

    while (queue.popFront(notification))
    {
        lastFSNotificationTime = syncs.waiter.ds;

        // Skip invalidated notifications.
        if (notification.invalidated())
        {
            LOG_debug << syncname << "Notification skipped: "
                      << notification.path.toPath();
            continue;
        }

        // Skip notifications from this sync's debris folder.
        if (notification.fromDebris(*this))
        {
            LOG_debug << syncname
                      << "Debris notification skipped: "
                      << notification.path.toPath();
            continue;
        }

        LocalPath remainder;
        LocalNode* nearest = nullptr;
        LocalNode* node = notification.localnode;

        // Notify the node or its parent
        LocalNode* match = localnodebypath(node, notification.path, &nearest, &remainder, false);

        bool scanDescendants = false;

        if (match)
        {
            if ((notification.scanRequirement != Notification::FOLDER_NEEDS_SELF_SCAN
                || match->type == FILENODE)
                && match->parent)
            {
                if (match->type == FILENODE)
                    match->recomputeFingerprint = true;

                nearest = match->parent;
            }
            else
            {
                nearest = match;
            }
        }
        else
        {
            size_t pos = 0;
            bool multipartRemainder = remainder.findNextSeparator(pos);
            scanDescendants = notification.scanRequirement == Notification::FOLDER_NEEDS_SELF_SCAN ?
                              !remainder.empty() :
                              multipartRemainder;
        }

        if (!nearest)
        {
            // we didn't find any ancestor within the sync
            continue;
        }

        if (nearest->expectedSelfNotificationCount > 0)
        {
            if (nearest->scanDelayUntil >= syncs.waiter.ds)
            {
                // self-caused notifications shouldn't cause extra waiting
                --nearest->expectedSelfNotificationCount;

                SYNC_verbose << "Skipping self-notification (remaining: "
                    << nearest->expectedSelfNotificationCount << ") at: "
                    << nearest->getLocalPath().toPath();

                continue;
            }
            else
            {
                SYNC_verbose << "Expected more self-notifications ("
                    << nearest->expectedSelfNotificationCount << ") but they were late, at: "
                    << nearest->getLocalPath().toPath();
                nearest->expectedSelfNotificationCount = 0;
            }
        }

        // Let the parent know it needs to perform a scan.
#ifdef DEBUG
        //if (nearest->scanAgain < TREE_ACTION_HERE)
        {
            SYNC_verbose << "Trigger scan flag by fs notification on " << nearest->getLocalPath();
        }
#endif

        nearest->setScanAgain(false, true, scanDescendants, SCANNING_DELAY_DS);

        if (nearest->rareRO().scanBlocked)
        {
            // in case permissions changed on a scan-blocked folder
            // retry straight away, but don't reset the backoff delay
            nearest->rare().scanBlocked->scanBlockedTimer.set(syncs.waiter.ds);
        }

        // Queue an extra notification if we're a network sync.
        if (isnetwork)
        {
            LOG_verbose << syncname << "Queuing extra notification for: "
                        << notification.path.toPath();

            dirnotify->notify(dirnotify->fsDelayedNetworkEventq,
                              node,
                              notification.scanRequirement,
                              std::move(notification.path));
        }

        // How long the caller should wait before syncing.
        delay = SCANNING_DELAY_DS;
    }

    return delay;
}

bool Sync::movetolocaldebris(const LocalPath& localpath)
{
    assert(syncs.onSyncThread());
    assert(!isBackup());

    // first make sure the debris folder exists
    createDebrisTmpLockOnce();

    char buf[42];
    struct tm tms;
    string day, localday;
    struct tm* ptm = m_localtime(m_time(), &tms);

    // first try a subfolder with only the date (we expect that we may have target filename clashes here)
    sprintf(buf, "%04d-%02d-%02d", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday);
    LocalPath targetFolder = localdebris;
    targetFolder.appendWithSeparator(LocalPath::fromRelativePath(buf), true);

    bool failedDueToTargetExists = false;

    if (movetolocaldebrisSubfolder(localpath, targetFolder, false, failedDueToTargetExists))
    {
        return true;
    }

    if (!failedDueToTargetExists) return false;

    // next try a subfolder with additional time and sequence - target filename clashes here should not occur
    sprintf(strchr(buf, 0), " %02d.%02d.%02d.", ptm->tm_hour,  ptm->tm_min, ptm->tm_sec);

    string datetime = buf;
    bool counterReset = false;
    if (datetime != mLastDailyDateTimeDebrisName)
    {
        mLastDailyDateTimeDebrisName = datetime;
        mLastDailyDateTimeDebrisCounter = 0;
        counterReset = true;
    }

    // initially try wih the same sequence number as last time, to avoid making large numbers of these when possible
    targetFolder = localdebris;
    targetFolder.appendWithSeparator(LocalPath::fromRelativePath(
        datetime + std::to_string(mLastDailyDateTimeDebrisCounter)), false);

    if (movetolocaldebrisSubfolder(localpath, targetFolder, counterReset, failedDueToTargetExists))
    {
        return true;
    }

    if (!failedDueToTargetExists) return false;

    if (counterReset)
    {
        // no need to try an incremented number if it was a new folder anyway
        return false;
    }

    // if that fails, try with the sequence incremented, that should be a new, empty folder with no filename clash possible
    ++mLastDailyDateTimeDebrisCounter;

    targetFolder = localdebris;
    targetFolder.appendWithSeparator(LocalPath::fromRelativePath(
        datetime + std::to_string(mLastDailyDateTimeDebrisCounter)), true);

    if (movetolocaldebrisSubfolder(localpath, targetFolder, true, failedDueToTargetExists))
    {
        return true;
    }

    return false;
}

bool Sync::movetolocaldebrisSubfolder(const LocalPath& localpath, const LocalPath& targetFolder, bool logFailReason, bool& failedDueToTargetExists)
{
    failedDueToTargetExists = false;

    bool createdFolder = false;
    if (syncs.fsaccess->mkdirlocal(targetFolder, false, false))
    {
        createdFolder = true;
    }
    else
    {
        if (!syncs.fsaccess->target_exists)
        {
            return false;
        }
    }

    LocalPath moveTarget = targetFolder;
    moveTarget.appendWithSeparator(localpath.subpathFrom(localpath.getLeafnameByteIndex()), true);

    syncs.fsaccess->skip_targetexists_errorreport = !logFailReason;
    bool success = syncs.fsaccess->renamelocal(localpath, moveTarget, false);
    syncs.fsaccess->skip_targetexists_errorreport = false;

    failedDueToTargetExists = !success && syncs.fsaccess->target_exists;

    if (createdFolder)
    {
        if (success)
        {
            LOG_verbose << syncname << "Created daily local debris folder: " << targetFolder.toPath();
        }
        else
        {
            // we didn't use the folder anyway, remove to avoid making huge numbers of them
            syncs.fsaccess->rmdirlocal(targetFolder);
        }
    }
    return success;
}

UnifiedSync::UnifiedSync(Syncs& s, const SyncConfig& c)
    : syncs(s), mConfig(c)
{
    mNextHeartbeat.reset(new HeartBeatSyncInfo());
}


void Syncs::enableSyncByBackupId(handle backupId, bool paused, bool resetFingerprint, bool notifyApp, bool setOriginalPath, std::function<void(error, SyncError)> completion, const string& logname)
{
    assert(!onSyncThread());

    auto clientCompletion = [=](error e, SyncError se, handle)
        {
            queueClient([completion, e, se](MegaClient&, DBTableTransactionCommitter&)
                {
                    if (completion) completion(e, se);
                });
        };

    queueSync([=]()
        {
            enableSyncByBackupId_inThread(backupId, paused, resetFingerprint, notifyApp, setOriginalPath, clientCompletion, logname);
        });
}

void Syncs::enableSyncByBackupId_inThread(handle backupId, bool paused, bool resetFingerprint, bool notifyApp, bool setOriginalPath, std::function<void(error, SyncError, handle)> completion, const string& logname)
{
    assert(onSyncThread());

    UnifiedSync* usPtr = nullptr;

    for (auto& s : mSyncVec)
    {
        if (s->mConfig.mBackupId == backupId)
        {
            usPtr = s.get();
        }
    }

    if (!usPtr)
    {
        LOG_debug << "Enablesync could not find sync";
        if (completion) completion(API_ENOENT, UNKNOWN_ERROR, backupId);
        return;
    }

    UnifiedSync& us = *usPtr;

    if (us.mSync)
    {
        // it's already running, just set whether it's paused or not.
        LOG_debug << "Sync pause/unpause set to " << us.mConfig.mTemporarilyPaused;
        us.mConfig.mTemporarilyPaused = paused;
        us.mConfig.mRunState = paused? SyncRunState::Pause : SyncRunState::Run;
        if (completion) completion(API_OK, NO_SYNC_ERROR, backupId);
        return;
    }

    us.mConfig.mError = NO_SYNC_ERROR;
    us.mConfig.mRunState = SyncRunState::Loading;

    if (resetFingerprint)
    {
        us.mConfig.mFilesystemFingerprint = 0; //This will cause the local filesystem fingerprint to be recalculated
    }

    if (setOriginalPath)
    {
        CloudNode cloudNode;
        string cloudNodePath;
        if (lookupCloudNode(us.mConfig.mRemoteNode, cloudNode, &cloudNodePath, nullptr, nullptr, nullptr, Syncs::FOLDER_ONLY)
            &&  us.mConfig.mOriginalPathOfRemoteRootNode != cloudNodePath)
        {
            us.mConfig.mOriginalPathOfRemoteRootNode = cloudNodePath;
            saveSyncConfig(us.mConfig);
        }
    }

    LocalPath rootpath;
    std::unique_ptr<FileAccess> openedLocalFolder;
    bool inshare, isnetwork;

    error e;
    {
        // todo: even better thead safety
        lock_guard<mutex> g(mClient.nodeTreeMutex);
        e = mClient.checkSyncConfig(us.mConfig, rootpath, openedLocalFolder, inshare, isnetwork);
    }

    if (e)
    {
        // error and enable flag were already changed
        LOG_debug << "Enablesync checks resulted in error: " << e;

        us.mConfig.mRunState = us.mConfig.mDatabaseExists ? SyncRunState::Suspend : SyncRunState::Disable;

        us.changedConfigState(true, notifyApp);
        if (completion) completion(e, us.mConfig.mError, backupId);
        return;
    }

    // Does this sync contain an ignore file?
    if (!hasIgnoreFile(us.mConfig))
    {
        // Try and create the missing ignore file.
        if (!mDefaultFilterChain.create(us.mConfig.mLocalPath, *fsaccess))
        {
            LOG_debug << "Failed to create ignore file for sync without one";

            us.mConfig.mError = COULD_NOT_CREATE_IGNORE_FILE;
            us.mConfig.mEnabled = false;
            us.mConfig.mRunState = us.mConfig.mDatabaseExists ? SyncRunState::Suspend : SyncRunState::Disable;

            us.changedConfigState(true, notifyApp);

            if (completion)
                completion(API_EWRITE, us.mConfig.mError, backupId);

            return;
        }
    }

    us.mConfig.mError = NO_SYNC_ERROR;
    us.mConfig.mEnabled = true;
    us.mConfig.mRunState = SyncRunState::Loading;

    // If we're a backup sync...
    if (us.mConfig.isBackup())
    {
        auto& config = us.mConfig;

        auto firstTime = config.mBackupState == SYNC_BACKUP_NONE;
        auto isExternal = config.isExternal();
        auto wasDisabled = config.knownError() == BACKUP_MODIFIED;

        if (firstTime || isExternal || wasDisabled)
        {
            // Then we must come up in mirroring mode.
            config.mBackupState = SYNC_BACKUP_MIRROR;
        }
    }

    string debris = DEBRISFOLDER;
    auto localdebris = LocalPath();

    us.changedConfigState(true, notifyApp);
    mHeartBeatMonitor->updateOrRegisterSync(us);

    startSync_inThread(us, debris, localdebris, inshare, isnetwork, rootpath, completion, logname);
    us.mNextHeartbeat->updateSPHBStatus(us);
}

bool Syncs::checkSyncRemoteLocationChange(UnifiedSync& us, bool exists, string cloudPath)
{
    assert(onSyncThread());

    bool pathChanged = false;
    if (exists)
    {
        if (cloudPath != us.mConfig.mOriginalPathOfRemoteRootNode)
        {
            // the sync will be suspended. Should the user manually start it again,
            // then the recorded path will be updated to whatever path the Node is then at.
            LOG_debug << "Sync root path changed!  Was: " << us.mConfig.mOriginalPathOfRemoteRootNode << " now: " << cloudPath;
            pathChanged = true;
        }
    }
    else //unset remote node: failed!
    {
        if (!us.mConfig.mRemoteNode.isUndef())
        {
            us.mConfig.mRemoteNode = NodeHandle();
        }
    }

    return pathChanged;
}

void Syncs::startSync_inThread(UnifiedSync& us, const string& debris, const LocalPath& localdebris,
    bool inshare, bool isNetwork, const LocalPath& rootpath,
    std::function<void(error, SyncError, handle)> completion, const string& logname)
{
    assert(onSyncThread());

    auto prevFingerprint = us.mConfig.mFilesystemFingerprint;

    assert(!us.mSync);

    us.mConfig.mRunState = SyncRunState::Loading;
    us.changedConfigState(false, true);

    us.mSync.reset(new Sync(us, debris, localdebris, inshare, logname));
    us.mConfig.mFilesystemFingerprint = us.mSync->fsfp;

    us.mConfig.mRunState = SyncRunState::Run;
    us.changedConfigState(false, true);

    // Assume we'll encounter an error.
    error e = API_EFAILED;

    // Reduce duplication.
    auto signalError = [&](SyncError error) {
        us.changeState(error, false, true, true);
        us.mSync.reset();
    };

    // Make sure we were able to assign the root's FSID.
    if (us.mSync->localroot->fsid_lastSynced == UNDEF)
    {
        LOG_err << "Unable to retrieve the sync root's FSID: "
                << us.mConfig.getLocalPath();

        signalError(UNABLE_TO_RETRIEVE_ROOT_FSID);
    }
    else if (us.mSync->localroot->watch(us.mConfig.getLocalPath(), UNDEF) != WR_SUCCESS)
    {
        LOG_err << "Unable to add a watch for the sync root: "
                << us.mConfig.getLocalPath();

        signalError(UNABLE_TO_ADD_WATCH);
    }
    else if (prevFingerprint && prevFingerprint != us.mConfig.mFilesystemFingerprint)
    {
        LOG_err << "New sync local fingerprint mismatch. Previous: "
                << prevFingerprint
                << "  Current: "
                << us.mConfig.mFilesystemFingerprint;

        signalError(LOCAL_FILESYSTEM_MISMATCH);
    }
    else
    {
        us.mSync->isnetwork = isNetwork;

        saveSyncConfig(us.mConfig);
        mSyncFlags->isInitialPass = true;
        e = API_OK;

    }

    LOG_debug << "Final error for sync start: " << e;

    if (completion) completion(e, us.mConfig.mError, us.mConfig.mBackupId);
}

void UnifiedSync::changedConfigState(bool save, bool notifyApp)
{
    assert(syncs.onSyncThread());

    if (mConfig.stateFieldsChanged())
    {
        LOG_debug << "Sync " << toHandle(mConfig.mBackupId)
                  << " now in runState: " << int(mConfig.mRunState)
                  << " enabled: " << mConfig.mEnabled
                  << " error: " << mConfig.mError;

        if (save)
        {
            syncs.saveSyncConfig(mConfig);
        }

        if (notifyApp)
        {
            assert(syncs.onSyncThread());
            syncs.mClient.app->syncupdate_stateconfig(mConfig);
        }
    }
}

Syncs::Syncs(MegaClient& mc)
  : mClient(mc)
  , fsaccess(::mega::make_unique<FSACCESS_CLASS>())
  , mSyncFlags(new SyncFlags)
  , mScanService(new ScanService(waiter))
{
    fsaccess->initFilesystemNotificationSystem();

    mHeartBeatMonitor.reset(new BackupMonitor(*this));
    syncThread = std::thread([this]() { syncLoop(); });
}

Syncs::~Syncs()
{
    assert(!onSyncThread());

    // null function is the signal to end the thread
    syncThreadActions.pushBack(nullptr);
    waiter.notify();
    if (syncThread.joinable()) syncThread.join();
}

void Syncs::getSyncProblems(std::function<void(SyncProblems&)> completion,
                            bool completionInClient,
                            bool detailed)
{
    using MC = MegaClient;
    using DBTC = DBTableTransactionCommitter;

    if (completionInClient)
    {
        completion = [this, completion](SyncProblems& problems) {
            queueClient([completion, problems](MC&, DBTC&) mutable {
                completion(problems);
            });
        };
    }

    queueSync([this, detailed, completion]() mutable {
        SyncProblems problems;
        getSyncProblems_inThread(problems, detailed);
        completion(problems);
    });
}

void Syncs::getSyncProblems_inThread(SyncProblems& problems, bool detailed)
{
    assert(onSyncThread());

    problems.mConflictsDetected = conflictsFlagged();
    problems.mStallsDetected = syncStallState;

    if (!detailed) return;

    conflictsDetected(problems.mConflicts);
    problems.mStalls = stallReport;
}

void Syncs::getSyncStatusInfo(handle backupID,
                              SyncStatusInfoCompletion completion,
                              bool completionInClient)
{
    // No completion? No work to be done!
    if (!completion)
        return;

    // Convenience.
    using DBTC = DBTableTransactionCommitter;
    using MC = MegaClient;
    using SV = vector<SyncStatusInfo>;

    // Is it up to the client to call the completion function?
    if (completionInClient)
        completion = [completion, this](SV info) {
            // Necessary as we can't move-capture before C++14.
            auto temp = std::make_shared<SV>(std::move(info));

            // Delegate to the user's completion function.
            queueClient([completion, temp](MC&, DBTC&) mutable {
                completion(std::move(*temp));
            });
        };

    // Queue the request on the sync thread.
    queueSync([backupID, completion, this]() {
        getSyncStatusInfoInThread(backupID, std::move(completion));
    });
}

void Syncs::getSyncStatusInfoInThread(handle backupID,
                                      SyncStatusInfoCompletion completion)
{
    // Make sure we're running on the right thread.
    assert(onSyncThread());

    // Make sure no one's changing the syncs beneath our feet.
    lock_guard<mutex> guard(mSyncVecMutex);

    // Gathers information about a specific sync.
    struct gather
    {
        gather(const Sync& sync)
          : mSync(sync)
        {
        }

        operator SyncStatusInfo() const
        {
            SyncStatusInfo info;

            auto& config = mSync.getConfig();

            info.mBackupID = config.mBackupId;
            info.mName = config.mName;
            info.mTransferCounts = mSync.transferCounts();

            tally(info, *mSync.localroot);

            return info;
        }

        void tally(SyncStatusInfo& info, const LocalNode& node) const
        {
            // Not synced? Not interested.
            if (node.parent && node.syncedCloudNodeHandle.isUndef())
                return;

            ++info.mTotalSyncedNodes;

            // Directories don't have a size.
            if (node.type == FILENODE)
                info.mTotalSyncedBytes += node.syncedFingerprint.size;

            // Process children, if any.
            for (auto& childIt : node.children)
                tally(info, *childIt.second);
        }

        const Sync& mSync;
    }; // gather

    // Status info collected from syncs.
    vector<SyncStatusInfo> info;

    // Gather status information from active syncs.
    for (auto& us : mSyncVec)
    {
        // Not active? Not interested.
        if (!us->mSync)
            continue;

        // Convenience.
        auto& config = us->mConfig;

        // Is this sync something we're interested in?
        if (backupID != UNDEF && backupID != config.mBackupId)
            continue;

        // Gather status information about this sync.
        info.emplace_back(gather(*us->mSync));
    }

    // Pass the information to the caller.
    completion(std::move(info));
}

void Syncs::getSyncStalls(std::function<void(SyncStallInfo& syncStallInfo)> completionClosure,
        bool completionInClient)
{
    using MC = MegaClient;
    using DBTC = DBTableTransactionCommitter;

    if (completionInClient)
    {
        completionClosure = [this, completionClosure](SyncStallInfo& syncStallInfo) {
            queueClient([completionClosure, syncStallInfo](MC&, DBTC&) mutable {
                completionClosure(syncStallInfo);
            });
        };
    }

    queueSync([this, completionClosure]() mutable {
        SyncStallInfo syncStallInfo;
        syncStallDetected(syncStallInfo); // Collect sync stalls
        completionClosure(syncStallInfo);
    });
}

SyncConfigVector Syncs::configsForDrive(const LocalPath& drive) const
{
    assert(onSyncThread() || !onSyncThread());

    lock_guard<mutex> g(mSyncVecMutex);

    SyncConfigVector v;
    for (auto& s : mSyncVec)
    {
        if (s->mConfig.mExternalDrivePath == drive)
        {
            v.push_back(s->mConfig);
        }
    }
    return v;
}

SyncConfigVector Syncs::allConfigs() const
{
    assert(onSyncThread() || !onSyncThread());

    lock_guard<mutex> g(mSyncVecMutex);

    SyncConfigVector v;
    for (auto& s : mSyncVec)
    {
        v.push_back(s->mConfig);
    }
    return v;
}

bool Syncs::configById(handle backupId, SyncConfig& configResult) const
{
    assert(!onSyncThread());

    lock_guard<mutex> g(mSyncVecMutex);

    for (auto& s : mSyncVec)
    {
        if (s->mConfig.mBackupId == backupId)
        {
            configResult = s->mConfig;
            return true;
        }
    }
    return false;
}

bool Syncs::configByRootNode(NodeHandle syncroot, SyncConfig& configResult) const
{
    assert(!onSyncThread());

    lock_guard<mutex> g(mSyncVecMutex);

    for (auto& s : mSyncVec)
    {
        if (s->mConfig.mRemoteNode == syncroot)
        {
            configResult = s->mConfig;
            return true;
        }
    }
    return false;
}

void Syncs::backupCloseDrive(LocalPath drivePath, std::function<void(Error)> clientCallback)
{
    assert(!onSyncThread());
    assert(clientCallback);

    queueSync([this, drivePath, clientCallback]()
        {
            Error e = backupCloseDrive_inThread(drivePath);
            queueClient([clientCallback, e](MegaClient& mc, DBTableTransactionCommitter& committer)
                {
                    clientCallback(e);
                });
        });
}

error Syncs::backupCloseDrive_inThread(LocalPath drivePath)
{
    assert(onSyncThread());
    assert(drivePath.isAbsolute() || drivePath.empty());

    // Is the path valid?
    if (drivePath.empty())
    {
        return API_EARGS;
    }

    auto* store = syncConfigStore();

    // Does the store exist?
    if (!store)
    {
        // Nope and we need it.
        return API_EINTERNAL;
    }

    // Is this drive actually loaded?
    if (!store->driveKnown(drivePath))
    {
        return API_ENOENT;
    }

    auto result = store->write(drivePath, configsForDrive(drivePath));
    store->removeDrive(drivePath);

    unloadSelectedSyncs(
      [&](SyncConfig& config, Sync*)
      {
          return config.mExternalDrivePath == drivePath;
      }, false);

    return result;
}

void Syncs::backupOpenDrive(LocalPath drivePath, std::function<void(Error)> clientCallback)
{
    assert(!onSyncThread());
    assert(clientCallback);

    queueSync([this, drivePath, clientCallback]()
        {
            Error e = backupOpenDrive_inThread(drivePath);
            queueClient([clientCallback, e](MegaClient& mc, DBTableTransactionCommitter& committer)
                {
                    clientCallback(e);
                });
        });
}

error Syncs::backupOpenDrive_inThread(LocalPath drivePath)
{
    assert(onSyncThread());
    assert(drivePath.isAbsolute());

    // Is the drive path valid?
    if (drivePath.empty())
    {
        return API_EARGS;
    }

    // Can we get our hands on the config store?
    auto* store = syncConfigStore();

    if (!store)
    {
        LOG_err << "Couldn't restore "
                << drivePath
                << " as there is no config store.";

        // Nope and we can't do anything without it.
        return API_EINTERNAL;
    }

    // Has this drive already been opened?
    if (store->driveKnown(drivePath))
    {
        LOG_debug << "Skipped restore of "
                  << drivePath
                  << " as it has already been opened.";

        // Then we don't have to do anything.
        return API_EEXIST;
    }

    SyncConfigVector configs;

    // Try and open the database on the drive.
    auto result = store->read(drivePath, configs, true);

    // Try and restore the backups in the database.
    if (result == API_OK)
    {
        LOG_debug << "Attempting to restore backup syncs from "
                  << drivePath;

        size_t numRestored = 0;

        // Create a unified sync for each backup config.
        for (const auto& config : configs)
        {
            // Create the unified sync.
            lock_guard<mutex> g(mSyncVecMutex);

            bool skip = false;
            for (auto& us : mSyncVec)
            {
                // Make sure there aren't any syncs with this backup id.
                if (config.mBackupId == us->mConfig.mBackupId)
                {
				    skip = true;
                    LOG_err << "Skipping restore of backup "
                            << config.mLocalPath
                            << " on "
                            << drivePath
                            << " as a sync already exists with the backup id "
                            << toHandle(config.mBackupId);
                }
            }

            if (!skip)
            {
                mSyncVec.emplace_back(new UnifiedSync(*this, config));

                // Track how many configs we've restored.
                ++numRestored;
            }
        }

        // Log how many backups we could restore.
        LOG_debug << "Restored "
                  << numRestored
                  << " out of "
                  << configs.size()
                  << " backup(s) from "
                  << drivePath;

        return API_OK;
    }

    // Couldn't open the database.
    LOG_warn << "Failed to restore "
             << drivePath
             << " as we couldn't open its config database.";

    return result;
}

SyncConfigStore* Syncs::syncConfigStore()
{
    assert(onSyncThread());

    // Have we already created the database?
    if (mSyncConfigStore)
    {
        // Yep, return a reference to the caller.
        return mSyncConfigStore.get();
    }

    // Is the client using a database?
    if (!mClient.dbaccess)
    {
        // Nope and we need it for the configuration path.
        return nullptr;
    }

    // Can we get our hands on an IO context?
    if (!syncConfigIOContext())
    {
        // We need it if we want to write the DB to disk.
        return nullptr;
    }

    // Where the database will be stored.
    auto dbPath = mClient.dbaccess->rootPath();

    // Create the database.
    mSyncConfigStore.reset(
      new SyncConfigStore(dbPath, *mSyncConfigIOContext));

    return mSyncConfigStore.get();
}

NodeHandle Syncs::getSyncedNodeForLocalPath(const LocalPath& lp)
{
    assert(!onSyncThread());

    // synchronous for now but we could make async one day (intermediate layer would need its function made async first)
    NodeHandle result;
    syncRun([&](){

        lock_guard<mutex> g(mSyncVecMutex);
        for (auto& us : mSyncVec)
        {
            if (us->mSync)
            {
                LocalNode* match = us->mSync->localnodebypath(NULL, lp, nullptr, nullptr, false);
                if (match)
                {
                    result = match->syncedCloudNodeHandle;
                    break;
                }
            }
        }

    });
    return result;
}

treestate_t Syncs::getSyncStateForLocalPath(handle backupId, const LocalPath& lp)
{
    assert(!onSyncThread());

    // mLocalNodeChangeMutex must already be locked!!

    // we must lock the sync vec mutex when not on the sync thread
    // careful of lock ordering to avoid deadlock between threads
    // we never have mSyncVecMutex and then lock mLocalNodeChangeMutex
    lock_guard<mutex> g(mSyncVecMutex);
    for (auto& us : mSyncVec)
    {
        if (us->mConfig.mBackupId == backupId && us->mSync)
        {
            if (LocalNode* match = us->mSync->localnodebypath(nullptr, lp, nullptr, nullptr, true))
            {
                return match->checkTreestate(false);
            }
            return TREESTATE_NONE;
        }
    }
    return TREESTATE_NONE;
}

error Syncs::syncConfigStoreAdd(const SyncConfig& config)
{
    assert(!onSyncThread());

    error result = API_OK;
    syncRun([&](){ syncConfigStoreAdd_inThread(config, [&](error e){ result = e; }); });
    return result;
}

void Syncs::syncConfigStoreAdd_inThread(const SyncConfig& config, std::function<void(error)> completion)
{
    assert(onSyncThread());

    // Convenience.
    static auto equal =
      [](const LocalPath& lhs, const LocalPath& rhs)
      {
          return !platformCompareUtf(lhs, false, rhs, false);
      };

    auto* store = syncConfigStore();

    // Could we get our hands on the store?
    if (!store)
    {
        // Nope and we can't proceed without it.
        completion(API_EINTERNAL);
        return;
    }

    SyncConfigVector configs;
    bool known = store->driveKnown(LocalPath());

    // Load current configs from disk.
    auto result = store->read(LocalPath(), configs, false);

    if (result == API_ENOENT || result == API_OK)
    {
        SyncConfigVector::iterator i = configs.begin();

        // Are there any syncs already present for this root?
        for ( ; i != configs.end(); ++i)
        {
            if (equal(i->mLocalPath, config.mLocalPath))
            {
                break;
            }
        }

        // Did we find any existing config?
        if (i != configs.end())
        {
            // Yep, replace it.
            LOG_debug << "Replacing existing sync config for: "
                      << i->mLocalPath.toPath();

            *i = config;
        }
        else
        {
            // Nope, add it.
            configs.emplace_back(config);
        }

        // Write the configs to disk.
        result = store->write(LocalPath(), configs);
    }

    // Remove the drive if it wasn't already known.
    if (!known)
    {
        store->removeDrive(LocalPath());
    }

    completion(result);
    return;
}

bool Syncs::syncConfigStoreDirty()
{
    assert(onSyncThread());
    return mSyncConfigStore && mSyncConfigStore->dirty();
}

bool Syncs::syncConfigStoreFlush()
{
    assert(onSyncThread());

    // No need to flush if the store's not dirty.
    if (!syncConfigStoreDirty()) return true;

    // Try and flush changes to disk.
    LOG_debug << "Attempting to flush config store changes.";

    auto failed = mSyncConfigStore->writeDirtyDrives(allConfigs());

    if (failed.empty()) return true;

    LOG_err << "Failed to flush "
             << failed.size()
             << " drive(s).";

    // Disable syncs present on drives that we couldn't write.
    //size_t nFailed = failed.size();


// todo: changing the config again here would cause another flush, and another failure, in an endless cycle?
    //disableSelectedSyncs_inThread(
    //  [&](SyncConfig& config, Sync*)
    //  {
    //      // But only if they're not already disabled.
    //      if (!config.getEnabled()) return false;

    //      auto matched = failed.count(config.mExternalDrivePath);

    //        return matched > 0;
    //    },
    //    false,
    //    SYNC_CONFIG_WRITE_FAILURE,
    //    false,
    //    [=](size_t disabled){
    //        LOG_warn << "Disabled "
    //            << disabled
    //            << " sync(s) on "
    //            << nFailed
    //            << " drive(s).";
    //    });

    return false;
}

error Syncs::syncConfigStoreLoad(SyncConfigVector& configs)
{
    assert(onSyncThread());

    LOG_debug << "Attempting to load internal sync configs from disk.";

    auto result = API_EAGAIN;

    // Can we get our hands on the internal sync config database?
    if (auto* store = syncConfigStore())
    {
        // Try and read the internal database from disk.
        result = store->read(LocalPath(), configs, false);

        if (result == API_ENOENT || result == API_OK)
        {
            LOG_debug << "Loaded "
                      << configs.size()
                      << " internal sync config(s) from disk.";

            // check if sync databases exist, so we know if a sync is disabled or merely suspended
            for (auto& c: configs)
            {
                auto fas = fsaccess->newfileaccess(false);
                if (fas->fopen(c.mLocalPath, true, false))
                {
                    string dbname = c.getSyncDbStateCacheName(fas->fsid, c.mRemoteNode, mClient.me);

                    // Note, we opened dbaccess in thread-safe mode
                    LocalPath dbPath;
                    c.mDatabaseExists = mClient.dbaccess->checkDbFileAndAdjustLegacy(*fsaccess, dbname, DB_OPEN_FLAG_TRANSACTED, dbPath);
                }

                if (c.mEnabled)
                {
                    c.mRunState = SyncRunState::Pending;
                }
                else
                {
                    c.mRunState = c.mDatabaseExists ? SyncRunState::Suspend : SyncRunState::Disable;
                }

            }

            return API_OK;
        }
    }

    LOG_err << "Couldn't load internal sync configs from disk: "
            << result;

    return result;
}

string Syncs::exportSyncConfigs(const SyncConfigVector configs) const
{
    assert(!onSyncThread());
    JSONWriter writer;

    writer.beginobject();
    writer.beginarray("configs");

    for (const auto& config : configs)
    {
        exportSyncConfig(writer, config);
    }

    writer.endarray();
    writer.endobject();

    return writer.getstring();
}

string Syncs::exportSyncConfigs() const
{
    assert(!onSyncThread());
    return exportSyncConfigs(configsForDrive(LocalPath()));
}

void Syncs::importSyncConfigs(const char* data, std::function<void(error)> completion)
{
    assert(!onSyncThread());

    // Convenience.
    struct Context;

    using CompletionFunction = std::function<void(error)>;
    using ContextPtr = std::shared_ptr<Context>;

    // Bundles state we need to create backup IDs.
    struct Context
    {
        static void put(ContextPtr context)
        {
            using std::bind;
            using std::move;
            using std::placeholders::_1;
            using std::placeholders::_2;

            // Convenience.
            auto& client = *context->mClient;
            auto& config = *context->mConfig;
            auto& deviceHash = context->mDeviceHash;

            // Backup Info.
            auto state = BackupInfoSync::getSyncState(config, context->mSyncs->mDownloadsPaused, context->mSyncs->mUploadsPaused);
            auto info  = BackupInfoSync(config, deviceHash, UNDEF, state);

            LOG_debug << "Generating backup ID for config "
                      << context->signature()
                      << "...";

            // Completion chain.
            auto completion = bind(&putComplete, move(context), _1, _2);

            // Create and initiate request.
            auto* request = new CommandBackupPut(&client, info, move(completion));
            client.reqs.add(request);
        }

        static void putComplete(ContextPtr context, Error result, handle backupID)
        {
            // No backup ID even though the request succeeded?
            if (!result && ISUNDEF(result))
            {
                // Then we've encountered an internal error.
                result = API_EINTERNAL;
            }

            // Convenience;
            auto& client = *context->mClient;

            // Were we able to create a backup ID?
            if (result)
            {
                LOG_err << "Unable to generate backup ID for config "
                        << context->signature();

                auto i = context->mConfigs.begin();
                auto j = context->mConfig;

                // Remove the IDs we've created so far.
                LOG_debug << "Releasing backup IDs generated so far...";

                for ( ; i != j; ++i)
                {
                    auto* request = new CommandBackupRemove(&client, i->mBackupId);
                    client.reqs.add(request);
                }

                // Let the client know the import has failed.
                context->mCompletion(result);
                return;
            }

            // Assign the newly generated backup ID.
            context->mConfig->mBackupId = backupID;

            // Have we assigned IDs for all the syncs?
            if (++context->mConfig == context->mConfigs.end())
            {
                auto& syncs = *context->mSyncs;

                LOG_debug << context->mConfigs.size()
                          << " backup ID(s) have been generated.";

                LOG_debug << "Importing "
                          << context->mConfigs.size()
                          << " configs(s)...";

                // Yep, add them to the sync.
                for (const auto& config : context->mConfigs)
                {
                    std::promise<bool> synchronous;
                    syncs.appendNewSync(config, false, false, [&](error, SyncError, handle){ synchronous.set_value(true); }, false, "");
                    synchronous.get_future().get();
                }

                LOG_debug << context->mConfigs.size()
                          << " sync(s) imported successfully.";

                // Let the client know the import has completed.
                context->mCompletion(API_OK);
                return;
            }

            // Generate an ID for the next config.
            put(std::move(context));
        }

        string signature() const
        {
            ostringstream ostream;

            ostream << mConfig - mConfigs.begin() + 1
                    << "/"
                    << mConfigs.size();

            return ostream.str();
        }

        // Client.
        MegaClient* mClient;

        // Who to call back when we're done.
        CompletionFunction mCompletion;

        // Next config requiring a backup ID.
        SyncConfigVector::iterator mConfig;

        // Configs requiring a backup ID.
        SyncConfigVector mConfigs;

        // Identifies the device we're adding configs to.
        string mDeviceHash;

        // Who we're adding the configs to.
        Syncs* mSyncs;
    }; // Context

    // Sanity.
    if (!data || !*data)
    {
        completion(API_EARGS);
        return;
    }

    // Preprocess input so to remove all extraneous whitespace.
    auto strippedData = JSON::stripWhitespace(data);

    // Try and translate JSON back into sync configs.
    SyncConfigVector configs;

    if (!importSyncConfigs(strippedData, configs))
    {
        // No love. Inform the client.
        completion(API_EREAD);
        return;
    }

    // Don't import configs that already appear to be present.
    {
        lock_guard<mutex> guard(mSyncVecMutex);

        // Checks if two configs have an equivalent mapping.
        auto equivalent = [](const SyncConfig& lhs, const SyncConfig& rhs) {
            auto& lrp = lhs.mOriginalPathOfRemoteRootNode;
            auto& rrp = rhs.mOriginalPathOfRemoteRootNode;

            return lhs.mLocalPath == rhs.mLocalPath && lrp == rrp;
        };

        // Checks if an equivalent config has already been loaded.
        auto present = [&](const SyncConfig& config) {
            for (auto& us : mSyncVec)
            {
                if (equivalent(us->mConfig, config))
                    return true;
            }

            return false;
        };

        // Strip configs that already appear to be present.
        auto j = std::remove_if(configs.begin(), configs.end(), present);
        configs.erase(j, configs.end());
    }

    // No configs? Nothing to import!
    if (configs.empty())
    {
        completion(API_OK);
        return;
    }

    // Create and initialize context.
    ContextPtr context = make_unique<Context>();

    context->mClient = &mClient;
    context->mCompletion = std::move(completion);
    context->mConfigs = std::move(configs);
    context->mConfig = context->mConfigs.begin();
    context->mDeviceHash = mClient.getDeviceidHash();
    context->mSyncs = this;

    LOG_debug << "Attempting to generate backup IDs for "
              << context->mConfigs.size()
              << " imported config(s)...";

    // Generate backup IDs.
    Context::put(std::move(context));
}

void Syncs::exportSyncConfig(JSONWriter& writer, const SyncConfig& config) const
{
    assert(!onSyncThread());

    // Internal configs only for the time being.
    if (!config.mExternalDrivePath.empty())
    {
        LOG_warn << "Skipping export of external backup: "
                 << config.mLocalPath.toPath();
        return;
    }

    string localPath = config.mLocalPath.toPath();
    string remotePath;
    const string& name = config.mName;
    const char* type = SyncConfig::synctypename(config.mSyncType);

    if (const auto* node = mClient.nodeByHandle(config.mRemoteNode))
    {
        // Get an accurate remote path, if possible.
        remotePath = node->displaypath();
    }
    else
    {
        // Otherwise settle for what we had stored.
        remotePath = config.mOriginalPathOfRemoteRootNode;
    }

    writer.beginobject();
    writer.arg_stringWithEscapes("localPath", localPath);
    writer.arg_stringWithEscapes("name", name);
    writer.arg_stringWithEscapes("remotePath", remotePath);
    writer.arg_stringWithEscapes("type", type);

    const auto changeMethod =
      changeDetectionMethodToString(config.mChangeDetectionMethod);

    writer.arg_stringWithEscapes("changeMethod", changeMethod);
    writer.arg("scanInterval", config.mScanIntervalSec);

    writer.endobject();
}

bool Syncs::importSyncConfig(JSON& reader, SyncConfig& config)
{
    assert(!onSyncThread());

    static const string TYPE_CHANGE_METHOD = "changeMethod";
    static const string TYPE_LOCAL_PATH    = "localPath";
    static const string TYPE_NAME          = "name";
    static const string TYPE_REMOTE_PATH   = "remotePath";
    static const string TYPE_SCAN_INTERVAL = "scanInterval";
    static const string TYPE_TYPE          = "type";

    LOG_debug << "Attempting to parse config object: "
              << reader.pos;

    // Default to notification change detection method.
    string changeMethod =
      changeDetectionMethodToString(CDM_NOTIFICATIONS);

    string localPath;
    string name;
    string remotePath;
    string scanInterval;
    string type;

    // Parse config properties.
    for (string key; ; )
    {
        // What property are we parsing?
        key = reader.getname();

        // Have we processed all the properties?
        if (key.empty()) break;

        string value;

        // Extract property value if we can.
        if (!reader.storeobject(&value))
        {
            LOG_err << "Parse error extracting property: "
                    << key
                    << ": "
                    << reader.pos;

            return false;
        }

        if (key == TYPE_CHANGE_METHOD)
        {
            changeMethod = std::move(value);
        }
        if (key == TYPE_LOCAL_PATH)
        {
            localPath = std::move(value);
        }
        else if (key == TYPE_NAME)
        {
            name = std::move(value);
        }
        else if (key == TYPE_REMOTE_PATH)
        {
            remotePath = std::move(value);
        }
        else if (key == TYPE_SCAN_INTERVAL)
        {
            scanInterval = std::move(value);
        }
        else if (key == TYPE_TYPE)
        {
            type = std::move(value);
        }
        else
        {
            LOG_debug << "Skipping unknown property: "
                      << key
                      << ": "
                      << value;
        }
    }

    // Basic validation on properties.
    if (localPath.empty())
    {
        LOG_err << "Invalid config: no local path defined.";
        return false;
    }

    if (name.empty())
    {
        LOG_err << "Invalid config: no name defined.";
        return false;
    }

    if (remotePath.empty())
    {
        LOG_err << "Invalid config: no remote path defined.";
        return false;
    }

    reader.unescape(&localPath);
    reader.unescape(&name);
    reader.unescape(&remotePath);
    reader.unescape(&type);

    // Populate config object.
    config.mBackupId = UNDEF;
    config.mBackupState = SYNC_BACKUP_NONE;
    config.mEnabled = false;
    config.mError = NO_SYNC_ERROR;
    config.mFilesystemFingerprint = 0;
    config.mLocalPath = LocalPath::fromAbsolutePath(localPath);
    config.mName = std::move(name);
    config.mOriginalPathOfRemoteRootNode = remotePath;
    config.mWarning = NO_SYNC_WARNING;

    // Set node handle if possible.
    if (const auto* root = mClient.nodeByPath(remotePath.c_str()))
    {
        config.mRemoteNode = root->nodeHandle();
    }
    else
    {
        LOG_err << "Invalid config: "
                << "unable to find node for remote path: "
                << remotePath;

        return false;
    }

    // Set change detection method.
    config.mChangeDetectionMethod =
      changeDetectionMethodFromString(changeMethod);

    if (config.mChangeDetectionMethod == CDM_UNKNOWN)
    {
        LOG_err << "Invalid config: "
                << "unknown change detection method: "
                << changeMethod;

        return false;
    }

    // Set scan interval.
    if (config.mChangeDetectionMethod == CDM_PERIODIC_SCANNING)
    {
        std::istringstream istream(scanInterval);

        istream >> config.mScanIntervalSec;

        auto failed = istream.fail() || !istream.eof();

        if (failed || !config.mScanIntervalSec)
        {
            LOG_err << "Invalid config: "
                    << "malformed scan interval: "
                    << scanInterval;

            return false;
        }
    }

    // Set type.
    if (!config.synctypefromname(type, config.mSyncType))
    {
        LOG_err << "Invalid config: "
                << "unknown sync type name: "
                << type;

        return false;
    }

    // Config's been parsed.
    LOG_debug << "Config successfully parsed.";

    return true;
}

bool Syncs::importSyncConfigs(const string& data, SyncConfigVector& configs)
{
    assert(!onSyncThread());

    static const string TYPE_CONFIGS = "configs";

    JSON reader(data);

    LOG_debug << "Attempting to import configs from: "
              << data;

    // Enter configs object.
    if (!reader.enterobject())
    {
        LOG_err << "Parse error entering root object: "
                << reader.pos;

        return false;
    }

    // Parse sync configs.
    for (string key; ; )
    {
        // What property are we parsing?
        key = reader.getname();

        // Is it a property we know about?
        if (key != TYPE_CONFIGS)
        {
            // Have we hit the end of the configs object?
            if (key.empty()) break;

            // Skip unknown properties.
            string object;

            if (!reader.storeobject(&object))
            {
                LOG_err << "Parse error skipping unknown property: "
                        << key
                        << ": "
                        << reader.pos;

                return false;
            }

            LOG_debug << "Skipping unknown property: "
                      << key
                      << ": "
                      << object;

            // Parse the next property.
            continue;
        }

        LOG_debug << "Found configs property: "
                  << reader.pos;

        // Enter array of sync configs.
        if (!reader.enterarray())
        {
            LOG_err << "Parse error entering configs array: "
                    << reader.pos;

            return false;
        }

        // Parse each sync config object.
        while (reader.enterobject())
        {
            SyncConfig config;

            // Try and parse this sync config object.
            if (!importSyncConfig(reader, config)) return false;

            if (!reader.leaveobject())
            {
                LOG_err << "Parse error leaving config object: "
                        << reader.pos;
                return false;
            }

            configs.emplace_back(std::move(config));
        }

        if (!reader.leavearray())
        {
            LOG_err << "Parse error leaving configs array: "
                    << reader.pos;

            return false;
        }

        LOG_debug << configs.size()
                  << " config(s) successfully parsed.";
    }

    // Leave configs object.
    if (!reader.leaveobject())
    {
        LOG_err << "Parse error leaving root object: "
                << reader.pos;

        return false;
    }

    return true;
}

SyncConfigIOContext* Syncs::syncConfigIOContext()
{
    assert(onSyncThread());

    // Has a suitable IO context already been created?
    if (mSyncConfigIOContext)
    {
        // Yep, return a reference to it.
        return mSyncConfigIOContext.get();
    }

//TODO: User access is not thread safe
    // Which user are we?
    User* self = mClient.ownuser();
    if (!self)
    {
        LOG_warn << "syncConfigIOContext: own user not available";
        return nullptr;
    }

    // Try and retrieve this user's config data attribute.
    auto* payload = self->getattr(ATTR_JSON_SYNC_CONFIG_DATA);
    if (!payload)
    {
        // Attribute hasn't been created yet.
        LOG_warn << "syncConfigIOContext: JSON config data is not available";
        return nullptr;
    }

    // Try and decrypt the payload.
    assert(!memcmp(syncKey.key, mClient.key.key, sizeof(syncKey.key)));
    unique_ptr<TLVstore> store(
      TLVstore::containerToTLVrecords(payload, &syncKey));

    if (!store)
    {
        // Attribute is malformed.
        LOG_err << "syncConfigIOContext: JSON config data is malformed";
        return nullptr;
    }

    // Convenience.
    constexpr size_t KEYLENGTH = SymmCipher::KEYLENGTH;

    // Verify payload contents.
    string authKey;
    string cipherKey;
    string name;

    if (!store->get("ak", authKey) || authKey.size() != KEYLENGTH ||
        !store->get("ck", cipherKey) || cipherKey.size() != KEYLENGTH ||
        !store->get("fn", name) || name.size() != KEYLENGTH)
    {
        // Payload is malformed.
        LOG_err << "syncConfigIOContext: JSON config data is incomplete";
        return nullptr;
    }

    // Create the IO context.
    mSyncConfigIOContext.reset(
      new SyncConfigIOContext(*fsaccess,
                                  std::move(authKey),
                                  std::move(cipherKey),
                                  Base64::btoa(name),
                                  rng));

    // Return a reference to the new IO context.
    return mSyncConfigIOContext.get();
}

void Syncs::clear_inThread()
{
    assert(onSyncThread());

    assert(!mSyncConfigStore);

    mSyncConfigStore.reset();
    mSyncConfigIOContext.reset();
    {
        lock_guard<mutex> g(mSyncVecMutex);
        mSyncVec.clear();
    }
    mSyncVecIsEmpty = true;
    syncKey.setkey((byte*)"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0");
    stallReport = SyncStallInfo();
    triggerHandles.clear();
    localnodeByScannedFsid.clear();
    localnodeBySyncedFsid.clear();
    localnodeByNodeHandle.clear();
    mSyncFlags.reset(new SyncFlags);
    mHeartBeatMonitor.reset(new BackupMonitor(*this));
    mFileChangingCheckState.clear();

    if (syncscanstate)
    {
        assert(onSyncThread());
        mClient.app->syncupdate_scanning(false);
        syncscanstate = false;
    }

    if (syncBusyState)
    {
        assert(onSyncThread());
        mClient.app->syncupdate_syncing(false);
        syncBusyState = false;
    }

    syncStallState = false;
    syncConflictState = false;

    totalLocalNodes = 0;
}

vector<NodeHandle> Syncs::getSyncRootHandles(bool mustBeActive)
{
    assert(!onSyncThread() || onSyncThread());  // still called via checkSyncConfig, currently

    lock_guard<mutex> g(mSyncVecMutex);

    vector<NodeHandle> v;
    for (auto& s : mSyncVec)
    {
        if (!mustBeActive || s->mSync)
        {
            v.emplace_back(s->mConfig.mRemoteNode);
        }
    }
    return v;
}

void Syncs::appendNewSync(const SyncConfig& c, bool startSync, bool notifyApp, std::function<void(error, SyncError, handle)> completion, bool completionInClient, const string& logname)
{
    assert(!onSyncThread());
    assert(c.mBackupId != UNDEF);

    auto clientCompletion = [this, completion](error e, SyncError se, handle backupId)
    {
        queueClient([e, se, backupId, completion](MegaClient& mc, DBTableTransactionCommitter& committer)
            {
                if (completion) completion(e, se, backupId);
            });
    };

    queueSync([=]()
    {
        appendNewSync_inThread(c, startSync, notifyApp, completionInClient ? clientCompletion : completion, logname);
    });
}

void Syncs::appendNewSync_inThread(const SyncConfig& c, bool startSync, bool notifyApp, std::function<void(error, SyncError, handle)> completion, const string& logname)
{
    assert(onSyncThread());

    // Get our hands on the sync config store.
    auto* store = syncConfigStore();

    // Can we get our hands on the config store?
    if (!store)
    {
        LOG_err << "Unable to add backup "
            << c.mLocalPath.toPath()
            << " on "
            << c.mExternalDrivePath.toPath()
            << " as there is no config store.";

        if (completion)
            completion(API_EINTERNAL, c.mError, c.mBackupId);

        return;
    }

    // Do we already know about this drive?
    if (!store->driveKnown(c.mExternalDrivePath))
    {
        // Are we adding an internal sync?
        if (c.isInternal())
        {
            LOG_debug << "Drive for internal syncs not known: " << c.mExternalDrivePath;

            // Then signal failure as the internal drive isn't available.
            if (completion)
                completion(API_EFAILED, UNKNOWN_DRIVE_PATH, c.mBackupId);

            return;
        }

        // Restore the drive's backups, if any.
        auto result = backupOpenDrive_inThread(c.mExternalDrivePath);

        if (result != API_OK && result != API_ENOENT)
        {
            // Couldn't read an existing database.
            LOG_err << "Unable to add backup "
                    << c.mLocalPath.toPath()
                    << " on "
                    << c.mExternalDrivePath.toPath()
                    << " as we could not read its config database.";

            if (completion)
                completion(API_EFAILED, c.mError, c.mBackupId);

            return;
        }
    }

    {
        lock_guard<mutex> g(mSyncVecMutex);
        mSyncVec.push_back(unique_ptr<UnifiedSync>(new UnifiedSync(*this, c)));
        mSyncVecIsEmpty = false;
    }

    saveSyncConfig(c);

    mClient.app->sync_added(c);

    if (!startSync)
    {
        if (completion) completion(API_OK, c.mError, c.mBackupId);
        return;
    }

    enableSyncByBackupId_inThread(c.mBackupId, false, false, notifyApp, true, completion, logname);
}

Sync* Syncs::runningSyncByBackupIdForTests(handle backupId) const
{
    assert(!onSyncThread());
    // todo: returning a Sync* is not really thread safe but the tests are using these directly currently.  So long as they only browse the Sync while nothing changes, it should be ok

    lock_guard<mutex> g(mSyncVecMutex);
    for (auto& s : mSyncVec)
    {
        if (s->mSync && s->mConfig.mBackupId == backupId)
        {
            return s->mSync.get();
        }
    }
    return nullptr;
}

bool Syncs::syncConfigByBackupId(handle backupId, SyncConfig& c) const
{
    // returns a copy for thread safety
    assert(!onSyncThread());

    lock_guard<mutex> g(mSyncVecMutex);
    for (auto& s : mSyncVec)
    {
        if (s->mConfig.mBackupId == backupId)
        {
            c = s->mConfig;

            // double check we updated fsfp_t
            if (s->mSync)
            {
                assert(c.mFilesystemFingerprint == s->mSync->fsfp);

                // just in case, for now
                c.mFilesystemFingerprint = s->mSync->fsfp;
            }

            return true;
        }
    }

    return false;
}

void Syncs::transferPauseFlagsUpdated(bool downloadsPaused, bool uploadsPaused)
{
    assert(!onSyncThread());

    queueSync([this, downloadsPaused, uploadsPaused]() {

        assert(onSyncThread());
        lock_guard<mutex> g(mSyncVecMutex);

        mDownloadsPaused = downloadsPaused;
        mUploadsPaused = uploadsPaused;

        for (auto& us : mSyncVec)
        {
            mHeartBeatMonitor->updateOrRegisterSync(*us);
        }
    });
}

void Syncs::forEachUnifiedSync(std::function<void(UnifiedSync&)> f)
{
    // This function is deprecated; very few still using it, eventually we should remove it

    assert(!onSyncThread());

    for (auto& s : mSyncVec)
    {
        f(*s);
    }
}

void Syncs::forEachRunningSync(bool includePaused, std::function<void(Sync* s)> f) const
{
    // This function is deprecated; very few still using it, eventually we should remove it

    assert(!onSyncThread());

    lock_guard<mutex> g(mSyncVecMutex);
    for (auto& s : mSyncVec)
    {
        if (s->mSync && (includePaused || !s->mConfig.mTemporarilyPaused))
        {
            f(s->mSync.get());
        }
    }
}

bool Syncs::forEachRunningSync_shortcircuit(bool includePaused, std::function<bool(Sync* s)> f)
{
    // only used in MegaApiImpl::syncPathState.
    // We'll move that functionality into Syncs once we can do so without too much difference from develop branch
    assert(!onSyncThread());

    lock_guard<mutex> g(mSyncVecMutex);
    for (auto& s : mSyncVec)
    {
        if (s->mSync && (includePaused || !s->mConfig.mTemporarilyPaused))
        {
            if (!f(s->mSync.get()))
            {
                return false;
            }
        }
    }
    return true;
}

void Syncs::stopSyncsInErrorState()
{
    assert(onSyncThread());

    // An error has occurred, and it's time to destroy the in-RAM structures
    // If the sync db should be kept, then we already null'd the sync->syncstatecache
    for (auto& unifiedSync : mSyncVec)
    {
        if (unifiedSync->mSync &&
            unifiedSync->mConfig.mError != NO_SYNC_ERROR)
        {
            unifiedSync->mSync.reset();
        }
    }
}

void Syncs::purgeRunningSyncs()
{
    assert(!onSyncThread());
    syncRun([&](){ purgeRunningSyncs_inThread(); });
}

void Syncs::purgeRunningSyncs_inThread()
{
    assert(onSyncThread());

    // Called from locallogout (which always happens on ~MegaClient as well as on request)
    // Any syncs that are running should be resumed on next start.
    // We stop the syncs here, but don't call the client to say they are stopped.
    // And localnode databases are preserved.
    for (auto& s : mSyncVec)
    {
        if (s->mSync)
        {
            // Deleting the sync will close/save the sync's localnode database file in its current state.
            // And then delete objects in RAM.
            s->mSync.reset();
        }
    }
}

void Syncs::renameSync(handle backupId, const string& newname, std::function<void(Error e)> completion)
{
    assert(!onSyncThread());
    assert(completion);

    auto clientCompletion = [this, completion](Error e)
    {
        queueClient([completion, e](MegaClient& mc, DBTableTransactionCommitter& committer)
            {
                completion(e);
            });
    };
    queueSync([this, backupId, newname, clientCompletion]()
        {
            renameSync_inThread(backupId, newname, clientCompletion);
        });
}

void Syncs::renameSync_inThread(handle backupId, const string& newname, std::function<void(Error e)> completion)
{
    assert(onSyncThread());

    lock_guard<mutex> g(mSyncVecMutex);

    for (auto &i : mSyncVec)
    {
        if (i->mConfig.mBackupId == backupId)
        {
            i->mConfig.mName = newname;

            // cause an immediate `sp` command to update the backup/sync heartbeat master record
            mHeartBeatMonitor->updateOrRegisterSync(*i);

            // queue saving the change locally
            if (mSyncConfigStore) mSyncConfigStore->markDriveDirty(i->mConfig.mExternalDrivePath);

            completion(API_OK);
            return;
        }
    }

    completion(API_EEXIST);
}

void Syncs::disableSyncs(SyncError syncError, bool newEnabledFlag, bool keepSyncDb)
{
    assert(!onSyncThread());

    queueSync([this, syncError, newEnabledFlag, keepSyncDb]()
        {
            assert(onSyncThread());
            SyncConfigVector v = allConfigs();

            int nEnabled = 0;
            for (auto& c : v)
            {
                if (c.getEnabled()) ++nEnabled;
            }

            for (auto& c : v)
            {
                if (c.getEnabled())
                {

                    std::function<void()> completion = nullptr;
                    if (!--nEnabled)
                    {
                        completion = [=](){
                            LOG_info << "Disabled syncs. error = " << syncError;
                            mClient.app->syncs_disabled(syncError);
                        };
                    }

                    disableSyncByBackupId_inThread(c.mBackupId, syncError, newEnabledFlag, keepSyncDb, completion);
                }
            }
        });
}

void Syncs::disableSyncByBackupId(handle backupId, SyncError syncError, bool newEnabledFlag, bool keepSyncDb, std::function<void()> completion)
{
    assert(!onSyncThread());
    queueSync([this, backupId, syncError, newEnabledFlag, keepSyncDb, completion]()
    {
            disableSyncByBackupId_inThread(backupId, syncError, newEnabledFlag, keepSyncDb, completion);
    });
}

void Syncs::disableSyncByBackupId_inThread(handle backupId, SyncError syncError, bool newEnabledFlag, bool keepSyncDb, std::function<void()> completion)
{
    assert(onSyncThread());

    for (auto i = mSyncVec.size(); i--; )
    {
        auto& us = *mSyncVec[i];
        auto& config = us.mConfig;

        if (config.mBackupId == backupId)
        {
            if (syncError == NO_SYNC_ERROR)
            {
                syncError = UNLOADING_SYNC;
            }

            us.changeState(syncError, newEnabledFlag, true, keepSyncDb); //This will cause the later deletion of Sync (not MegaSyncPrivate) object

            mHeartBeatMonitor->updateOrRegisterSync(us);
        }
    }
    if (completion) completion();
}

void Syncs::syncRun(std::function<void()> f)
{
    assert(!onSyncThread());
    std::promise<bool> synchronous;
    syncThreadActions.pushBack([&]()
        {
            f();
            synchronous.set_value(true);
        });

    mSyncFlags->earlyRecurseExitRequested = true;
    waiter.notify();
    synchronous.get_future().get();
}

void Syncs::removeSelectedSyncs(std::function<bool(SyncConfig&, Sync*)> selector, bool keepSyncDb, bool notifyApp, bool unregisterHeartbeat)
{
    assert(!onSyncThread());
    syncRun([&](){ removeSelectedSyncs_inThread(selector, keepSyncDb, notifyApp, unregisterHeartbeat); });
}

void Syncs::removeSelectedSyncs_inThread(std::function<bool(SyncConfig&, Sync*)> selector, bool keepSyncDb, bool notifyApp, bool unregisterHeartbeat)
{
    assert(onSyncThread());

    for (auto i = mSyncVec.size(); i--; )
    {
        if (selector(mSyncVec[i]->mConfig, mSyncVec[i]->mSync.get()))
        {
            removeSyncByIndex(i, keepSyncDb, notifyApp, unregisterHeartbeat);
        }
    }
}

void Syncs::unloadSelectedSyncs(std::function<bool(SyncConfig&, Sync*)> selector, bool newEnabledFlag)
{
    assert(onSyncThread());

    for (auto i = mSyncVec.size(); i--; )
    {
        if (selector(mSyncVec[i]->mConfig, mSyncVec[i]->mSync.get()))
        {
            unloadSyncByIndex(i, newEnabledFlag);
        }
    }
}

void Syncs::locallogout(bool removecaches, bool keepSyncsConfigFile)
{
    assert(!onSyncThread());
    syncRun([&](){ locallogout_inThread(removecaches, keepSyncsConfigFile); });
}

void Syncs::locallogout_inThread(bool removecaches, bool keepSyncsConfigFile)
{
    assert(onSyncThread());
    mExecutingLocallogout = true;

    // NULL the statecachetable databases for Syncs first, then Sync destruction won't remove LocalNodes from them
    // If we are deleting syncs then just remove() the database direct

    for (auto i = mSyncVec.size(); i--; )
    {
        if (Sync* sync = mSyncVec[i]->mSync.get())
        {
            if (sync->statecachetable)
            {
                if (removecaches) sync->statecachetable->remove();
                sync->statecachetable.reset();
            }
        }
    }

    if (removecaches && keepSyncsConfigFile)
    {
        // Special case backward compatibility for MEGAsync
        // The syncs will be disabled, if the user logs back in they can then manually re-enable.

        for (auto& us : mSyncVec)
        {
            if (us->mConfig.getEnabled())
            {
                disableSyncByBackupId_inThread(us->mConfig.mBackupId, LOGGED_OUT, false, false, nullptr);
            }
        }
    }

    if (mSyncConfigStore)
    {
        if (!keepSyncsConfigFile)
        {
            mSyncConfigStore->write(LocalPath(), SyncConfigVector());
        }
        else
        {
            syncConfigStoreFlush();
        }
    }
    mSyncConfigStore.reset();

    // Remove all syncs from RAM.
    unloadSelectedSyncs([](SyncConfig&, Sync*) { return true; }, true);

    // make sure we didn't resurrect the store, singleton style
    assert(!mSyncConfigStore);

    clear_inThread();
    mExecutingLocallogout = false;
}

void Syncs::removeSyncByIndex(size_t index, bool keepSyncDb, bool notifyApp, bool unregisterHeartbeat)
{
    assert(onSyncThread());

    if (index < mSyncVec.size())
    {
        if (auto& syncPtr = mSyncVec[index]->mSync)
        {
            syncPtr->changestate(DECONFIGURING_SYNC, false, false, keepSyncDb);
            assert(!syncPtr->statecachetable);
            syncPtr.reset(); // deletes sync
        }

        if (mSyncConfigStore) mSyncConfigStore->markDriveDirty(mSyncVec[index]->mConfig.mExternalDrivePath);

        SyncConfig configCopy = mSyncVec[index]->mConfig;

        // call back before actual removal (intermediate layer may need to make a temp copy to call client app)
        if (notifyApp)
        {
            assert(onSyncThread());
            mClient.app->sync_removed(configCopy);
        }

        if (unregisterHeartbeat)
        {
            // unregister this sync/backup from API (backup center)
            queueClient([configCopy](MegaClient& mc, DBTableTransactionCommitter& committer)
                {
                    mc.reqs.add(new CommandBackupRemove(&mc, configCopy.mBackupId));

                    if (auto n = mc.nodeByHandle(configCopy.mRemoteNode))
                    {
                        attr_map m;
                        m[AttrMap::string2nameid("dev-id")] = "";
                        m[AttrMap::string2nameid("drv-id")] = "";
                        mc.setattr(n, move(m), nullptr);
                    }
                });
        }

        lock_guard<mutex> g(mSyncVecMutex);
        mSyncVec.erase(mSyncVec.begin() + index);
        mSyncVecIsEmpty = mSyncVec.empty();
    }
}

void Syncs::unloadSyncByIndex(size_t index, bool newEnabledFlag)
{
    assert(onSyncThread());

    if (index < mSyncVec.size())
    {
        if (auto& syncPtr = mSyncVec[index]->mSync)
        {
            // if it was running, the app gets a callback saying it's no longer active
            // SYNC_CANCELED is a special value that means we are shutting it down without changing config
            mSyncVec[index]->changeState(UNLOADING_SYNC, newEnabledFlag, false, true);
            assert(!syncPtr->statecachetable);
            syncPtr.reset(); // deletes sync
        }

        // the sync config is not affected by this operation; it should already be up to date on disk (or be pending)
        // we don't call sync_removed back since the sync is not deleted
        // we don't unregister from the backup/sync heartbeats as the sync can be resumed later

        lock_guard<mutex> g(mSyncVecMutex);
        mSyncVec.erase(mSyncVec.begin() + index);
        mSyncVecIsEmpty = mSyncVec.empty();
    }
}

void Syncs::saveSyncConfig(const SyncConfig& config)
{
    assert(onSyncThread());

    if (auto* store = syncConfigStore())
    {
        store->markDriveDirty(config.mExternalDrivePath);
    }
}

void Syncs::loadSyncConfigsOnFetchnodesComplete(bool resetSyncConfigStore)
{
    assert(!onSyncThread());

    syncThreadActions.pushBack([this, resetSyncConfigStore]()
        {
            loadSyncConfigsOnFetchnodesComplete_inThread(resetSyncConfigStore);
        });
}

void Syncs::resumeSyncsOnStateCurrent()
{
    assert(!onSyncThread());

    syncThreadActions.pushBack([this]()
        {
            resumeSyncsOnStateCurrent_inThread();
        });
}

void Syncs::loadSyncConfigsOnFetchnodesComplete_inThread(bool resetSyncConfigStore)
{
    assert(onSyncThread());

    if (resetSyncConfigStore)
    {
        mSyncConfigStore.reset();
        static_cast<void>(syncConfigStore());
    }

    SyncConfigVector configs;

    if (syncConfigStoreLoad(configs) != API_OK)
    {
        mClient.app->syncs_restored(SYNC_CONFIG_READ_FAILURE);
        return;
    }

    // There should be no syncs yet.
    assert(mSyncVec.empty());

    {
        lock_guard<mutex> g(mSyncVecMutex);
        for (auto& config : configs)
        {
            mSyncVec.push_back(unique_ptr<UnifiedSync>(new UnifiedSync(*this, config)));
            mSyncVecIsEmpty = false;
        }
    }

    for (auto& us : mSyncVec)
    {
        mClient.app->sync_added(us->mConfig);
    }
}

void Syncs::resumeSyncsOnStateCurrent_inThread()
{
    assert(onSyncThread());

    for (auto& unifiedSync : mSyncVec)
    {
        if (!unifiedSync->mSync)
        {
            if (unifiedSync->mConfig.mOriginalPathOfRemoteRootNode.empty())
            {
                // this should only happen on initial migraion from from old caches
                CloudNode cloudNode;
                string cloudNodePath;
                if (lookupCloudNode(unifiedSync->mConfig.mRemoteNode, cloudNode, &cloudNodePath, nullptr, nullptr, nullptr, Syncs::FOLDER_ONLY))
                {
                    unifiedSync->mConfig.mOriginalPathOfRemoteRootNode = cloudNodePath;
                    saveSyncConfig(unifiedSync->mConfig);
                }
            }

            if (unifiedSync->mConfig.getEnabled())
            {

#ifdef __APPLE__
                unifiedSync->mConfig.mFilesystemFingerprint = 0; //for certain MacOS, fsfp seems to vary when restarting. we set it to 0, so that it gets recalculated
#endif
                LOG_debug << "Resuming cached sync: " << toHandle(unifiedSync->mConfig.mBackupId) << " " << unifiedSync->mConfig.getLocalPath().toPath() << " fsfp= " << unifiedSync->mConfig.mFilesystemFingerprint << " error = " << unifiedSync->mConfig.mError;

                enableSyncByBackupId_inThread(unifiedSync->mConfig.mBackupId, false, false, false, false, [this, unifiedSync](error e, SyncError se, handle backupId)
                    {
                        LOG_debug << "Sync autoresumed: " << toHandle(backupId) << " " << unifiedSync->mConfig.getLocalPath().toPath() << " fsfp= " << unifiedSync->mConfig.mFilesystemFingerprint << " error = " << se;
                    }, "");
            }
            else
            {
                LOG_debug << "Sync loaded (but not resumed): " << toHandle(unifiedSync->mConfig.mBackupId) << " " << unifiedSync->mConfig.getLocalPath().toPath() << " fsfp= " << unifiedSync->mConfig.mFilesystemFingerprint << " error = " << unifiedSync->mConfig.mError;
            }
        }
    }

    mClient.app->syncs_restored(NO_SYNC_ERROR);
}

bool Sync::recursiveCollectNameConflicts(list<NameConflict>& conflicts)
{
    assert(syncs.onSyncThread());

    FSNode rootFsNode(localroot->getLastSyncedFSDetails());
    syncRow row{ &cloudRoot, localroot.get(), &rootFsNode };
    SyncPath pathBuffer(syncs, localroot->localname, cloudRootPath);
    recursiveCollectNameConflicts(row, conflicts, pathBuffer);
    return !conflicts.empty();
}

void syncRow::inferOrCalculateChildSyncRows(bool wasSynced, vector<syncRow>& childRows, vector<FSNode>& fsInferredChildren, vector<FSNode>& fsChildren, vector<CloudNode>& cloudChildren,
                bool belowRemovedFsNode, fsid_localnode_map& localnodeByScannedFsid)
{
    // This is the function that determines the list of syncRows that recursiveSync() will operate on for this folder.
    // Each syncRow a (CloudNodes, SyncNodes, FSNodes) tuple that all match up by name (taking escapes/case into account)
    // For the case of a folder that contains already-synced content, and in which nothing changed, we can "infer" the set
    // much more efficiently, by generating it from SyncNodes alone.
    // Otherwise we need to calculate it, which involves sorting the three sets and matching them up.
    // An additional complication is that we try to save RAM by not storing the scanned FSNodes for this folder
    // If we can regenerate the list of FSNodes from the LocalNodes, then we would have done that, and so we
    // need to recreate that list.  So our extra RAM usage is just for the folders on the stack, and not the entire tree.
    // (Plus for those SyncNode folders where regeneration wouldn't match the FSNodes (yet))
    // (SyncNode = LocalNode, we'll rename LocalNode eventually)

    // Effective children are from the last scan, if present.
    vector<FSNode>* effectiveFsChildren = belowRemovedFsNode ? nullptr : syncNode->lastFolderScan.get();

    if (wasSynced && !belowRemovedFsNode && syncNode->sync->inferRegeneratableTriplets(cloudChildren, *syncNode, fsInferredChildren, childRows))
    {
        effectiveFsChildren = &fsInferredChildren;
    }
    else
    {
        // Otherwise, we can reconstruct the filesystem entries from the LocalNodes
        if (!effectiveFsChildren)
        {
            fsChildren.reserve(syncNode->children.size() + 50);  // leave some room for others to be added in syncItem()

            for (auto &childIt : syncNode->children)
            {
                if (belowRemovedFsNode)
                {
                    if (childIt.second->fsid_asScanned != UNDEF)
                    {
                        childIt.second->setScannedFsid(UNDEF, localnodeByScannedFsid, LocalPath(), FileFingerprint());
                        childIt.second->scannedFingerprint = FileFingerprint();
                    }
                }
                else if (childIt.second->fsid_asScanned != UNDEF)
                {
                    fsChildren.emplace_back(childIt.second->getScannedFSDetails());
                }
            }

            effectiveFsChildren = &fsChildren;
        }

        childRows = syncNode->sync->computeSyncTriplets(cloudChildren, *syncNode, *effectiveFsChildren);
    }
}


void Sync::recursiveCollectNameConflicts(syncRow& row, list<NameConflict>& ncs, SyncPath& fullPath)
{
    assert(syncs.onSyncThread());

    assert(row.syncNode);
    if (!row.syncNode->conflictsDetected())
    {
        return;
    }

    // Get sync triplets.
    vector<syncRow> childRows;
    vector<FSNode> fsInferredChildren;
    vector<FSNode> fsChildren;
    vector<CloudNode> cloudChildren;

    if (row.cloudNode)
    {
        syncs.lookupCloudChildren(row.cloudNode->handle, cloudChildren);
    }

    bool wasSynced = false;  // todo
    row.inferOrCalculateChildSyncRows(wasSynced, childRows, fsInferredChildren, fsChildren, cloudChildren, false, syncs.localnodeByScannedFsid);


    for (auto& childRow : childRows)
    {
        if (childRow.hasClashes())
        {
            NameConflict nc;

            if (childRow.hasCloudPresence())
                nc.cloudPath = fullPath.cloudPath;

            if (childRow.hasLocalPresence())
                nc.localPath = fullPath.localPath;

            // Only meaningful if there are no cloud clashes.
            if (auto* c = childRow.cloudNode)
                nc.clashingCloudNames.emplace_back(c->name);

            // Only meaningful if there are no local clashes.
            if (auto* f = childRow.fsNode)
                nc.clashingLocalNames.emplace_back(f->localname);

            for (auto* c : childRow.cloudClashingNames)
                nc.clashingCloudNames.emplace_back(c->name);

            for (auto* f : childRow.fsClashingNames)
                nc.clashingLocalNames.emplace_back(f->localname);

            ncs.emplace_back(std::move(nc));
        }

        // recurse after dealing with all items, so any renames within the folder have been completed
        if (childRow.syncNode && childRow.syncNode->type == FOLDERNODE)
        {

            ScopedSyncPathRestore syncPathRestore(fullPath);

            if (!fullPath.appendRowNames(childRow, mFilesystemType) ||
                localdebris.isContainingPathOf(fullPath.localPath))
            {
                // This is a legitimate case; eg. we only had a syncNode and it is removed in resolve_delSyncNode
                // Or if this is the debris folder, ignore it
                continue;
            }

            recursiveCollectNameConflicts(childRow, ncs, fullPath);
        }
    }
}

bool syncRow::hasClashes() const
{
    return !cloudClashingNames.empty() || !fsClashingNames.empty();
}

bool syncRow::hasCloudPresence() const
{
    return cloudNode || !cloudClashingNames.empty();
}

bool syncRow::hasLocalPresence() const
{
    return fsNode || !fsClashingNames.empty();
}

const LocalPath& syncRow::comparisonLocalname() const
{
    if (syncNode)
    {
        return syncNode->localname;
    }
    else if (fsNode)
    {
        return fsNode->localname;
    }
    else if (!fsClashingNames.empty())
    {
        return fsClashingNames[0]->localname;
    }
    else
    {
        assert(false);
        static LocalPath nullResult;
        return nullResult;
    }
}

SyncRowType syncRow::type() const
{
    auto c = static_cast<unsigned>(cloudNode != nullptr);
    auto s = static_cast<unsigned>(syncNode != nullptr);
    auto f = static_cast<unsigned>(fsNode != nullptr);

    return static_cast<SyncRowType>(c * 4 + s * 2 + f);
}

bool syncRow::ignoreFileChanged() const
{
    assert(syncNode);
    assert(syncNode->type == FOLDERNODE);

    return mIgnoreFileChanged;
}

void syncRow::ignoreFileChanging()
{
    assert(syncNode);
    assert(syncNode->type == FOLDERNODE);

    mIgnoreFileChanged = true;
}

bool syncRow::ignoreFileStable() const
{
    assert(syncNode);
    assert(syncNode->type == FOLDERNODE);

    return !mIgnoreFileChanged
           && !syncNode->waitingForIgnoreFileLoad();
}

ExclusionState syncRow::exclusionState(const CloudNode& node) const
{
    assert(syncNode);
    assert(syncNode->type != FILENODE);

    return syncNode->exclusionState(node.name,
                                    node.type,
                                    node.fingerprint.size);
}

ExclusionState syncRow::exclusionState(const FSNode& node) const
{
    assert(syncNode);
    assert(syncNode->type != FILENODE);

    return syncNode->exclusionState(node.localname,
                                    node.type,
                                    node.fingerprint.size);
}

ExclusionState syncRow::exclusionState(const LocalPath& name, nodetype_t type) const
{
    assert(syncNode);
    assert(syncNode->type != FILENODE);

    return syncNode->exclusionState(name, type);
}

bool syncRow::hasReservedName(const FileSystemAccess& fsAccess) const
{
    if (auto* c = cloudNode)
        return isReservedName(c->name, c->type);

    if (!cloudClashingNames.empty())
    {
        auto* c = cloudClashingNames.front();

        return isReservedName(c->name, c->type);
    }

    if (auto* s = syncNode)
        return isReservedName(fsAccess, s->localname, s->type);

    if (auto* f = fsNode)
        return isReservedName(fsAccess, f->localname, f->type);

    if (!fsClashingNames.empty())
    {
        auto* f = fsClashingNames.front();

        return isReservedName(fsAccess, f->localname, f->type);
    }

    return false;
}

bool syncRow::isIgnoreFile() const
{
    if (auto* s = syncNode)
        return s->isIgnoreFile();

    if (auto* f = fsNode)
        return f->type == FILENODE
               && f->localname == IGNORE_FILE_NAME;

    if (auto* c = cloudNode)
        return c->type == FILENODE
               && c->name == IGNORE_FILE_NAME;

    return false;
}

void Sync::combineTripletSet(vector<syncRow>::iterator a, vector<syncRow>::iterator b) const
{
    assert(syncs.onSyncThread());

//#ifdef DEBUG
//    // log before case
//    LOG_debug << " combineTripletSet BEFORE " << std::distance(a, b);
//    for (auto i = a; i != b; ++i)
//    {
//        LOG_debug
//            << (i->cloudNode ? i->cloudNode->name : "<null>") << " "
//            << (i->syncNode ? i->syncNode->localname.toPath() : "<null>") << " "
//            << (i->fsNode ? i->fsNode->localname.toPath() : "<null>") << " ";
//    }
//#endif

    // match up elements that are still present and were alrady synced
    vector<syncRow>::iterator lastFullySynced = b;
    vector<syncRow>::iterator lastNotFullySynced = b;
    unsigned syncNode_nfs_count = 0;

    for (auto i = a; i != b; ++i)
    {
        if (auto sn = i->syncNode)
        {
            if (!sn->syncedCloudNodeHandle.isUndef())
            {
                for (auto j = a; j != b; ++j)
                {
                    if (j->cloudNode && j->cloudNode->handle == sn->syncedCloudNodeHandle)
                    {
                        std::swap(j->cloudNode, i->cloudNode);
                        break;
                    }
                }
            }
            if (sn->fsid_lastSynced != UNDEF)
            {
                for (auto j = a; j != b; ++j)
                {
                    if (j->fsNode && j->fsNode->fsid == sn->fsid_lastSynced)
                    {
                        std::swap(j->fsNode, i->fsNode);
                        break;
                    }
                }
            }

            // is this row fully synced alrady? if so, put it aside in case there are more syncNodes
            if (i->cloudNode && i->fsNode)
            {
                std::swap(*a, *i);
                lastFullySynced = a;
                ++a;
            }
            else
            {
                lastNotFullySynced = i;
                ++syncNode_nfs_count;
            }
        }
    }

    // if this fails, please figure out how we got into that state
    assert(syncNode_nfs_count < 2);

    // gather up the remaining into a single row, there may be clashes.

    auto targetrow = lastNotFullySynced != b ? lastNotFullySynced :
                     (lastFullySynced != b ? lastFullySynced : a);

    // check for duplicate names as we go. All but one row will be left as all nullptr
    for (auto i = a; i != b; ++i)
    if (i != targetrow)
    {
        if (i->fsNode)
        {
            if (targetrow->fsNode &&
                !(targetrow->syncNode &&
                targetrow->syncNode->fsid_lastSynced
                == targetrow->fsNode->fsid))
            {
                LOG_debug << syncname << "Conflicting filesystem name: "
                    << targetrow->fsNode->localname.toPath();
                targetrow->fsClashingNames.push_back(targetrow->fsNode);
                targetrow->fsNode = nullptr;
            }
            if (targetrow->fsNode ||
                !targetrow->fsClashingNames.empty())
            {
                LOG_debug << syncname << "Conflicting filesystem name: "
                    << i->fsNode->localname.toPath();
                targetrow->fsClashingNames.push_back(i->fsNode);
                i->fsNode = nullptr;
            }
            if (!targetrow->fsNode &&
                targetrow->fsClashingNames.empty())
            {
                std::swap(targetrow->fsNode, i->fsNode);
            }
        }
        if (i->cloudNode)
        {
            if (targetrow->cloudNode &&
                !(targetrow->syncNode &&
                targetrow->syncNode->syncedCloudNodeHandle
                == targetrow->cloudNode->handle))
            {
                LOG_debug << syncname << "Conflicting filesystem name: "
                    << targetrow->cloudNode->name;
                targetrow->cloudClashingNames.push_back(targetrow->cloudNode);
                targetrow->cloudNode = nullptr;
            }
            if (targetrow->cloudNode ||
                !targetrow->cloudClashingNames.empty())
            {
                LOG_debug << syncname << "Conflicting filesystem name: "
                    << i->cloudNode->name;
                targetrow->cloudClashingNames.push_back(i->cloudNode);
                i->cloudNode = nullptr;
            }
            if (!targetrow->cloudNode &&
                targetrow->cloudClashingNames.empty())
            {
                std::swap(targetrow->cloudNode, i->cloudNode);
            }
        }
    }

//#ifdef DEBUG
//    // log after case
//    LOG_debug << " combineTripletSet AFTER " << std::distance(a, b);
//    for (auto i = a; i != b; ++i)
//    {
//        LOG_debug
//            << (i->cloudNode ? i->cloudNode->name : "<null>") << " "
//            << (i->syncNode ? i->syncNode->localname.toPath() : "<null>") << " "
//            << (i->fsNode ? i->fsNode->localname.toPath() : "<null>") << " ";
//        for (auto j : i->cloudClashingNames)
//        {
//            LOG_debug << "with clashing cloud name: " << j->name;
//        }
//        for (auto j : i->fsClashingNames)
//        {
//            LOG_debug << "with clashing fs name: " << j->localname.toPath();
//        }
//    }
//#endif


#ifdef DEBUG
    // confirm all are empty except target
    for (auto i = a; i != b; ++i)
    {
        assert(i == targetrow || i->empty());
    }
#endif
}

auto Sync::computeSyncTriplets(vector<CloudNode>& cloudNodes, const LocalNode& syncParent, vector<FSNode>& fsNodes) const -> vector<syncRow>
{
    assert(syncs.onSyncThread());

    CodeCounter::ScopeTimer rst(syncs.mClient.performanceStats.computeSyncTripletsTime);

    vector<syncRow> triplets;
    triplets.reserve(cloudNodes.size() + syncParent.children.size() + fsNodes.size());

    for (auto& cn : cloudNodes)          triplets.emplace_back(&cn, nullptr, nullptr);
    for (auto& sn : syncParent.children) triplets.emplace_back(nullptr, sn.second, nullptr);
    for (auto& fsn : fsNodes)            triplets.emplace_back(nullptr, nullptr, &fsn);

    bool caseInsensitive = isCaseInsensitive(mFilesystemType);

    auto tripletCompare = [=](const syncRow& lhs, const syncRow& rhs) -> int {

        if (lhs.cloudNode)
        {
            if (rhs.cloudNode)
            {
                return compareUtf(lhs.cloudNode->name, true, rhs.cloudNode->name, true, caseInsensitive);
            }
            else if (rhs.syncNode)
            {
                return compareUtf(lhs.cloudNode->name, true, rhs.syncNode->localname, true, caseInsensitive);
            }
            else // rhs.fsNode
            {
                return compareUtf(lhs.cloudNode->name, true, rhs.fsNode->localname, true, caseInsensitive);
            }
        }
        else if (lhs.syncNode)
        {
            if (rhs.cloudNode)
            {
                return compareUtf(lhs.syncNode->localname, true, rhs.cloudNode->name, true, caseInsensitive);
            }
            else if (rhs.syncNode)
            {
                return compareUtf(lhs.syncNode->localname, true, rhs.syncNode->localname, true, caseInsensitive);
            }
            else // rhs.fsNode
            {
                return compareUtf(lhs.syncNode->localname, true, rhs.fsNode->localname, true, caseInsensitive);
            }
        }
        else // lhs.fsNode
        {
            if (rhs.cloudNode)
            {
                return compareUtf(lhs.fsNode->localname, true, rhs.cloudNode->name, true, caseInsensitive);
            }
            else if (rhs.syncNode)
            {
                return compareUtf(lhs.fsNode->localname, true, rhs.syncNode->localname, true, caseInsensitive);
            }
            else // rhs.fsNode
            {
                return compareUtf(lhs.fsNode->localname, true, rhs.fsNode->localname, true, caseInsensitive);
            }
        }
    };

    std::sort(triplets.begin(), triplets.end(),
           [=](const syncRow& lhs, const syncRow& rhs)
           { return tripletCompare(lhs, rhs) < 0; });

    auto currSet = triplets.begin();
    auto end  = triplets.end();

    while (currSet != end)
    {
        // Determine the next set that are all comparator-equal
        auto nextSet = currSet;
        ++nextSet;
        while (nextSet != end && 0 == tripletCompare(*currSet, *nextSet))
        {
            ++nextSet;
        }

        combineTripletSet(currSet, nextSet);

        currSet = nextSet;
    }

    auto newEnd = std::remove_if(triplets.begin(), triplets.end(), [](syncRow& row){ return row.empty(); });
    triplets.erase(newEnd, triplets.end());

    return triplets;
}

bool Sync::inferRegeneratableTriplets(vector<CloudNode>& cloudChildren, const LocalNode& syncParent, vector<FSNode>& inferredFsNodes, vector<syncRow>& inferredRows) const
{
    assert(syncs.onSyncThread());

    CodeCounter::ScopeTimer rst(syncs.mClient.performanceStats.inferSyncTripletsTime);

    if (cloudChildren.size() != syncParent.children.size()) return false;

    inferredFsNodes.reserve(syncParent.children.size());

    auto cloudHandleLess = [](const CloudNode& a, const CloudNode& b){ return a.handle < b.handle; };
    std::sort(cloudChildren.begin(), cloudChildren.end(), cloudHandleLess);

    for (auto& child : syncParent.children)
    {

        CloudNode compareTo;
        compareTo.handle = child.second->syncedCloudNodeHandle;
        auto iters = std::equal_range(cloudChildren.begin(), cloudChildren.end(), compareTo, cloudHandleLess);

        if (std::distance(iters.first, iters.second) != 1)
        {
            // the node tree has actually changed so we need to run the full algorithm
            return false;
        }

        CloudNode* node = &*iters.first;

        if (node->parentType == FILENODE)
        {
            LOG_err << "Looked up a file version node during infer: " << node->name;
			assert(false);
            return false;
        }

        if (child.second->fsid_asScanned == UNDEF ||
           (!child.second->scannedFingerprint.isvalid && child.second->type == FILENODE))
        {
            // we haven't scanned yet, or the scans don't match up with LocalNodes yet
            return false;
        }

        inferredFsNodes.push_back(child.second->getScannedFSDetails());
        inferredRows.emplace_back(node, child.second, &inferredFsNodes.back());
    }
    return true;
}

using IndexPair = pair<size_t, size_t>;
using IndexPairVector = vector<IndexPair>;

CodeCounter::ScopeStats computeSyncSequencesStats = { "computeSyncSequences" };


static IndexPairVector computeSyncSequences(vector<syncRow>& children)
{
    // No children, no work to be done.
    if (children.empty())
        return IndexPairVector();

    CodeCounter::ScopeTimer rst(computeSyncSequencesStats);

    // Separate our children into those that are ignore files and those that are not.
    auto i = std::partition(children.begin(), children.end(), [](const syncRow& child) {
        return child.isIgnoreFile();
    });

    // No prioritization necessary if there's only a single class of child.
    if (i == children.begin() || i == children.end())
    {
        // Is it a run of regular files?
        if (!children.front().isIgnoreFile())
            return IndexPairVector(1, IndexPair(0, children.size()));

        IndexPairVector sequences;

        sequences.emplace_back(0, children.size());
        sequences.emplace_back(children.size(), children.size());

        return sequences;
    }

    IndexPairVector sequences;

    // Convenience.
    auto j = i - children.begin();

    // Ignore files should be completely processed first.
    sequences.emplace_back(0, j);
    sequences.emplace_back(j, children.size());

    return sequences;
}

bool Sync::recursiveSync(syncRow& row, SyncPath& fullPath, bool belowRemovedCloudNode, bool belowRemovedFsNode, unsigned depth)
{
    assert(syncs.onSyncThread());

    // in case of sync failing while we recurse
    if (getConfig().mError) return false;
    (void)depth;  // just useful for setting conditional breakpoints when debugging

    assert(row.syncNode);
    assert(row.syncNode->type != FILENODE);
    assert(row.syncNode->getLocalPath() == fullPath.localPath);

    // nothing to do for this subtree? Skip traversal
    if (!(row.syncNode->scanRequired() || row.syncNode->mightHaveMoves() || row.syncNode->syncRequired()))
    {
        //SYNC_verbose << syncname << "No scanning/moving/syncing needed at " << logTriplet(row, fullPath);
        return true;
    }

    SYNC_verbose << syncname << (belowRemovedCloudNode ? "belowRemovedCloudNode " : "") << (belowRemovedFsNode ? "belowRemovedFsNode " : "")
        << "Entering folder with "
        << row.syncNode->scanAgain  << "-"
        << row.syncNode->checkMovesAgain << "-"
        << row.syncNode->syncAgain << " ("
        << row.syncNode->conflicts << ") at "
        << fullPath.syncPath;

    row.syncNode->propagateAnySubtreeFlags();

    // Whether we should perform sync actions at this level.
    bool wasSynced = row.syncNode->scanAgain < TREE_ACTION_HERE
                  && row.syncNode->syncAgain < TREE_ACTION_HERE
                  && row.syncNode->checkMovesAgain < TREE_ACTION_HERE;
    bool syncHere = !wasSynced;
    bool recurseHere = true;

    //SYNC_verbose << syncname << "sync/recurse here: " << syncHere << recurseHere;

    // reset this node's sync flag. It will be set again below or while recursing if anything remains to be done.
    auto originalScanAgain = row.syncNode->scanAgain;
    auto originalSyncAgain = row.syncNode->syncAgain;
    row.syncNode->syncAgain = TREE_RESOLVED;

    if (!row.fsNode || belowRemovedFsNode)
    {
        row.syncNode->scanAgain = TREE_RESOLVED;
        row.syncNode->setScannedFsid(UNDEF, syncs.localnodeByScannedFsid, LocalPath(), FileFingerprint());
        syncHere = row.syncNode->parent ? row.syncNode->parent->scanAgain < TREE_ACTION_HERE : true;
        recurseHere = false;  // If we need to scan, we need the folder to exist first - revisit later
        row.syncNode->lastFolderScan.reset();
        belowRemovedFsNode = true; // this flag will prevent us reconstructing from scannedFingerprint etc
    }
    else
    {
        if (row.syncNode->fsid_asScanned == UNDEF ||
            row.syncNode->fsid_asScanned != row.fsNode->fsid)
        {
            row.syncNode->scanAgain = TREE_ACTION_HERE;
            row.syncNode->setScannedFsid(row.fsNode->fsid, syncs.localnodeByScannedFsid, row.fsNode->localname, row.fsNode->fingerprint);
        }
        else if (row.syncNode->scannedFingerprint != row.fsNode->fingerprint)
        {
            // this can change anytime, set it anyway
            row.syncNode->scannedFingerprint = row.fsNode->fingerprint;
        }
    }

    // Do we need to scan this node?
    if (row.syncNode->scanAgain >= TREE_ACTION_HERE)
    {

        if (!row.syncNode->rareRO().scanBlocked)
        {
            // not stalling, so long as we are still scanning.  Destructor resets stall state
            ProgressingMonitor monitor(syncs);
        }

        syncs.mSyncFlags->reachableNodesAllScannedThisPass = false;
        syncHere = row.syncNode->processBackgroundFolderScan(row, fullPath);
    }
    else
    {
        // this will be restored at the end of the function if any nodes below in the tree need it
        row.syncNode->scanAgain = TREE_RESOLVED;
    }

    if (row.syncNode->scanAgain >= TREE_ACTION_HERE)
    {
        // we must return later when we do have the scan data.
        // restore sync flag
        row.syncNode->setSyncAgain(false, true, false);
        SYNC_verbose << syncname << "Early exit from recursiveSync due to no scan data yet. " << logTriplet(row, fullPath);
        syncHere = false;
        recurseHere = false;
    }

    auto originalCheckMovesAgain = row.syncNode->checkMovesAgain;
    auto originalConflicsFlag = row.syncNode->conflicts;

    bool earlyExit = false;

    if (syncHere || recurseHere)
    {
        // Reset these flags before we evaluate each subnode.
        // They could be set again during that processing,
        // And additionally we double check with each child node after, in case we had reason to skip it.
        if (!belowRemovedCloudNode && !belowRemovedFsNode)
        {
            // only expect to resolve these in normal case
            row.syncNode->checkMovesAgain = TREE_RESOLVED;
        }
        row.syncNode->conflicts = TREE_RESOLVED;

        // Get sync triplets.
        vector<syncRow> childRows;
        vector<FSNode> fsInferredChildren;
        vector<FSNode> fsChildren;
        vector<CloudNode> cloudChildren;

        if (row.cloudNode)
        {
            syncs.lookupCloudChildren(row.cloudNode->handle, cloudChildren);
        }

        row.inferOrCalculateChildSyncRows(wasSynced, childRows, fsInferredChildren, fsChildren, cloudChildren, belowRemovedFsNode, syncs.localnodeByScannedFsid);

        bool anyNameConflicts = false;

        // Ignore files must be fully processed before any other child.
        auto sequences = computeSyncSequences(childRows);

        bool ignoreFilePresent = sequences.size() > 1;
        bool hasFilter = !!row.syncNode->rareRO().filterChain;

        if (ignoreFilePresent != hasFilter)
        {
            row.syncNode->ignoreFilterPresenceChanged(ignoreFilePresent, childRows[sequences.front().first].fsNode);
        }

        // Here is where we loop over the syncRows for this folder.
        // We must process things in a particular order
        //    - first, check all rows for involvement in moves (case 0)
        //      such a row is excluded from further checks
        //    - second, check all remainging rows for sync actions on that row (case 1)
        //    - third, recurse into each folder (case 2)
        //      there are various reasons we may skip that, based on flags set by earlier steps
        //    - zeroth, we perform first->third for any .megaignore file before any others
        //      as any change involving .megaignore may affect anything else we do

        for (auto& sequence : sequences)
        {
            // The main three steps: moves, node itself, recurse
            for (unsigned step = 0; step < 3; ++step)
            {
                for (auto i = sequence.first; i != sequence.second; ++i)
                {
                    // Convenience.
                    auto& childRow = childRows[i];

                    // in case of sync failing while we recurse
                    if (getConfig().mError) return false;

                    if (syncs.mSyncFlags->earlyRecurseExitRequested)
                    {
                        // restore flags to at least what they were, for when we revisit on next full recurse
                        row.syncNode->scanAgain = std::max<TreeState>(row.syncNode->scanAgain, originalScanAgain);
                        row.syncNode->syncAgain = std::max<TreeState>(row.syncNode->syncAgain, originalSyncAgain);
                        row.syncNode->checkMovesAgain = std::max<TreeState>(row.syncNode->checkMovesAgain, originalCheckMovesAgain);
                        row.syncNode->conflicts = std::max<TreeState>(row.syncNode->conflicts, originalConflicsFlag);

                        LOG_debug << syncname
                            << "recursiveSync early exit due to pending outside request with "
                            << row.syncNode->scanAgain  << "-"
                            << row.syncNode->checkMovesAgain << "-"
                            << row.syncNode->syncAgain << " ("
                            << row.syncNode->conflicts << ") at "
                            << fullPath.syncPath;

                        return false;
                    }

                    if (!childRow.cloudClashingNames.empty() ||
                        !childRow.fsClashingNames.empty())
                    {
                        anyNameConflicts = true;
                        row.syncNode->setContainsConflicts(false, true, false); // in case we don't call setItem due to !syncHere
                    }
                    childRow.rowSiblings = &childRows;

                    if (auto* s = childRow.syncNode)
                    {
                        if (auto* f = childRow.fsNode)
                        {
                            // Maintain scanned FSID.
                            if (s->fsid_asScanned != f->fsid)
                            {
                                syncs.setScannedFsidReused(f->fsid);
                                s->setScannedFsid(f->fsid, syncs.localnodeByScannedFsid, f->localname, f->fingerprint);
                            }
							else if (s->scannedFingerprint != f->fingerprint)
                            {
                                // Maintain the scanned fingerprint.
                                s->scannedFingerprint = f->fingerprint;
                            }
                        }

                        // Recompute this row's exclusion state.
                        if (s->recomputeExclusionState())
                        {
                            // Make sure we visit this node's children if its filter state
                            // has changed and we're currently recursing below a removed
                            // node.
                            childRow.recurseBelowRemovedCloudNode |= belowRemovedCloudNode;
                            childRow.recurseBelowRemovedFsNode |= belowRemovedFsNode;
                        }
                    }

                    ScopedSyncPathRestore syncPathRestore(fullPath);

                    if (!fullPath.appendRowNames(childRow, mFilesystemType) ||
                        localdebris.isContainingPathOf(fullPath.localPath))
                    {
                        // This is a legitimate case; eg. we only had a syncNode and it is removed in resolve_delSyncNode
                        // Or if this is the debris folder, ignore it
                        continue;
                    }

                    if (childRow.syncNode)
                    {
                        if (childRow.syncNode->getLocalPath() != fullPath.localPath)
                        {
                            auto s = childRow.syncNode->getLocalPath();
                        }

                        childRow.syncNode->reassignUnstableFsidsOnceOnly(childRow.fsNode);
                    }

                    switch (step)
                    {
                    case 0:
                        // first pass: check for any renames within the folder (or other move activity)
                        // these must be processed first, otherwise if another file
                        // was added with a now-renamed name, an upload would be
                        // attached to the wrong node, resulting in node versions
                        if (syncHere || belowRemovedCloudNode || belowRemovedFsNode)
                        {
                            if (!syncItem_checkMoves(childRow, row, fullPath, belowRemovedCloudNode, belowRemovedFsNode))
                            {
                                if (childRow.itemProcessed)
                                {
                                    row.syncNode->setSyncAgain(false, true, false);
                                }
                            }
                        }
                        break;

                    case 1:
                        // second pass: full syncItem processing for each node that wasn't part of a move
                        if (belowRemovedCloudNode)
                        {
                            // when syncing/scanning below a removed cloud node, we just want to collect up scan fsids
                            // and make syncNodes to visit, so we can be sure of detecting all the moves,
                            // in particular contradictroy moves.
                            if (childRow.type() == SRT_XXF && row.exclusionState(*childRow.fsNode) == ES_INCLUDED)
                            {
                                resolve_makeSyncNode_fromFS(childRow, row, fullPath, false);
                            }
                        }
                        else if (belowRemovedFsNode)
                        {
                            // when syncing/scanning below a removed local node, we just want to
                            // and make syncNodes to visit, so we can be sure of detecting all the moves,
                            // in particular contradictroy moves.
                            if (childRow.type() == SRT_CXX && row.exclusionState(*childRow.cloudNode) == ES_INCLUDED)
                            {
                                resolve_makeSyncNode_fromCloud(childRow, row, fullPath, false);
                            }
                        }
                        else if (syncHere && !childRow.itemProcessed)
                        {
                            // normal case: consider all the combinations
                            if (!syncItem(childRow, row, fullPath))
                            {
                                row.syncNode->setSyncAgain(false, true, false);
                            }

                            if (ignoreFilePresent && childRow.isIgnoreFile() && childRow.fsNode)
                            {
                                // Load filters if new/updated.  If it fails, filters are marked invalid
                                bool ok = row.syncNode->loadFiltersIfChanged(childRow.fsNode->fingerprint, fullPath.localPath);

                                if (ok != !row.syncNode->rareRO().badlyFormedIgnoreFilePath)
                                {
                                    if (ok)
                                    {
                                        row.syncNode->rare().badlyFormedIgnoreFilePath.reset();
                                    }
                                    else
                                    {
                                        row.syncNode->rare().badlyFormedIgnoreFilePath.reset(new LocalNode::RareFields::BadlyFormedIgnore(fullPath.localPath, this));
                                        syncs.badlyFormedIgnoreFilePaths.push_back(row.syncNode->rare().badlyFormedIgnoreFilePath);
                                        syncs.mClient.app->syncupdate_filter_error(getConfig());
                                    }
                                }
                            }
                        }
                        if (childRow.syncNode &&
                            childRow.syncNode->type == FILENODE)
                        {
                            childRow.syncNode->checkTreestate(true);
                        }
                        break;

                    case 2:
                        // third and final pass: recurse into the folders
                        if (childRow.syncNode &&
                            childRow.syncNode->type != FILENODE && (
                                (childRow.recurseBelowRemovedCloudNode &&
                                 (childRow.syncNode->scanRequired() || childRow.syncNode->syncRequired()))
                                ||
                                (childRow.recurseBelowRemovedFsNode &&
                                 childRow.syncNode->syncRequired())
                                ||
                                (recurseHere &&
                                !childRow.suppressRecursion &&
                                //!childRow.syncNode->deletedFS &&   we should not check this one, or we won't remove the LocalNode
                                childRow.syncNode->rareRO().removeNodeHere.expired() &&
                                childRow.syncNode->rareRO().unlinkHere.expired() &&
                                !childRow.syncNode->rareRO().moveToHere))) // don't create new LocalNodes under a moving-to folder, we'll move the already existing LocalNodes when the move completes
                        {
                            // Add watches as necessary.
                            if (childRow.fsNode)
                            {
                                auto result = childRow.syncNode->watch(fullPath.localPath, childRow.fsNode->fsid);

                                // Any fatal errors while adding the watch?
                                if (result == WR_FATAL)
                                    changestate(UNABLE_TO_ADD_WATCH, false, true, true);
                            }

                            if (!recursiveSync(childRow, fullPath, belowRemovedCloudNode || childRow.recurseBelowRemovedCloudNode, belowRemovedFsNode || childRow.recurseBelowRemovedFsNode, depth+1))
                            {
                                earlyExit = true;
                            }
                        }
                        if (childRow.syncNode &&
                            childRow.syncNode->type != FILENODE)
                        {
                            childRow.syncNode->checkTreestate(true);
                        }
                        break;

                    }
                }
            }

            if (ignoreFilePresent)
            {
                if (!row.syncNode->filterChainRO().isValid())
                {
                    // we can't calculate what's included, come back when the .megaignore is present and well-formed
                    break;
                }

                // processed already, don't worry about it for the rest
                ignoreFilePresent = false;
            }
        }

        // If we added any FSNodes that aren't part of our scan data (and we think we don't need another scan), add them to the scan data
        if (!row.fsAddedSiblings.empty())
        {
            auto& scan = row.syncNode->lastFolderScan;
            if (scan && row.syncNode->scanAgain < TREE_ACTION_HERE)
            {
                scan->reserve(scan->size() + row.fsAddedSiblings.size());
                for (auto& ptr: row.fsAddedSiblings)
                {
                    scan->push_back(move(ptr));
                }
                row.fsAddedSiblings.clear();
            }
        }

        if (!anyNameConflicts)
        {
            row.syncNode->clearRegeneratableFolderScan(fullPath, childRows);
        }
    }

    // Recompute our LocalNode flags from children
    // Flags for this row could have been set during calls to the node
    // If we skipped a child node this time (or if not), the set-parent
    // flags let us know if future actions are needed at this level
    for (auto& child : row.syncNode->children)
    {
        if (row.ignoreFileStable() && child.second->type != FILENODE)
        {
            row.syncNode->scanAgain = updateTreestateFromChild(row.syncNode->scanAgain, child.second->scanAgain);
            row.syncNode->syncAgain = updateTreestateFromChild(row.syncNode->syncAgain, child.second->syncAgain);
        }
        row.syncNode->checkMovesAgain = updateTreestateFromChild(row.syncNode->checkMovesAgain, child.second->checkMovesAgain);
        row.syncNode->conflicts = updateTreestateFromChild(row.syncNode->conflicts, child.second->conflicts);

        if (child.second->parentSetScanAgain) row.syncNode->setScanAgain(false, true, false, 0);
        if (child.second->parentSetCheckMovesAgain) row.syncNode->setCheckMovesAgain(false, true, false);
        if (child.second->parentSetSyncAgain) row.syncNode->setSyncAgain(false, true, false);
        if (child.second->parentSetContainsConflicts) row.syncNode->setContainsConflicts(false, true, false);

        child.second->parentSetScanAgain = false;  // we should only use this one once
    }

    SYNC_verbose << syncname << (belowRemovedCloudNode ? "belowRemovedCloudNode " : "")
                << "Exiting folder with "
                << row.syncNode->scanAgain  << "-"
                << row.syncNode->checkMovesAgain << "-"
                << row.syncNode->syncAgain << " ("
                << row.syncNode->conflicts << ") at "
                << fullPath.syncPath;

    return !earlyExit;
}

string Sync::logTriplet(syncRow& row, SyncPath& fullPath)
{
    ostringstream s;
    s << " triplet:" <<
        " " << (row.cloudNode ? fullPath.cloudPath : "(null)") <<
        " " << (row.syncNode ? fullPath.syncPath : "(null)") <<
        " " << (row.fsNode ? fullPath.localPath_utf8() : "(null)");
    return s.str();
}

bool Sync::syncItem_checkMoves(syncRow& row, syncRow& parentRow, SyncPath& fullPath,
        bool belowRemovedCloudNode, bool belowRemovedFsNode)
{
    assert(syncs.onSyncThread());
    CodeCounter::ScopeTimer rst(syncs.mClient.performanceStats.syncItemCheckMove);

    // Since we are visiting this node this time, reset its flags-for-parent
    // They should only stay set when the conditions require it
    if (row.syncNode)
    {
        row.syncNode->parentSetScanAgain = false;
        row.syncNode->parentSetCheckMovesAgain = false;
        row.syncNode->parentSetSyncAgain = false;
        row.syncNode->parentSetContainsConflicts = false;
    }
    //

    // todo:  check             if (child->syncable(root))


//todo: this used to be in scan().  But now we create LocalNodes for all - shall we check it in this function
    //// check if this record is to be ignored
    //if (client->app->sync_syncable(this, name.c_str(), localPath))
    //{
    //}
    //else
    //{
    //    LOG_debug << "Excluded: " << name;
    //}

    // Under some circumstances on sync startup, our shortname records can be out of date.
    // If so, we adjust for that here, as the diretories are scanned
    if (row.syncNode && row.fsNode && row.fsNode->shortname)
    {
        if ((!row.syncNode->slocalname ||
            *row.syncNode->slocalname != *row.fsNode->shortname) &&
            row.syncNode->localname != *row.fsNode->shortname)
        {
            LOG_warn << syncname
                     << "Updating slocalname: " << row.fsNode->shortname->toPath()
                     << " at " << fullPath.localPath_utf8()
                     << " was " << (row.syncNode->slocalname ? row.syncNode->slocalname->toPath() : "(null)")
                     << logTriplet(row, fullPath);
            row.syncNode->setnameparent(row.syncNode->parent, row.syncNode->localname, row.fsNode->cloneShortname());
            statecacheadd(row.syncNode);
        }
    }

    if (row.fsNode &&
       (row.fsNode->type == TYPE_UNKNOWN ||
        row.fsNode->fsid == UNDEF))
    {
        // We can't consider moves if we don't even know file/folder or fsid.
        // Skip ahead to plain item comparison where we wil consider exclusion filters.
        return false;
    }

    // First deal with detecting local moves/renames and propagating correspondingly
    // Independent of the syncItem() combos below so we don't have duplicate checks in those.
    // Be careful of unscannable folder entries which may have no detected type or fsid.

    if (row.fsNode &&
        (!row.syncNode || row.syncNode->fsid_lastSynced == UNDEF ||
                          row.syncNode->fsid_lastSynced != row.fsNode->fsid))
    {
        // Don't perform any moves until we know the row's exclusion state.
        if (parentRow.exclusionState(*row.fsNode) == ES_UNKNOWN)
        {
            row.itemProcessed = true;
            row.suppressRecursion = true;

            return true;
        }

        bool rowResult;
        if (checkLocalPathForMovesRenames(row, parentRow, fullPath, rowResult, belowRemovedCloudNode))
        {
            row.itemProcessed = true;
            return rowResult;
        }
    }

    if (row.cloudNode && (!row.syncNode || row.syncNode->syncedCloudNodeHandle.isUndef() ||
        row.syncNode->syncedCloudNodeHandle != row.cloudNode->handle))
    {
        // Don't perform any moves until we know the row's exclusion state.
        if (parentRow.exclusionState(*row.cloudNode) == ES_UNKNOWN)
        {
            row.itemProcessed = true;
            row.suppressRecursion = true;

            return true;
        }

        bool rowResult;
        if (checkCloudPathForMovesRenames(row, parentRow, fullPath, rowResult, belowRemovedFsNode))
        {
            row.itemProcessed = true;
            return rowResult;
        }
    }

    // It turns out that after LocalPath implicitly uses \\?\ and
    // absolute paths, we don't need to worry about reserved names.

    //// Does this row have a reserved name?
    //if (row.hasReservedName(*syncs.fsaccess))
    //{
    //    // Let the client know why the row isn't being synchronized.
    //    ProgressingMonitor monitor(syncs);

    //    monitor.waitingLocal(fullPath.localPath,
    //                         LocalPath(),
    //                         fullPath.cloudPath,
    //                         SyncWaitReason::ItemHasReservedName);

    //    // Don't try and synchronize the row.
    //    row.itemProcessed = true;

    //    // Don't recurse if the row's a directory.
    //    row.suppressRecursion = true;

    //    // Row's not synchronized.
    //    return false;
    //}

    // Avoid syncing nodes that have multiple clashing names
    // Except if we previously had a folder (just itself) synced, allow recursing into that one.
    if (!row.fsClashingNames.empty() || !row.cloudClashingNames.empty())
    {
        if (row.syncNode) row.syncNode->setContainsConflicts(true, false, false);
        else parentRow.syncNode->setContainsConflicts(false, true, false);

        if (row.cloudNode && row.syncNode && row.fsNode &&
            row.syncNode->type == FOLDERNODE &&
            row.cloudNode->handle == row.syncNode->syncedCloudNodeHandle &&
            row.fsNode->fsid != UNDEF && row.fsNode->fsid == row.syncNode->fsid_lastSynced)
        {
            SYNC_verbose << syncname << "Name clashes at this already-synced folder.  We will sync nodes below though." << logTriplet(row, fullPath);
        }
        else
        {
            LOG_debug << syncname << "Multple names clash here.  Excluding this node from sync for now." << logTriplet(row, fullPath);
            row.suppressRecursion = true;
            if (row.syncNode)
            {
                row.syncNode->scanAgain = TREE_RESOLVED;
                row.syncNode->checkMovesAgain = TREE_RESOLVED;
                row.syncNode->syncAgain = TREE_RESOLVED;
            }
            row.itemProcessed = true;
            return true;
        }
    }
    return false;
}


bool Sync::syncItem(syncRow& row, syncRow& parentRow, SyncPath& fullPath)
{
    CodeCounter::ScopeTimer rst(syncs.mClient.performanceStats.syncItem);

    assert(syncs.onSyncThread());

    ////auto isIgnoreFile = row.isIgnoreFile();

    //if (row.fsNode && row.fsNode->type == FILENODE)
    //{
    //    if (!row.fsNode->fingerprint.isvalid)
    //    {

    //        // We were not able to get details of the filesystem item when scanning the directory.
    //        // Consider it a blocked file, and we'll rescan the folder from time to time.
    //        LOG_verbose << sync->syncname << "File/folder was blocked when reading directory, retry later: " << localnodedisplaypath();

    //        // Setting node as scan-blocked. The main loop will check it regularly by weak_ptr
    //        rare().scanBlocked.reset(new RareFields::ScanBlocked(sync->syncs.rng, getLocalPath(), this));
    //        sync->syncs.scanBlockedPaths.push_back(rare().scanBlocked);

    //    }
    //    else
    //    {
    //    }
    //}

    //{

    //    return true;
    //}



    unsigned confirmDeleteCount = 0;
    if (row.syncNode)
    {
        // reset the count pre-emptively in case we don't choose SRT_XSX
        confirmDeleteCount = row.syncNode->confirmDeleteCount;
        row.syncNode->confirmDeleteCount = 0;

        if (row.syncNode->certainlyOrphaned)
        {
            SYNC_verbose << "Removing certainly orphaned LN. " << logTriplet(row, fullPath);
            delete row.syncNode;
            row.syncNode = nullptr;
            if (!row.cloudNode && !row.fsNode) return false;
        }
    }

    // check for cases in progress that we shouldn't be re-evaluating yet
    if (auto* s = row.syncNode)
    {
        // Any rare fields?
        if (s->hasRare())
        {
            // Move in progress?
            if (auto& moveFromHere = s->rare().moveFromHere)
            {
                if (moveFromHere->failed || moveFromHere->syncCodeProcessedResult)
                {
                    // Move's completed.
                    moveFromHere.reset();
                }
                else
                {
                    // Move's still underway.
                    return false;
                }
            }

            // Is this row (effectively) excluded?
            if (s->exclusionState() != ES_INCLUDED)
            {
                // Is it a move target?
                if (auto& moveToHere = s->rare().moveToHere)
                {
                    assert(!moveToHere->failed);
                    assert(!moveToHere->syncCodeProcessedResult);
                    assert(moveToHere->succeeded);

                    // Necessary as excluded rows may not reach CSF.
                    resolve_checkMoveComplete(row, parentRow, fullPath);
                }
            }

            // Unlink in progress?
            if (!s->rare().unlinkHere.expired())
            {
                // Unlink's still underway.
                return false;
            }
        }

        // as it turns out, this is too early.
        // we need to wait for resolve_rowMatched to recognise it
        // otherwise there may be more renames to process first
        //s->checkTransferCompleted(row, parentRow, fullPath);

        // Is the row excluded?
        if (s->exclusionState() == ES_EXCLUDED)
        {
            // Can we remove the node from memory?
            auto removable = true;

            // Excluded ignore files need to remain in memory so that we can
            // track changes made to their filters.
            removable &= !s->isIgnoreFile();

            // Let transfers complete.
            removable &= !s->transferSP;

            // Purge the node from memory.
            if (removable)
            {
                // Extra sanity.
                assert(!s->rareRO().moveFromHere);
                assert(!s->rareRO().moveToHere);

                return resolve_delSyncNode(row, parentRow, fullPath, 100);
            }
        }
    }

    // Check blocked status.  Todo:  figure out use blocked flag clearing
    if (!row.syncNode && row.fsNode && (
        row.fsNode->isBlocked || row.fsNode->type == TYPE_UNKNOWN) &&
        parentRow.syncNode->exclusionState(row.fsNode->localname, TYPE_UNKNOWN) == ES_INCLUDED)
    {
        // so that we can checkForScanBlocked() immediately below
        resolve_makeSyncNode_fromFS(row, parentRow, fullPath, false);
    }
    if (row.syncNode &&
        row.syncNode->checkForScanBlocked(row.fsNode))
    {
        row.suppressRecursion = true;
        row.itemProcessed = true;
        return false;
    }

    auto rowType = row.type();

    //if (rowType != SRT_CSF && row.cloudNode &&
    //    threadSafeState->isNodeHandleFromCompletedUpload(row.cloudNode->handle))
    //{
    //    // pretend we didn't see the cloud node which is a result of upload
    //    // when that node's source may have moved (until we've double checked etc)
    //    // this covers the case of eg. removing the old LocalNode pre-rename/move
    //    LOG_debug << "converting type " << rowType << " to ignore uploading-resolving cloud node." << logTriplet(row, fullPath);
    //    row.cloudNode = nullptr;
    //    rowType = row.type();
    //    if (rowType == SRT_XXX) return false;
    //}

    switch (rowType)
    {
    case SRT_CSF:
    {
        CodeCounter::ScopeTimer rst(syncs.mClient.performanceStats.syncItemCSF);

        // all three exist; compare
        bool fsCloudEqual = syncEqual(*row.cloudNode, *row.fsNode);
        bool cloudEqual = syncEqual(*row.cloudNode, *row.syncNode);
        bool fsEqual = syncEqual(*row.fsNode, *row.syncNode);

        if (fsCloudEqual)
        {
            // success! this row is synced
            if (!cloudEqual || !fsEqual)
            {
                row.syncNode->syncedFingerprint = row.fsNode->fingerprint;
                assert(row.syncNode->syncedFingerprint == row.cloudNode->fingerprint);
                statecacheadd(row.syncNode);
            }

            return resolve_rowMatched(row, parentRow, fullPath);
        }

        if (cloudEqual || isBackupAndMirroring())
        {
            // filesystem changed, put the change
            return resolve_upsync(row, parentRow, fullPath);
        }

        if (fsEqual)
        {
            // cloud has changed, get the change
            return resolve_downsync(row, parentRow, fullPath, true);
        }

        if (auto uploadPtr = threadSafeState->isNodeAnExpectedUpload(row.cloudNode->parentHandle, row.cloudNode->name))
        {
            if (row.cloudNode->fingerprint == *uploadPtr &&
                uploadPtr.get() == row.syncNode->transferSP.get())
            {
                // we uploaded a file and the user already re-updated the local version of the file
                SYNC_verbose << syncname << "Node is a recent upload, while the FSFile is already updated: " << fullPath.cloudPath << logTriplet(row, fullPath);
                row.syncNode->setSyncedNodeHandle(row.cloudNode->handle);
                row.syncNode->syncedFingerprint  = row.cloudNode->fingerprint;
                row.syncNode->transferSP.reset();
                return false;
            }
        }

        // both changed, so we can't decide without the user's help
        return resolve_userIntervention(row, parentRow, fullPath);
    }
    case SRT_XSF:
    {
        CodeCounter::ScopeTimer rst(syncs.mClient.performanceStats.syncItemXSF);

        // cloud item absent
        if (isBackupAndMirroring())
        {
            // for backups, we only change the cloud
            return resolve_upsync(row, parentRow, fullPath);
        }

        if (!row.syncNode->syncedCloudNodeHandle.isUndef()
            && row.syncNode->fsid_lastSynced != UNDEF
            && row.syncNode->fsid_lastSynced == row.fsNode->fsid
            // on disk, content could be changed without an fsid change
            && syncEqual(*row.fsNode, *row.syncNode))
        {
            // used to be fully synced and the fs side still has that version
            // remove in the fs (if not part of a move)
            return resolve_cloudNodeGone(row, parentRow, fullPath);
        }

        // either
        //  - cloud item did not exist before; upsync
        //  - the fs item has changed too; upsync (this lets users recover from the both sides changed state - user deletes the one they don't want anymore)
        return resolve_upsync(row, parentRow, fullPath);
    }
    case SRT_CSX:
    {
        CodeCounter::ScopeTimer rst(syncs.mClient.performanceStats.syncItemCSX);

        // local item not present
        if (isBackupAndMirroring())
        {
            // in mirror mode, make the cloud the same as local
            // if backing up and not mirroring, the resolve_downsync branch will cancel the sync
            return resolve_fsNodeGone(row, parentRow, fullPath);
        }

        // where we uploaded this node but the fsNode moved already, consider the cloud side synced.
        if (row.syncNode->fsid_lastSynced != UNDEF &&
            row.syncNode->syncedCloudNodeHandle.isUndef() &&
            syncEqual(*row.cloudNode, *row.syncNode))
        {
            row.syncNode->setSyncedNodeHandle(row.cloudNode->handle);
            return false;  // let the next pass determine if it's a move
        }

        if (row.syncNode->fsid_lastSynced == UNDEF)
        {
            if (auto uploadPtr = threadSafeState->isNodeAnExpectedUpload(row.cloudNode->parentHandle, row.cloudNode->name))
            {
                // The local file is no longer at the matching position
                // and we need to set it up to be moved/deleted correspondingly
                // Setting the synced fsid without a corresponding FSNode will cause move detection

                SYNC_verbose << syncname << "Node is a recent upload, sync node present, source FS file is no longer here: " << fullPath.cloudPath << logTriplet(row, fullPath);
                row.syncNode->setSyncedNodeHandle(row.cloudNode->handle);
                row.syncNode->setSyncedFsid(uploadPtr->sourceFsid, syncs.localnodeBySyncedFsid, uploadPtr->sourceLocalname, nullptr);
                row.syncNode->syncedFingerprint  = row.cloudNode->fingerprint;
                row.syncNode->transferSP.reset();
                return false;
            }
            else
            {
                // fs item did not exist before; downsync
                return resolve_downsync(row, parentRow, fullPath, false);
            }
        }
        else if (//!row.syncNode->syncedCloudNodeHandle.isUndef() &&
                 row.syncNode->syncedCloudNodeHandle != row.cloudNode->handle)
        {
            // the cloud item has changed too; downsync (this lets users recover from both sides changed state - user deletes the one they don't want anymore)
            return resolve_downsync(row, parentRow, fullPath, false);
        }
        else
        {
            // It might be a removal; it might be that the local fs item was moved during upload and now the cloud node from the upload has appeared
            // resolve_fsNodeGone only removes if we know the fs item isn't anywhere else in the sync filesystem subtrees.

            if (row.syncNode->fsid_lastSynced != UNDEF
                && !row.syncNode->syncedCloudNodeHandle.isUndef()
                // for cloud nodes, same handle must be same content
                && row.syncNode->syncedCloudNodeHandle == row.cloudNode->handle)
            {
                // used to be fully synced and the cloud side still has that version
                // remove in the cloud (if not part of a move)
                return resolve_fsNodeGone(row, parentRow, fullPath);
            }
            LOG_debug << "interesting case, does it occur?";

            return false;
        }
    }
    case SRT_XSX:
    {
        CodeCounter::ScopeTimer rst(syncs.mClient.performanceStats.syncItemXSX);

        // local and cloud disappeared; remove sync item also
        return resolve_delSyncNode(row, parentRow, fullPath, confirmDeleteCount);
    }
    case SRT_CXF:
    {
        CodeCounter::ScopeTimer rst(syncs.mClient.performanceStats.syncItemCXF);

        switch (parentRow.exclusionState(row.fsNode->localname,
                                   row.fsNode->type))
        {
            case ES_UNKNOWN:
                LOG_verbose << "Exclusion state uknown, come back later: " << logTriplet(row, fullPath);
                return false;

            case ES_EXCLUDED:
                return true;

            case ES_INCLUDED:
                break;
        }

        // Item exists locally and remotely but we haven't synced them previously
        // If they are equal then join them with a Localnode. Othewise report to user.
        // The original algorithm would compare mtime, and if that was equal then size/crc

        if (syncEqual(*row.cloudNode, *row.fsNode))
        {
            return resolve_makeSyncNode_fromFS(row, parentRow, fullPath, false);
        }
        else
        {
            return resolve_userIntervention(row, parentRow, fullPath);
        }
    }
    case SRT_XXF:
    {
        CodeCounter::ScopeTimer rst(syncs.mClient.performanceStats.syncItemXXF);

        // Have we detected a new ignore file?
        if (row.isIgnoreFile())
        {
            // Don't process any other rows.
            parentRow.ignoreFileChanging();

            // Create a sync node to represent the ignore file.
            //
            // This is necessary even when the ignore file itself is excluded as
            // we still need to load the filters it defines.
            return resolve_makeSyncNode_fromFS(row, parentRow, fullPath, false);
        }

        // Don't create a sync node for this file unless we know that it's included.
        if (parentRow.exclusionState(*row.fsNode) != ES_INCLUDED)
            return true;

        // Item exists locally only. Check if it was moved/renamed here, or Create
        // If creating, next run through will upload it
        return resolve_makeSyncNode_fromFS(row, parentRow, fullPath, false);
    }
    case SRT_CXX:
    {
        CodeCounter::ScopeTimer rst(syncs.mClient.performanceStats.syncItemCXX);

        // Don't create sync nodes unless we know the row is included.
        if (parentRow.exclusionState(*row.cloudNode) != ES_INCLUDED)
            return true;

        // Are we creating a node for an ignore file?
        if (row.isIgnoreFile())
        {
            // Then let the parent know we want to process it exclusively.
            parentRow.ignoreFileChanging();
        }

        // item exists remotely only
        return resolve_makeSyncNode_fromCloud(row, parentRow, fullPath, false);
    }
    default:
    {
        // Silence compiler warning.
        break;
    }
    } // switch

    // SRT_XXX  (should not occur)
    // no entries - can occur when names clash, but should be caught above
    CodeCounter::ScopeTimer rstXXX(syncs.mClient.performanceStats.syncItemXXX);
    assert(false);
    return false;
}

bool Sync::resolve_checkMoveComplete(syncRow& row, syncRow& parentRow, SyncPath& fullPath)
{
    // Confirm that the move details are the same as recorded (LocalNodes may have changed or been deleted by now, etc.
    auto movePtr = row.syncNode->rare().moveToHere;
    LocalNode* sourceSyncNode = nullptr;

    LOG_debug << syncname << "Checking move source/target by fsid " << toHandle(movePtr->sourceFsid);

    if ((sourceSyncNode = syncs.findLocalNodeBySyncedFsid(movePtr->sourceFsid, movePtr->sourceType, movePtr->sourceFingerprint, this, nullptr)))
    {
        LOG_debug << syncname << "Sync cloud move/rename from : " << sourceSyncNode->getCloudPath() << " resolved here! " << logTriplet(row, fullPath);

        assert(sourceSyncNode == movePtr->sourcePtr);

        // remove fsid (and handle) from source node, so we don't detect
        // that as a move source anymore
        sourceSyncNode->setSyncedFsid(UNDEF, syncs.localnodeBySyncedFsid, sourceSyncNode->localname, sourceSyncNode->cloneShortname());
        sourceSyncNode->setSyncedNodeHandle(NodeHandle());
        sourceSyncNode->sync->statecacheadd(sourceSyncNode);

        // Move all the LocalNodes under the source node to the new location
        // We can't move the source node itself as the recursive callers may be using it
        sourceSyncNode->moveContentTo(row.syncNode, fullPath.localPath, true);

        row.syncNode->setScanAgain(false, true, true, 0);
        sourceSyncNode->setScanAgain(true, false, false, 0);

        sourceSyncNode->rare().moveFromHere->syncCodeProcessedResult = true;
        sourceSyncNode->rare().moveFromHere.reset();
        sourceSyncNode->trimRareFields();

        // If this node was repurposed for the move, rather than the normal case of creating a fresh one, we remove the old content if it was a folder
        // We have to do this after all processing of sourceSyncNode, in case the source was (through multiple operations) one of the subnodes about to be removed.
        // TODO: however, there is a risk of name collisions - probably we should use a multimap for LocalNode::children.
        for (auto& oldc : movePtr->priorChildrenToRemove)
        {
            for (auto& c : row.syncNode->children)
            {
                if (*c.first == oldc.first && c.second == oldc.second)
                {
                    delete c.second; // removes itself from the parent map
                    break;
                }
            }
        }

    }
    else
    {
        // just alert us to this an double check the case in the debugger
        assert(false);
    }

    // regardless, make sure we don't get stuck
    row.syncNode->rare().moveToHere->syncCodeProcessedResult = true;
    row.syncNode->rare().moveToHere.reset();
    row.syncNode->trimRareFields();

    return sourceSyncNode != nullptr;
}

bool Sync::resolve_rowMatched(syncRow& row, syncRow& parentRow, SyncPath& fullPath)
{
    assert(syncs.onSyncThread());

    //if (threadSafeState->isNodeHandleFromCompletedUpload(row.cloudNode->handle))
    //{
    //    SYNC_verbose << syncname << "Node is a recent upload, skipping LN match for test: " << fullPath.cloudPath << logTriplet(row, fullPath);
    //    return false;
    //}

    // these comparisons may need to be adjusted for UTF, escapes
    assert(row.syncNode->fsid_lastSynced != row.fsNode->fsid || 0 == compareUtf(row.syncNode->localname, true, row.fsNode->localname, true, isCaseInsensitive(mFilesystemType)));
    assert(row.syncNode->fsid_lastSynced == row.fsNode->fsid || 0 == compareUtf(row.syncNode->localname, true, row.fsNode->localname, true, isCaseInsensitive(mFilesystemType)));

    assert((!!row.syncNode->slocalname == !!row.fsNode->shortname) &&
            (!row.syncNode->slocalname ||
            (*row.syncNode->slocalname == *row.fsNode->shortname)));

    if (row.syncNode->fsid_lastSynced != row.fsNode->fsid ||
        row.syncNode->syncedCloudNodeHandle != row.cloudNode->handle ||
        0 != compareUtf(row.syncNode->localname, true, row.fsNode->localname, true, isCaseInsensitive(mFilesystemType)))
    {
        if (row.syncNode->hasRare() && row.syncNode->rare().moveToHere)
        {
            resolve_checkMoveComplete(row, parentRow, fullPath);
        }

        LOG_verbose << syncname << "Row is synced, setting fsid and nodehandle" << logTriplet(row, fullPath);

        if (row.syncNode->type == FOLDERNODE && row.syncNode->fsid_lastSynced != row.fsNode->fsid)
        {
            // a folder disappeared and was replaced by a different one - scan it all
            row.syncNode->setScanAgain(false, true, true, 0);
        }

        row.syncNode->setSyncedFsid(row.fsNode->fsid, syncs.localnodeBySyncedFsid, row.fsNode->localname, row.fsNode->cloneShortname());
        row.syncNode->setSyncedNodeHandle(row.cloudNode->handle);

        row.syncNode->syncedFingerprint = row.fsNode->fingerprint;

        if (row.syncNode->transferSP)
        {
            LOG_debug << "Clearing transfer for matched row at " << logTriplet(row, fullPath);
            row.syncNode->resetTransfer(nullptr);
        }

        statecacheadd(row.syncNode);
        ProgressingMonitor monitor(syncs); // not stalling
    }
    else
    {
        SYNC_verbose << syncname << "Row was already synced" << logTriplet(row, fullPath);
    }

    if (row.syncNode->type == FILENODE)
    {
        row.syncNode->scanAgain = TREE_RESOLVED;
        row.syncNode->checkMovesAgain = TREE_RESOLVED;
        row.syncNode->syncAgain = TREE_RESOLVED;
    }

    return true;
}


bool Sync::resolve_makeSyncNode_fromFS(syncRow& row, syncRow& parentRow, SyncPath& fullPath, bool considerSynced)
{
    assert(syncs.onSyncThread());
    ProgressingMonitor monitor(syncs);

    if (row.fsNode->type == FILENODE && !row.fsNode->fingerprint.isvalid)
    {
        SYNC_verbose << "We can't create a LocalNode yet without a FileFingerprint: " << logTriplet(row, fullPath);

        // we couldn't get the file crc yet (opened by another proecess, etc)
        monitor.waitingLocal(fullPath.localPath, LocalPath(), string(), SyncWaitReason::CantFingrprintFileYet);
        return false;
    }

    // this really is a new node: add
    LOG_debug << syncname << "Creating LocalNode from FS with fsid " << toHandle(row.fsNode->fsid) << " at: " << fullPath.localPath_utf8() << logTriplet(row, fullPath);

    assert(row.syncNode == nullptr);
    row.syncNode = new LocalNode(this);

    row.syncNode->init(row.fsNode->type, parentRow.syncNode, fullPath.localPath, row.fsNode->cloneShortname());
    row.syncNode->setScannedFsid(row.fsNode->fsid, syncs.localnodeByScannedFsid, row.fsNode->localname, row.fsNode->fingerprint);

    if (row.fsNode->type == FILENODE)
    {
        row.syncNode->scannedFingerprint = row.fsNode->fingerprint;
    }

    if (considerSynced)
    {
        // we should be careful about considering synced, eg this might be a node moved from somewhere else
        SYNC_verbose << "Considering this node synced on fs side already: " << toHandle(row.fsNode->fsid);
        row.syncNode->setSyncedFsid(row.fsNode->fsid, syncs.localnodeBySyncedFsid, row.fsNode->localname, row.fsNode->cloneShortname());
        row.syncNode->syncedFingerprint  = row.fsNode->fingerprint;
    }

    if (row.syncNode->type != FILENODE)
    {
        row.syncNode->setScanAgain(false, true, true, 0);
    }

    statecacheadd(row.syncNode);

    //row.syncNode->setSyncAgain(true, false, false);

    return false;
}

bool Sync::resolve_makeSyncNode_fromCloud(syncRow& row, syncRow& parentRow, SyncPath& fullPath, bool considerSynced)
{
    assert(syncs.onSyncThread());
    ProgressingMonitor monitor(syncs);

    SYNC_verbose << syncname << "Creating LocalNode from Cloud at: " << fullPath.cloudPath << logTriplet(row, fullPath);

    assert(row.syncNode == nullptr);
    row.syncNode = new LocalNode(this);

    if (row.cloudNode->type == FILENODE)
    {
        assert(row.cloudNode->fingerprint.isvalid); // todo: move inside considerSynced?
        row.syncNode->syncedFingerprint = row.cloudNode->fingerprint;
    }
    row.syncNode->init(row.cloudNode->type, parentRow.syncNode, fullPath.localPath, nullptr);

    if (auto uploadPtr = threadSafeState->isNodeAnExpectedUpload(row.cloudNode->parentHandle, row.cloudNode->name))
    {
        // If we make a LocalNode for an upload we created ourselves,
        // it's because the local file is no longer at the matching position
        // and we need to set it up to be moved correspondingly
        // Setting the synced fsid without a corresponding FSNode will cause move detection

        SYNC_verbose << syncname << "Node is a recent upload, but source FS file is no longer here: " << fullPath.cloudPath << logTriplet(row, fullPath);
        row.syncNode->setSyncedNodeHandle(row.cloudNode->handle);
        row.syncNode->setSyncedFsid(uploadPtr->sourceFsid, syncs.localnodeBySyncedFsid, uploadPtr->sourceLocalname, nullptr);
        row.syncNode->syncedFingerprint  = row.cloudNode->fingerprint;
        return false;
    }


    if (considerSynced)
    {
        row.syncNode->setSyncedNodeHandle(row.cloudNode->handle);
    }
    if (row.syncNode->type != FILENODE)
    {
        row.syncNode->setSyncAgain(false, true, true);
    }
    statecacheadd(row.syncNode);
    row.syncNode->setSyncAgain(true, false, false);

    return false;
}

bool Sync::resolve_delSyncNode(syncRow& row, syncRow& parentRow, SyncPath& fullPath, unsigned deleteCounter)
{
    assert(syncs.onSyncThread());
    ProgressingMonitor monitor(syncs);

    // Are we deleting an ignore file?
    if (row.syncNode->isIgnoreFile())
    {
        // Then make sure we process it exclusively.
        parentRow.ignoreFileChanging();
    }

    if (row.syncNode->hasRare() &&
        row.syncNode->rare().moveFromHere &&
        !row.syncNode->rare().moveFromHere->syncCodeProcessedResult)
    {
        SYNC_verbose << syncname << "Not deleting still-moving/renaming source node yet." << logTriplet(row, fullPath);
        monitor.waitingCloud(fullPath.cloudPath, "", fullPath.localPath, SyncWaitReason::MoveNeedsDestinationNodeProcessing);
        return false;
    }

    // We need to be sure we really can delete this node.
    // The risk is that it may actually be part of a move
    // and the other end of the move
    // (ie a new LocalNode with matching fsid/nodehandle)
    // is only appearing in this traversal of the tree.
    // It may have appeared but hasn't been evaluated for moves.
    // We may not even have processed that new node/path yet.
    // To work around this, we don't delete on the first go
    // instead, we start a counter.  If we decide that yes
    // this node should be deleted on two consecutive passes
    // then its ok, we've confirmed it really isn't part of a move.


    if (deleteCounter < 1)
    {
        SYNC_verbose << "This LocalNode is a candidate for deletion, we'll confirm on the next pass." << logTriplet(row, fullPath);
        row.syncNode->confirmDeleteCount = (deleteCounter + 1) & 0x3;
        return false;
    }

    // local and cloud disappeared; remove sync item also

    // todo: do we still need any of these flags?
    if (row.syncNode->moveAppliedToLocal)
    {
        SYNC_verbose << syncname << "Deleting Localnode (moveAppliedToLocal)" << logTriplet(row, fullPath);
    }
    else if (row.syncNode->deletedFS)
    {
        SYNC_verbose << syncname << "Deleting Localnode (deletedFS)" << logTriplet(row, fullPath);
    }
    else if (syncs.mSyncFlags->movesWereComplete)
    {
        SYNC_verbose << syncname << "Deleting Localnode (movesWereComplete)" << logTriplet(row, fullPath);
    }
    else
    {
        SYNC_verbose << syncname << "Deleting Localnode" << logTriplet(row, fullPath);
    }

    if (row.syncNode->deletedFS)
    {
        if (row.syncNode->type == FOLDERNODE)
        {
            LOG_debug << syncname << "Sync - local folder deletion detected: " << fullPath.localPath.toPath();
        }
        else
        {
            LOG_debug << syncname << "Sync - local file deletion detected: " << fullPath.localPath.toPath();
        }
    }

    // Are we deleting an ignore file?
    if (row.syncNode->isIgnoreFile())
    {
        // Then make sure the parent's filters are cleared.
        parentRow.syncNode->clearFilters();
    }

    SYNC_verbose << "Deleting LocalNode " << row.syncNode->syncedCloudNodeHandle << " " << toHandle(row.syncNode->fsid_lastSynced) << logTriplet(row, fullPath);
    // deletes itself and subtree, queues db record removal
    delete row.syncNode;
    row.syncNode = nullptr;

    return false;
}

bool Sync::resolve_upsync(syncRow& row, syncRow& parentRow, SyncPath& fullPath)
{
    assert(syncs.onSyncThread());
    ProgressingMonitor monitor(syncs);

    // Don't do anything unless we know the node's included.
    if (row.syncNode->exclusionState() != ES_INCLUDED)
    {
        // Unless the node was already uploading.
        if (!row.syncNode->transferSP)
        {
            // We'll revisit this row later if necessary.
            return true;
        }
    }

    if (row.fsNode->type == FILENODE)
    {
        // Make sure the ignore file is uploaded before processing other rows.
        if (row.syncNode->isIgnoreFile())
        {
            parentRow.ignoreFileChanging();
        }

        // upload the file if we're not already uploading it
        row.syncNode->transferResetUnlessMatched(PUT, row.fsNode->fingerprint);

        shared_ptr<SyncUpload_inClient> existingUpload = std::dynamic_pointer_cast<SyncUpload_inClient>(row.syncNode->transferSP);

        if (existingUpload && !!existingUpload->putnodesStarted)
        {
            // keep the name and target folder details current:

            // if we were already matched with a name that is not exactly the same as toName(), keep using it
            string nodeName = row.cloudNode ? row.cloudNode->name : row.fsNode->localname.toName(*syncs.fsaccess);
            if (nodeName != existingUpload->name)
            {
                LOG_debug << syncname << "Upload name changed, updating: " << existingUpload->name << " to " << nodeName << logTriplet(row, fullPath);
                existingUpload->name = nodeName;  // todo: thread safety
            }

            // make sure the target folder for putnodes is current:
            if (parentRow.cloudNode && parentRow.cloudNode->handle == parentRow.syncNode->syncedCloudNodeHandle)
            {
                if (existingUpload->h != parentRow.cloudNode->handle)
                {
                    LOG_debug << syncname << "Upload target folder changed, updating for putnodes. " << existingUpload->h << " to " << parentRow.cloudNode->handle << logTriplet(row, fullPath);
                    existingUpload->h = parentRow.cloudNode->handle;
                }
            }
            else
            {
                LOG_debug << syncname << "Upload target folder changed and there's no handle, abandoning transfer " << logTriplet(row, fullPath);
                row.syncNode->transferSP.reset();
                return false;
            }
        }

        if (!existingUpload)
        {
            // Don't bother restarting the upload if we're excluded.
            if (row.syncNode->exclusionState() != ES_INCLUDED)
            {
                // We'll revisit later if needed.
                return true;
            }

            // Sanity.
            assert(row.syncNode->parent);
            assert(row.syncNode->parent == parentRow.syncNode);

            if (parentRow.cloudNode && parentRow.cloudNode->handle == parentRow.syncNode->syncedCloudNodeHandle)
            {
                LOG_debug << syncname << "Sync - local file addition detected: " << fullPath.localPath.toPath();

                LOG_debug << syncname << "Uploading file " << fullPath.localPath_utf8() << logTriplet(row, fullPath);
                assert(row.syncNode->scannedFingerprint.isvalid); // LocalNodes for files always have a valid fingerprint
                assert(row.syncNode->scannedFingerprint == row.fsNode->fingerprint);

                // if we were already matched with a name that is not exactly the same as toName(), keep using it
                string nodeName = row.cloudNode ? row.cloudNode->name : row.fsNode->localname.toName(*syncs.fsaccess);

                auto upload = std::make_shared<SyncUpload_inClient>(parentRow.cloudNode->handle,
                    fullPath.localPath, nodeName, row.fsNode->fingerprint, threadSafeState,
                    row.fsNode->fsid, row.fsNode->localname, inshare);

                row.syncNode->queueClientUpload(upload, UseLocalVersioningFlag);  // we'll take care of versioning ourselves ( we take over the putnodes step below)

                LOG_debug << syncname << "Sync - sending file " << fullPath.localPath_utf8();

                if (row.syncNode->syncedCloudNodeHandle.isUndef() && !row.fsNode)
                {
                    // Set the fs-side synced id.
                    // Once the upload completes, potentially the local fs item may have moved.
                    // If that's the case, having this synced fsid set lets us track the local move
                    // and move the uploadPtr from the old LN to the new one.

                    // todo:
                    // Note that the case where the row was already synced is more complex
                    // We may need to generate a 2nd LocalNode for the same Localnode
                    // and these two could operate in parallel - after all, one might
                    // be a move away from this location, and then simulataneous
                    // upload of a replacement file.

                    SYNC_verbose << syncname << "Considering this file synced on the fs side: " << toHandle(row.fsNode->fsid) << logTriplet(row, fullPath);
                    row.syncNode->setSyncedFsid(row.fsNode->fsid, syncs.localnodeBySyncedFsid, row.fsNode->localname, row.fsNode->cloneShortname());
                    row.syncNode->syncedFingerprint  = row.fsNode->fingerprint;
                    statecacheadd(row.syncNode);
                }
            }
            else
            {
                SYNC_verbose << syncname << "Parent cloud folder to upload to doesn't exist yet" << logTriplet(row, fullPath);
                row.syncNode->setSyncAgain(true, false, false);
                monitor.waitingLocal(fullPath.localPath, LocalPath(), fullPath.cloudPath, SyncWaitReason::UpsyncNeedsTargetFolder);
            }
        }
        else if (existingUpload->wasCompleted && !existingUpload->putnodesStarted)
        {
            // We issue putnodes from the sync thread like this because localnodes may have moved/renamed in the meantime
            // And consider that the old target parent node may not even exist anymore

            existingUpload->putnodesStarted = true;

            SYNC_verbose << syncname << "Queueing putnodes for completed upload" << logTriplet(row, fullPath);

            threadSafeState->addExpectedUpload(parentRow.cloudNode->handle, existingUpload->name, existingUpload);

            NodeHandle displaceHandle = row.cloudNode ? row.cloudNode->handle : NodeHandle();
            auto noDebris = inshare;

            syncs.queueClient([existingUpload, displaceHandle, noDebris](MegaClient& mc, DBTableTransactionCommitter& committer)
                {

                    Node* displaceNode = mc.nodeByHandle(displaceHandle);
                    if (displaceNode && mc.versions_disabled)
                    {
                        MegaClient* c = &mc;
                        mc.movetosyncdebris(displaceNode, noDebris,

                            // after the old node is out of the way, we wll putnodes
                            [c, existingUpload](NodeHandle, Error){
                                existingUpload->sendPutnodes(c, NodeHandle());
                            });

                        // putnodes will be executed after or simultaneous with the
                        // move to sync debris
                        return;
                    }

                    // the case where we are making versions, or not displacing something with the same name
                    existingUpload->sendPutnodes(&mc, displaceNode ? displaceNode->nodeHandle() : NodeHandle());
                });
        }
        else if (existingUpload->putnodesStarted)
        {
            SYNC_verbose << syncname << "Upload's putnodes already in progress" << logTriplet(row, fullPath);
        }
        else
        {
            SYNC_verbose << syncname << "Upload already in progress" << logTriplet(row, fullPath);
        }
    }
    else // FOLDERNODE
    {
        if (row.syncNode->hasRare() && !row.syncNode->rare().createFolderHere.expired())
        {
            SYNC_verbose << syncname << "Create folder already in progress" << logTriplet(row, fullPath);
        }
        else
        {
            if (parentRow.cloudNode)
            {
                // there can't be a matching cloud node in this row (for folders), so just toName() is correct
                string foldername = row.syncNode->localname.toName(*syncs.fsaccess);

                // Check for filename anomalies.
                {
                    auto type = isFilenameAnomaly(row.syncNode->localname, foldername);

                    if (type != FILENAME_ANOMALY_NONE)
                    {
                        auto lp = fullPath.localPath;
                        auto rp = fullPath.cloudPath;

                        syncs.queueClient([type, lp, rp](MegaClient& mc, DBTableTransactionCommitter& committer)
                            {
                                mc.filenameAnomalyDetected(type, lp, rp);
                            });
                    }
                }

                LOG_verbose << syncname << "Creating cloud node for: " << fullPath.localPath_utf8() << " as " << foldername << logTriplet(row, fullPath);
                // while the operation is in progress sync() will skip over the parent folder

                NodeHandle targethandle = parentRow.cloudNode->handle;
                auto createFolderPtr = std::make_shared<LocalNode::RareFields::CreateFolderInProgress>();
                row.syncNode->rare().createFolderHere = createFolderPtr;
                syncs.queueClient([foldername, targethandle, createFolderPtr](MegaClient& mc, DBTableTransactionCommitter& committer)
                    {
                        vector<NewNode> nn(1);
                        mc.putnodes_prepareOneFolder(&nn[0], foldername);
                        mc.putnodes(targethandle, NoVersioning, move(nn), nullptr, 0,
                            [createFolderPtr](const Error&, targettype_t, vector<NewNode>&, bool targetOverride){
                                //createFolderPtr.reset();  // lives until this point
                            });

                    });
            }
            else
            {
                SYNC_verbose << "Delay creating cloud node until parent cloud node exists: " << fullPath.localPath_utf8() << logTriplet(row, fullPath);
                row.syncNode->setSyncAgain(true, false, false);
                monitor.waitingLocal(fullPath.localPath, LocalPath(), fullPath.cloudPath, SyncWaitReason::UpsyncNeedsTargetFolder);
            }
        }
        // we may not see some moves/renames until the entire folder structure is created.
        row.syncNode->setCheckMovesAgain(true, false, false);     // todo: double check - might not be needed for the wait case? might cause a stall?
    }
    return false;
}

bool Sync::resolve_downsync(syncRow& row, syncRow& parentRow, SyncPath& fullPath, bool alreadyExists)
{
    assert(syncs.onSyncThread());
    ProgressingMonitor monitor(syncs);

    // Don't do anything unless we know the row's included.
    if (parentRow.exclusionState(*row.cloudNode) != ES_INCLUDED)
    {
        // But only if we weren't already downloading.
        if (!row.syncNode->transferSP)
        {
            // We'll revisit this row later when the filters are stable.
            return true;
        }
    }

    if (isBackup())
    {
        // Backups must not change the local
        changestate(BACKUP_MODIFIED, false, true, false);
        return false;
    }

    // Consider making this a class-wide function.
    auto checkForFilenameAnomaly = [this](const SyncPath& path, const string& name) {
        // Have we encountered an anomalous filename?
        auto type = isFilenameAnomaly(path.localPath, name);

        // Nope so we can bail early.
        if (type == FILENAME_ANOMALY_NONE) return;

        // Get our hands on the relevant paths.
        auto localPath = path.localPath;
        auto remotePath = path.cloudPath;

        // Report the anomaly.
        syncs.queueClient([=](MegaClient& client, DBTableTransactionCommitter&) {
            client.filenameAnomalyDetected(type, localPath, remotePath);
        });
    };

    if (row.cloudNode->type == FILENODE)
    {
        // download the file if we're not already downloading
        // if (alreadyExists), we will move the target to the trash when/if download completes //todo: check

        row.syncNode->transferResetUnlessMatched(GET, row.cloudNode->fingerprint);

        if (!row.syncNode->transferSP)
        {
            // Don't bother restarting the download if we're effectively excluded.
            if (row.syncNode->exclusionState() != ES_INCLUDED)
            {
                // We'll revisit this node later if necessary.
                return true;
            }
        }

        if (parentRow.fsNode)
        {
            auto downloadPtr = std::dynamic_pointer_cast<SyncDownload_inClient>(row.syncNode->transferSP);

            if (!downloadPtr)
            {
                LOG_debug << syncname << "Sync - remote file addition detected: " << row.cloudNode->handle << " " << fullPath.cloudPath;

                // FIXME: to cover renames that occur during the
                // download, reconstruct localname in complete()
                LOG_debug << syncname << "Start sync download: " << row.syncNode << logTriplet(row, fullPath);
                LOG_debug << syncname << "Sync - requesting file " << fullPath.localPath_utf8();

                createDebrisTmpLockOnce();

                // download to tmpfaPath (folder debris/tmp). We will rename/mv it to correct location (updated if necessary) after that completes
                row.syncNode->queueClientDownload(std::make_shared<SyncDownload_inClient>(*row.cloudNode,
                    fullPath.localPath, inshare, *syncs.fsaccess, threadSafeState));

                //row.syncNode->treestate(TREESTATE_SYNCING);
                //parentRow.syncNode->treestate(TREESTATE_SYNCING);

                // If there's a legit .megaignore file present, we use it until (if and when) it is actually replaced.

                //// Are we downloading an ignore file?
                //if (row.syncNode->isIgnoreFile())
                //{
                //    // Then signal that it's downloading.
                //    parentRow.syncNode->setWaitingForIgnoreFileLoad(true);
                //}
            }
            else if (downloadPtr->wasTerminated)
            {
                SYNC_verbose << syncname << "Download was terminated " << logTriplet(row, fullPath);
                row.syncNode->resetTransfer(nullptr);
            }
            else if (downloadPtr->wasCompleted)
            {
                assert(downloadPtr->downloadDistributor);

                // Convenience.
                auto& fsAccess = *syncs.fsaccess;

                // Clarity.
                auto& cloudPath  = fullPath.cloudPath;
                auto& targetPath = fullPath.localPath;

                // Try and move/rename the downloaded file into its new home.
                bool nameTooLong = false;
                bool transientError = false;

                if (downloadPtr->downloadDistributor->distributeTo(targetPath, fsAccess, FileDistributor::MoveReplacedFileToSyncDebris, transientError, nameTooLong, this))
                {
                    // Move was successful.
                    SYNC_verbose << syncname << "Download complete, moved file to final destination" << logTriplet(row, fullPath);

                    // Check for anomalous file names.
                    checkForFilenameAnomaly(fullPath, row.cloudNode->name);

                    // Let the engine know the file exists, even if it hasn't detected it yet.
                    //
                    // This is necessary as filesystem events may be delayed.
                    if (auto fsNode = FSNode::fromPath(fsAccess, targetPath))
                    {
                        parentRow.fsAddedSiblings.emplace_back(std::move(*fsNode));
                        row.fsNode = &parentRow.fsAddedSiblings.back();
                    }

                    // Download was moved into place.
                    downloadPtr->wasDistributed = true;

                    // No longer necessary as the transfer's complete.
                    row.syncNode->resetTransfer(nullptr);

                    // this case is covered when we syncItem

                    //// Have we just downloaded an ignore file?
                    //if (row.syncNode->isIgnoreFile())
                    //{
                    //    // Then load the filters.
                    //    parentRow.syncNode->loadFilters(fullPath.localPath);
                    //}
                }
                else if (nameTooLong)
                {
                    SYNC_verbose << syncname
                                 << "Download complete but the target's name is too long: "
                                 << logTriplet(row, fullPath);

                    monitor.waitingLocal(targetPath,
                                         LocalPath(),
                                         cloudPath,
                                         SyncWaitReason::DownloadTargetNameTooLong);

                    // Leave the transfer intact so we don't reattempt the download.
                }
                else if (transientError)
                {
                    // Transient error while moving download into place.
                    SYNC_verbose << syncname << "Download complete, but move transient error" << logTriplet(row, fullPath);

                    // Let the monitor know what we're up to.
                    monitor.waitingLocal(targetPath, LocalPath(), cloudPath, SyncWaitReason::MovingDownloadToTarget);
                }
                else
                {
                    // Hard error while moving download into place.
                    SYNC_verbose << syncname << "Download complete, but move failed" << logTriplet(row, fullPath);
                    row.syncNode->resetTransfer(nullptr);
                }

                // Rescan our parent if we're operating in periodic scan mode.
                //
                // The reason we're issuing a scan in almost all cases is
                // that we can't tell whether we were able to move any
                // existing file at the donwload target into the debris.
                if (!fsAccess.target_name_too_long && !dirnotify.get())
                    parentRow.syncNode->setScanAgain(false, true, false, 0);
            }
            else
            {
                SYNC_verbose << syncname << "Download already in progress" << logTriplet(row, fullPath);
            }
        }
        else
        {
            SYNC_verbose << "Delay starting download until parent local folder exists: " << fullPath.cloudPath << logTriplet(row, fullPath);
            row.syncNode->setSyncAgain(true, false, false);
            monitor.waitingCloud(fullPath.cloudPath, "", fullPath.localPath, SyncWaitReason::DownsyncNeedsTargetFolder);
        }
    }
    else // FOLDERNODE
    {
        assert(!alreadyExists); // if it did we would have matched it

        if (parentRow.fsNode)
        {
            // Check for and report filename anomalies.
            checkForFilenameAnomaly(fullPath, row.cloudNode->name);

            LOG_verbose << syncname << "Creating local folder at: " << fullPath.localPath_utf8() << logTriplet(row, fullPath);

            assert(!isBackup());
            if (syncs.fsaccess->mkdirlocal(fullPath.localPath, false, true))
            {
                assert(row.syncNode);
                assert(row.syncNode->localname == fullPath.localPath.leafName());

                // Update our records of what we know is on disk for this (parent) LocalNode.
                // This allows the next level of folders to be created too

                auto fa = syncs.fsaccess->newfileaccess(false);
                if (fa->fopen(fullPath.localPath, true, false))
                {
                    auto fsnode = FSNode::fromFOpened(*fa, fullPath.localPath, *syncs.fsaccess);

                    // Mark other nodes with this FSID as having their FSID reused.
                    syncs.setSyncedFsidReused(fsnode->fsid);
                    syncs.setScannedFsidReused(fsnode->fsid);

                    row.syncNode->localname = fsnode->localname;
                    row.syncNode->slocalname = fsnode->cloneShortname();

					// setting synced variables here means we can skip a scan of the parent folder, if just the one expected notification arrives for it
                    row.syncNode->setSyncedNodeHandle(row.cloudNode->handle);
                    row.syncNode->setSyncedFsid(fsnode->fsid, syncs.localnodeBySyncedFsid, fsnode->localname, fsnode->cloneShortname());
					row.syncNode->setScannedFsid(fsnode->fsid, syncs.localnodeByScannedFsid, fsnode->localname, fsnode->fingerprint);
                    statecacheadd(row.syncNode);

                    // So that we can recurse into the new directory immediately.
                    parentRow.fsAddedSiblings.emplace_back(std::move(*fsnode));
                    row.fsNode = &parentRow.fsAddedSiblings.back();

                    row.syncNode->setScanAgain(false, true, true, 0);
                    row.syncNode->setSyncAgain(false, true, false);

                    // set up to skip the fs notification from this folder creation
                    parentRow.syncNode->expectedSelfNotificationCount += 1;  // TODO:  probably different platforms may have different counts, or it may vary, maybe some are skipped or double ups condensed?
                    parentRow.syncNode->scanDelayUntil = std::max<dstime>(parentRow.syncNode->scanDelayUntil, syncs.waiter.ds + 1);
                }
                else
                {
                    LOG_warn << syncname << "Failed to fopen folder straight after creation - revisit in 5s. " << fullPath.localPath_utf8() << logTriplet(row, fullPath);
                    row.syncNode->setScanAgain(true, false, false, 50);
                }
            }
            else if (syncs.fsaccess->target_name_too_long)
            {
                LOG_warn << syncname
                         << "Unable to create target folder as its name is too long "
                         << fullPath.localPath_utf8()
                         << logTriplet(row, fullPath);

                assert(row.syncNode);

                monitor.waitingLocal(fullPath.localPath,
                                     LocalPath(),
                                     fullPath.cloudPath,
                                     SyncWaitReason::CreateFolderNameTooLong);
            }
            else
            {
                // let's consider this case as blocked too, alert the user
                LOG_warn << syncname << "Unable to create folder " << fullPath.localPath_utf8() << logTriplet(row, fullPath);
                assert(row.syncNode);
                monitor.waitingLocal(fullPath.localPath, LocalPath(), fullPath.cloudPath, SyncWaitReason::CreateFolderFailed);
            }
        }
        else
        {
            SYNC_verbose << "Delay creating local folder until parent local folder exists: " << fullPath.localPath_utf8() << logTriplet(row, fullPath);
            row.syncNode->setSyncAgain(true, false, false);
            monitor.waitingCloud(fullPath.cloudPath, "", fullPath.localPath, SyncWaitReason::DownsyncNeedsTargetFolder);
        }

        // we may not see some moves/renames until the entire folder structure is created.
        row.syncNode->setCheckMovesAgain(true, false, false);  // todo: is this still right for the watiing case
    }
    return false;
}


bool Sync::resolve_userIntervention(syncRow& row, syncRow& parentRow, SyncPath& fullPath)
{
    assert(syncs.onSyncThread());
    ProgressingMonitor monitor(syncs);

    if (row.syncNode)
    {
        monitor.waitingCloud(fullPath.cloudPath, string(), fullPath.localPath, SyncWaitReason::LocalAndRemoteChangedSinceLastSyncedState_userMustChoose);
        monitor.waitingLocal(fullPath.localPath, LocalPath(), fullPath.cloudPath, SyncWaitReason::LocalAndRemoteChangedSinceLastSyncedState_userMustChoose);
    }
    else
    {
        monitor.waitingCloud(fullPath.cloudPath, string(), fullPath.localPath, SyncWaitReason::LocalAndRemotePreviouslyUnsyncedDiffer_userMustChoose);
        monitor.waitingLocal(fullPath.localPath, LocalPath(), fullPath.cloudPath, SyncWaitReason::LocalAndRemotePreviouslyUnsyncedDiffer_userMustChoose);
    }
    return false;
}

bool Sync::resolve_cloudNodeGone(syncRow& row, syncRow& parentRow, SyncPath& fullPath)
{
    enum MoveType {
        // Not a possible move.
        MT_NONE,
        // Move is possibly pending.
        MT_PENDING,
        // Move is in progress.
        MT_UNDERWAY
    }; // MoveType

    auto isPossibleCloudMoveSource = [&](string& cloudPath) {
        // Is the move source an ignore file?
        if (row.syncNode->isIgnoreFile())
        {
            // Then it's not subject to move processing.
            return MT_NONE;
        }

        CloudNode cloudNode;
        bool active = false;
        bool nodeIsDefinitelyExcluded = false;
        bool found = false;

        // Does the remote associated with this row exist elsewhere?
        found = syncs.lookupCloudNode(row.syncNode->syncedCloudNodeHandle,
                                      cloudNode,
                                      &cloudPath,
                                      nullptr,
                                      &active,
                                      &nodeIsDefinitelyExcluded,
                                      Syncs::LATEST_VERSION);

        // Remote doesn't exist under an active sync or is excluded.
        if (!found || !active || nodeIsDefinitelyExcluded)
            return MT_NONE;

        // Does the remote represent an ignore file?
        if (cloudNode.isIgnoreFile())
        {
            // Then we know it can't be a move target.
            return MT_NONE;
        }

        // Trim the rare fields.
        row.syncNode->trimRareFields();

        // Is this row a known move source?
        if (auto& movePtr = row.syncNode->rareRO().moveFromHere)
        {
            // Has the move completed?
            if (!movePtr->syncCodeProcessedResult)
            {
                // Move is still underway.
                return MT_UNDERWAY;
            }
        }

        // It's in an active, unpaused sync, and not excluded
        return MT_PENDING;
    };

    assert(syncs.onSyncThread());
    ProgressingMonitor monitor(syncs);

    string cloudPath;

    if (auto mt = isPossibleCloudMoveSource(cloudPath))
    {
        row.syncNode->setCheckMovesAgain(true, false, false);

        row.syncNode->trimRareFields();

        if (mt == MT_UNDERWAY)
        {
            SYNC_verbose << syncname
                         << "Node is a cloud move/rename source, move is under way: "
                         << logTriplet(row, fullPath);
            row.suppressRecursion = true;
        }
        else
        {
            SYNC_verbose << syncname
                         << "Letting move destination node process this first (cloud node is at "
			 << cloudPath
                         << "): "
                         << logTriplet(row, fullPath);
        }

        monitor.waitingCloud(fullPath.cloudPath,
                             cloudPath,
                             fullPath.localPath,
                             SyncWaitReason::MoveNeedsDestinationNodeProcessing);
    }
    else if (row.syncNode->deletedFS)
    {
        SYNC_verbose << syncname << "FS item already removed: " << logTriplet(row, fullPath);
        monitor.noResult();
    }
    else if (syncs.mSyncFlags->movesWereComplete)
    {
        if (isBackup())
        {
            // Backups must not change the local
            changestate(BACKUP_MODIFIED, false, true, false);
            return false;
        }

        if (movetolocaldebris(fullPath.localPath))
        {
            LOG_debug << syncname << "Moved local item to local sync debris: " << fullPath.localPath_utf8() << logTriplet(row, fullPath);
            row.syncNode->setScanAgain(true, false, false, 0);
            row.syncNode->scanAgain = TREE_RESOLVED;

            //todo: remove deletedFS flag, it should be sufficient to suppress recursion now, and parent scan will take care of the rest.

            // don't let revisits do anything until the tree is cleaned up
            row.syncNode->deletedFS = true;
        }
        else
        {
            monitor.waitingLocal(fullPath.localPath, LocalPath(), string(), SyncWaitReason::CouldNotMoveToLocalDebrisFolder);
            LOG_err << syncname << "Failed to move to local debris:  " << fullPath.localPath_utf8();
            // todo: do we need some sort of delay before retry on the next go-round?
        }
    }
    else
    {
        // todo: but, nodes are always current before we call recursiveSync - shortcut this case for nodes?
        SYNC_verbose << syncname << "Wait for scanning+moving to finish before removing local node: " << logTriplet(row, fullPath);
        row.syncNode->setSyncAgain(true, false, false); // make sure we revisit (but don't keep checkMoves set)
        if (parentRow.cloudNode)
        {
            monitor.waitingCloud(fullPath.cloudPath, "", LocalPath(), SyncWaitReason::DeleteWaitingOnMoves);
        }
        else
        {
            monitor.noResult();
        }

        // make sure we are not waiting for ourselves TODO: (or, would this be better done in step 2, recursion (for folders)?)
        row.syncNode->checkMovesAgain = TREE_RESOLVED;
    }

    row.suppressRecursion = true;
    row.recurseBelowRemovedCloudNode = true;

    return false;
}

LocalNode* Syncs::findLocalNodeBySyncedFsid(mega::handle fsid, nodetype_t type, const FileFingerprint& fingerprint, Sync* filesystemSync, std::function<bool(LocalNode* ln)> extraCheck)
{
    assert(onSyncThread());
    if (fsid == UNDEF) return nullptr;

    auto range = localnodeBySyncedFsid.equal_range(fsid);

    for (auto it = range.first; it != range.second; ++it)
    {
        if (it->second->type != type) continue;
        if (it->second->fsidSyncedReused)   continue;

        //todo: make sure that when we compare fsids, they are from the same filesystem.  (eg on windows, same drive)

        if (filesystemSync)
        {
            const auto& root1 = it->second->sync->localroot->localname;
            const auto& root2 = filesystemSync->localroot->localname;

            auto fp1 = fsaccess->fsFingerprint(root1);
            auto fp2 = fsaccess->fsFingerprint(root2);

            if (!fp1 || !fp2 || fp1 != fp2)
            {
                continue;
            }
        }

#ifdef _WIN32
        // (from original sync code) Additionally for windows, check drive letter
        // only consider fsid matches between different syncs for local drives with the
        // same drive letter, to prevent problems with cloned Volume IDs
        if (filesystemSync)
        {
            if (it->second->sync->localroot->localname.driveLetter() !=
                filesystemSync->localroot->localname.driveLetter())
            {
                continue;
            }
        }
#endif
        if (type == FILENODE &&
            (fingerprint.mtime != it->second->syncedFingerprint.mtime ||
                fingerprint.size != it->second->syncedFingerprint.size))
        {
            // fsid match, but size or mtime mismatch
            // treat as different
            continue;
        }

        // If we got this far, it's a good enough match to use
        // todo: come back for other matches?
        if (!extraCheck || extraCheck(it->second))
        {
            LOG_verbose << mClient.clientname << "findLocalNodeBySyncedFsid - found " << toHandle(fsid) << " at: " << it->second->getLocalPath().toPath();
            return it->second;
        }
    }
    return nullptr;
}

LocalNode* Syncs::findLocalNodeByScannedFsid(mega::handle fsid, nodetype_t type, const FileFingerprint* fingerprint, Sync* filesystemSync, std::function<bool(LocalNode* ln)> extraCheck)
{
    assert(onSyncThread());
    if (fsid == UNDEF) return nullptr;

    auto range = localnodeByScannedFsid.equal_range(fsid);

    for (auto it = range.first; it != range.second; ++it)
    {
        if (it->second->type != type) continue;
        if (it->second->fsidScannedReused)   continue;

        //todo: make sure that when we compare fsids, they are from the same filesystem.  (eg on windows, same drive)

        if (filesystemSync)
        {
            const auto& root1 = it->second->sync->localroot->localname;
            const auto& root2 = filesystemSync->localroot->localname;

            auto fp1 = fsaccess->fsFingerprint(root1);
            auto fp2 = fsaccess->fsFingerprint(root2);

            if (!fp1 || !fp2 || fp1 != fp2)
            {
                continue;
            }
        }

#ifdef _WIN32
        // (from original sync code) Additionally for windows, check drive letter
        // only consider fsid matches between different syncs for local drives with the
        // same drive letter, to prevent problems with cloned Volume IDs
        if (filesystemSync)
        {
            if (it->second->sync->localroot->localname.driveLetter() !=
                filesystemSync->localroot->localname.driveLetter())
            {
                continue;
            }
        }
#endif
        if (fingerprint)
        {
            if (type == FILENODE &&
                (fingerprint->mtime != it->second->syncedFingerprint.mtime ||
                    fingerprint->size != it->second->syncedFingerprint.size))
            {
                // fsid match, but size or mtime mismatch
                // treat as different
                continue;
            }
        }

        // If we got this far, it's a good enough match to use
        // todo: come back for other matches?
        if (!extraCheck || extraCheck(it->second))
        {
            LOG_verbose << mClient.clientname << "findLocalNodeByScannedFsid - found at: " << it->second->getLocalPath().toPath();
            return it->second;
        }
    }
    return nullptr;
}

void Syncs::setSyncedFsidReused(mega::handle fsid)
{
    assert(onSyncThread());
    for (auto range = localnodeBySyncedFsid.equal_range(fsid);
        range.first != range.second;
        ++range.first)
    {
        range.first->second->fsidSyncedReused = true;
    }
}

void Syncs::setScannedFsidReused(mega::handle fsid)
{
    assert(onSyncThread());
    for (auto range = localnodeByScannedFsid.equal_range(fsid);
        range.first != range.second;
        ++range.first)
    {
        range.first->second->fsidScannedReused = true;
    }
}

LocalNode* Syncs::findLocalNodeByNodeHandle(NodeHandle h)
{
    assert(onSyncThread());
    if (h.isUndef()) return nullptr;

    auto range = localnodeByNodeHandle.equal_range(h);

    for (auto it = range.first; it != range.second; ++it)
    {
        // check the file/folder actually exists on disk for this LocalNode
        LocalPath lp = it->second->getLocalPath();

        auto prevfa = fsaccess->newfileaccess(false);
        bool exists = prevfa->fopen(lp);
        if (exists || prevfa->type == FOLDERNODE)
        {
            return it->second;
        }
    }
    return nullptr;
}

bool Sync::checkIfFileIsChanging(FSNode& fsNode, const LocalPath& fullPath)
{
    assert(syncs.onSyncThread());
    // code extracted from the old checkpath()

    // logic to prevent moving/uploading files that may still be being updated

    // (original sync code comment:)
    // detect files being updated in the local computer moving the original file
    // to another location as a temporary backup

    assert(fsNode.type == FILENODE);

    bool waitforupdate = false;
    Syncs::FileChangingState& state = syncs.mFileChangingCheckState[fullPath];

    m_time_t currentsecs = m_time();
    if (!state.updatedfileinitialts)
    {
        state.updatedfileinitialts = currentsecs;
    }

    if (currentsecs >= state.updatedfileinitialts)
    {
        if (currentsecs - state.updatedfileinitialts <= Sync::FILE_UPDATE_MAX_DELAY_SECS)
        {
            auto prevfa = syncs.fsaccess->newfileaccess(false);
            if (prevfa->fopen(fullPath))
            {
                LOG_debug << syncname << "File detected in the origin of a move";

                if (currentsecs >= state.updatedfilets)
                {
                    if ((currentsecs - state.updatedfilets) < (Sync::FILE_UPDATE_DELAY_DS / 10))
                    {
                        LOG_verbose << syncname << "currentsecs = " << currentsecs << "  lastcheck = " << state.updatedfilets
                            << "  currentsize = " << prevfa->size << "  lastsize = " << state.updatedfilesize;
                        LOG_debug << "The file size changed too recently. Waiting " << currentsecs - state.updatedfilets << " ds for " << fsNode.localname.toPath();
                        waitforupdate = true;
                    }
                    else if (state.updatedfilesize != prevfa->size)
                    {
                        LOG_verbose << syncname << "currentsecs = " << currentsecs << "  lastcheck = " << state.updatedfilets
                            << "  currentsize = " << prevfa->size << "  lastsize = " << state.updatedfilesize;
                        LOG_debug << "The file size has changed since the last check. Waiting...";
                        state.updatedfilesize = prevfa->size;
                        state.updatedfilets = currentsecs;
                        waitforupdate = true;
                    }
                    else
                    {
                        LOG_debug << syncname << "The file size seems stable";
                    }
                }
                else
                {
                    LOG_warn << syncname << "File checked in the future";
                }

                if (!waitforupdate)
                {
                    if (currentsecs >= prevfa->mtime)
                    {
                        if (currentsecs - prevfa->mtime < (Sync::FILE_UPDATE_DELAY_DS / 10))
                        {
                            LOG_verbose << syncname << "currentsecs = " << currentsecs << "  mtime = " << prevfa->mtime;
                            LOG_debug << syncname << "File modified too recently. Waiting...";
                            waitforupdate = true;
                        }
                        else
                        {
                            LOG_debug << syncname << "The modification time seems stable.";
                        }
                    }
                    else
                    {
                        LOG_warn << syncname << "File modified in the future";
                    }
                }
            }
            else
            {
                if (prevfa->retry)
                {
                    LOG_debug << syncname << "The file in the origin is temporarily blocked. Waiting...";
                    waitforupdate = true;
                }
                else
                {
                    LOG_debug << syncname << "There isn't anything in the origin path";
                }
            }
        }
        else
        {
            syncs.queueClient([](MegaClient& mc, DBTableTransactionCommitter& committer)
                {
                    mc.sendevent(99438, "Timeout waiting for file update", 0);
                });
        }
    }
    else
    {
        LOG_warn << syncname << "File check started in the future";
    }

    if (!waitforupdate)
    {
        syncs.mFileChangingCheckState.erase(fullPath);
    }
    return waitforupdate;
}

bool Sync::resolve_fsNodeGone(syncRow& row, syncRow& parentRow, SyncPath& fullPath)
{
    assert(syncs.onSyncThread());
    ProgressingMonitor monitor(syncs);

    LocalNode* movedLocalNode = nullptr;

    // Has the user removed an ignore file?
    if (row.syncNode->isIgnoreFile())
    {
        // Then make sure we process it exclusively.
        parentRow.ignoreFileChanging();
    }
    // Ignore files aren't subject to the usual move processing.
    else if (!row.syncNode->fsidSyncedReused)
    {
        auto predicate = [&row](LocalNode* n) {
            return n != row.syncNode && !n->isIgnoreFile();
        };

        movedLocalNode =
            syncs.findLocalNodeByScannedFsid(row.syncNode->fsid_lastSynced,
                row.syncNode->type,
                &row.syncNode->syncedFingerprint,
                this,
                std::move(predicate));
    }

    if (movedLocalNode)
    {
        // if we can find the place it moved to, we don't need to wait for scanning be complete
        row.syncNode->setCheckMovesAgain(true, false, false);

        if (row.syncNode->moveAppliedToLocal)
        {
            SYNC_verbose << syncname << "This file/folder was moved, it will be removed next pass: " << logTriplet(row, fullPath);
        }
        else if (row.syncNode->moveApplyingToLocal)
        {
            SYNC_verbose << syncname << "Node was our own cloud move source, move is propagating: " << logTriplet(row, fullPath);
        }
        else
        {
            SYNC_verbose << syncname << "This file/folder was moved, letting destination node at "
                         << movedLocalNode->getLocalPath() << " process this first: " << logTriplet(row, fullPath);
        }
        // todo: do we need an equivalent to row.recurseToScanforNewLocalNodesOnly = true;  (in resolve_cloudNodeGone)
        monitor.waitingLocal(fullPath.localPath, movedLocalNode->getLocalPath(), string(), SyncWaitReason::MoveNeedsDestinationNodeProcessing);
    }
    else if (!syncs.mSyncFlags->scanningWasComplete)
    {
        SYNC_verbose << syncname << "Wait for scanning to finish before confirming fsid " << toHandle(row.syncNode->fsid_lastSynced) << " deleted or moved: " << logTriplet(row, fullPath);
        monitor.waitingLocal(fullPath.localPath, LocalPath(), string(), SyncWaitReason::DeleteOrMoveWaitingOnScanning);
    }
    else if (syncs.mSyncFlags->movesWereComplete)
    {
        if (row.syncNode->rareRO().removeNodeHere.expired())
        {
            // We need to be sure before sending to sync trash.  If we have received
            // a lot of delete notifications, but not yet the corrsponding add that makes it a move
            // then it would be a mistake.  Give the filesystem 2 seconds to deliver that one.
            // On windows at least, under some circumstance, it may first deliver many deletes for the subfolder in a reverse depth first order
            bool timeToBeSure = syncs.waiter.ds - lastFSNotificationTime > 20;

            if (timeToBeSure)
            {
                // What's this node's exclusion state?
                auto exclusionState = row.syncNode->exclusionState();

                if (exclusionState == ES_INCLUDED)
                {
                    // Row's included.
                    LOG_debug << syncname << "Moving cloud item to cloud sync debris: " << fullPath.cloudPath << logTriplet(row, fullPath);
                    bool fromInshare = inshare;
                    auto debrisNodeHandle = row.cloudNode->handle;

                    auto deletePtr = std::make_shared<LocalNode::RareFields::DeleteToDebrisInProgress>();
                    deletePtr->pathDeleting = fullPath.cloudPath;

                    syncs.queueClient([debrisNodeHandle, fromInshare, deletePtr](MegaClient& mc, DBTableTransactionCommitter& committer)
                        {
                            if (auto n = mc.nodeByHandle(debrisNodeHandle, true))
                            {
                                if (n->parent && n->parent->type == FILENODE)
                                {
                                    // if we decided to remove a file, but it turns out not to be
                                    // the latest version of that file, abandon the action
                                    // and let the sync recalculate
                                    LOG_debug << "Sync delete was out of date, there is a more recent version of the file. " << debrisNodeHandle << " " << n->displaypath();
                                    return;
                                }

                                mc.movetosyncdebris(n, fromInshare, [deletePtr](NodeHandle, Error){

                                    // deletePtr lives until this moment
                                    LOG_debug << "Sync delete to sync debris completed: " << deletePtr->pathDeleting;

                                });
                            }
                        });
                    row.syncNode->rare().removeNodeHere = deletePtr;
                }
                else if (exclusionState == ES_EXCLUDED)
                {
                    // Row's excluded.
                    auto& s = *row.syncNode;

                    // Node's no longer associated with any file.
                    s.setScannedFsid(UNDEF, syncs.localnodeByScannedFsid, LocalPath(), FileFingerprint());
                    s.setSyncedFsid(UNDEF, syncs.localnodeBySyncedFsid, s.localname, nullptr);

                    // Persist above changes.
                    statecacheadd(&s);
                }
            }
            else
            {
                SYNC_verbose << syncname << "Waiting to be sure before moving to cloud sync debris: " << fullPath.cloudPath << logTriplet(row, fullPath);
            }
        }
        else
        {
            SYNC_verbose << syncname << "Already moving cloud item to cloud sync debris: " << fullPath.cloudPath << logTriplet(row, fullPath);
        }
    }
    else
    {
        // in case it's actually a move and we just haven't visted the target node yet
        SYNC_verbose << syncname << "Wait for moves to finish before confirming fsid " << toHandle(row.syncNode->fsid_lastSynced) << " deleted: " << logTriplet(row, fullPath);
        monitor.waitingLocal(fullPath.localPath, LocalPath(), string(), SyncWaitReason::DeleteWaitingOnMoves);
    }

    // there's no folder so clear the flag so we don't stall
    row.syncNode->scanAgain = TREE_RESOLVED;
    row.syncNode->checkMovesAgain = TREE_RESOLVED;

    row.suppressRecursion = true;
    row.recurseBelowRemovedFsNode = true;
    row.syncNode->setSyncAgain(true, false, false); // make sure we revisit

    return false;
}

bool Sync::syncEqual(const CloudNode& n, const FSNode& fs)
{
    // Assuming names already match
    if (n.type != fs.type) return false;
    if (n.type != FILENODE) return true;
    assert(n.fingerprint.isvalid && fs.fingerprint.isvalid);
    return n.fingerprint == fs.fingerprint;  // size, mtime, crc
}

bool Sync::syncEqual(const CloudNode& n, const LocalNode& ln)
{
    // return true if this node was previously synced, and the CloudNode fingerprint is equal to the fingerprint from then.
    // Assuming names already match
    // Not comparing nodehandle here.  If they all match we set syncedCloudNodeHandle
    if (n.type != ln.type) return false;
    if (n.type != FILENODE) return true;
    assert(n.fingerprint.isvalid);
    return ln.syncedFingerprint.isvalid &&
            n.fingerprint == ln.syncedFingerprint;  // size, mtime, crc
}

bool Sync::syncEqual(const FSNode& fsn, const LocalNode& ln)
{
    // return true if this node was previously synced, and the FSNode fingerprint is equal to the fingerprint from then.
    // Assuming names already match
    // Not comparing fsid here. If they all match then we set LocalNode's fsid
    if (fsn.type != ln.type) return false;
    if (fsn.type != FILENODE) return true;
    assert(fsn.fingerprint.isvalid);
    return ln.syncedFingerprint.isvalid &&
            fsn.fingerprint == ln.syncedFingerprint;  // size, mtime, crc
}

std::future<size_t> Syncs::triggerFullScan(handle backupID)
{
    assert(!onSyncThread());

    auto indiscriminate = backupID == UNDEF;
    auto notifier = std::make_shared<std::promise<size_t>>();
    auto result = notifier->get_future();

    queueSync([backupID, indiscriminate, notifier, this]() {
        lock_guard<mutex> guard(mSyncVecMutex);
        size_t count = 0;

        for (auto& us : mSyncVec)
        {
            auto* s = us->mSync.get();

            if (!s)
                continue;

            if (!indiscriminate && us->mConfig.mBackupId != backupID)
                continue;

            if (us->mConfig.isScanOnly())
                s->localroot->setScanAgain(false, true, true, 0);

            ++count;

            if (!indiscriminate)
                break;
        }

        notifier->set_value(count);
    });

    return result;
}

void Syncs::triggerSync(NodeHandle h, bool recurse)
{
    assert(!onSyncThread());

    if (mClient.fetchingnodes) return;  // on start everything needs scan+sync anyway

    lock_guard<mutex> g(triggerMutex);
    auto& entry = triggerHandles[h];
    if (recurse) entry = true;
}

std::future<bool> Syncs::moveToLocalDebris(LocalPath path)
{
    assert(!onSyncThread());

    auto notifier = std::make_shared<std::promise<bool>>();
    auto result = notifier->get_future();

    queueSync([notifier, path = std::move(path), this]() mutable {
        // What sync contains this path?
        auto* sync = syncContainingPath(path, true);

        // Move file to local debris if a suitable sync was located.
        notifier->set_value(sync && sync->movetolocaldebris(path));
    });

    return result;
}

void Syncs::processTriggerHandles()
{
    assert(onSyncThread());

    map<NodeHandle, bool> triggers;
    {
        lock_guard<mutex> g(triggerMutex);
        triggers.swap(triggerHandles);
    }

    if (mSyncVec.empty()) return;

    for (auto& t : triggers)
    {
        NodeHandle h = t.first;
        bool recurse = t.second;

        for (;;)
        {
            auto range = localnodeByNodeHandle.equal_range(h);

            if (range.first == range.second)
            {
                // corresponding sync node not found.
                // this could be a move target though, to a syncNode we have not created yet
                // go back up the (cloud) node tree to find an ancestor we can mark as needing sync checks
                CloudNode cloudNode;
                string cloudNodePath;
                bool isInTrash = false;
                bool found = lookupCloudNode(h, cloudNode, &cloudNodePath, &isInTrash, nullptr, nullptr, Syncs::EXACT_VERSION);
                if (found && !isInTrash)
                {
                    // if the parent is a file, then it's just old versions being mentioned in the actionpackets, ignore
                    if (cloudNode.parentType != FILENODE && cloudNode.parentType != TYPE_UNKNOWN && !cloudNode.parentHandle.isUndef())
                    {
                        auto& syncs = *this;
                        SYNC_verbose << mClient.clientname << "Trigger syncNode not found for " << cloudNodePath << ", will trigger parent";
                        recurse = true;
                        h = cloudNode.parentHandle;
                        continue;
                    }
                }
            }
            else
            {
                // we are already being called with the handle of the parent of the thing that changed
                for (auto it = range.first; it != range.second; ++it)
                {
                    auto& syncs = *this;
                    SYNC_verbose << mClient.clientname << "Triggering sync flag for " << it->second->getLocalPath() << (recurse ? " recursive" : "");
                    it->second->setSyncAgain(false, true, recurse);
                }
            }
            break;
        }
    }
}

#ifdef _WIN32
#define PATHSTRING(s) L ## s
#else // _WIN32
#define PATHSTRING(s) s
#endif // ! _WIN32

const LocalPath BACKUP_CONFIG_DIR =
LocalPath::fromPlatformEncodedRelative(PATHSTRING(".megabackup"));

#undef PATHSTRING

const unsigned int NUM_CONFIG_SLOTS = 2;

SyncConfigStore::SyncConfigStore(const LocalPath& dbPath, SyncConfigIOContext& ioContext)
    : mInternalSyncStorePath(dbPath)
    , mIOContext(ioContext)
{
    assert(mInternalSyncStorePath.isAbsolute());
}

SyncConfigStore::~SyncConfigStore()
{
    assert(!dirty());
}

void SyncConfigStore::markDriveDirty(const LocalPath& drivePath)
{
    assert(drivePath.isAbsolute() || drivePath.empty());

    // Drive should be known.
    assert(mKnownDrives.count(drivePath));

    mKnownDrives[drivePath].dirty = true;
}

bool SyncConfigStore::equal(const LocalPath& lhs, const LocalPath& rhs) const
{
    return platformCompareUtf(lhs, false, rhs, false) == 0;
}

bool SyncConfigStore::dirty() const
{
    for (auto& d : mKnownDrives)
    {
        if (d.second.dirty) return true;
    }
    return false;
}

LocalPath SyncConfigStore::dbPath(const LocalPath& drivePath) const
{
    if (drivePath.empty())
    {
        return mInternalSyncStorePath;
    }

    LocalPath dbPath = drivePath;

    dbPath.appendWithSeparator(BACKUP_CONFIG_DIR, false);

    return dbPath;
}

bool SyncConfigStore::driveKnown(const LocalPath& drivePath) const
{
    assert(drivePath.isAbsolute() || drivePath.empty());

    return mKnownDrives.count(drivePath) > 0;
}

vector<LocalPath> SyncConfigStore::knownDrives() const
{
    vector<LocalPath> result;

    for (auto& i : mKnownDrives)
    {
        assert(i.first.empty() || i.first.isAbsolute());
        result.emplace_back(i.first);
    }

    return result;
}

bool SyncConfigStore::removeDrive(const LocalPath& drivePath)
{
    assert(drivePath.isAbsolute() || drivePath.empty());
    return mKnownDrives.erase(drivePath) > 0;
}

error SyncConfigStore::read(const LocalPath& drivePath, SyncConfigVector& configs, bool isExternal)
{
    assert(drivePath.empty() || drivePath.isAbsolute());
    DriveInfo driveInfo;
    //driveInfo.dbPath = dbPath(drivePath);
    driveInfo.drivePath = drivePath;

    vector<unsigned int> confSlots;

    auto result = mIOContext.getSlotsInOrder(dbPath(driveInfo.drivePath), confSlots);

    if (result == API_OK)
    {
        for (const auto& slot : confSlots)
        {
            result = read(driveInfo, configs, slot, isExternal);

            if (result == API_OK)
            {
                driveInfo.slot = (slot + 1) % NUM_CONFIG_SLOTS;
                break;
            }
        }
    }

    if (result != API_EREAD)
    {
        mKnownDrives[drivePath] = driveInfo;
    }

    return result;
}


error SyncConfigStore::write(const LocalPath& drivePath, const SyncConfigVector& configs)
{
    for (const auto& config : configs)
    {
        assert(equal(config.mExternalDrivePath, drivePath));
    }

    // Drive should already be known.
    assert(mKnownDrives.count(drivePath));

    auto& drive = mKnownDrives[drivePath];

    // Always mark drives as clean.
    // This is to avoid us attempting to flush a failing drive forever.
    drive.dirty = false;

    if (configs.empty())
    {
        error e = mIOContext.remove(dbPath(drive.drivePath));
        if (e)
        {
            LOG_warn << "Unable to remove sync configs at: "
                     << drivePath.toPath() << " error " << e;
        }
        return e;
    }
    else
    {
        JSONWriter writer;
        mIOContext.serialize(configs, writer);

        error e = mIOContext.write(dbPath(drive.drivePath),
            writer.getstring(),
            drive.slot);

        if (e)
        {
            LOG_warn << "Unable to write sync configs at: "
                     << drivePath.toPath() << " error " << e;

            return API_EWRITE;
        }

        // start using a different slot (a different file)
        drive.slot = (drive.slot + 1) % NUM_CONFIG_SLOTS;

        // remove the existing slot (if any), since it is obsolete now
        mIOContext.remove(dbPath(drive.drivePath), drive.slot);

        return API_OK;
    }
}


error SyncConfigStore::read(DriveInfo& driveInfo, SyncConfigVector& configs,
                             unsigned int slot, bool isExternal)
{
    auto dbp = dbPath(driveInfo.drivePath);
    string data;

    if (mIOContext.read(dbp, data, slot) != API_OK)
    {
        return API_EREAD;
    }

    JSON reader(data);

    if (!mIOContext.deserialize(dbp, configs, reader, slot, isExternal))
    {
        return API_EREAD;
    }

    const auto& drivePath = driveInfo.drivePath;

    for (auto& config : configs)
    {
        config.mExternalDrivePath = drivePath;

        if (!drivePath.empty())
        {
            // As it came from an external drive, the path is relative
            // but we didn't know that until now, for non-external it's absolute of course
            config.mLocalPath = LocalPath::fromRelativePath(config.mLocalPath.toPath());

            config.mLocalPath.prependWithSeparator(drivePath);
        }
    }

    return API_OK;
}

auto SyncConfigStore::writeDirtyDrives(const SyncConfigVector& configs) -> DriveSet
{
    DriveSet failed;

    for (auto& d : mKnownDrives)
    {
        if (!d.second.dirty) continue;

        const auto& drivePath = d.second.drivePath;

        SyncConfigVector v;

        for (auto& c : configs)
        {
            if (c.mExternalDrivePath == drivePath)
            {
                v.push_back(c);
            }
        }

        error e = write(drivePath, v);
        if (e)
        {
            LOG_err << "Could not write sync configs at "
                    << drivePath.toPath()
                    << " error "
                    << e;

            failed.emplace(drivePath);
        }
    }

    return failed;
}


const string SyncConfigIOContext::NAME_PREFIX = "megaclient_syncconfig_";

SyncConfigIOContext::SyncConfigIOContext(FileSystemAccess& fsAccess,
                                         const string& authKey,
                                         const string& cipherKey,
                                         const string& name,
                                         PrnGen& rng)
  : mCipher()
  , mFsAccess(fsAccess)
  , mName(LocalPath::fromRelativePath(NAME_PREFIX + name))
  , mRNG(rng)
  , mSigner()
{
    // Convenience.
    constexpr size_t KEYLENGTH = SymmCipher::KEYLENGTH;

    // These attributes *must* be sane.
    assert(authKey.size() == KEYLENGTH);
    assert(cipherKey.size() == KEYLENGTH);
    assert(name.size() == Base64Str<KEYLENGTH>::STRLEN);

    // Load the authentication key into our internal signer.
    mSigner.setkey(reinterpret_cast<const byte*>(authKey.data()), KEYLENGTH);

    // Load the encryption key into our internal cipher.
    mCipher.setkey(reinterpret_cast<const byte*>(cipherKey.data()));
}

SyncConfigIOContext::~SyncConfigIOContext()
{
}

bool SyncConfigIOContext::deserialize(const LocalPath& dbPath,
                                      SyncConfigVector& configs,
                                      JSON& reader,
                                      unsigned int slot,
                                      bool isExternal) const
{
    auto path = dbFilePath(dbPath, slot).toPath();

    LOG_debug << "Attempting to deserialize config DB: "
              << path;

    if (deserialize(configs, reader, isExternal))
    {
        LOG_debug << "Successfully deserialized config DB: "
                  << path;

        return true;
    }

    LOG_debug << "Unable to deserialize config DB: "
              << path;

    return false;
}

bool SyncConfigIOContext::deserialize(SyncConfigVector& configs,
                                      JSON& reader, bool isExternal) const
{
    const auto TYPE_SYNCS = MAKENAMEID2('s', 'y');

    if (!reader.enterobject())
    {
        return false;
    }

    for ( ; ; )
    {
        switch (reader.getnameid())
        {
        case EOO:
            return reader.leaveobject();

        case TYPE_SYNCS:
        {
            if (!reader.enterarray())
            {
                return false;
            }

            while (reader.enterobject())
            {
                SyncConfig config;

                if (deserialize(config, reader, isExternal))
                {
                    configs.emplace_back(std::move(config));
                }
                else
                {
                    LOG_err << "Failed to deserialize a sync config";
                    assert(false);
                }

                reader.leaveobject();
            }

            if (!reader.leavearray())
            {
                return false;
            }

            break;
        }

        default:
            if (!reader.storeobject())
            {
                return false;
            }
            break;
        }
    }
}

FileSystemAccess& SyncConfigIOContext::fsAccess() const
{
    return mFsAccess;
}

error SyncConfigIOContext::getSlotsInOrder(const LocalPath& dbPath,
                                           vector<unsigned int>& confSlots)
{
    using std::isdigit;
    using std::sort;

    using SlotTimePair = pair<unsigned int, m_time_t>;

    // Glob for configuration directory.
    LocalPath globPath = dbPath;

    globPath.appendWithSeparator(mName, false);
    globPath.append(LocalPath::fromRelativePath(".?"));

    // Open directory for iteration.
    unique_ptr<DirAccess> dirAccess(mFsAccess.newdiraccess());

    if (!dirAccess->dopen(&globPath, nullptr, true))
    {
        // Couldn't open directory for iteration.
        return API_ENOENT;
    }

    auto fileAccess = mFsAccess.newfileaccess(false);
    LocalPath filePath;
    vector<SlotTimePair> slotTimes;
    nodetype_t type;

    // Iterate directory.
    while (dirAccess->dnext(globPath, filePath, false, &type))
    {
        // Skip directories.
        if (type != FILENODE)
        {
            continue;
        }

        // Determine slot suffix.
        const char suffix = filePath.toPath().back();

        // Skip invalid suffixes.
        if (!isdigit(suffix))
        {
            continue;
        }

        // Determine file's modification time.
        if (!fileAccess->fopen(filePath))
        {
            // Couldn't stat file.
            continue;
        }

        // Record this slot-time pair.
        unsigned int slot = suffix - 0x30; // convert char to int
        slotTimes.emplace_back(slot, fileAccess->mtime);
    }

    // Sort the list of slot-time pairs.
    sort(slotTimes.begin(),
         slotTimes.end(),
         [](const SlotTimePair& lhs, const SlotTimePair& rhs)
         {
             // Order by descending modification time.
             if (lhs.second != rhs.second)
             {
                 return lhs.second > rhs.second;
             }

             // Otherwise by descending slot.
             return lhs.first > rhs.first;
         });

    // Transmit sorted list of slots to the caller.
    for (const auto& slotTime : slotTimes)
    {
        confSlots.emplace_back(slotTime.first);
    }

    return API_OK;
}

error SyncConfigIOContext::read(const LocalPath& dbPath,
                                string& data,
                                unsigned int slot)
{
    // Generate path to the configuration file.
    LocalPath path = dbFilePath(dbPath, slot);

    LOG_debug << "Attempting to read config DB: "
              << path.toPath();

    // Try and open the file for reading.
    auto fileAccess = mFsAccess.newfileaccess(false);

    if (!fileAccess->fopen(path, true, false))
    {
        // Couldn't open the file for reading.
        LOG_err << "Unable to open config DB for reading: "
                << path.toPath();

        return API_EREAD;
    }

    // Try and read the data from the file.
    string d;

    if (!fileAccess->fread(&d, static_cast<unsigned>(fileAccess->size), 0, 0x0))
    {
        // Couldn't read the file.
        LOG_err << "Unable to read config DB: "
                << path.toPath();

        return API_EREAD;
    }

    // Try and decrypt the data.
    if (!decrypt(d, data))
    {
        // Couldn't decrypt the data.
        LOG_err << "Unable to decrypt config DB: "
                << path.toPath();

        return API_EREAD;
    }

    LOG_debug << "Config DB successfully read from disk: "
              << path.toPath()
              << ": "
              << data;

    return API_OK;
}

error SyncConfigIOContext::remove(const LocalPath& dbPath,
                                  unsigned int slot)
{
    LocalPath path = dbFilePath(dbPath, slot);

    if (mFsAccess.fileExistsAt(path) &&  // don't add error messages to the log when it's not an error
        !mFsAccess.unlinklocal(path))
    {
        LOG_warn << "Unable to remove config DB: "
                 << path.toPath();

        return API_EWRITE;
    }

    return API_OK;
}

error SyncConfigIOContext::remove(const LocalPath& dbPath)
{
    vector<unsigned int> confSlots;

    // What slots are present on disk?
    if (getSlotsInOrder(dbPath, confSlots) == API_ENOENT)
    {
        // None so nothing to do.
        return API_ENOENT;
    }

    bool result = true;

    // Remove the slots from disk.
    for (auto confSlot : confSlots)
    {
        result &= remove(dbPath, confSlot) == API_OK;
    }

    // Signal success only if all slots could be removed.
    return result ? API_OK : API_EWRITE;
}

void SyncConfigIOContext::serialize(const SyncConfigVector& configs,
                                    JSONWriter& writer) const
{
    writer.beginobject();
    writer.beginarray("sy");

    for (const auto& config : configs)
    {
        serialize(config, writer);
    }

    writer.endarray();
    writer.endobject();
}

error SyncConfigIOContext::write(const LocalPath& dbPath,
                                 const string& data,
                                 unsigned int slot)
{
    LocalPath path = dbPath;

    LOG_debug << "Attempting to write config DB: "
              << dbPath.toPath()
              << " / "
              << slot;

    // Try and create the backup configuration directory.
    if (!(mFsAccess.mkdirlocal(path, false, false) || mFsAccess.target_exists))
    {
        LOG_err << "Unable to create config DB directory: "
                << dbPath.toPath();

        // Couldn't create the directory and it doesn't exist.
        return API_EWRITE;
    }

    // Generate the rest of the path.
    path = dbFilePath(dbPath, slot);

    // Open the file for writing.
    auto fileAccess = mFsAccess.newfileaccess(false);

    if (!fileAccess->fopen(path, false, true))
    {
        // Couldn't open the file for writing.
        LOG_err << "Unable to open config DB for writing: "
                << path.toPath();

        return API_EWRITE;
    }

    // Ensure the file is empty.
    if (!fileAccess->ftruncate())
    {
        // Couldn't truncate the file.
        LOG_err << "Unable to truncate config DB: "
                << path.toPath();

        return API_EWRITE;
    }

    // Encrypt the configuration data.
    const string d = encrypt(data);

    // Write the encrypted configuration data.
    auto* bytes = reinterpret_cast<const byte*>(&d[0]);

    if (!fileAccess->fwrite(bytes, static_cast<unsigned>(d.size()), 0x0))
    {
        // Couldn't write out the data.
        LOG_err << "Unable to write config DB: "
                << path.toPath();

        return API_EWRITE;
    }

    LOG_debug << "Config DB successfully written to disk: "
              << path.toPath()
              << ": "
              << data;

    return API_OK;
}

LocalPath SyncConfigIOContext::dbFilePath(const LocalPath& dbPath,
                                          unsigned int slot) const
{
    using std::to_string;

    LocalPath path = dbPath;

    path.appendWithSeparator(mName, false);
    path.append(LocalPath::fromRelativePath("." + to_string(slot)));

    return path;
}

bool SyncConfigIOContext::decrypt(const string& in, string& out)
{
    // Handy constants.
    const size_t IV_LENGTH       = SymmCipher::KEYLENGTH;
    const size_t MAC_LENGTH      = 32;
    const size_t METADATA_LENGTH = IV_LENGTH + MAC_LENGTH;

    // Is the file too short to be valid?
    if (in.size() <= METADATA_LENGTH)
    {
        return false;
    }

    // For convenience (format: <data><iv><hmac>)
    const byte* data = reinterpret_cast<const byte*>(&in[0]);
    const byte* iv   = &data[in.size() - METADATA_LENGTH];
    const byte* mac  = &data[in.size() - MAC_LENGTH];

    byte cmac[MAC_LENGTH];

    // Compute HMAC on file.
    mSigner.add(data, in.size() - MAC_LENGTH);
    mSigner.get(cmac);

    // Is the file corrupt?
    if (memcmp(cmac, mac, MAC_LENGTH))
    {
        return false;
    }

    // Try and decrypt the file.
    return mCipher.cbc_decrypt_pkcs_padding(data,
                                            in.size() - METADATA_LENGTH,
                                            iv,
                                            &out);
}

bool SyncConfigIOContext::deserialize(SyncConfig& config, JSON& reader, bool isExternal) const
{
    const auto TYPE_BACKUP_ID       = MAKENAMEID2('i', 'd');
    const auto TYPE_BACKUP_STATE    = MAKENAMEID2('b', 's');
    const auto TYPE_CHANGE_METHOD   = MAKENAMEID2('c', 'm');
    const auto TYPE_ENABLED         = MAKENAMEID2('e', 'n');
    const auto TYPE_FINGERPRINT     = MAKENAMEID2('f', 'p');
    const auto TYPE_LAST_ERROR      = MAKENAMEID2('l', 'e');
    const auto TYPE_LAST_WARNING    = MAKENAMEID2('l', 'w');
    const auto TYPE_NAME            = MAKENAMEID1('n');
    const auto TYPE_SCAN_INTERVAL   = MAKENAMEID2('s', 'i');
    const auto TYPE_SOURCE_PATH     = MAKENAMEID2('s', 'p');
    const auto TYPE_SYNC_TYPE       = MAKENAMEID2('s', 't');
    const auto TYPE_TARGET_HANDLE   = MAKENAMEID2('t', 'h');
    const auto TYPE_TARGET_PATH     = MAKENAMEID2('t', 'p');

    for ( ; ; )
    {
        switch (reader.getnameid())
        {
        case EOO:
            // success if we reached the end of the object
            return *reader.pos == '}';

        case TYPE_CHANGE_METHOD:
            config.mChangeDetectionMethod =
              static_cast<ChangeDetectionMethod>(reader.getint32());
            break;

        case TYPE_ENABLED:
            config.mEnabled = reader.getbool();
            break;

        case TYPE_FINGERPRINT:
            config.mFilesystemFingerprint = reader.getfp();
            break;

        case TYPE_LAST_ERROR:
            config.mError =
              static_cast<SyncError>(reader.getint32());
            break;

        case TYPE_LAST_WARNING:
            config.mWarning =
              static_cast<SyncWarning>(reader.getint32());
            break;

        case TYPE_NAME:
            reader.storebinary(&config.mName);
            break;

        case TYPE_SOURCE_PATH:
        {
            string sourcePath;

            reader.storebinary(&sourcePath);

            if (isExternal)
            {
                config.mLocalPath =
                  LocalPath::fromRelativePath(sourcePath);
            }
            else
            {
                config.mLocalPath =
                    LocalPath::fromAbsolutePath(sourcePath);
            }

            break;
        }

        case TYPE_SCAN_INTERVAL:
            config.mScanIntervalSec =reader.getuint32();
            break;

        case TYPE_SYNC_TYPE:
            config.mSyncType =
              static_cast<SyncConfig::Type>(reader.getint32());
            break;

        case TYPE_BACKUP_ID:
            config.mBackupId = reader.gethandle(sizeof(handle));
            break;

        case TYPE_BACKUP_STATE:
            config.mBackupState =
              static_cast<SyncBackupState>(reader.getint32());
            break;

        case TYPE_TARGET_HANDLE:
            config.mRemoteNode = reader.getNodeHandle();
            break;

        case TYPE_TARGET_PATH:
            reader.storebinary(&config.mOriginalPathOfRemoteRootNode);
            break;

        default:
            if (!reader.storeobject())
            {
                return false;
            }
            break;
        }
    }
}

string SyncConfigIOContext::encrypt(const string& data)
{
    byte iv[SymmCipher::KEYLENGTH];

    // Generate initialization vector.
    mRNG.genblock(iv, sizeof(iv));

    string d;

    // Encrypt file using IV.
    mCipher.cbc_encrypt_pkcs_padding(&data, iv, &d);

    // Add IV to file.
    d.append(std::begin(iv), std::end(iv));

    byte mac[32];

    // Compute HMAC on file (including IV).
    mSigner.add(reinterpret_cast<const byte*>(&d[0]), d.size());
    mSigner.get(mac);

    // Add HMAC to file.
    d.append(std::begin(mac), std::end(mac));

    // We're done.
    return d;
}

void SyncConfigIOContext::serialize(const SyncConfig& config,
                                    JSONWriter& writer) const
{
    auto sourcePath = config.mLocalPath.toPath();

    // Strip drive path from source.
    if (config.isExternal())
    {
        auto drivePath = config.mExternalDrivePath.toPath();
        sourcePath.erase(0, drivePath.size());
    }

    writer.beginobject();
    writer.arg("id", config.mBackupId, sizeof(handle));
    writer.arg_B64("sp", sourcePath);
    writer.arg_B64("n", config.mName);
    writer.arg_B64("tp", config.mOriginalPathOfRemoteRootNode);
    writer.arg_fsfp("fp", config.mFilesystemFingerprint);
    writer.arg("th", config.mRemoteNode);
    writer.arg("le", config.mError);
    writer.arg("lw", config.mWarning);
    writer.arg("st", config.mSyncType);
    writer.arg("en", config.mEnabled);
    writer.arg("bs", config.mBackupState);
    writer.arg("cm", config.mChangeDetectionMethod);
    writer.arg("si", config.mScanIntervalSec);
    writer.endobject();
}

void Syncs::syncLoop()
{
    syncThreadId = std::this_thread::get_id();
    assert(onSyncThread());

    std::condition_variable cv;
    std::mutex dummy_mutex;
    std::unique_lock<std::mutex> dummy_lock(dummy_mutex);

    unsigned lastRecurseMs = 0;
    bool lastLoopEarlyExit = false;

    for (;;)
    {
        waiter.bumpds();

        // Flush changes made to internal configs.
        syncConfigStoreFlush();

        mHeartBeatMonitor->beat();

        // Aim to wait at least one second between recursiveSync traversals, keep CPU down.
        // If traversals are very long, have a fair wait between (up to 5 seconds)
        // If something happens that means the sync needs attention, the waiter
        // should be woken up by a waiter->notify() call, and we break out of this wait
        waiter.init(10 + std::min<unsigned>(lastRecurseMs, 10000)/200);
        waiter.wakeupby(fsaccess.get(), Waiter::NEEDEXEC);
        waiter.wait();

        fsaccess->checkevents(&waiter);

        // make sure we are using the client key (todo: shall we set it just when the client sets its key? easy to miss one though)
        syncKey.setkey(mClient.key.key);

        // reset flag now, if it gets set then we speed back to processsing syncThreadActions
        mSyncFlags->earlyRecurseExitRequested = false;

        // execute any requests from the MegaClient
        waiter.bumpds();
        std::function<void()> f;
        while (syncThreadActions.popFront(f))
        {
            if (!f)
            {
                // null function is the signal to end the thread
                // Be sure to flush changes made to internal configs.
                syncConfigStoreFlush();
                return;
            }

            f();
        }

        // verify filesystem fingerprints, disable deviating syncs
        // (this covers mountovers, some device removals and some failures)
        for (auto& us : mSyncVec)
        {
            if (Sync* sync = us->mSync.get())
            {
                if (us->mConfig.mError != NO_SYNC_ERROR)
                {
                    continue;
                }

                if (sync->fsfp)
                {
                    fsfp_t current = fsaccess->fsFingerprint(sync->localroot->localname);
                    if (sync->fsfp != current)
                    {
                        LOG_err << "Local filesystem mismatch. Previous: " << sync->fsfp
                            << "  Current: " << current;
                        sync->changestate(current ? LOCAL_FILESYSTEM_MISMATCH : LOCAL_PATH_UNAVAILABLE, false, true, true);
                        continue;
                    }
                }

                bool inTrash = false;
                bool foundRootNode = lookupCloudNode(sync->localroot->syncedCloudNodeHandle, sync->cloudRoot, &sync->cloudRootPath, &inTrash, nullptr, nullptr, Syncs::FOLDER_ONLY);

                // update path in sync configuration (if moved)  (even if no mSync - tests require this currently)
                bool pathChanged = checkSyncRemoteLocationChange(*us, foundRootNode, sync->cloudRootPath);

                if (!foundRootNode)
                {
                    LOG_err << "Detected sync root node no longer exists";
                    sync->changestate(REMOTE_NODE_NOT_FOUND, false, true, true);
                }
                else if (inTrash)
                {
                    LOG_err << "Detected sync root node is now in trash";
                    sync->changestate(REMOTE_NODE_MOVED_TO_RUBBISH, false, true, true);
                }
                else if (pathChanged /*&& us->mConfig.isBackup()*/)  // TODO: decide if we cancel non-backup syncs unnecessarily.  Users may like to move some high level folders around, without breaking contained syncs.
                {
                    LOG_err << "Detected sync root node is now at a different path.";
                    sync->changestate(REMOTE_PATH_HAS_CHANGED, false, true, true);
                    mClient.app->syncupdate_remote_root_changed(sync->getConfig());
                }
            }
        };

        stopSyncsInErrorState();

        // Clear the context if the associated sync is no longer active.
        mIgnoreFileFailureContext.reset(*this);

        waiter.bumpds();

        // Process filesystem notifications.
        for (auto& us : mSyncVec)
        {
            // Only process active syncs.
            if (Sync* sync = us->mSync.get())
            {
                // And only those that make use of filesystem events.
                if (sync->dirnotify)
                {
                    sync->procextraq();
                    sync->procscanq();
                }
            }
        }

        processTriggerHandles();

        waiter.bumpds();

        // We must have actionpacketsCurrent so that any LocalNode created can straight away indicate if it matched a Node
        if (!mClient.actionpacketsCurrent)
            continue;

        // Does it look like we have no work to do?
        if (!syncStallState && !isAnySyncSyncing(true))
        {
            auto mustScan = false;

            // Check if any syncs need a periodic scan.
            for (auto& us : mSyncVec)
            {
                auto* sync = us->mSync.get();

                // Only active syncs without a notifier.
                if (!sync || sync->dirnotify)
                    continue;

                mustScan |= sync->syncscanbt.armed();
            }

            // No work to do.
            if (!mustScan)
                continue;
        }

        if (syncStallState &&
            (waiter.ds < mSyncFlags->recursiveSyncLastCompletedDs + 10) &&
            (waiter.ds > mSyncFlags->recursiveSyncLastCompletedDs) &&
            !lastLoopEarlyExit)
        {
            LOG_debug << "Don't process syncs too often in stall state";
            continue;
        }

        bool earlyExit = false;
        auto recurseStart = std::chrono::high_resolution_clock::now();
        CodeCounter::ScopeTimer rst(mClient.performanceStats.recursiveSyncTime);

        if (!lastLoopEarlyExit)
        {
            // we need one pass with recursiveSync() after scanning is complete, to be sure there are no moves left.
            auto scanningCompletePreviously = mSyncFlags->scanningWasComplete && !mSyncFlags->isInitialPass;
            mSyncFlags->scanningWasComplete = !isAnySyncScanning(false);   // paused syncs do not participate in move detection
            mSyncFlags->reachableNodesAllScannedLastPass = mSyncFlags->reachableNodesAllScannedThisPass && !mSyncFlags->isInitialPass;
            mSyncFlags->reachableNodesAllScannedThisPass = true;
            mSyncFlags->movesWereComplete = scanningCompletePreviously && !mightAnySyncsHaveMoves(false); // paused syncs do not participate in move detection
            mSyncFlags->noProgress = mSyncFlags->reachableNodesAllScannedLastPass;
        }

        unsigned skippedForScanning = 0;

        for (auto& us : mSyncVec)
        {
            Sync* sync = us->mSync.get();

            if (sync && !us->mConfig.mError)
            {
                // Does this sync rely on filesystem notifications?
                if (auto* notifier = sync->dirnotify.get())
                {
                    // Has it encountered a recoverable error?
                    if (notifier->mErrorCount.load() > 0)
                    {
                        // Then issue a full scan.
                        LOG_err << "Sync "
                                << toHandle(sync->getConfig().mBackupId)
                                << " had a filesystem notification buffer overflow. Triggering full scan.";

                        // Reset the error counter.
                        notifier->mErrorCount.store(0);

                        // Rescan everything from the root down.
                        sync->localroot->setScanAgain(false, true, true, 5);
                    }

                    string reason;

                    // Has it encountered an unrecoverable error?
                    if (notifier->getFailed(reason))
                    {
                        // Then fail the sync.
                        LOG_err << "Sync "
                                << toHandle(sync->getConfig().mBackupId)
                                << " notifications failed or were not available (reason: "
                                << reason
                                << ")";

                        sync->changestate(NOTIFICATION_SYSTEM_UNAVAILABLE, false, true, true);
                    }
                }
                else
                {
                    // No notifier.

                    // Is it time to issue a rescan?
                    if (sync->syncscanbt.armed())
                    {
                        // Issue full rescan.
                        sync->localroot->setScanAgain(false, true, true, 0);

                        // Translate interval into deciseconds.
                        auto intervalDs = sync->getConfig().mScanIntervalSec * 10;

                        // Queue next scan.
                        sync->syncscanbt.backoff(intervalDs);
                    }
                }

                if (!sync->getConfig().mTemporarilyPaused)
                {
                    bool activeIncomplete = sync->mActiveScanRequestGeneral &&
                        !sync->mActiveScanRequestGeneral->completed();

                    bool unscannedIncomplete = sync->mActiveScanRequestUnscanned &&
                        !sync->mActiveScanRequestUnscanned->completed();

                    if ((activeIncomplete && unscannedIncomplete) ||
                        (activeIncomplete && sync->threadSafeState->neverScannedFolderCount.load() == 0) ||
                        (unscannedIncomplete && !sync->mActiveScanRequestGeneral))
                    {
                        // Save CPU by not starting another recurse of the LocalNode tree
                        // if a scan is not finished yet.  Scans can take a fair while for large
                        // folders since they also extract file fingerprints if not known yet.
                        ++skippedForScanning;
                        continue;
                    }

                    // make sure we don't have a LocalNode for the debris folder (delete if we have added one historically)
                    if (LocalNode* debrisNode = sync->localroot->childbyname(&sync->localdebrisname))
                    {
                        delete debrisNode; // cleans up its own entries in parent maps
                    }

                    // pathBuffer will have leafnames appended as we recurse
                    SyncPath pathBuffer(*this, sync->localroot->localname, sync->cloudRootPath);

                    FSNode rootFsNode(sync->localroot->getLastSyncedFSDetails());
                    syncRow row{&sync->cloudRoot, sync->localroot.get(), &rootFsNode};

                    {
                        // later we can make this lock much finer-grained
                        std::lock_guard<std::timed_mutex> g(mLocalNodeChangeMutex);

                        DBTableTransactionCommitter committer(sync->statecachetable);

                        if (!sync->recursiveSync(row, pathBuffer, false, false, 0))
                        {
                            earlyExit = true;
                        }

                        sync->cachenodes();
                    }

                    if (!earlyExit)
                    {
                        if (sync->isBackupAndMirroring() &&
                            !sync->localroot->scanRequired() &&
                            !sync->localroot->mightHaveMoves() &&
                            !sync->localroot->syncRequired())

                        {
                            sync->setBackupMonitoring();
                        }
                    }
                }
            }
        }

#ifdef MEGA_MEASURE_CODE
        rst.complete();
#endif
        lastRecurseMs = unsigned(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::high_resolution_clock::now() - recurseStart).count());

        LOG_verbose << "recursiveSync took ms: " << lastRecurseMs
                    << (skippedForScanning ? " (" + std::to_string(skippedForScanning)+ " skipped due to ongoing scanning)" : "")
                    << (mSyncFlags->noProgressCount ? " no progress count: " + std::to_string(mSyncFlags->noProgressCount) : "");


        waiter.bumpds();
        mSyncFlags->recursiveSyncLastCompletedDs = waiter.ds;

        if (skippedForScanning > 0)
        {
            // avoid flip-flopping on stall state if the stalled paths were inside a sync that is still scanning
            earlyExit = true;
        }

        if (earlyExit)
        {
            mSyncFlags->scanningWasComplete = false;
            mSyncFlags->reachableNodesAllScannedThisPass = false;
        }
        else
        {
            mSyncFlags->isInitialPass = false;

            if (mSyncFlags->noProgress)
            {
                ++mSyncFlags->noProgressCount;
            }

            bool conflictsNow = conflictsFlagged();
            if (conflictsNow != syncConflictState)
            {
                assert(onSyncThread());
                mClient.app->syncupdate_conflicts(conflictsNow);
                syncConflictState = conflictsNow;
                LOG_info << mClient.clientname << "Sync conflicting paths state app notified: " << conflictsNow;
            }
        }

        if (!earlyExit)
        {
            bool anySyncScanning = isAnySyncScanning(false);
            if (anySyncScanning != syncscanstate)
            {
                assert(onSyncThread());
                mClient.app->syncupdate_scanning(anySyncScanning);
                syncscanstate = anySyncScanning;
            }

            bool anySyncBusy = isAnySyncSyncing(false);
            if (anySyncBusy != syncBusyState)
            {
                assert(onSyncThread());
                mClient.app->syncupdate_syncing(anySyncBusy);
                syncBusyState = anySyncBusy;
            }

            bool stalled = syncStallState;

            {
                // add .megaignore broken file paths to stall records (if they are still broken)
                //if (mIgnoreFileFailureContext.signalled())
                //{
                //    // Has the problem been resolved?
                //    if (!mIgnoreFileFailureContext.resolve(*fsaccess))
                //    {
                //        // Not resolved so report as a stall.
                //        mIgnoreFileFailureContext.report(mSyncFlags->stall);
                //    }
                //}

                for (auto i = badlyFormedIgnoreFilePaths.begin(); i != badlyFormedIgnoreFilePaths.end(); )
                {
                    if (auto lp = i->lock())
                    {
                        if (!(lp->sync && lp->sync->getConfig().mTemporarilyPaused))
                        {
                            mSyncFlags->stall.waitingLocal(lp->localPath, LocalPath(), string(), SyncWaitReason::UnableToLoadIgnoreFile);
                        }
                        ++i;
                    }
                    else i = badlyFormedIgnoreFilePaths.erase(i);
                }

                // add scan-blocked paths to stall records
                for (auto i = scanBlockedPaths.begin(); i != scanBlockedPaths.end(); )
                {
                    if (auto sbp = i->lock())
                    {
                        if (sbp->scanBlockedTimer.armed())
                        {
                            if (sbp->folderUnreadable)
                            {
                                LOG_verbose << "Scan blocked timer elapsed, trigger folder rescan: " << sbp->scanBlockedLocalPath.toPath();
                                sbp->localNode->setScanAgain(false, true, false, 0);
                                sbp->scanBlockedTimer.backoff(); // wait increases exponentially (up to 10 mins) until we succeed
                            }
                            else
                            {
                                LOG_verbose << "Locked file fingerprint timer elapsed, trigger folder rescan: " << sbp->scanBlockedLocalPath.toPath();
                                sbp->localNode->setScanAgain(false, true, false, 0);
                                sbp->scanBlockedTimer.backoff(300); // limited wait (30s) as the user will expect it uploaded fairly soon after they stop editing it
                            }
                        }

                        if (sbp->folderUnreadable)
                        {
                            mSyncFlags->stall.waitingLocal(sbp->scanBlockedLocalPath, LocalPath(), string(), SyncWaitReason::LocalFolderNotScannable);
                        }
                        else
                        {
                            assert(sbp->filesUnreadable);
                            mSyncFlags->stall.waitingLocal(sbp->scanBlockedLocalPath, LocalPath(), string(), SyncWaitReason::FolderContainsLockedFiles);
                        }
                        ++i;
                    }
                    else i = scanBlockedPaths.erase(i);
                }

                lock_guard<mutex> g(stallReportMutex);
                stallReport.cloud.swap(mSyncFlags->stall.cloud);
                stallReport.local.swap(mSyncFlags->stall.local);
                mSyncFlags->stall.cloud.clear();
                mSyncFlags->stall.local.clear();

                stalled = !stallReport.empty()
                          && (stallReport.hasImmediateStallReason()
                              || (mSyncFlags->noProgressCount > 10
                                  && mSyncFlags->reachableNodesAllScannedThisPass));

                if (stalled)
                {
                    LOG_warn << mClient.clientname << "Stall detected!";
                    for (auto& p : stallReport.cloud) LOG_warn << "stalled node path (" << syncWaitReasonString(p.second.reason) << "): " << p.first;
                    for (auto& p : stallReport.local) LOG_warn << "stalled local path (" << syncWaitReasonString(p.second.reason) << "): " << p.first.toPath();
                }
            }

            if (stalled != syncStallState)
            {
                assert(onSyncThread());
                mClient.app->syncupdate_stalled(stalled);
                syncStallState = stalled;
                LOG_warn << mClient.clientname << "Stall state app notified: " << stalled;
            }
        }


        lastLoopEarlyExit = earlyExit;
    }
}

bool Syncs::isAnySyncSyncing(bool includePausedSyncs)
{
    assert(onSyncThread());

    for (auto& us : mSyncVec)
    {
        if (Sync* sync = us->mSync.get())
        {
            if (includePausedSyncs || !us->mConfig.mTemporarilyPaused)
            {
                if (!us->mConfig.mError &&
                    (sync->localroot->scanRequired()
                        || sync->localroot->mightHaveMoves()
                        || sync->localroot->syncRequired()))
                {
                    return true;
                }
            }
        }
    }
    return false;
}

bool Syncs::isAnySyncScanning(bool includePausedSyncs)
{
    assert(onSyncThread());

    for (auto& us : mSyncVec)
    {
        if (Sync* sync = us->mSync.get())
        {
            if (includePausedSyncs || !us->mConfig.mTemporarilyPaused)
            {
                if (!us->mConfig.mError &&
                    sync->localroot->scanRequired())
                {
                    return true;
                }
            }
        }
    }
    return false;
}

bool Syncs::mightAnySyncsHaveMoves(bool includePausedSyncs)
{
    assert(onSyncThread());

    for (auto& us : mSyncVec)
    {
        if (Sync* sync = us->mSync.get())
        {
            if (includePausedSyncs || !us->mConfig.mTemporarilyPaused)
            {
                if (!us->mConfig.mError &&
                    (sync->localroot->mightHaveMoves()
                        || sync->localroot->scanRequired()))
                {
                    return true;
                }
            }
        }
    }
    return false;
}

bool Syncs::conflictsDetected(list<NameConflict>& conflicts) const
{
    assert(onSyncThread());

    for (auto& us : mSyncVec)
    {
        if (Sync* sync = us->mSync.get())
        {
            sync->recursiveCollectNameConflicts(conflicts);
        }
    }
    return !conflicts.empty();
}

bool Syncs::conflictsFlagged() const
{
    assert(onSyncThread());

    for (auto& us : mSyncVec)
    {
        if (Sync* sync = us->mSync.get())
        {
            if (sync->localroot->conflictsDetected())
            {
                return true;
            }
        }
    }
    return false;
}

bool Syncs::syncStallDetected(SyncStallInfo& si) const
{
    assert(!onSyncThread());
    lock_guard<mutex> g(stallReportMutex);

    if (syncStallState)
    {
        si = stallReport;
        return true;
    }
    return false;
}

void Syncs::collectSyncNameConflicts(handle backupId, std::function<void(list<NameConflict>&& nc)> completion, bool completionInClient)
{
    assert(!onSyncThread());

    auto clientCompletion = [this, completion](list<NameConflict>&& nc)
    {
        shared_ptr<list<NameConflict>> ncptr(new list<NameConflict>(move(nc)));
        queueClient([completion, ncptr](MegaClient& mc, DBTableTransactionCommitter& committer)
            {
                if (completion) completion(move(*ncptr));
            });
    };

    auto finalcompletion = completionInClient ? clientCompletion : completion;

    queueSync([=]()
        {
            list<NameConflict> nc;
            for (auto& us : mSyncVec)
            {
                if (us->mSync && (us->mConfig.mBackupId == backupId || backupId == UNDEF))
                {
                    us->mSync->recursiveCollectNameConflicts(nc);
                }
            }
            finalcompletion(move(nc));
        });
}

void Syncs::setSyncsNeedFullSync(bool andFullScan, handle backupId)
{
    assert(!onSyncThread());
    queueSync([=](){

        assert(onSyncThread());
        for (auto & us : mSyncVec)
        {
            if ((us->mConfig.mBackupId == backupId
                || backupId == UNDEF) &&
                us->mSync)
            {
                us->mSync->localroot->setSyncAgain(false, true, true);
                if (andFullScan)
                {
                    us->mSync->localroot->setScanAgain(false, true, true, 0);
                }
            }
        }
    });
}

void Syncs::proclocaltree(LocalNode* n, LocalTreeProc* tp)
{
    assert(onSyncThread());

    if (n->type != FILENODE)
    {
        for (localnode_map::iterator it = n->children.begin(); it != n->children.end(); )
        {
            LocalNode *child = it->second;
            it++;
            proclocaltree(child, tp);
        }
    }

    tp->proc(*n->sync->syncs.fsaccess, n);
}

bool Syncs::lookupCloudNode(NodeHandle h, CloudNode& cn, string* cloudPath, bool* isInTrash,
        bool* nodeIsInActiveUnpausedSyncQuery, bool* nodeIsDefinitelyExcluded, WhichCloudVersion whichVersion)
{
    // we have to avoid doing these lookups when the client thread might be changing the Node tree
    // so we use the mutex to prevent access during that time - which is only actionpacket processing.
    assert(onSyncThread());
    assert(!nodeIsDefinitelyExcluded || nodeIsInActiveUnpausedSyncQuery); // if you ask if it's excluded, you must ask if it's in sync too

    if (h.isUndef()) return false;

    vector<pair<NodeHandle, Sync*>> activeSyncHandles;
    vector<pair<Node*, Sync*>> activeSyncRoots;

    if (nodeIsInActiveUnpausedSyncQuery)
    {
        *nodeIsInActiveUnpausedSyncQuery = false;

        for (auto & us : mSyncVec)
        {
            if (us->mSync && !us->mConfig.mError && !us->mConfig.mTemporarilyPaused)
            {
                activeSyncHandles.emplace_back(us->mConfig.mRemoteNode, us->mSync.get());
            }
        }
    }

    lock_guard<mutex> g(mClient.nodeTreeMutex);

    if (nodeIsInActiveUnpausedSyncQuery)
    {
        for (auto & rh : activeSyncHandles)
        {
            if (Node* rn = mClient.nodeByHandle(rh.first, true))
            {
                activeSyncRoots.emplace_back(rn, rh.second);
            }
        }
    }

    if (const Node* n = mClient.nodeByHandle(h, true))
    {
        switch (whichVersion)
        {
            case EXACT_VERSION:
                break;

            case LATEST_VERSION:
                {
                    const Node* m = n->latestFileVersion();
                    if (m != n)
                    {
                        auto& syncs = *this;
                        SYNC_verbose << "Looking up Node " << n->nodeHandle() << " chose latest version " << m->nodeHandle();
                        n = m;
                    }
                }
                break;

            case FOLDER_ONLY:
                assert(n->type != FILENODE);
                break;
        }

        if (isInTrash)
        {
            *isInTrash = n->firstancestor()->nodeHandle() == mClient.rootnodes.rubbish;
        }

        if (cloudPath) *cloudPath = n->displaypath();
        cn = CloudNode(*n);

        if (nodeIsInActiveUnpausedSyncQuery)
        {
            for (auto & rn : activeSyncRoots)
            {
                if (n->isbelow(rn.first) && !rn.second->getConfig().mTemporarilyPaused)
                {
                    *nodeIsInActiveUnpausedSyncQuery = true;

                    if (nodeIsDefinitelyExcluded)
                        *nodeIsDefinitelyExcluded = isDefinitelyExcluded(rn, n);
                }
            }
        }

        return true;
    }
    return false;
}

bool Syncs::lookupCloudChildren(NodeHandle h, vector<CloudNode>& cloudChildren)
{
    // we have to avoid doing these lookups when the client thread might be changing the Node tree
    // so we use the mutex to prevent access during that time - which is only actionpacket processing.
    assert(onSyncThread());

    lock_guard<mutex> g(mClient.nodeTreeMutex);
    if (Node* n = mClient.nodeByHandle(h, true))
    {
        assert(n->type != FILENODE);
        assert(!n->parent || n->parent->type != FILENODE);
        cloudChildren.reserve(n->children.size());
        for (auto c : n->children)
        {
            cloudChildren.push_back(*c);
            assert(cloudChildren.back().parentHandle == h);
        }
        return true;
    }
    return false;
}

bool Syncs::isDefinitelyExcluded(const pair<Node*, Sync*>& root, const Node* child)
{
    // Make sure we're on the sync thread.
    assert(onSyncThread());

    // Make sure we're looking at the latest version of child.
    child = child->latestFileVersion();

    // Sanity.
    assert(child->isbelow(root.first));

    // Determine the trail from root to child.
    vector<pair<NodeHandle, string>> trail;

    for (auto* node = child; node != root.first; node = node->parent)
        trail.emplace_back(node->nodeHandle(), node->displayname());

    // Determine whether any step from root to child is definitely excluded.
    auto* parent = root.second->localroot.get();
    auto i = trail.crbegin();
    auto j = trail.crend();

    for ( ; i != j; ++i)
    {
        // Does this node on the trail have a local node?
        auto* node = parent->findChildWithSyncedNodeHandle(i->first);

        // No node so we'll have to check by remote path.
        if (!node)
            break;

        // Name doesn't match so we'll have to check by remote path.
        if (node->getCloudName() != i->second)
            break;

        // Children are definitely excluded if their parent is.
        if (node->exclusionState() == ES_EXCLUDED)
            return true;

        // Children are considered included if their parent's state is indeterminate.
        if (node->exclusionState() == ES_UNKNOWN)
            return false;

        // Node's included so check the next step along the trail.
        parent = node;
    }

    if (i == j)
    {
        // handle + name matched all the way down
        return false;
    }

    // Compute relative path from last parent to child.
    RemotePath cloudPath;

    for ( ; i != j; ++i)
        cloudPath.appendWithSeparator(i->second, false);

    // Would the child definitely be excluded?
    return parent->exclusionState(cloudPath, child->type, child->size) == ES_EXCLUDED;
}

Sync* Syncs::syncContainingPath(const LocalPath& path, bool includePaused)
{
    auto predicate = [&path](const UnifiedSync& us) {
        return us.mConfig.mLocalPath.isContainingPathOf(path);
    };

    return syncMatching(std::move(predicate), includePaused);
}

Sync* Syncs::syncContainingPath(const string& path, bool includePaused)
{
    auto predicate = [&path](const UnifiedSync& us) {
        return IsContainingCloudPathOf(
                 us.mConfig.mOriginalPathOfRemoteRootNode,
                 path);
    };

    return syncMatching(std::move(predicate), includePaused);
}

void Syncs::ignoreFileLoadFailure(const Sync& sync, const LocalPath& path)
{
    // We should never be asked to report multiple ignore file failures.
    assert(mIgnoreFileFailureContext.mBackupID == UNDEF);

    // Record the failure.
    mIgnoreFileFailureContext.mBackupID = sync.getConfig().mBackupId;
    mIgnoreFileFailureContext.mPath = path;

    // Let the application know an ignore file has failed to load.
    mClient.app->syncupdate_filter_error(sync.getConfig());
}

void Syncs::queueSync(std::function<void()>&& f)
{
    assert(!onSyncThread());
    syncThreadActions.pushBack(move(f));
    mSyncFlags->earlyRecurseExitRequested = true;
    waiter.notify();
}

void Syncs::queueClient(std::function<void(MegaClient&, DBTableTransactionCommitter&)>&& f)
{
    assert(onSyncThread());
    clientThreadActions.pushBack(move(f));
    mClient.waiter->notify();
}

bool Syncs::hasIgnoreFile(const SyncConfig& config)
{
    // Should only be run from the sync thread.
    assert(onSyncThread());

    // Is there an ignore file present in the cloud?
    {
        // Ensure we have exclusive access to the remote node tree.
        lock_guard<mutex> guard(mClient.nodeTreeMutex);

        // Get our hands on the sync root.
        auto* root = mClient.nodeByHandle(config.mRemoteNode);

        // The root can't contain anything if it doesn't exist.
        if (!root)
            return false;

        // Does the root contain an ignore file?
        if (root->hasChildWithName(IGNORE_FILE_NAME))
            return true;
    }

    // Does the ignore file already exist on disk?
    auto fileAccess = fsaccess->newfileaccess(false);
    auto filePath = config.mLocalPath;

    filePath.appendWithSeparator(IGNORE_FILE_NAME, false);

    return fileAccess->isfile(filePath);
}

} // namespace

#endif
