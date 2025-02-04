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
#include "mega.h"

#include <cctype>
#include <future>
#include <memory>
#include <type_traits>

#ifdef ENABLE_SYNC
#include "mega/base64.h"
#include "mega/heartbeats.h"
#include "mega/megaapp.h"
#include "mega/megaclient.h"
#include "mega/scoped_helpers.h"
#include "mega/sync.h"
#include "mega/syncinternals/syncinternals_logging.h"
#include "mega/syncinternals/syncuploadthrottlingmanager.h"
#include "mega/transfer.h"

namespace mega {

const int Sync::SCANNING_DELAY_DS = 5;
const int Sync::EXTRA_SCANNING_DELAY_DS = 150;
const int Sync::FILE_UPDATE_DELAY_DS = 30;
const int Sync::FILE_UPDATE_MAX_DELAY_SECS = 60;
const dstime Sync::RECENT_VERSION_INTERVAL_SECS = 10800;

const unsigned Sync::MAX_CLOUD_DEPTH = 64;

using namespace std::chrono_literals;
const std::chrono::milliseconds Syncs::MIN_DELAY_BETWEEN_SYNC_STALLS_OR_CONFLICTS_COUNT{100ms};
const std::chrono::milliseconds Syncs::MAX_DELAY_BETWEEN_SYNC_STALLS_OR_CONFLICTS_COUNT{10s};

bool PerSyncStats::operator==(const PerSyncStats& other)
{
    return  scanning == other.scanning &&
            syncing == other.syncing &&
            numFiles == other.numFiles &&
            numFolders == other.numFolders &&
            numUploads == other.numUploads &&
            numDownloads == other.numDownloads;
}

bool PerSyncStats::operator!=(const PerSyncStats& other)
{
    return !(*this == other);
}


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

auto makeScopedSyncPathRestorer(SyncPath& path)
{
    return std::make_tuple(makeScopedSizeRestorer(path.cloudPath),
                           makeScopedSizeRestorer(path.localPath),
                           makeScopedSizeRestorer(path.syncPath));
}

bool SyncPath::appendRowNames(const SyncRow& row, FileSystemType filesystemType)
{
    if (row.isNoName())
    {
        auto noName = string("NO_NAME");

        // Multiple no name triplets?
        if (row.hasClashes())
            noName += "S";

        cloudPath += "/" + noName;
        localPath.appendWithSeparator(LocalPath::fromRelativePath(noName), true);
        syncPath += "/" + noName;

        return true;
    }

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
    if (cloudPath.empty() || cloudPath.back() != '/') cloudPath += "/";
    CloudNode cn;
    if (row.cloudNode)
    {
        cloudPath += row.cloudNode->name;
    }
    else if (row.syncNode && syncs.lookupCloudNode(row.syncNode->syncedCloudNodeHandle, cn,
        nullptr, nullptr, nullptr, nullptr, nullptr, Syncs::LATEST_VERSION))
    {
        cloudPath += cn.name;
    }
    else if (row.syncNode)
    {
        cloudPath += row.syncNode->toName_of_localname;
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
    if (syncPath.empty() || syncPath.back() != '/') syncPath += "/";
    if (row.cloudNode)
    {
        syncPath += row.cloudNode->name;
    }
    else if (row.syncNode)
    {
        syncPath += row.syncNode->toName_of_localname;
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

    tc.mPending += static_cast<uint32_t>(adjustQueued);
    tc.mCompleted += static_cast<uint32_t>(adjustCompleted);
    tc.mPendingBytes += static_cast<uint64_t>(adjustQueuedBytes);
    tc.mCompletedBytes += static_cast<uint64_t>(adjustCompletedBytes);

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
    adjustTransferCounts(direction == PUT, -1, 1, -numBytes, numBytes);
}

void SyncThreadsafeState::transferFailed(direction_t direction, m_off_t numBytes)
{
    adjustTransferCounts(direction == PUT, -1, 0, -numBytes, 0);
}

SyncTransferCounts SyncThreadsafeState::transferCounts() const
{
    lock_guard<mutex> guard(mMutex);

    return mTransferCounts;
}


void SyncThreadsafeState::incrementSyncNodeCount(nodetype_t type, int32_t count)
{
    lock_guard<mutex> guard(mMutex);
    if (type == FILENODE) mFileCount += count;
    if (type == FOLDERNODE) mFolderCount += count;
}

void SyncThreadsafeState::getSyncNodeCounts(int32_t& files, int32_t& folders)
{
    lock_guard<mutex> guard(mMutex);
    files = mFileCount;
    folders = mFolderCount;
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
    , mLocalPathFsid(UNDEF)
    , mSyncType(syncType)
    , mError(error)
    , mWarning(warning)
    , mBackupId(hearBeatID)
    , mExternalDrivePath(externalDrivePath)
    , mBackupState(SYNC_BACKUP_NONE)
{}

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
    case UNLOADING_SYNC:
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
    case ACCOUNT_EXPIRED:
        return "Your plan has expired";
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
    case BACKUP_MODIFIED:
        return "Backup externally modified";
    case BACKUP_SOURCE_NOT_BELOW_DRIVE:
        return "Backup source path not below drive path.";
    case SYNC_CONFIG_WRITE_FAILURE:
        return "Unable to write sync config to disk.";
    case ACTIVE_SYNC_SAME_PATH:
        return "Active sync same path";
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
    case UNABLE_TO_OPEN_DATABASE:
        return "Unable to open state cache database.";
    case INSUFFICIENT_DISK_SPACE:
        return "Insufficient disk space.";
    case FAILURE_ACCESSING_PERSISTENT_STORAGE:
        return "Failure accessing to persistent storage";
    case UNABLE_TO_RETRIEVE_DEVICE_ID:
        return "Unable to retrieve the ID of current device";
    case MISMATCH_OF_ROOT_FSID:
        return "Mismatch on sync root FSID.";
    case FILESYSTEM_FILE_IDS_ARE_UNSTABLE:
        return "Syncing of exFAT, FAT32, FUSE and LIFS file systems is not supported by MEGA on macOS.";
    case FILESYSTEM_ID_UNAVAILABLE:
        return "Could not get the filesystem's ID.";
    case LOCAL_PATH_MOUNTED:
        return "Local path is a FUSE mount.";
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

    assert(false && "Unknown sync type name.");

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
    dbname.resize(
        static_cast<size_t>(Base64::btoa((byte*)tableid, sizeof tableid, (char*)dbname.c_str())));
    return dbname;
}

std::optional<std::filesystem::path> SyncConfig::getSyncDbPath(const FileSystemAccess& fsAccess,
                                                               const MegaClient& client) const
{
    const auto fname = getSyncDbStateCacheName(mLocalPathFsid, mRemoteNode, client.me);
    return client.dbaccess->getExistingDbPath(fsAccess, fname);
}

void SyncConfig::renameDBToMatchTarget(const SyncConfig& targetConfig,
                                       const FileSystemAccess& fsAccess,
                                       const MegaClient& client) const
{
    const auto currentDbPath = getSyncDbPath(fsAccess, client);
    if (!currentDbPath)
        return;

    const auto newDbFileName = targetConfig.getSyncDbStateCacheName(targetConfig.mLocalPathFsid,
                                                                    targetConfig.mRemoteNode,
                                                                    client.me);
    std::filesystem::path newDbPath{
        client.dbaccess->databasePath(fsAccess, newDbFileName, DbAccess::DB_VERSION).rawValue()};
    std::filesystem::rename(*currentDbPath, newDbPath);
}

// new Syncs are automatically inserted into the session's syncs list
// and a full read of the subtree is initiated
Sync::Sync(UnifiedSync& us, const std::string& logname, SyncError& e):
    syncs(us.syncs),
    localroot(nullptr),
    mUnifiedSync(us),
    syncscanbt(us.syncs.rng),
    threadSafeState(
        new SyncThreadsafeState(us.mConfig.mBackupId,
                                &syncs.mClient,
                                us.mConfig.isBackup())), // assuming backups are only in Vault
    mLocalPath(us.mConfig.getLocalPath())
{
    e = NO_SYNC_ERROR;
    assert(syncs.onSyncThread());

    localroot.reset(new LocalNode(this));

    const SyncConfig& config = us.mConfig;

    syncs.lookupCloudNode(config.mRemoteNode,
                          cloudRoot,
                          &cloudRootPath,
                          nullptr,
                          nullptr,
                          nullptr,
                          nullptr,
                          Syncs::FOLDER_ONLY,
                          &cloudRootOwningUser);
    inshare = syncs.isCloudNodeInShare(cloudRoot);
    tmpfa = NULL;
    syncname = logname; // can be updated to be more specific in logs

    assert(mUnifiedSync.mConfig.mRunState == SyncRunState::Loading);

    mFilesystemType = syncs.fsaccess->getlocalfstype(mLocalPath);
    LOG_debug << "Sync being created on filesystem type " << mFilesystemType << ": " << FileSystemAccess::fstypetostring(mFilesystemType);
    isnetwork = isNetworkFilesystem(mFilesystemType);

    localroot->init(FOLDERNODE, NULL, mLocalPath, nullptr);  // the root node must have the absolute path.  We don't store shortname, to avoid accidentally using relative paths.
    localroot->setSyncedNodeHandle(config.mRemoteNode);
    localroot->setScanAgain(false, true, true, 0);
    localroot->setCheckMovesAgain(false, true, true);
    localroot->setSyncAgain(false, true, true);

    debris = DEBRISFOLDER;
    localdebrisname = LocalPath::fromRelativePath(debris);
    localdebris = localdebrisname;
    localdebris.prependWithSeparator(mLocalPath);

    // Should this sync make use of filesystem notifications?
    if (us.mConfig.mChangeDetectionMethod == CDM_NOTIFICATIONS)
    {
        // Notifications may be queueing from this moment
        dirnotify.reset(syncs.fsaccess->newdirnotify(*localroot, mLocalPath, syncs.waiter.get()));
    }

    // set specified fsfp or get from fs if none
    auto fsfp = syncs.fsaccess->fsFingerprint(mLocalPath);
    if (!fsfp)
    {
        e = FILESYSTEM_ID_UNAVAILABLE;
        return;
    }

    auto& original_fsfp = mUnifiedSync.mConfig.mFilesystemFingerprint;
    if (original_fsfp)
    {
        if (!original_fsfp.equivalent(fsfp))
        {
            // leave this function before we create a Rubbish/.debris folder where we maybe shouldn't
            LOG_err << "Sync root path is a different Filesystem than when the sync was created. Original filesystem id: "
                    << original_fsfp.toString()
                    << "  Current: "
                    << fsfp.toString();

            e = LOCAL_FILESYSTEM_MISMATCH;
            return;
        }
    }

    fsstableids = syncs.fsaccess->fsStableIDs(mLocalPath);
    LOG_info << "Filesystem IDs are stable: " << fsstableids;

    if (!fsstableids)
    {
#ifdef __APPLE__
        // On Mac, stat() can and sometimes does return different IDs between calls
        // for the same path, for exFAT, and probably other FAT variants too.
        e = FILESYSTEM_FILE_IDS_ARE_UNSTABLE;
        return;
#endif
    }

    auto fas = syncs.fsaccess->newfileaccess();

    // we do allow, eg. mounting an exFAT drive over an NTFS folder, and making a sync at that path
    bool reparsePointOkAtRoot = true;

    if (!fas->fopen(mLocalPath, true, false, FSLogging::logOnError, nullptr, reparsePointOkAtRoot, true, nullptr)
        || fas->fsid == UNDEF)
    {
        LOG_err << "Could not open sync root folder, could not get its fsid: " << mLocalPath;
        e = UNABLE_TO_RETRIEVE_ROOT_FSID;
        return;
    }
    else if (us.mConfig.mLocalPathFsid != UNDEF &&
            us.mConfig.mLocalPathFsid != fas->fsid)
    {
        // We can't start a sync with the wrong root folder fsid because that is part of
        // the name of the sync database.  So we can't retrieve the sync state
        LOG_err << "Sync root folder does not have the same fsid as before: " << mLocalPath
                << " was " << toHandle(us.mConfig.mLocalPathFsid) << " now " << toHandle(fas->fsid);
        e = MISMATCH_OF_ROOT_FSID;
        return;
    }

    // record the fsid of the synced folder
    localroot->fsid_lastSynced = fas->fsid;
    us.mConfig.mLocalPathFsid = fas->fsid;
    us.mConfig.mFilesystemFingerprint = fsfp;

    // Make sure the engine knows about this fingerprint.
    syncs.mFingerprintTracker.add(fsfp);

    LOG_debug << "Constructed Sync has filesystemId: "
              << us.mConfig.mFilesystemFingerprint.toString()
              << " and root folder id: "
              << us.mConfig.mLocalPathFsid;

    // load LocalNodes from cache (only for internal syncs)
    us.mConfig.mDatabaseExists = shouldHaveDatabase() && openOrCreateDb(
                                                             [this](DBError error)
                                                             {
                                                                 syncs.mClient.handleDbError(error);
                                                             });
    if (us.mConfig.mDatabaseExists)
        readstatecache();

    us.mConfig.mRunState = SyncRunState::Run;

    mCaseInsensitive = determineCaseInsenstivity(false);
    LOG_debug << "Sync case insensitivity for " << mLocalPath << " is " << mCaseInsensitive;

    // Increment counter of active syncs.
    ++syncs.mNumSyncsActive;
}

Sync::~Sync()
{
    assert(syncs.onSyncThread());

    // Remove our reference to this fingerprint.
    syncs.mFingerprintTracker.remove(fsfp());

    // must be set to prevent remote mass deletion while rootlocal destructor runs
    mDestructorRunning = true;
    mUnifiedSync.mConfig.mRunState = mUnifiedSync.mConfig.mDatabaseExists ? SyncRunState::Suspend : SyncRunState::Disable;

    // unlock tmp lock
    tmpfa.reset();

    // Deleting localnodes after this will not remove them from the db.
    statecachetable.reset();

    // This will recursively delete all LocalNodes in the sync.
    // If they have transfers associated, the SyncUpload_inClient and SyncDownload_inClient will have their wasRequesterAbandoned flag set true
    localroot.reset();

    // Decrement counter of active syncs.
    --syncs.mNumSyncsActive;
}

bool Sync::openOrCreateDb(DBErrorCallback&& errorHandler)
{
    // We are using SQLite in the no-mutex mode, so only access a database from a single thread.
    assert(syncs.onSyncThread());

    auto& config = mUnifiedSync.mConfig;
    const std::string dbname =
        config.getSyncDbStateCacheName(config.mLocalPathFsid, config.mRemoteNode, syncs.mClient.me);

    // Check if the database exists on disk.
    const bool dbExistsOnDisk = syncs.mClient.dbaccess->probe(*syncs.fsaccess, dbname);

    // Note, we opened dbaccess in thread-safe mode
    statecachetable.reset(
        syncs.mClient.dbaccess->open(syncs.rng,
                                     *syncs.fsaccess,
                                     dbname,
                                     DB_OPEN_FLAG_RECYCLE | DB_OPEN_FLAG_TRANSACTED,
                                     std::move(errorHandler)));

    return dbExistsOnDisk || statecachetable != nullptr;
};

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

    LOG_verbose << "Backup Sync " << toHandle(config.mBackupId)
                << " transitioning to monitoring mode";

    config.setBackupState(SYNC_BACKUP_MONITOR);

    syncs.ensureDriveOpenedAndMarkDirty(config.mExternalDrivePath);
}

bool Sync::shouldHaveDatabase() const
{
    return mUnifiedSync.shouldHaveDatabase();
}

const fsfp_t& Sync::fsfp() const
{
    return getConfig().mFilesystemFingerprint;
}

void Sync::addstatecachechildren(uint32_t parent_dbid, idlocalnode_map* tmap, LocalPath& localpath, LocalNode *p, int maxdepth)
{
    assert(syncs.onSyncThread());

    auto range = tmap->equal_range(parent_dbid);

    // remove processed elements as we go, so we can then clean the database at the end.
    for (auto it = range.first; it != tmap->end() && it->first == parent_dbid; it = tmap->erase(it))
    {
        LocalNode* const l = it->second;

        auto preExisting = p->children.find(l->localname);
        if (preExisting != p->children.end())
        {
            // tidying up from prior versions of the SDK which might have duplicate LocalNodes
            LOG_debug << "Removing duplicate LocalNode: " << preExisting->second->debugGetParentList();
            delete preExisting->second;   // also detaches and preps removal from db
            assert(p->children.find(l->localname) == p->children.end());
            // l will be added in its place.  Later entries were the ones used by the old algorithm
        }

        auto restoreLen = makeScopedSizeRestorer(localpath);

        localpath.appendWithSeparator(l->localname, true);

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

    LOG_debug << syncname << "Sync " << toHandle(getConfig().mBackupId) << " about to load from db";

    statecachetable->rewind();
    unsigned numLocalNodes = 0;

    // bulk-load cached nodes into tmap
    assert(!SymmCipher::isZeroKey(syncs.syncKey.key, sizeof(syncs.syncKey.key)));
    while (statecachetable->next(&cid, &cachedata, &syncs.syncKey))
    {
        uint32_t parentID = 0;

        if (auto l = LocalNode::unserialize(*this, cachedata, parentID))
        {
            l->dbid = cid;
            tmap.emplace(parentID, l.release());
            numLocalNodes += 1;
        }
    }

    // recursively build LocalNode tree
    {
        DBTableTransactionCommitter committer(statecachetable);
        LocalPath pathBuffer = localroot->localname; // don't let localname be appended during recurse
        addstatecachechildren(0, &tmap, pathBuffer, localroot.get(), 100);

        if (!tmap.empty())
        {
            // if there is anything left in tmap, those are orphan nodes - tidy up the db
            LOG_debug << "Removing " << tmap.size() << " LocalNode orphans from db";
            for (auto& ln : tmap)
            {
                statecachedel(ln.second);
            }
        }
    }
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
    assert(l->sync == this);

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
    assert(l->sync == this);

    if (!statecachetable)
    {
        return;
    }

    if (l->type < 0)
    {
        LOG_verbose << syncname << "Leaving type " << l->type << " out of DB, (scan blocked/symlink/reparsepoint/systemhidden etc): " << l->getLocalPath();
        return;
    }

    insertq.insert(l);
    assert(l != localroot.get());
    assert(l->parent);
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
                assert((*it)->type >= 0);
                assert((*it)->sync == this);
                assert((*it)->parent->parent || (*it)->parent == localroot.get());
                if ((*it)->parent->dbid || (*it)->parent == localroot.get())
                {
                    // add once we know the parent dbid so that the parent/child structure is correct in db
                    assert(!SymmCipher::isZeroKey(syncs.syncKey.key, sizeof(syncs.syncKey.key)));
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
    assert(!(newEnableFlag && !keepSyncDb));

    if (!keepSyncDb)
    {
        if (mSync && mSync->statecachetable)
        {
            // flush our data structures before we close it.
            mSync->cachenodes();

            // remove the LocalNode database files on sync disablement (historic behaviour; sync re-enable with LocalNode state from non-matching SCSN is not supported (yet))
            mSync->statecachetable->remove();
            mSync->statecachetable.reset();
        }
        else
        {
            // delete the database file directly since we don't have an object for it
            auto fas = syncs.fsaccess->newfileaccess(false);
            if (fas->fopen(mConfig.mLocalPath, true, false, FSLogging::logOnError))
            {
                string dbname = mConfig.getSyncDbStateCacheName(fas->fsid, mConfig.mRemoteNode, syncs.mClient.me);

                // If the user is upgrading from NO SRW to SRW, we rename the DB files to the new SRW version.
                // However, if there are db files from a previous SRW version (i.e., the user downgraded from SRW to NO SRW and then upgraded again to SRW)
                // we need to remove the SRW db files. The flag DB_OPEN_FLAG_RECYCLE is used for this purpose.
                int dbFlags = DB_OPEN_FLAG_TRANSACTED; // Unused
                if (DbAccess::LEGACY_DB_VERSION == DbAccess::LAST_DB_VERSION_WITHOUT_SRW)
                {
                    dbFlags |= DB_OPEN_FLAG_RECYCLE;
                }
                LocalPath dbPath;
                syncs.mClient.dbaccess->checkDbFileAndAdjustLegacy(*syncs.fsaccess, dbname, dbFlags, dbPath);

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

    changedConfigState(!!syncs.mSyncConfigStore, notifyApp);
    mNextHeartbeat->updateSPHBStatus(*this);
}

void UnifiedSync::suspendSync()
{
    assert(syncs.onSyncThread());
    if (!mSync)
        return;
    changeState(UNLOADING_SYNC, false, false, true);
    mSync.reset();
}

void UnifiedSync::resumeSync(std::function<void(error, SyncError, handle)>&& completion)
{
    assert(syncs.onSyncThread());
    syncs.enableSyncByBackupId_inThread(mConfig.mBackupId, true, std::move(completion), "");
}

bool UnifiedSync::shouldHaveDatabase() const
{
    return syncs.mClient.dbaccess && !mConfig.isExternal();
}

SyncError UnifiedSync::changeConfigLocalRoot(const LocalPath& newPath)
{
    if (mSync != nullptr)
    {
        LOG_err << "Calling changeConfigLocalRoot on a enabled sync. Disable it first";
        return UNKNOWN_ERROR;
    }

    if (!mConfig.isGoodPathForExternalBackup(newPath))
        return BACKUP_SOURCE_NOT_BELOW_DRIVE;

    const auto fsfp = syncs.fsaccess->fsFingerprint(newPath);
    if (!fsfp)
    {
        LOG_err << "Unable to get the file system fingerprint for path " << newPath;
        return FILESYSTEM_ID_UNAVAILABLE;
    }

    if (const auto& original_fsfp = mConfig.mFilesystemFingerprint;
        original_fsfp && !original_fsfp.equivalent(fsfp))
    {
        LOG_err << "Sync root path is a different Filesystem than when the sync was created. "
                   "Original filesystem id: "
                << original_fsfp.toString() << "  Current: " << fsfp.toString();
        return LOCAL_FILESYSTEM_MISMATCH;
    }

    auto fas = syncs.fsaccess->newfileaccess();
    // we do allow, eg. mounting an exFAT drive over an NTFS folder and making a sync at that path
    const bool reparsePointOkAtRoot = true;
    if (!fas->fopen(newPath,
                    true,
                    false,
                    FSLogging::logOnError,
                    nullptr,
                    reparsePointOkAtRoot,
                    true,
                    nullptr) ||
        fas->fsid == UNDEF)
    {
        LOG_err << "Could not open sync root folder, could not get its fsid: " << newPath;
        return UNABLE_TO_RETRIEVE_ROOT_FSID;
    }

    // Shortcut, no need to rename the database file
    if (mConfig.mLocalPath == newPath && mConfig.mLocalPathFsid == fas->fsid)
        return NO_SYNC_ERROR;

    const auto oldConfig = mConfig;
    mConfig.mLocalPath = newPath;
    mConfig.mLocalPathFsid = fas->fsid;
    mConfig.mFilesystemFingerprint = fsfp;
    if (!syncs.commitConfigToDb(mConfig))
    {
        mConfig = oldConfig;
        LOG_err
            << "Couldn't commit the configuration into the database, cancelling remote root change";
        return SYNC_CONFIG_WRITE_FAILURE;
    }
    oldConfig.renameDBToMatchTarget(mConfig, *syncs.fsaccess, syncs.mClient);
    return NO_SYNC_ERROR;
}

// walk localpath and return corresponding LocalNode and its parent
// localpath must be relative to l or start with the root prefix if l == NULL
// localpath must be a full sync path, i.e. start with localroot->localname
// NULL: no match, optionally returns residual path
LocalNode* Sync::localnodebypath(LocalNode* l, const LocalPath& localpath, LocalNode** parent, LocalPath* outpath, bool
#ifndef NDEBUG
                                 fromOutsideThreadAlreadyLocked
#endif
                                 )
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
        if ((it = l->children.find(component)) == l->children.end()
            && (it = l->schildren.find(component)) == l->schildren.end())
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

bool Sync::determineCaseInsenstivity(bool secondTry)
{
    assert(mLocalPath == getConfig().mLocalPath);

    auto da = unique_ptr<DirAccess>(syncs.fsaccess->newdiraccess());
    auto lp = mLocalPath;
    if (da->dopen(&lp, NULL, false))
    {
        LocalPath leafName;
        nodetype_t dirEntryType;
        while (da->dnext(lp, leafName, false, &dirEntryType))
        {
            auto uc = Utils::toUpperUtf8(leafName.toPath(false));
            auto lc = Utils::toLowerUtf8(leafName.toPath(false));

            if (uc == lc) continue;

            auto lpuc = mLocalPath;
            auto lplc = mLocalPath;

            lpuc.appendWithSeparator(LocalPath::fromRelativePath(uc), true);
            lplc.appendWithSeparator(LocalPath::fromRelativePath(lc), true);

            LOG_debug << "Testing sync case sensitivity with " << lpuc << " vs " << lplc;

            auto fa1 = syncs.fsaccess->newfileaccess();
            auto fa2 = syncs.fsaccess->newfileaccess();

            bool opened1 = fa1->fopen(lpuc, true, false, FSLogging::logExceptFileNotFound, nullptr, false, true);
            fa1->closef();
            bool opened2 = fa2->fopen(lplc, true, false, FSLogging::logExceptFileNotFound, nullptr, false, true);
            fa2->closef();

            opened1 = opened1 && fa1->fsidvalid;
            opened2 = opened2 && fa2->fsidvalid;

            if (!opened1 && !opened2) continue;

            if (opened1 != opened2) return false;

            return fa1->fsidvalid && fa2->fsidvalid && fa1->fsid == fa1->fsid;
        }
    }

    if (secondTry)
    {
        // If we didn't figure it out, it may be a read-only empty folder, in which case it's irrelevant whether the fs is case insensitive
        LOG_debug << "We could not determine case sensitivity even after attempting to create a local sync .debris folder.  Using platform default";
#if defined(WIN32) || defined(__APPLE__)
        return true;
#else
        return false;
#endif
    }

    // we didn't find any files/folders that could be tested for case sensitivity.
    // so create the debris folder (if we can) and retry
    createDebrisTmpLockOnce();
    return determineCaseInsenstivity(true);
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

            if (tmpfa->fopen(localfilename, false, true, FSLogging::logOnError))
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

/* StallInfoMaps BEGIN */
void SyncStallInfo::StallInfoMaps::moveFromKeepingProgress(SyncStallInfo::StallInfoMaps& source)
{
    cloud = std::move(source.cloud);
    local = std::move(source.local);
    noProgress = source.noProgress;
    noProgressCount = source.noProgressCount;
}

SyncStallInfo::StallInfoMaps& SyncStallInfo::StallInfoMaps::operator=(SyncStallInfo::StallInfoMaps&& other) noexcept
{
    if (this != &other)
    {
        moveFromKeepingProgress(other);
    }
    return *this;
}

bool SyncStallInfo::StallInfoMaps::hasProgressLack() const
{
    return noProgressCount > MIN_NOPROGRESS_COUNT_FOR_LACK_OF_PROGRESS;
}

bool SyncStallInfo::StallInfoMaps::empty() const
{
    return cloud.empty() && local.empty();
}

size_t SyncStallInfo::StallInfoMaps::size() const
{
    return cloud.size() + local.size();
}

size_t SyncStallInfo::StallInfoMaps::reportableSize() const
{
    if (hasProgressLack())
    {
        return size();
    }
    size_t totalReportableSize = 0;
    for (auto& cloudStallEntry: cloud)
    {
        if (cloudStallEntry.second.alertUserImmediately)
        {
            ++totalReportableSize;
        }
    }
    for (auto& localStallEntry: local)
    {
        if (localStallEntry.second.alertUserImmediately)
        {
            ++totalReportableSize;
        }
    }
    return totalReportableSize;
}

void SyncStallInfo::StallInfoMaps::updateNoProgress()
{
    if (noProgress && noProgressCount < MAX_NOPROGRESS_COUNT)
    {
        ++noProgressCount;
    }
}

void SyncStallInfo::StallInfoMaps::setNoProgress()
{
    assert((noProgress || noProgressCount == 0) && "noProgressCount is not zero when setting progress");
    noProgress = true;
}

void SyncStallInfo::StallInfoMaps::resetNoProgress()
{
    noProgress = false;
    noProgressCount = 0;
}

void SyncStallInfo::StallInfoMaps::clearStalls()
{
    cloud.clear();
    local.clear();
}
/* StallInfoMaps END */

/* SyncStallInfo BEGIN */
bool SyncStallInfo::empty() const
{
    for (auto& syncStallInfoMapPair : syncStallInfoMaps)
    {
        auto& syncStallInfoMap = syncStallInfoMapPair.second;
        if (!syncStallInfoMap.empty())
        {
            return false;
        }
    }
    return true;
}

bool SyncStallInfo::waitingCloud(handle backupId, const string& mapKeyPath, SyncStallEntry&& e)
{
    auto& syncStallInfoMap = syncStallInfoMaps[backupId];
    for (auto i = syncStallInfoMap.cloud.begin(); i != syncStallInfoMap.cloud.end(); )
    {
        // No need to add a new entry as we've already reported some parent.
        if (IsContainingCloudPathOf(i->first, mapKeyPath) && e.reason == i->second.reason)
            return false;

        // Remove entries that are below cloudPath1.
        if (IsContainingCloudPathOf(mapKeyPath, i->first) && e.reason == i->second.reason)
        {
            i = syncStallInfoMap.cloud.erase(i);
            continue;
        }

        // Check the next entry.
        ++i;
    }

    // Add a new entry.
    syncStallInfoMap.cloud.emplace(mapKeyPath, std::move(e));
    return true;
}

bool SyncStallInfo::waitingLocal(handle backupId, const LocalPath& mapKeyPath, SyncStallEntry&& e)
{
    auto& syncStallInfoMap = syncStallInfoMaps[backupId];
    for (auto i = syncStallInfoMap.local.begin(); i != syncStallInfoMap.local.end(); )
    {
        if (i->first.isContainingPathOf(mapKeyPath) && e.reason == i->second.reason)
            return false;

        if (mapKeyPath.isContainingPathOf(i->first) && e.reason == i->second.reason)
        {
            i = syncStallInfoMap.local.erase(i);
            continue;
        }

        ++i;
    }

    syncStallInfoMap.local.emplace(mapKeyPath, std::move(e));
    return true;
}

bool SyncStallInfo::isSyncStalled(handle backupId) const
{
    return syncStallInfoMaps.find(backupId) != syncStallInfoMaps.end();
}

bool SyncStallInfo::hasImmediateStallReason() const
{
    for (auto& syncStallInfoMapPair : syncStallInfoMaps)
    {
        auto& syncStallInfoMap = syncStallInfoMapPair.second;
        for (auto& i : syncStallInfoMap.cloud)
        {
            if (i.second.alertUserImmediately)
            {
                return true;
            }
        }
        for (auto& i : syncStallInfoMap.local)
        {
            if (i.second.alertUserImmediately)
            {
                return true;
            }
        }
    }
    return false;
}

bool SyncStallInfo::hasProgressLackStall() const
{
    for (auto& syncStallInfoMapPair : syncStallInfoMaps)
    {
        auto& syncStallInfoMap = syncStallInfoMapPair.second;
        if (syncStallInfoMap.hasProgressLack())
        {
            return true;
        }
    }
    return false;
}

size_t SyncStallInfo::size() const
{
    size_t stallInfoSize = 0;
    for (auto& syncStallInfoMapPair : syncStallInfoMaps)
    {
        auto& syncStallInfoMap = syncStallInfoMapPair.second;
        stallInfoSize += syncStallInfoMap.size();
    }
    return stallInfoSize;
}

size_t SyncStallInfo::reportableSize() const
{
    size_t stallInfoReportableSize = 0;
    for (auto& syncStallInfoMapPair : syncStallInfoMaps)
    {
        auto& syncStallInfoMap = syncStallInfoMapPair.second;
        stallInfoReportableSize += syncStallInfoMap.reportableSize();
    }
    return stallInfoReportableSize;
}

void SyncStallInfo::updateNoProgress()
{
    for (auto& syncStallInfoMapPair : syncStallInfoMaps)
    {
        auto& syncStallInfoMap = syncStallInfoMapPair.second;
        syncStallInfoMap.updateNoProgress();
    }
}

void SyncStallInfo::setNoProgress()
{
    for (auto& syncStallInfoMapPair : syncStallInfoMaps)
    {
        auto& syncStallInfoMap = syncStallInfoMapPair.second;
        syncStallInfoMap.setNoProgress();
    }
}

void SyncStallInfo::moveFromButKeepCounters(SyncStallInfo& source)
{
    for (auto sourceSyncStallInfoMapIt = source.syncStallInfoMaps.begin(); sourceSyncStallInfoMapIt != source.syncStallInfoMaps.end(); )
    {
        auto&& [id, sourceSyncStallInfoMap] = *sourceSyncStallInfoMapIt;
        if (sourceSyncStallInfoMap.empty())
        {
            // if there are no stalls, this key is obsolete: remove entry in the source map and update iterator
            sourceSyncStallInfoMapIt = source.syncStallInfoMaps.erase(sourceSyncStallInfoMapIt);
        }
        else
        {
            // Add or update the key by moving the source maps (counters will be kept in the source maps)
            syncStallInfoMaps[id] = std::move(sourceSyncStallInfoMap);
            ++sourceSyncStallInfoMapIt;
        }
    }
}

void SyncStallInfo::clearObsoleteKeys(SyncStallInfo& source)
{
    // Clear obsolete keys
    for (auto syncStallInfoMapIt = syncStallInfoMaps.begin(); syncStallInfoMapIt != syncStallInfoMaps.end(); )
    {
        if (source.syncStallInfoMaps.find(syncStallInfoMapIt->first) == source.syncStallInfoMaps.end())
        {
            syncStallInfoMapIt = syncStallInfoMaps.erase(syncStallInfoMapIt);
        }
        else
        {
            ++syncStallInfoMapIt;
        }
    }
}

void SyncStallInfo::moveFromButKeepCountersAndClearObsoleteKeys(SyncStallInfo& source)
{
    moveFromButKeepCounters(source);
    clearObsoleteKeys(source);
}

#ifndef NDEBUG
void SyncStallInfo::debug() const
{
    LOG_debug << "[SyncStallInfo] Num SyncIDs = " << syncStallInfoMaps.size() << "";
    for (const auto& syncStallInfoMapPair : syncStallInfoMaps)
    {
        const auto& syncStallInfoMap = syncStallInfoMapPair.second;
        LOG_debug << "[SyncID: " << syncStallInfoMapPair.first << "]";
        LOG_debug << "noProgress: " << syncStallInfoMap.noProgress << ", noProgressCount: " << syncStallInfoMap.noProgressCount << " [HasProgressLack: " << std::string(syncStallInfoMap.hasProgressLack() ? "true" : "false") << "]";
        LOG_debug << "Num cloud stalls: " << syncStallInfoMap.cloud.size();
        for (const auto& [stallPath, stallEntry] : syncStallInfoMap.cloud)
        {
            LOG_debug << "     Cloud stall reason: " << syncWaitReasonDebugString(stallEntry.reason) << " [path = '" << stallPath << "'] [immediate = " << stallEntry.alertUserImmediately << "]";
        }
        LOG_debug << "Num local stalls: " << syncStallInfoMap.local.size();
        for (const auto& [stallPath, stallEntry] : syncStallInfoMap.local)
        {
            LOG_debug << "     Local stall reason: " << syncWaitReasonDebugString(stallEntry.reason) << " [path = '" << stallPath << "'] [immediate = " << stallEntry.alertUserImmediately << "]";
        }
    }
}
#endif
/* SyncStallInfo END */

struct Sync::ProgressingMonitor
{
    bool resolved = false;
    Sync& sync;
    SyncFlags& sf;
    SyncRow& sr;
    SyncPath& sp;
    ProgressingMonitor(Sync& s, SyncRow& row, SyncPath& fullPath) : sync(s), sf(*s.syncs.mSyncFlags), sr(row), sp(fullPath) {}

    void waitingCloud(const string& mapKeyPath, SyncStallEntry&& e)
    {
        // the caller has a path in the cloud that an operation is in progress for, or can't be dealt with yet.
        // update our list of subtree roots containing such paths
        resolved = true;

        if (sf.stall.empty())
        {
            SYNCS_verbose_timed << sync.syncname << "First sync node cloud-waiting: " << int(e.reason) << " " << sync.logTriplet(sr, sp);
        }

        sf.stall.waitingCloud(sync.getConfig().mBackupId, mapKeyPath, std::move(e));
    }

    void waitingLocal(const LocalPath& mapKeyPath, SyncStallEntry&& e)
    {
        // the caller has a local path that an operation is in progress for, or can't be dealt with yet.
        // update our list of subtree roots containing such paths
        resolved = true;

        if (sf.stall.empty())
        {
            SYNCS_verbose_timed << sync.syncname << "First sync node local-waiting: " << int(e.reason) << " " << sync.logTriplet(sr, sp);
        }

        sf.stall.waitingLocal(sync.getConfig().mBackupId, mapKeyPath, std::move(e));
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
            auto syncStallInfoMapPair = sf.stall.syncStallInfoMaps.find(sync.getConfig().mBackupId);
            if (syncStallInfoMapPair != sf.stall.syncStallInfoMaps.end())
            {
                auto& syncStallInfoMap = syncStallInfoMapPair->second;
                syncStallInfoMap.resetNoProgress();
            }
        }
    }
};

bool Sync::checkSpecialFile(SyncRow& child, SyncRow& parent, SyncPath& path)
{
    // Convenience.
    auto* f = child.fsNode;

    // Child doesn't have a local presence.
    if (!f)
        return false;

    // Assume child isn't a special file.
    auto message = "";
    auto problem = PathProblem::NoProblem;

    // Is this child a special file?
    switch (f->type)
    {
    case TYPE_NESTED_MOUNT:
        message = "nested mount";
        problem = PathProblem::DetectedNestedMount;
        break;
    case TYPE_SPECIAL:
        message = "special file";
        problem = PathProblem::DetectedSpecialFile;
        break;
    case TYPE_SYMLINK:
        message = "symbolic link";
        problem = PathProblem::DetectedSymlink;
        break;
    default:
        break;
    }

    // Child isn't a special file.
    if (problem == PathProblem::NoProblem)
        return false;

    // Check if this child's excluded.
    {
        // Convenience.
        auto& p = parent;
        auto* s = child.syncNode;

        if (s && s->exclusionState() == ES_EXCLUDED)
            return false;

        if (p.syncNode && p.exclusionState(*f) == ES_EXCLUDED)
            return false;
    }

    // Child's not excluded so emit a stall.
    ProgressingMonitor monitor(*this, child, path);

    monitor.waitingLocal(path.localPath,
                         SyncStallEntry(SyncWaitReason::FileIssue,
                                        true,
                                        false,
                                        {},
                                        {},
                                        {path.localPath, problem},
                                        {}));

    // Leave a trail for debuggers.
    LOG_warn << syncname
             << "Checked path is a "
             << message
             << ", blocked: "
             << path.localPath;

    // Let caller know we've encountered a special file.
    return true;
}

bool Sync::checkLocalPathForMovesRenames(SyncRow& row, SyncRow& parentRow, SyncPath& fullPath, bool& rowResult, bool belowRemovedCloudNode)
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
    //
    // If the move/rename is successful, then we will set the syncedNodeHandle and syncedFsid for this syncNode.
    // That captures that the row has been synced, and if the Node and FSNode are still in place, that completes the operation
    // (well, the old syncNode representing the source location will need to be removed, that won't have any fields preventing that anymore)
    // However if other changes occurred in the meantime, such as the local item being renamed or moved again,
    // while this operation was in progress, then the actions taken here will set it up to be in a situation
    // equal to how it would have been if we completed this operation first, so that the fields are set up for
    // the new change to be detected and acted on in turn.
    //
    // In the original conception of the algoritm, we would have waited for resolve_rowMatched to detect a fully synced row,
    // however for the case described above, it was insufficient because with another move/rename starting, the
    // FSNode or cloudNode would not be present at the end of our initial operation, and so the syncedFsid and
    // syncedNodeHandle would not be set, and so we could not get into a synced state or indeed detect the subsequent
    // move/rename properly.  However we still keep resolve_rowMatched as a backstop to deal with
    // cases where rows become synced independently, such as via the actions of the user themselves,
    // perhaps while a stall is going on.
    //
    // We do briefly have two LocalNodes for a the single moved file/folder while the move goes on.
    // That's convenient algorithmicaly for tracking the move, and also it's not safe to delete the old node early
    // as it might have come from a parent folder, which in data structures in the recursion stack are referring to.
    // If the move/rename fails, it's likely because the move is no longer appropriate, eg new parent folder
    // is missing.  In that case, we clean up the data structure and let the algorithm make a new choice.

    assert(syncs.onSyncThread());

    // New or different FSNode at this row.  Check if this node is where a filesystem item moved to.

    if (row.fsNode->type == TYPE_DONOTSYNC)
    {
        return false;
    }

    if (row.syncNode)
    {
        if (row.syncNode->exclusionState() == ES_EXCLUDED)
        {
            return rowResult = false, false;
        }
    }
    else if (parentRow.syncNode && parentRow.exclusionState(*row.fsNode) == ES_EXCLUDED)
    {
        return rowResult = false, false;
    }

    // Convenience.
    using MovePending = LocalNode::RareFields::MovePending;

    // Valid only if:
    // - We were part of a move that was pending due to unstable files.
    // - Those files have now become stable.
    //
    // If this pointer is valid, it means that the source has stabilized
    // and that we don't need to check it again.
    shared_ptr<MovePending> pendingTo;

    if (auto* s = row.syncNode)
    {
        // Do w have any rare fields?
        if (s->hasRare())
        {
            // Move is pending?
            if (auto& movePendingTo = s->rare().movePendingTo)
            {
                // If checkIfFileIsChanging returns nullopt it means that mFileChangingCheckState is
                // not initialized, so it's stable and we can procceed with sync
                auto waitforupdateOpt =
                    checkIfFileIsChanging(*row.fsNode, movePendingTo->sourcePath);

                // Check if the source/target has stabilized.
                if (waitforupdateOpt && *waitforupdateOpt)
                {
                    ProgressingMonitor monitor(*this, row, fullPath);

                    // Let the engine know why we're not making any progress.
                    monitor.waitingLocal(movePendingTo->sourcePath, SyncStallEntry(
                        SyncWaitReason::FileIssue, false, false,
                        {}, {},
                        {movePendingTo->sourcePath, PathProblem::FileChangingFrequently}, {}));

                    // Source and/or target is still unstable.
                    return rowResult = false, true;
                }

                // Source and target have become stable.
                pendingTo = std::move(movePendingTo);
            }

            // Move is (was) in progress?
            if (auto& moveToHere = s->rare().moveToHere)
            {
                if (moveToHere->failed)
                {
                    // Move's failed. Try again.
                    moveToHere->syncCodeProcessedResult = true;
                    moveToHere.reset();
                    s->updateMoveInvolvement();
                }
                else
                {
                    if (moveToHere->succeeded &&
                        s->rare().moveFromHere.get() == moveToHere.get())
                    {
                        // case insensitive rename is complete
                        s->rare().moveFromHere.reset();
                        s->rare().moveToHere.reset();
                        s->updateMoveInvolvement();
                        row.suppressRecursion = true;
                        rowResult = false;
                        // pass through to resolve_rowMatched on this pass, if appropriate
                        row.syncNode->setSyncAgain(false, true, false);
                        return false;
                    }

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

    // find where this fsid used to be, and the corresponding cloud Node is the one to move (provided it hasn't also moved)
    // Note that it's possible that there are multiple LocalNodes with this synced fsid, due to chained moves for example.
    // We want the one that does have the corresponding synced Node still present on the cloud side.

    // also possible: was the file overwritten by moving an existing file over it?

    LocalNode* sourceSyncNodeExcludedByFingerprintDuringPutnodes{nullptr};
    const NodeMatchByFSIDAttributes fsNodeAttributes{row.fsNode->type,
                                                     fsfp(),
                                                     cloudRootOwningUser,
                                                     row.fsNode->fingerprint};
    const auto [foundExclusionUnknown, sourceSyncNode] = syncs.findLocalNodeBySyncedFsid(
        row.fsNode->fsid,
        fsNodeAttributes,
        fullPath.localPath,
        nullptr,
        [&sourceSyncNodeExcludedByFingerprintDuringPutnodes](LocalNode* matchedNodeByFsid)
        {
            sourceSyncNodeExcludedByFingerprintDuringPutnodes = matchedNodeByFsid;
        });

    if (sourceSyncNodeExcludedByFingerprintDuringPutnodes)
    {
        ProgressingMonitor monitor(*this, row, fullPath);
        if (auto upload = std::dynamic_pointer_cast<SyncUpload_inClient>(
                sourceSyncNodeExcludedByFingerprintDuringPutnodes->transferSP);
            upload && upload->putnodesStarted)
        {
            // If the putnodes request has started, we need to wait.
            LOG_debug << "Potential move-source has outstanding putnodes: "
                      << logTriplet(row, fullPath);

            // Only emit a stall for this case if:
            // - A sync controller has been injected into the engine.
            // - The reference to that controller is still "live."
            if (syncs.hasSyncController())
            {
                // Signal a stall that observers can easily detect.
                monitor.waitingLocal(
                    fullPath.localPath,
                    SyncStallEntry(
                        SyncWaitReason::MoveOrRenameCannotOccur,
                        false,
                        false,
                        {},
                        {},
                        {sourceSyncNodeExcludedByFingerprintDuringPutnodes->getLocalPath(),
                         PathProblem::PutnodeCompletionPending},
                        {fullPath.localPath, PathProblem::NoProblem}));
            }
        }
        else
        {
            LOG_warn << "Source sync node matched by FSID has been detected as excluded by "
                        "fingerprint while having a putnodes operation ongoing, but the upload is "
                        "not found or putnodes has not started";
            assert(false && "Source sync node matched by FSID excluded by fingerprint during "
                            "putnodes operation ongoing is not meeting the requirements");
        }
        // Make sure we visit the source again.
        sourceSyncNodeExcludedByFingerprintDuringPutnodes->setSyncAgain(false, true, false);

        // We can't move just yet.
        rowResult = false;
        return true;
    }

    if (foundExclusionUnknown)
    {
        // this may occur during eg. the first pass of the tree after loading from Suspended state
        // and the corresponding node is later in the tree
        // on the next pass we should have resolved all the exclusion states, so delay this decision until then

        LOG_debug << syncname << "Move detected by fsid but the fsid's exclusion state is not determined yet. Destination here at " << logTriplet(row, fullPath);

        // Attempt the move later once exclusions have propagatged through the tree.
        parentRow.syncNode->setCheckMovesAgain(false, true, false);
        return rowResult = false, true;
    }

    if (sourceSyncNode)
    {
        // We've found a node associated with the local file's FSID.

        assert(parentRow.syncNode);
        ProgressingMonitor monitor(*this, row, fullPath);

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

        auto markSiblingSourceRow = [&sourceSyncNode = std::as_const(*sourceSyncNode),
                                     &syncNode = std::as_const(*parentRow.syncNode),
                                     &rowSiblings = row.rowSiblings]()
        {
            if (!rowSiblings)
                return false;

            if (&sourceSyncNode != &syncNode)
                return false;

            for (auto& sibling: *rowSiblings)
            {
                if (sibling.syncNode == &sourceSyncNode)
                {
                    sibling.itemProcessed = true;
                    sibling.syncNode->setSyncAgain(true, false, false);
                    return true;
                }
            }

            return false;
        };

        if (!row.syncNode)
        {
            if (!makeSyncNode_fromFS(row, parentRow, fullPath, false))
            {
                // if it failed, it already set up a waitingLocal with PathProblem::CannotFingerprintFile
                row.suppressRecursion = true;
                return rowResult = false, true;
            }
            assert(row.syncNode);
        }

        row.syncNode->namesSynchronized = sourceSyncNode->namesSynchronized;
        row.syncNode->setCheckMovesAgain(true, false, false);

        // Is the source's exclusion state well-defined?
        if (sourceSyncNode->exclusionState() == ES_UNKNOWN)
        {
            // Let the engine know why we can't perform the move.
            monitor.waitingLocal(sourceSyncNode->getLocalPath(), SyncStallEntry(
                SyncWaitReason::FileIssue, false, false,
                {}, {},
                {sourceSyncNode->getLocalPath(), PathProblem::IgnoreRulesUnknown}, {}));

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

        // Check if the move source is still present and has the same
        // FSID as the target. If it does, we've encountered a hard link
        // and need to stall.
        //
        // If we don't stall, we'll trigger an infinite rename loop.
        {
            auto sourcePath = sourceSyncNode->getLocalPath();
            auto targetPath = fullPath.localPath;

            auto sourceFSID = syncs.fsaccess->fsidOf(sourcePath, false, false, FSLogging::logExceptFileNotFound);  // skipcasecheck == false here so we only get the fsid for an exact name match
            auto targetFSID = syncs.fsaccess->fsidOf(targetPath, false, false, FSLogging::logExceptFileNotFound);  // recheck this node again to be 100% confident there are two FSNodes with the same fsid

            if (sourcePath == targetPath)
            {
                // if we run the pre-rework sync code against the same database,
                // sometimes we end up with duplicate LocalNodes that then make it seem
                // that we have hard links.
                LOG_warn << "Possible duplicate LocalNode at " << sourceSyncNode->debugGetParentList() << " vs " << row.syncNode->debugGetParentList();
                return rowResult = false, true;
            }
            else if (sourceFSID != UNDEF &&
                     targetFSID != UNDEF &&
                     sourceFSID == targetFSID)
            {
                assert(targetFSID == row.fsNode->fsid);

                // Let the user know why we can't perform the move.
                // Actually we shouldn't even think this is a move since
                // it's just due to duplicate fsids.  Just report these
                // two files as hard-links of the same file
                monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
                    SyncWaitReason::FileIssue, true, false,
                    {},
                    {},
                    {sourceSyncNode->getLocalPath(), PathProblem::DetectedHardLink},
                    {fullPath.localPath, PathProblem::DetectedHardLink}));

                // Don't try and synchronize our associate.
                markSiblingSourceRow();

                // Don't descend below this node.
                row.suppressRecursion = true;

                // Attempt the move later.
                return rowResult = false, true;
            }
        }

        // Is there something in the way at the move destination?
        string nameOverwritten;

        if (row.cloudNode && !row.hasCaseInsensitiveLocalNameChange())
        {
            if (row.cloudNode->handle == sourceSyncNode->syncedCloudNodeHandle)
            {
                // The user or someone/something else already performed the corresponding move
                // just let the syncItem() notice the local and cloud match now

                SYNC_verbose << syncname
                         << "Detected local move that is already performed remotely, at "
                         << logTriplet(row, fullPath);

                // and also let the source LocalNode be deleted now
                sourceSyncNode->setSyncedNodeHandle(NodeHandle());
                sourceSyncNode->setSyncedFsid(UNDEF, syncs.localnodeBySyncedFsid, sourceSyncNode->localname, nullptr);
                sourceSyncNode->sync->statecacheadd(sourceSyncNode);

                // not synced and no move needed
                return rowResult = false, false;
            }

            SYNC_verbose << syncname
                         << "Move detected by fsid "
                         << toHandle(row.fsNode->fsid)
                         << " but something else with that name ("
                         << row.cloudNode->name
                         << ") is already here in the cloud. Type: "
                         << row.cloudNode->type
                         << " new path: "
                         << fullPath.localPath
                         << " old localnode: "
                         << sourceSyncNode->getLocalPath()
                         << logTriplet(row, fullPath);

            // but, is it ok to overwrite that thing?  If that's what
            // happened locally to a synced file, and the cloud item was
            // also synced and is still there, then it's legit
            // overwriting a folder like that is not possible so far as
            // I know Note that the original algorithm would overwrite a
            // file or folder, moving the old one to cloud debris

            // Assume the overwrite is legitimate.
            PathProblem problem = PathProblem::NoProblem;

            // Does the overwrite appear legitimate?
            if ((row.syncNode->syncedCloudNodeHandle != row.cloudNode->handle) && !isBackup())
                problem = PathProblem::DifferentFileOrFolderIsAlreadyPresent;

            // Has the engine completed all pending scans?
            if (problem == PathProblem::NoProblem
                && !mScanningWasComplete)
                problem = PathProblem::WaitingForScanningToComplete;

            // Is the file on disk visible elsewhere?
            const auto [foundOtherButExclusionUnknown, other] = std::invoke(
                [&syncs = std::as_const(syncs),
                 &cloudRootOwningUser = std::as_const(cloudRootOwningUser),
                 &fsfp = std::as_const(fsfp()),
                 &syncNode = std::as_const(*row.syncNode),
                 &fullPath = std::as_const(fullPath),
                 problem]() -> std::pair<bool, LocalNode*>
                {
                    if (problem == PathProblem::NoProblem && !syncNode.fsidSyncedReused)
                    {
                        const NodeMatchByFSIDAttributes syncNodeAttributes{
                            syncNode.type,
                            fsfp,
                            cloudRootOwningUser,
                            syncNode.syncedFingerprint};

                        return syncs.findLocalNodeByScannedFsid(syncNode.fsid_lastSynced,
                                                                syncNodeAttributes,
                                                                fullPath.localPath);
                    }
                    return {false, nullptr};
                });

            // Then it's probably part of another move.
            if ((other && other != row.syncNode && other != sourceSyncNode)
               || foundOtherButExclusionUnknown)
            {
                problem = PathProblem::WaitingForAnotherMoveToComplete;
            }

            // Is the overwrite legitimate?
            if (problem != PathProblem::NoProblem)
            {
                // Make sure we revisit the source, too.
                sourceSyncNode->setSyncAgain(false, true, false);

                // The move source might be below the target.
                row.recurseBelowRemovedFsNode = true;

                // If there is a different file or folder already present in the cloud side, let the user decide
                if (problem == PathProblem::DifferentFileOrFolderIsAlreadyPresent)
                    return resolve_userIntervention(row, fullPath);

                // Otherwise, let the engine know why we can't proceed.
                monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
                    SyncWaitReason::MoveOrRenameCannotOccur, false, false,
                    {sourceSyncNode->syncedCloudNodeHandle, sourceSyncNode->getCloudPath(true)},
                    {NodeHandle(), fullPath.cloudPath},
                    {sourceSyncNode->getLocalPath()},
                    {fullPath.localPath, problem}));

                // Move isn't complete and this row isn't synced.
                return rowResult = false, true;
            }

            // Overwrite is legitimate.
            SYNC_verbose << syncname
                         << "Move is a legit overwrite of a synced file/folder, so we overwrite that in the cloud also."
                         << logTriplet(row, fullPath);

            // Capture the cloud node's name for anomaly detection.
            nameOverwritten = row.cloudNode->name;
        }

        // logic to detect files being updated in the local computer moving the original file
        // to another location as a temporary backup
        if (!pendingTo && sourceSyncNode->type == FILENODE)
        {
            // If checkIfFileIsChanging returns nullopt it means that mFileChangingCheckState is
            // not initialized, so it's stable and we can procceed with sync
            auto waitforupdateOpt =
                checkIfFileIsChanging(*row.fsNode, sourceSyncNode->getLocalPath());

            if (waitforupdateOpt && *waitforupdateOpt)
            {
                // Make sure we don't process the source until the move is completed.
                if (!markSiblingSourceRow())
                {
                    // Source isn't a sibling so we need to add a marker.
                    pendingTo = std::make_shared<MovePending>(sourceSyncNode->getLocalPath());

                    row.syncNode->rare().movePendingTo = pendingTo;
                    sourceSyncNode->rare().movePendingFrom = pendingTo;

                    // Make sure we revisit the source.
                    sourceSyncNode->setSyncAgain(true, false, false);
                }

                // if we revist here and the file is still the same after enough time, we'll move it
                monitor.waitingLocal(
                    sourceSyncNode->getLocalPath(),
                    SyncStallEntry(
                        SyncWaitReason::FileIssue,
                        false,
                        false,
                        {sourceSyncNode->syncedCloudNodeHandle, sourceSyncNode->getCloudPath(true)},
                        {NodeHandle(), fullPath.cloudPath},
                        {sourceSyncNode->getLocalPath(), PathProblem::FileChangingFrequently},
                        {fullPath.localPath}));

                return rowResult = false, true;
            }
        }

        row.suppressRecursion = true;   // wait until we have moved the other LocalNodes below this one

        // we don't want the source LocalNode to be visited until the move completes
        // because it might see a new file with the same name, and start an
        // upload attached to that LocalNode (which would create a wrong version chain in the account)
        // TODO: consider alternative of preventing version on upload completion - probably resulting in much more complicated name matching though
        markSiblingSourceRow();

        // Check if the move source is part of an ongoing download.
        auto sourceRequirement = Syncs::EXACT_VERSION;

        if (std::dynamic_pointer_cast<SyncDownload_inClient>(sourceSyncNode->transferSP))
        {
            // Since we were part of an ongoing download, we can infer
            // that the local move-source must have been considered
            // synced. If it wasn't, we wouldn't have detected this move
            // or a stall would've been generated during CSF processing.
            //
            // So, it should be safe for us to move the latest version
            // of the node in the cloud.
            //
            // Ideally, we'll be able to continue download processing as
            // soon as the move has been confirmed during CSF
            // processing, provided the local move-target matches the
            // previously synced local move-source.
            LOG_debug << "Move-source is part of an ongoing download: "
                      << logTriplet(row, fullPath);

            sourceRequirement = Syncs::LATEST_VERSION;
        }

        // Although we have detected a move locally, there's no guarantee the cloud side
        // is complete, the corresponding parent folders in the cloud may not match yet.
        // ie, either of sourceCloudNode or targetCloudNode could be null here.
        // So, we have a heirarchy of statuses:
        //    If any scan flags anywhere are set then we can't be sure a missing fs node isn't a move
        //    After all scanning is done, we need one clean tree traversal with no moves detected
        //    before we can be sure we can remove nodes or upload/download.
        CloudNode sourceCloudNode, targetCloudNode;
        string sourceCloudNodePath, targetCloudNodePath;
        handle sourceNodeUser = UNDEF, targetNodeUser = UNDEF;
        // Note that we get the EXACT_VERSION, not the latest version of that file.  A new file may have been added locally at that location
        // in the meantime, causing a version chain for that node.  But, we need the exact node (and especially so the Filefingerprint matches once the row lines up)
        bool foundSourceCloudNode = syncs.lookupCloudNode(sourceSyncNode->syncedCloudNodeHandle, sourceCloudNode, &sourceCloudNodePath, nullptr, nullptr, nullptr, nullptr, sourceRequirement, &sourceNodeUser);
        bool foundTargetCloudNode = syncs.lookupCloudNode(parentRow.syncNode->syncedCloudNodeHandle, targetCloudNode, &targetCloudNodePath, nullptr, nullptr, nullptr, nullptr, Syncs::FOLDER_ONLY, &targetNodeUser);

        if (foundSourceCloudNode && foundTargetCloudNode)
        {
            SYNC_verbose_timed << syncname << "Move detected by fsid "
                               << toHandle(row.fsNode->fsid) << ". Type: " << sourceSyncNode->type
                               << " new path: " << fullPath.localPath
                               << " old localnode: " << sourceSyncNode->getLocalPath()
                               << logTriplet(row, fullPath);

            if (sourceNodeUser != targetNodeUser)
            {
                LOG_debug << syncname << "Move cannot be performed in the cloud, node would be moved to a different user";
                // act like we did not find a move.  Sync will be by upload new, trash old
                return rowResult = false, false;
            }
            else if (belowRemovedCloudNode)
            {
                SYNC_verbose_timed << syncname << "Move destination detected for fsid " << toHandle(row.fsNode->fsid)
                                   << " but we are belowRemovedCloudNode, must wait for resolution at: "
                                   << fullPath.cloudPath << logTriplet(row, fullPath);

                monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
                    SyncWaitReason::MoveOrRenameCannotOccur, false, false,
                    {sourceSyncNode->syncedCloudNodeHandle, sourceSyncNode->getCloudPath(true)},
                    {NodeHandle(), fullPath.cloudPath, PathProblem::ParentFolderDoesNotExist},
                    {sourceSyncNode->getLocalPath()},
                    {fullPath.localPath}));

                row.syncNode->setSyncAgain(true, false, false);
            }
            else if (sourceSyncNode->parent && !sourceSyncNode->parent->syncedCloudNodeHandle.isUndef() &&
                     sourceSyncNode->parent->syncedCloudNodeHandle != sourceCloudNode.parentHandle)
            {
                monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
                    SyncWaitReason::MoveOrRenameCannotOccur, false, false,
                    {sourceSyncNode->syncedCloudNodeHandle, sourceSyncNode->getCloudPath(true)},
                    {NodeHandle(), fullPath.cloudPath},
                    {sourceSyncNode->getLocalPath()},
                    {fullPath.localPath, PathProblem::SourceWasMovedElsewhere}));

                    // Don't descend below this node.
                    row.suppressRecursion = true;

                    // Attempt the move later.
                    return rowResult = false, true;
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

                    // do not remember this one, as this is not the main operation this time
                    // we should not adjust syncedFsid on completion of this aspect
                    //sourceSyncNode->rare().removeNodeHere = deletePtr;

                    bool inshareFlag = inshare;
                    auto deleteHandle = row.cloudNode->handle;
                    bool canChangeVault = threadSafeState->mCanChangeVault;
                    simultaneousMoveReplacedNodeToDebris =
                        [deleteHandle, inshareFlag, deletePtr, canChangeVault](MegaClient& mc,
                                                                               TransferDbCommitter&)
                    {
                        if (auto n = mc.nodeByHandle(deleteHandle))
                        {
                            mc.movetosyncdebris(
                                n.get(),
                                inshareFlag,
                                [deletePtr](NodeHandle, Error e)
                                {
                                    // deletePtr must live until the operation is fully complete,
                                    // and we get the actionpacket back indicating Nodes are
                                    // adjusted already. otherwise, we may see the node still
                                    // present, no pending actions, and downsync it
                                    if (e)
                                        deletePtr->failed = true;
                                    else
                                        deletePtr->succeeded = true;
                                },
                                canChangeVault);
                        }
                    };

                    syncs.queueClient(std::move(simultaneousMoveReplacedNodeToDebris));

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
                movePtr->sourceFsfp = syncs.mFingerprintTracker.get(fsfp());
                movePtr->sourceFsid = row.fsNode->fsid;
                movePtr->sourceType = row.fsNode->type;
                movePtr->sourceFingerprint = row.fsNode->fingerprint;
                movePtr->sourcePtr = sourceSyncNode;
                movePtr->movedHandle = sourceCloudNode.handle;

                string newName = row.fsNode->localname.toName(*syncs.fsaccess);
                if (newName == sourceCloudNode.name ||
                    sourceSyncNode->localname == row.fsNode->localname)
                {
                    // if it wasn't renamed locally, or matches the target anyway
                    // then don't change the name
                    newName.clear();
                }

                std::function<void(MegaClient&)> signalMoveBegin;
#ifndef NDEBUG
                {
                    // For purposes of capture.
                    auto sourcePath = sourceSyncNode->getLocalPath();
                    auto targetPath = row.syncNode->getLocalPath();

                    signalMoveBegin = [sourcePath, targetPath](MegaClient& client) {
                        client.app->move_begin(sourcePath, targetPath);
                    };
                }
#endif // ! NDEBUG

                LOG_debug << syncname << "Sync - detected local rename/move " << sourceSyncNode->getLocalPath() << " -> " << fullPath.localPath;

                if (sourceCloudNode.parentHandle == targetCloudNode.handle && !newName.empty())
                {
                    // send the command to change the node name
                    LOG_debug << syncname
                              << "Renaming node: " << sourceCloudNodePath
                              << " to " << newName  << logTriplet(row, fullPath);

                    auto renameHandle = sourceCloudNode.handle;
                    bool canChangeVault = threadSafeState->mCanChangeVault;
                    syncs.queueClient([renameHandle, newName, movePtr, simultaneousMoveReplacedNodeToDebris, signalMoveBegin, canChangeVault](MegaClient& mc, TransferDbCommitter& committer)
                        {
                            if (auto n = mc.nodeByHandle(renameHandle))
                            {

                                // first move the old thing at the target path to debris.
                                // this should occur in the same batch so it looks simultaneous
                                if (simultaneousMoveReplacedNodeToDebris)
                                {
                                    simultaneousMoveReplacedNodeToDebris(mc, committer);
                                }

                                if (signalMoveBegin)
                                    signalMoveBegin(mc);

                                if (newName == ".gitignore")
                                {
                                    mc.sendevent(99493, "New .gitignore file synced up");
                                }

                                mc.setattr(n, attr_map('n', newName), [&mc, movePtr, newName](NodeHandle, Error err){

                                    LOG_debug << mc.clientname << "SYNC Rename completed: " << newName << " err:" << err;

                                    movePtr->succeeded = !error(err);
                                    movePtr->failed = !!error(err);
                                }, canChangeVault);
                            }
                        });

                    row.syncNode->rare().moveToHere = movePtr;
                    row.syncNode->updateMoveInvolvement();
                    sourceSyncNode->rare().moveFromHere = movePtr;
                    sourceSyncNode->updateMoveInvolvement();

                    rowResult = false;
                    return true;
                }
                else
                {
                    // send the command to move the node
                    LOG_debug << syncname << "Moving node: " << sourceCloudNodePath
                              << " into " << targetCloudNodePath
                              << (newName.empty() ? "" : (" as " + newName).c_str()) << logTriplet(row, fullPath);

                    bool canChangeVault = threadSafeState->mCanChangeVault;

                    if (!canChangeVault && sourceSyncNode->sync != this)
                    {
                        // possibly we need to move the source out of a backup in Vault, into a non-Vault sync
                        canChangeVault = sourceSyncNode->sync->threadSafeState->mCanChangeVault;
                    }

                    syncs.queueClient([sourceCloudNode, targetCloudNode, newName, movePtr, simultaneousMoveReplacedNodeToDebris, signalMoveBegin, canChangeVault](MegaClient& mc, TransferDbCommitter& committer)
                    {
                        if (signalMoveBegin)
                            signalMoveBegin(mc);

                        auto fromNode = mc.nodeByHandle(sourceCloudNode.handle);  // yes, it must be the exact version (should there be a version chain)
                        auto toNode = mc.nodeByHandle(targetCloudNode.handle);   // folders don't have version chains

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
                                        canChangeVault,
                                        [&mc, movePtr](NodeHandle, Error err){

                                            LOG_debug << mc.clientname << "SYNC Move completed. err:" << err;

                                            movePtr->succeeded = !error(err);
                                            movePtr->failed = !!error(err);
                                        });

                            if (err)
                            {
                                // todo: or should we mark this one as blocked and otherwise continue.

                                // err could be EACCESS or ECIRCULAR for example
                                LOG_warn << mc.clientname << "SYNC Rename not permitted due to err " << err << ": " << fromNode->displaypath()
                                    << " to " << toNode->displaypath()
                                    << (newName.empty() ? "" : (" as " + newName).c_str());

                                movePtr->failed = true;

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

                    row.syncNode->rare().moveToHere = movePtr;
                    row.syncNode->updateMoveInvolvement();
                    sourceSyncNode->rare().moveFromHere = movePtr;
                    sourceSyncNode->updateMoveInvolvement();

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
                if (!foundTargetCloudNode) SYNC_verbose_timed << syncname << "Target parent cloud node doesn't exist yet" << logTriplet(row, fullPath);

                monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
                    SyncWaitReason::MoveOrRenameCannotOccur, false, false,
                    {sourceSyncNode->syncedCloudNodeHandle, sourceSyncNode->getCloudPath(true)},
                    {NodeHandle(), fullPath.cloudPath, PathProblem::ParentFolderDoesNotExist},
                    {sourceSyncNode->getLocalPath()},
                    {fullPath.localPath}));

                row.suppressRecursion = true;
                rowResult = false;
                return true;
            }
        }
    }

    return false;
}

#ifndef NDEBUG
bool debug_confirm_getfsid(const LocalPath& p, FileSystemAccess& fsa, handle expectedFsid)
{
    auto fa = fsa.newfileaccess();
    LocalPath lp = p;
    if (fa->fopen(lp, true, false, FSLogging::logOnError, nullptr, false))
    {
        return fa->fsid == expectedFsid;
    }
    else
    {
        LOG_warn << "could not get fsid to confirm";
        return true;
    }
}
#endif

bool Sync::checkForCompletedFolderCreateHere(SyncRow& row,
                                             SyncRow& /*parentRow*/,
                                             SyncPath& fullPath,
                                             bool& rowResult)
{
    // if this cloud move was a sync decision, don't look to make it locally too
    if (row.syncNode && row.syncNode->hasRare() && row.syncNode->rare().createFolderHere)
    {
        auto& folderCreate = row.syncNode->rare().createFolderHere;

        if (folderCreate->failed)
        {
            SYNC_verbose << syncname << "Cloud folder create here failed, reset for reevaluation" << logTriplet(row, fullPath);
            folderCreate.reset();
            row.syncNode->updateMoveInvolvement();
        }
        else if (folderCreate->succeededHandle.isUndef())
        {
            SYNC_verbose << syncname << "Cloud folder move already issued for this node, waiting for it to complete. " << logTriplet(row, fullPath);
            rowResult = false;
            return true;  // row processed (no further action) but not synced
        }
        else if (row.cloudNode &&
                 row.cloudNode->handle == folderCreate->succeededHandle)
        {

            SYNC_verbose << syncname << "Cloud folder create completed in expected location, setting synced handle/fsid" << logTriplet(row, fullPath);

            // we consider this row synced now, as it was intended as a full row
            // the local node may have moved though, and there may even be a new and different item with this name in this row
            // but, setting up the row as if it had been synced means we can then calculate the next
            // action to continue syncing - such as moving this new node to the location/name of the moved/renamed FSNode

            row.syncNode->setSyncedNodeHandle(row.cloudNode->handle); //  we could set row.cloudNode->handle, but then we would not download after move if the file was both moved and updated;
            row.syncNode->setSyncedFsid(folderCreate->originalFsid, syncs.localnodeBySyncedFsid, row.syncNode->localname, nullptr);  // setting the synced fsid enables chained moves
            row.syncNode->syncedFingerprint = row.cloudNode->fingerprint;

            folderCreate.reset();
            row.syncNode->trimRareFields();
            statecacheadd(row.syncNode);

            rowResult = false;
            return true;
        }
        else
        {
            SYNC_verbose << syncname << "Folder Create completed, but cloud Node does not match now.  Reset to reevaluate." << logTriplet(row, fullPath);
            folderCreate.reset();
            row.syncNode->updateMoveInvolvement();
        }
    }

    rowResult = false;
    return false;
}

bool Sync::checkForCompletedCloudMovedToDebris(SyncRow& row,
                                               SyncRow& /*parentRow*/,
                                               SyncPath& fullPath,
                                               bool& rowResult)
{
    // if this cloud move was a sync decision, don't look to make it locally too
    if (row.syncNode && row.syncNode->hasRare() && row.syncNode->rare().removeNodeHere)
    {
        auto& ptr = row.syncNode->rare().removeNodeHere;

        if (ptr->failed)
        {
            SYNC_verbose << syncname << "Cloud move to debris here failed, reset for reevaluation" << logTriplet(row, fullPath);
            ptr.reset();
        }
        else if (!ptr->succeeded)
        {
            SYNC_verbose << syncname << "Cloud move to debris already issued for this node, waiting for it to complete. " << logTriplet(row, fullPath);
            rowResult = false;
            return true;  // row processed (no further action) but not synced
        }
        else
        {
            SYNC_verbose << syncname << "Cloud move to debris completed in expected location, setting synced handle/fsid" << logTriplet(row, fullPath);

            // Now that the operation completed, it's appropriate to set synced-ids so we can apply more logic to the updated state

            row.syncNode->setSyncedNodeHandle(NodeHandle()); //  we could set row.cloudNode->handle, but then we would not download after move if the file was both moved and updated;
            row.syncNode->setSyncedFsid(UNDEF, syncs.localnodeBySyncedFsid, row.syncNode->localname, nullptr);
            ptr.reset();
            row.syncNode->trimRareFields();
            statecacheadd(row.syncNode);
        }
    }

    rowResult = false;
    return false;
}

bool Sync::isSyncScanning() const
{
    if (!mUnifiedSync.mConfig.mError &&
        localroot->scanRequired())
    {
        SYNC_verbose_timed << syncname << " scan still required for this sync";
        return true;
    }
    return false;
}

bool Sync::checkScanningWasComplete()
{
    mScanningWasCompletePreviously = mScanningWasComplete && !syncs.mSyncFlags->isInitialPass;
    mScanningWasComplete = !isSyncScanning();
    return mScanningWasComplete;
}

void Sync::unsetScanningWasComplete()
{
    mScanningWasComplete = false;
}

bool Sync::scanningWasComplete() const
{
    return mScanningWasComplete;
}

bool Sync::checkMovesWereComplete()
{
    mMovesWereComplete = true;
    if (!mUnifiedSync.mConfig.mError)
    {
        if (!mScanningWasCompletePreviously)
        {
            SYNC_verbose_timed << syncname << " scan was not complete previously for this sync -> consider might have moves as true";
            mMovesWereComplete = false;
        }
        else if (localroot->scanRequired())
        {
            SYNC_verbose_timed << syncname << " scan still required for this sync -> consider might have moves as true";
            mMovesWereComplete = false;
        }
        else if (localroot->mightHaveMoves())
        {
            SYNC_verbose_timed << syncname << " might have pending moves";
            mMovesWereComplete = false;
        }
    }
    return mMovesWereComplete;
}

bool Sync::movesWereComplete() const
{
    return mMovesWereComplete;
}

bool Sync::processCompletedUploadFromHere(SyncRow& row,
                                          SyncRow& /*parentRow*/,
                                          SyncPath& fullPath,
                                          bool& rowResult,
                                          shared_ptr<SyncUpload_inClient> upload)
{
    // we already checked that the upload including putnodes completed before calling here.
    assert(row.syncNode && upload && upload->wasPutnodesCompleted);

    if (upload->putnodesResultHandle.isUndef())
    {
        assert(upload->putnodesFailed);

        SYNC_verbose << syncname << "Upload from here failed, reset for reevaluation"
                     << logTriplet(row, fullPath);

        row.syncNode->bypassThrottlingNextTime(syncs.maxUploadsBeforeThrottle());
    }
    else
    {
        assert(!upload->putnodesFailed);

        // Should we complete the putnodes later?
        if (syncs.deferPutnodeCompletion(fullPath.localPath))
        {
            // Let debuggers know why we haven't completed the putnodes request.
            LOG_debug << syncname
                      << "Putnode completion deferred by controller "
                      << fullPath.localPath
                      << logTriplet(row, fullPath);

            // Don't process this row any further.
            row.itemProcessed = true;

            // File isn't synchronized.
            rowResult = false;

            // Emit a special stall for observers to detect.
            ProgressingMonitor monitor(*this, row, fullPath);

            // Convenience.
            auto problem = PathProblem::PutnodeCompletionDeferredByController;

            monitor.waitingLocal(fullPath.localPath,
                                 SyncStallEntry(SyncWaitReason::UploadIssue,
                                                false,
                                                false,
                                                {NodeHandle(), fullPath.cloudPath, problem},
                                                {},
                                                {fullPath.localPath, problem},
                                                {}));

            // The upload is still in progress.
            return true;
        }

        // connect up the original cloud-sync-fs triplet, so that we can detect any
        // further moves that happened in the meantime.

        row.syncNode->increaseUploadCounter();
        SYNC_verbose << syncname
                     << "Upload from here completed, considering this file synced to original: "
                     << toHandle(upload->sourceFsid)
                     << " [Num uploads: " << row.syncNode->uploadCounter() << "]"
                     << logTriplet(row, fullPath);
        row.syncNode->setSyncedFsid(upload->sourceFsid, syncs.localnodeBySyncedFsid, row.syncNode->localname, row.syncNode->cloneShortname());
        row.syncNode->syncedFingerprint = *upload;
        row.syncNode->setSyncedNodeHandle(upload->putnodesResultHandle);
        statecacheadd(row.syncNode);

        // void going into syncItem() in case we only just got the cloud Node
        // and we are iterating that very directory already, in which case we won't have
        // the cloud side node, and we would create an extra upload
        row.itemProcessed = true;
    }

    // either way, we reset and revisit.  Following the signature pattern for similar functions
    row.syncNode->transferSP.reset();
    rowResult = false;
    return true;
}

bool Sync::checkForCompletedCloudMoveToHere(SyncRow& row,
                                            SyncRow& /*parentRow*/,
                                            SyncPath& fullPath,
                                            bool& rowResult)
{
    // if this cloud move was a sync decision, don't look to make it locally too
    if (row.syncNode && row.syncNode->hasRare() && row.syncNode->rare().moveToHere &&
        !(mCaseInsensitive && row.hasCaseInsensitiveCloudNameChange()))
    {
        auto& moveHerePtr = row.syncNode->rare().moveToHere;

        if (moveHerePtr->failed)
        {
            SYNC_verbose << syncname << "Cloud move to here failed, reset for reevaluation" << logTriplet(row, fullPath);
            moveHerePtr.reset();
            row.syncNode->updateMoveInvolvement();
        }
        else if (!moveHerePtr->succeeded)
        {
            SYNC_verbose << syncname << "Cloud move already issued for this node, waiting for it to complete. " << logTriplet(row, fullPath);
            rowResult = false;
            return true;  // row processed (no further action) but not synced
        }
        else if (row.cloudNode &&
                 row.cloudNode->handle == moveHerePtr->movedHandle)
        {

            SYNC_verbose << syncname << "Cloud move completed, setting synced handle/fsid" << logTriplet(row, fullPath);
            syncs.setSyncedFsidReused(*moveHerePtr->sourceFsfp, moveHerePtr->sourceFsid); // prevent reusing that one as move source for chained move cases

            LOG_debug << syncname << "Looking up move source by fsid " << toHandle(moveHerePtr->sourceFsid);

            LocalNode* sourceSyncNode = syncs.findMoveFromLocalNode(moveHerePtr);

            if (sourceSyncNode == row.syncNode)
            {
                LOG_debug << syncname << "Resolving sync cloud case-only rename from : " << sourceSyncNode->getCloudPath(true) << ", here! " << logTriplet(row, fullPath);
                sourceSyncNode->rare().moveFromHere.reset();
            }
            else if (sourceSyncNode && sourceSyncNode->rareRO().moveFromHere == moveHerePtr)
            {
                LOG_debug << syncname << "Resolving sync cloud move/rename from : " << sourceSyncNode->getCloudPath(true) << ", here! " << logTriplet(row, fullPath);
                assert(sourceSyncNode == moveHerePtr->sourcePtr);

                row.syncNode->setSyncedNodeHandle(sourceSyncNode->syncedCloudNodeHandle); //  we could set row.cloudNode->handle, but then we would not download after move if the file was both moved and updated;
                row.syncNode->setSyncedFsid(moveHerePtr->sourceFsid, syncs.localnodeBySyncedFsid, row.syncNode->localname, nullptr);  // setting the synced fsid enables chained moves

                // Assign the same syncedFingerprint as the move-from node
                // That way, if that row had some other sync aspect needed
                // (such as upload from an edit-then-move case, and the sync performs the move first)
                // then we will detect that same operation at this new row.
                row.syncNode->syncedFingerprint = sourceSyncNode->syncedFingerprint;

                // remove fsid (and handle) from source node, so we don't detect
                // that as a move source anymore
                sourceSyncNode->syncedFingerprint = FileFingerprint();
                sourceSyncNode->setSyncedFsid(UNDEF, syncs.localnodeBySyncedFsid, sourceSyncNode->localname, sourceSyncNode->cloneShortname());
                sourceSyncNode->setSyncedNodeHandle(NodeHandle());
                sourceSyncNode->sync->statecacheadd(sourceSyncNode);

                // Move all the LocalNodes under the source node to the new location
                // We can't move the source node itself as the recursive callers may be using it
                sourceSyncNode->moveContentTo(row.syncNode, fullPath.localPath, true);

                row.syncNode->setScanAgain(false, true, true, 0);
                sourceSyncNode->setScanAgain(true, false, false, 0);

                sourceSyncNode->rare().moveFromHere.reset();
                sourceSyncNode->trimRareFields();
                sourceSyncNode->updateMoveInvolvement();

                // If this node was repurposed for the move, rather than the normal case of creating a fresh one, we remove the old content if it was a folder
                // We have to do this after all processing of sourceSyncNode, in case the source was (through multiple operations) one of the subnodes about to be removed.
                // TODO: however, there is a risk of name collisions - probably we should use a multimap for LocalNode::children.
                for (auto& oldc : moveHerePtr->priorChildrenToRemove)
                {
                    for (auto& c : row.syncNode->children)
                    {
                        if (c.first == oldc.first && c.second == oldc.second)
                        {
                            delete c.second; // removes itself from the parent map
                            break;
                        }
                    }
                }
            }
            else if (sourceSyncNode)
            {
                // just alert us to this an double check the case in the debugger
                // resetting the movePtrs should cause re-evaluation
                LOG_debug << syncname << "We found the soure move node, but the source movePtr is no longer there." << sourceSyncNode->getCloudPath(true) << logTriplet(row, fullPath);
                assert(false);
            }
            else
            {
                // just alert us to this an double check the case in the debugger
                // resetting the movePtrs should cause re-evaluation
                LOG_debug << syncname << "Could not find move source node." << logTriplet(row, fullPath);
                assert(false);
            }

            // regardless, make sure we don't get stuck
            moveHerePtr->syncCodeProcessedResult = true;
            moveHerePtr.reset();
            row.syncNode->trimRareFields();
            row.syncNode->updateMoveInvolvement();
            statecacheadd(row.syncNode);

            rowResult = false;
            return true;
        }
        else
        {
            SYNC_verbose << syncname << "Cloud move completed, but cloud Node does not match now.  Reset to reevaluate." << logTriplet(row, fullPath);
            moveHerePtr->syncCodeProcessedResult = true;
            moveHerePtr.reset();
            row.syncNode->updateMoveInvolvement();
        }
    }

    rowResult = false;
    return false;
}



bool Sync::checkCloudPathForMovesRenames(SyncRow& row, SyncRow& parentRow, SyncPath& fullPath, bool& rowResult, bool belowRemovedFsNode)
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
    //
    // If the move/rename is successful, we set the syncedNodeHandle and syncedFsid for this node appropriately,
    // so that even in the presence of other actions happening in this sync row, such as another move/rename occurring
    // that we see on the next pass over the tree, we can still know that this row was synced, and we have
    // sufficient state recorded in order to be able to detect that subsequent move/rename, and take further actions
    // to propagate that to the other side.
    //
    // We also have the backstop of resolve_rowMatched which will recognize rows that have gotten into a synced state
    // perhaps via the actions of the user themselves, rather than the efforts of the sync (eg, when resolving stall cases)
    // That function will update our data structures, moving the sub-LocalNodes from the
    // moved-from LocalNode to the moved-to LocalNode.  Later the moved-from LocalNode will be removed as it has no FSNode or CloudNode.
    //
    // We do briefly have two LocalNodes for a the single moved file/folder while the move goes on.
    // That's convenient algorithmicaly for tracking the move, and also it's not safe to delete the old node early
    // as it might have come from a parent folder, which in data structures in the recursion stack are referring to.
    // If the move/rename fails, it's likely because the move is no longer appropriate, eg new parent folder
    // is missing.  In that case, we clean up the data structure and let the algorithm make a new choice.

    assert(syncs.onSyncThread());

    // this one is a bit too verbose for large down-syncs
    //SYNC_verbose << syncname << "checking localnodes for synced cloud handle " << row.cloudNode->handle;

    if (!row.cloudNode) // row.cloudNode is expected to exist
    {
        LOG_err << "[Sync::checkCloudPathForMovesRenames] row.CloudNode is nullptr and it shouldn't be!!!";
        assert (false);
        return false;
    }

    // Are we moving an ignore file?
    if (row.isIgnoreFile())
    {
        // Then it's not subject to the usual move procesing.
        return false;
    }

    ProgressingMonitor monitor(*this, row, fullPath);

    unique_ptr<LocalNode> childrenToDeleteOnFunctionExit;

    // find out where the node was when synced, and is now.
    // If they are the same, both pointer are set to that one.
    LocalNode* sourceSyncNodeOriginal = nullptr;
    LocalNode* sourceSyncNode = nullptr;
    bool unsureDueToIncompleteScanning = false;
    bool unsureDueToUnknownExclusionMoveSource = false;

    if (syncs.findLocalNodeByNodeHandle(row.cloudNode->handle, sourceSyncNodeOriginal, sourceSyncNode, unsureDueToIncompleteScanning, unsureDueToUnknownExclusionMoveSource))
    {
        // If we reach this point, sourceSyncNodeOriginal and sourceSyncNode should be valid pointers, as their validity is checked by findLocalNodeByHandle before returning true
        assert(sourceSyncNode && sourceSyncNodeOriginal);

        // Check if the source file/folder is still present
        if (sourceSyncNodeOriginal != sourceSyncNode)
        {
            if (row.syncNode && sourceSyncNode->getLocalPath() == row.syncNode->getLocalPath())
            {
                SYNC_verbose << "Detected cloud move that is already performed remotely, at " << logTriplet(row, fullPath);

                // let the normal syncItem() matching resolve these completed moves to the same location

                auto oldFsid = sourceSyncNodeOriginal->fsid_lastSynced;

                // and also let the source LocalNode be deleted now (note it could have been in a different sync)
                sourceSyncNodeOriginal->setSyncedNodeHandle(NodeHandle());
                sourceSyncNodeOriginal->setSyncedFsid(UNDEF, syncs.localnodeBySyncedFsid, sourceSyncNodeOriginal->localname, nullptr);
                sourceSyncNodeOriginal->sync->statecacheadd(sourceSyncNodeOriginal);

                // since we caused this move, set the synced handle and fsid.
                // this will allow us to detect chained moves
                row.syncNode->setSyncedNodeHandle(row.cloudNode->handle);
                row.syncNode->setSyncedFsid(oldFsid, syncs.localnodeBySyncedFsid, row.syncNode->localname, nullptr);
                statecacheadd(row.syncNode);

                rowResult = false;
                return true;
            }
            else
            {
                monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
                    SyncWaitReason::MoveOrRenameCannotOccur, false, true,
                    {sourceSyncNodeOriginal->syncedCloudNodeHandle, sourceSyncNodeOriginal->getCloudPath(true)},
                    {NodeHandle(), fullPath.cloudPath},
                    {sourceSyncNodeOriginal->getLocalPath(), PathProblem::SourceWasMovedElsewhere},
                    {fullPath.localPath}));

                if (parentRow.syncNode) parentRow.syncNode->setSyncAgain(false, true, false);
                rowResult = false;
                return true;
            }
        }

        LocalPath sourcePath = sourceSyncNode->getLocalPath();

        if (sourceSyncNode == row.syncNode)
        {
            if (mCaseInsensitive && row.hasCaseInsensitiveCloudNameChange())
            {
                LOG_debug << "Move is the same node but is also a case insensitive name change: " << sourcePath;
            }
            else
            {
                return false;
            }
        }
        else if (sourcePath == fullPath.localPath)
        {
            // This case was seen in a log, possibly due to duplicate LocalNodes.
            // We don't want to move the target out of the way to the .debris, then find it's not present for move/rename
            LOG_debug << "Move would be to self: " << sourcePath;
            return false;
        }

        // Are we moving an ignore file?
        if (sourceSyncNode->isIgnoreFile())
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
            LOG_warn << "Cloud node move detected and this is a BACKUP! Triplet: "
                     << logTriplet(row, fullPath);
            assert(false && "Cloud node modifications should not happen for a backup!");
            rowResult = false; // Let's solve the issue in the syncItem step
            return true;
        }

        assert(parentRow.syncNode);
        if (parentRow.syncNode) parentRow.syncNode->setCheckMovesAgain(false, true, false);
        if (row.syncNode) row.syncNode->setCheckMovesAgain(true, false, false);

        // Is the source's exclusion state well defined?
        if (sourceSyncNode->exclusionState() == ES_UNKNOWN)
        {
            // Let the engine know why we couldn't process this move.
            monitor.waitingLocal(sourceSyncNode->getLocalPath(), SyncStallEntry(
                SyncWaitReason::FileIssue, false, true,
                {sourceSyncNode->syncedCloudNodeHandle, sourceSyncNode->getCloudPath(false), PathProblem::IgnoreRulesUnknown},
                {},
                {sourceSyncNode->getLocalPath(), PathProblem::IgnoreRulesUnknown},
                {}));

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

        // True if the move-target exists and we're free to "overwrite" it.
        auto overwrite = false;

        bool caseInsensitiveRename = mCaseInsensitive && row.syncNode &&
            row.syncNode->syncedCloudNodeHandle == row.cloudNode->handle &&
            row.hasCaseInsensitiveCloudNameChange();

        // is there already something else at the target location though?
        // and skipping the case of a case insensitive rename
        if (row.fsNode && !caseInsensitiveRename)
        {
            // todo: should we check if the node that is already here is in fact a match?  in which case we should allow progressing to resolve_rowMatched
            SYNC_verbose << syncname
                         << "Move detected by nodehandle, but something else with that name is already here locally. Type: "
                         << row.fsNode->type
                         << " moved node: "
                         << fullPath.cloudPath
                         << " old parent correspondence: "
                         << (sourceSyncNode->parent ? sourceSyncNode->parent->getLocalPath().toPath(false) : "<null>")
                         << logTriplet(row, fullPath);

            // Assume we've encountered an illegitimate overwrite.
            PathProblem problem = PathProblem::DifferentFileOrFolderIsAlreadyPresent;

            // Does the file on disk match what this node was previously synced against?
            if (row.syncNode
                && (row.syncNode->type == row.fsNode->type
                    && row.syncNode->fsid_lastSynced == row.fsNode->fsid))
                problem = PathProblem::NoProblem;

            // Does the node exist elsewhere in the tree?
            while (problem == PathProblem::NoProblem)
            {
                CloudNode node;
                auto active = false;
                auto excluded = false;
                auto trash = false;
                auto found = syncs.lookupCloudNode(row.syncNode->syncedCloudNodeHandle,
                                                   node,
                                                   nullptr,
                                                   &trash,
                                                   &active,
                                                   &excluded,
                                                   nullptr,
                                                   Syncs::EXACT_VERSION);

                if (!found || !active || excluded || trash)
                    break;

                if (node.parentHandle != row.cloudNode->parentHandle
                    || node.name != row.cloudNode->name)
                    problem = PathProblem::WaitingForAnotherMoveToComplete;

                break;
            }

            if (problem != PathProblem::NoProblem)
            {
                parentRow.syncNode->setCheckMovesAgain(false, true, false);

                // If there is a different file or folder already present in the local side, let the user decide
                if (problem == PathProblem::DifferentFileOrFolderIsAlreadyPresent && !isBackup())
                    return resolve_userIntervention(row, fullPath);

                monitor.waitingCloud(fullPath.cloudPath, SyncStallEntry(
                    SyncWaitReason::MoveOrRenameCannotOccur, false, true,
                    {sourceSyncNode->syncedCloudNodeHandle, sourceSyncNode->getCloudPath(true)},
                    {NodeHandle(), fullPath.cloudPath, problem},
                    {sourceSyncNode->getLocalPath()},
                    {fullPath.localPath}));

                // Move isn't complete and this row isn't synced.
                return rowResult = false, true;
            }

            SYNC_verbose << syncname
                         << "Move is a legit overwrite of a synced file, so we overwrite that locally too."
                         << logTriplet(row, fullPath);

            overwrite = true;
        }

        if (!sourceSyncNode->moveApplyingToLocal && !belowRemovedFsNode && parentRow.cloudNode)
        {
            LOG_debug << syncname << "Move detected by nodehandle. Type: " << sourceSyncNode->type
                << " moved node: " << fullPath.cloudPath
                << " old parent correspondence: " << (sourceSyncNode->parent ? sourceSyncNode->parent->getLocalPath().toPath(false) : "<null>")
                << logTriplet(row, fullPath);

            LOG_debug << "Sync - detected remote move " << fullPath.cloudPath <<
                " from corresponding " << (sourceSyncNode->parent ? sourceSyncNode->parent->getLocalPath().toPath(false) : "<null>") <<
                " to " << parentRow.cloudNode->name;

            sourceSyncNode->moveApplyingToLocal = true;
        }

        assert(!isBackup());

        // we don't want the source LocalNode to be visited until after the move completes, and we revisit with rescanned folder data
        // because it might see a new file with the same name, and start a download, keeping the row instead of removing it
        markSiblingSourceRow();

        if (belowRemovedFsNode)
        {
            SYNC_verbose_timed << syncname << "Move destination detected for node " << row.cloudNode->handle
                               << " but we are belowRemovedFsNode, must wait for resolution at: "
                               << logTriplet(row, fullPath);;

            monitor.waitingCloud(fullPath.cloudPath, SyncStallEntry(
                SyncWaitReason::MoveOrRenameCannotOccur, false, true,
                {sourceSyncNode->syncedCloudNodeHandle, sourceSyncNode->getCloudPath(true)},
                {NodeHandle(), fullPath.cloudPath},
                {sourceSyncNode->getLocalPath()},
                {fullPath.localPath, PathProblem::ParentFolderDoesNotExist}));

            if (parentRow.syncNode) parentRow.syncNode->setSyncAgain(false, true, false);
            rowResult = false;
            return true;
        }

        // check filesystem is not changing fsids as a result of rename
        //
        // Only meaningful on filesystems with stable FSIDs.
        //
        // We've observed strange behavior when running on FAT filesystems under Windows.
        // There, moving a directory (or file) to another parent will cause that directory
        // (or file) to gain a new FSID.
        assert(!fsstableids || debug_confirm_getfsid(sourcePath, *syncs.fsaccess, sourceSyncNode->fsid_lastSynced));

        if (overwrite)
        {
            // If overwrite is true, row.syncNode must exist at this point
            assert(row.syncNode);

            SYNC_verbose << "Move-target exists and must be moved to local debris: " << fullPath.localPath;

            if (!movetolocaldebris(fullPath.localPath))
            {
                // Couldn't move the target to local debris.
                LOG_err << "Couldn't move move-target to local debris: " << fullPath.localPath;

                monitor.waitingCloud(fullPath.cloudPath, SyncStallEntry(
                    SyncWaitReason::CannotPerformDeletion, false, true,
                    {},
                    {},
                    {fullPath.localPath, PathProblem::MoveToDebrisFolderFailed},
                    {}));

                // Don't recurse as the subtree's fubar.
                row.suppressRecursion = true;

                // Move hasn't completed.
                sourceSyncNode->moveAppliedToLocal = false;

                // Row hasn't been synced.
                rowResult = false;

                return true;
            }

            LOG_debug << syncname << "Move-target moved to local debris: " << fullPath.localPath;

            // Explicitly rescan the parent.
            //
            // This is necessary even when we're not operating in periodic
            // scan mode as we can't rely on the system delivering
            // filesystem events to us in a timely manner.
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

        if (caseInsensitiveRename)
        {
            auto oldPath = fullPath.localPath;
            fullPath.localPath = parentRow.syncNode->getLocalPath();
            fullPath.localPath.appendWithSeparator(LocalPath::fromRelativeName(row.cloudNode->name, *syncs.fsaccess, mFilesystemType), true);
            LOG_debug << "Executing case-only local rename: " << oldPath << " to " << fullPath.localPath << " (also this path should match: " << sourcePath << ")";
        }

        if (syncs.fsaccess->renamelocal(sourcePath, fullPath.localPath))
        {
            // todo: move anything at this path to sync debris first?  Old algo didn't though
            // todo: additional consideration: what if there is something here, and it should be moved/renamed to elsewhere in the sync (not the debris) first?
            // todo: additional consideration: what if things to be renamed/moved form a cycle?

            // check filesystem is not changing fsids as a result of rename
            //
            // Only meaningful on filesystems with stable FSIDs.
            //
            // We've observed strange behavior when running on FAT filesystems under Windows.
            // There, moving a directory (or file) to another parent will cause that directory
            // (or file) to gain a new FSID.
            assert(overwrite || !fsstableids || debug_confirm_getfsid(fullPath.localPath, *syncs.fsaccess, sourceSyncNode->fsid_lastSynced));

            LOG_debug << syncname << "Sync - executed local rename/move " << sourceSyncNode->getLocalPath() << " -> " << fullPath.localPath;

            if (caseInsensitiveRename)
            {
                row.syncNode->setScanAgain(false, true, false, 0); // if caseInsensitiveRename, row.syncNode is a valid pointer
            }
            else
            {
                if (!row.syncNode)
                {
                    resolve_makeSyncNode_fromCloud(row, parentRow, fullPath, false);
                    assert(row.syncNode);
                }

                row.syncNode->namesSynchronized = sourceSyncNode->namesSynchronized;

                // remove fsid (and handle) from source node, so we don't detect
                // that as a move source anymore
                sourceSyncNode->setSyncedFsid(UNDEF, syncs.localnodeBySyncedFsid, sourceSyncNode->localname, nullptr);  // shortname will be updated when rescan
                sourceSyncNode->setSyncedNodeHandle(NodeHandle());
                sourceSyncNode->sync->statecacheadd(sourceSyncNode);

                sourceSyncNode->moveContentTo(row.syncNode, fullPath.localPath, true);

                sourceSyncNode->moveAppliedToLocal = true;

                sourceSyncNode->setScanAgain(true, false, false, 0);
                row.syncNode->setScanAgain(true, true, true, 0);  // scan parent to see this moved fs item, also scan subtree to see if anything new is in there to overcome race conditions with fs notifications from the prior fs subtree paths

                // Mark this row as synced immediately, to cover the case where the user
                // moves the item back immediately, perhaps they made a mistake,
                // and we don't get a chance to recognise the row as synced in a future pass
                // Becuase in that case, we would end up with a download instead of a chained move

                if (auto fsNode = FSNode::fromPath(*syncs.fsaccess, fullPath.localPath, false, FSLogging::logOnError))
                {
                    // Make this new fsNode part of our sync data structure
                    parentRow.fsAddedSiblings.emplace_back(std::move(*fsNode));
                    row.fsNode = &parentRow.fsAddedSiblings.back();
                    row.syncNode->slocalname = row.fsNode->cloneShortname();

                    row.syncNode->setSyncedFsid(row.fsNode->fsid, syncs.localnodeBySyncedFsid, row.fsNode->localname, row.fsNode->cloneShortname());
                    row.syncNode->syncedFingerprint = row.fsNode->fingerprint;
                    row.syncNode->setSyncedNodeHandle(row.cloudNode->handle);
                    statecacheadd(row.syncNode);
                }
            }

            rowResult = false;
            return true;
        }
        else if (syncs.fsaccess->transient_error)
        {
            LOG_warn << "transient error moving folder: " << sourcePath << logTriplet(row, fullPath);

            monitor.waitingCloud(fullPath.cloudPath, SyncStallEntry(
                SyncWaitReason::MoveOrRenameCannotOccur, false, true,
                {sourceSyncNode->syncedCloudNodeHandle, sourceSyncNode->getCloudPath(true)},
                {NodeHandle(), fullPath.cloudPath},
                {sourceSyncNode->getLocalPath()},
                {fullPath.localPath, PathProblem::FilesystemErrorDuringOperation}));

            row.suppressRecursion = true;
            sourceSyncNode->moveApplyingToLocal = false;
            rowResult = false;
            return true;
        }
        else if (syncs.fsaccess->target_name_too_long)
        {
            LOG_warn << "Unable to move folder as the move target's name is too long: "
                     << sourcePath
                     << logTriplet(row, fullPath);

            monitor.waitingCloud(fullPath.cloudPath, SyncStallEntry(
                SyncWaitReason::MoveOrRenameCannotOccur, true, true,
                {sourceSyncNode->syncedCloudNodeHandle, sourceSyncNode->getCloudPath(true)},
                {NodeHandle(), fullPath.cloudPath},
                {sourceSyncNode->getLocalPath()},
                {fullPath.localPath, PathProblem::NameTooLongForFilesystem}));

            row.suppressRecursion = true;
            sourceSyncNode->moveApplyingToLocal = false;
            rowResult = false;

            return true;
        }
        else
        {
            SYNC_verbose << "Move to here delayed since local parent doesn't exist yet: " << sourcePath << logTriplet(row, fullPath);

            monitor.waitingCloud(fullPath.cloudPath, SyncStallEntry(
                SyncWaitReason::MoveOrRenameCannotOccur, false, true,
                {sourceSyncNode->syncedCloudNodeHandle, sourceSyncNode->getCloudPath(true)},
                {NodeHandle(), fullPath.cloudPath},
                {sourceSyncNode->getLocalPath()},
                {fullPath.localPath, PathProblem::ParentFolderDoesNotExist}));

            rowResult = false;
            return true;
        }
    }
    else if (unsureDueToIncompleteScanning)
    {
        monitor.waitingCloud(fullPath.cloudPath, SyncStallEntry(
            SyncWaitReason::MoveOrRenameCannotOccur, false, true,
            {sourceSyncNodeOriginal->syncedCloudNodeHandle, sourceSyncNodeOriginal->getCloudPath(true)},
            {NodeHandle(), fullPath.cloudPath},
            {sourceSyncNodeOriginal->getLocalPath()},
            {fullPath.localPath, PathProblem::WaitingForScanningToComplete}));

        rowResult = false;
        return true;
    }
    else if (unsureDueToUnknownExclusionMoveSource)
    {
        SYNC_verbose << "Move to here delayed since unsureDueToUnknownExclusionMoveSource at: " << logTriplet(row, fullPath);

        monitor.waitingCloud(fullPath.cloudPath, SyncStallEntry(
            SyncWaitReason::MoveOrRenameCannotOccur, false, true,
            {NodeHandle(), string(), PathProblem::IgnoreRulesUnknown},
            {row.cloudNode->handle, fullPath.cloudPath},
            {LocalPath(), PathProblem::IgnoreRulesUnknown},
            {fullPath.localPath}));

        rowResult = false;
        return true;
    }
    else
    {
        monitor.noResult();
    }
    return false;
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
        lastFSNotificationTime = syncs.waiter->ds;

        // Skip invalidated notifications.
        if (notification.invalidated())
        {
            LOG_debug << syncname << "Notification skipped: "
                      << notification.path;
            continue;
        }

        // Skip notifications from this sync's debris folder.
        if (notification.fromDebris(*this))
        {
            LOG_debug << syncname
                      << "Debris notification skipped: "
                      << notification.path;
            continue;
        }

        LocalPath remainder;
        LocalNode* nearest = nullptr;
        LocalNode* node = notification.localnode;

        // Notify the node or its parent
        LocalNode* match = localnodebypath(node, notification.path, &nearest, &remainder, false);

        // Check it's not below an excluded path
        if (nearest && !remainder.empty())
        {
            if (nearest->type == TYPE_DONOTSYNC)
            {
                SYNC_verbose << "Ignoring notification under do-not-sync node: "
                             << node->getLocalPath() << string(LocalPath::localPathSeparator_utf8, 1) << notification.path;
                continue;
            }

            LocalPath firstComponent;
            size_t index = 0;
            if (remainder.nextPathComponent(index, firstComponent))
            {
                // firstComponent is a folder if has next path component, otherwise it is unknown
                auto type = remainder.hasNextPathComponent(index) ? FOLDERNODE : TYPE_UNKNOWN;

                if (!(firstComponent == IGNORE_FILE_NAME) &&
                    (isDoNotSyncFileName(firstComponent.toPath(false)) ||
                    ES_EXCLUDED == nearest->exclusionState(firstComponent, type, 0)))
                {
                    // no need to rescan anything when the change was in an excluded folder
                    SYNC_verbose << "Ignoring notification under excluded/do-not-sync node:"
                                 << node->getLocalPath() << string(1, LocalPath::localPathSeparator_utf8) << notification.path;;
                    continue;
                }
            }
        }

        bool scanDescendants = false;

        // figure out which node we are going to scan.  'nearest' will be assigned the one (or it is already)
        if (match)
        {
            if (match->type == FILENODE)
            {
                // the node was a file, so it's always the parent that needs scanning so we see the
                // updated file metadata in the directory entry.  Additionally, re-fingerprint to detect data change
                match->recomputeFingerprint = true;
                nearest = match->parent;
                if (!nearest) continue;
            }
            else
            {
                // we found the exact node specified, recursion on request makes sense
                scanDescendants = notification.scanRequirement == Notification::FOLDER_NEEDS_SCAN_RECURSIVE;

                // for a folder path, we support either path specified (entries added/removed)
                // or the parent (eg access changed), depending on flags passed from platform layer
                nearest = match->parent && notification.scanRequirement == Notification::NEEDS_PARENT_SCAN
                        ? match->parent
                        : match;

            }
        }
        else
        {
            // we didn't find the exact path specified. But, if we are only one layer up
            // and we would have scanned parent anyway (ie, file or NEEDS_PARENT_SCAN), then it's equivalent
            // if we are higher up the tree than that, it's again the same
            // basically, scan the folder we can determine this notification is below: nearest.

            if (nearest && nearest->type == FILENODE)
            {
                nearest->recomputeFingerprint = true;
                nearest = nearest->parent;
                assert(nearest && nearest->type != FILENODE);
            }
        }

        if (!nearest)
        {
            // we didn't find any suitable ancestor within the sync
            LOG_debug << "Notification had no scannable result:"  << node->getLocalPath() << " " << notification.path;;
            continue;
        }

        if (nearest->expectedSelfNotificationCount > 0)
        {
            if (nearest->scanDelayUntil >= syncs.waiter->ds)
            {
                // self-caused notifications shouldn't cause extra waiting
                --nearest->expectedSelfNotificationCount;

                SYNC_verbose << "Skipping self-notification (remaining: "
                    << nearest->expectedSelfNotificationCount << ") at: "
                    << nearest->getLocalPath();

                continue;
            }
            else
            {
                SYNC_verbose << "Expected more self-notifications ("
                    << nearest->expectedSelfNotificationCount << ") but they were late, at: "
                    << nearest->getLocalPath();
                nearest->expectedSelfNotificationCount = 0;
            }
        }

        // Let the parent know it needs to perform a scan.
        //if (nearest->scanAgain < TREE_ACTION_HERE)
        {
            SYNC_verbose << "Trigger scan flag by fs notification on "
                         << nearest->getLocalPath()
                         << (scanDescendants ? " (recursive)" : "");
        }

        nearest->setScanAgain(false, true, scanDescendants, SCANNING_DELAY_DS);

        if (nearest->rareRO().scanBlocked)
        {
            // in case permissions changed on a scan-blocked folder
            // retry straight away, but don't reset the backoff delay
            nearest->rare().scanBlocked->scanBlockedTimer.set(syncs.waiter->ds);
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
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday);
    LocalPath targetFolder = localdebris;
    targetFolder.appendWithSeparator(LocalPath::fromRelativePath(buf), true);

    bool failedDueToTargetExists = false;

    if (movetolocaldebrisSubfolder(localpath, targetFolder, false, failedDueToTargetExists))
    {
        return true;
    }

    if (!failedDueToTargetExists) return false;

    // next try a subfolder with additional time and sequence - target filename clashes here should not occur
    snprintf(strchr(buf, 0), sizeof(buf) - strlen(buf), " %02d.%02d.%02d.", ptm->tm_hour,  ptm->tm_min, ptm->tm_sec);

    string datetime = buf;
    bool counterReset = false;
    if (datetime != mLastDailyDateTimeDebrisName)
    {
        mLastDailyDateTimeDebrisName = datetime;
        mLastDailyDateTimeDebrisCounter = 0;
        counterReset = true;
    }

    // initially try wih the same sequence number as last time, to avoid making large numbers of these when possible
    LocalPath targetFolderWithDate = targetFolder;
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

    targetFolder = targetFolderWithDate;
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
            LOG_verbose << syncname << "Created daily local debris folder: " << targetFolder;
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

void Syncs::confirmOrCreateDefaultMegaignore(bool transitionToMegaignore, unique_ptr<DefaultFilterChain>& resultIfDfc, unique_ptr<string_vector>& resultIfMegaignoreDefault)
{
    // In a new install, we would populate .megaignore.default with the default defaults.
    // But when upgrading an old MEGAsync that has syncs and legacy rules, copy
    // those legacy rules to .megaignore.default.  transitionToMegaignore tells us which it is.

    resultIfDfc.reset(new DefaultFilterChain(transitionToMegaignore ? mLegacyUpgradeFilterChain : mNewSyncFilterChain));

    // However, if .megaignore.default already exists, then use that one of course.
    // If it doesn't exist yet, write it.

    auto defaultpath = mClient.dbaccess->rootPath();
    defaultpath.appendWithSeparator(LocalPath::fromRelativePath(".megaignore.default"), false);
    if (!fsaccess->fileExistsAt(defaultpath))
    {
        LOG_info << "Writing .megaignore.default according to upgrade flag: " << transitionToMegaignore << " at " << defaultpath;
        if (!resultIfDfc->create(defaultpath, false, *fsaccess, false))
        {
            LOG_err << "Failed to write .megaignore.default";
        }
    }
    else if (!transitionToMegaignore)
    {
        // If we are in transitionToMegaignore, don't load from the default as it will lose
        // the absolute paths which might be relevant for a particular sync
        resultIfMegaignoreDefault.reset(new string_vector);
        auto fa = fsaccess->newfileaccess(false);
        if (fa->fopen(defaultpath, true, false, FSLogging::logOnError))
        {
            if (readLines(*fa, *resultIfMegaignoreDefault))
            {
                // Return the text of the file.
                // FilterChain and FilterChainDefault are very different and conversion between them is not really practical
                resultIfDfc.reset();
                return;
            }
        }
        LOG_err << "Failed to load .megaignore.default, going with default defaults instead";
        resultIfMegaignoreDefault.reset();
    }
}

error Syncs::createMegaignoreFromLegacyExclusions(const LocalPath& targetPath)
{
    LOG_info << "Writing .megaignore with legacy exclusion rules at " << targetPath;

    // Check whether the file already exists
    auto targetPathWithFileName = targetPath;
    targetPathWithFileName.appendWithSeparator(IGNORE_FILE_NAME, false);
    if (fsaccess->fileExistsAt(targetPathWithFileName))
    {
        LOG_err << "Failed to write " << targetPathWithFileName
                << " because the file already exists";
        return API_EEXIST;
    }

    // Safely copy the legacy filter chain
    auto legacyFilterChain = std::make_unique<DefaultFilterChain>(mLegacyUpgradeFilterChain);

    // Write the file
    if (!legacyFilterChain->create(targetPath, true, *fsaccess, false))
    {
        LOG_err << "Failed to write " << targetPath;
        return API_EACCESS;
    }

    return API_OK;
}

void Syncs::enableSyncByBackupId(handle backupId, bool setOriginalPath, std::function<void(error, SyncError, handle)> completion, bool completionInClient, const string& logname)
{
    assert(!onSyncThread());

    auto clientCompletion = [=](error e, SyncError se, handle)
        {
            queueClient([completion, e, se, backupId](MegaClient&, TransferDbCommitter&)
                {
                    if (completion) completion(e, se, backupId);
                });
        };

    queueSync([=]()
        {
            enableSyncByBackupId_inThread(backupId, setOriginalPath, completionInClient ? clientCompletion : completion, logname);
        }, "enableSyncByBackupId");
}

void Syncs::enableSyncByBackupId_inThread(handle backupId, bool setOriginalPath, std::function<void(error, SyncError, handle)> completion, const string& logname, const string& excludedPath)
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
        // already exists
        if (us.mConfig.mError == NO_SYNC_ERROR)
        {
            if (us.mConfig.mRunState == SyncRunState::Run)
            {
                // it's already running
                LOG_debug << "Sync with id " << backupId << " is already running";
            }
            else if (us.mConfig.mRunState == SyncRunState::Suspend) // this actually represents "paused" syncs
            {
                LOG_debug << "Sync with id " << backupId << " switched to running";
                us.mConfig.mRunState = SyncRunState::Run;
                mClient.app->syncupdate_stateconfig(us.mConfig);
            }
            else
            {
                LOG_err << "Sync with id " << backupId << " already exists and should be in SyncRunState::Run(" << static_cast<int>(SyncRunState::Run) << "), however, the actual state is " << static_cast<int>(us.mConfig.mRunState) << " (state will now be set to SyncRunState::Run)";
                assert(false && "us.mConfig.mRunState should be SyncRunState::Run but it is not");
                us.mConfig.mRunState = SyncRunState::Run;
                mClient.app->syncupdate_stateconfig(us.mConfig);
            }

            if (completion) completion(API_OK, NO_SYNC_ERROR, backupId);
            return;
        }

        // there is a sync error, it should've been reset in Syncs::stopSyncsInErrorState, but next loop hasn't be executed yet (so let's do it now)
        LOG_warn << "Sync with id " << backupId << " has a sync error (" << us.mConfig.mError << "). It will be reset now";
        us.mSync.reset();
    }

    us.mConfig.mError = NO_SYNC_ERROR;
    us.mConfig.mRunState = SyncRunState::Loading;

    // Regenerate LN cache if no fingerprint's been assigned.
    bool resetFingerprint = !us.mConfig.mFilesystemFingerprint;

#ifdef __APPLE__
    if (!resetFingerprint)
    {
        LOG_debug << "turning on reset of filesystem fingerprint on Mac, as they are not consistent there";  // eg. from networked filesystem, qnap shared drive
        resetFingerprint = true;
    }
#endif

    if (!resetFingerprint && !us.mConfig.mDatabaseExists)
    {
        // It's ok to sync to a new folder (new fs even) at the same path, if we are truly going from scratch
        // Users may, eg. put a sync to disabled, move the local folder elsewhere, make an empty folder with that same name, restart the sync
        LOG_debug << "turning on reset of filesystem fingerprint for previously disabled sync (ie, had no database)";
        resetFingerprint = true;
    }

    if (resetFingerprint)
    {
        us.mConfig.mFilesystemFingerprint.reset(); //This will cause the local filesystem fingerprint to be recalculated
        us.mConfig.mLocalPathFsid = UNDEF;
    }

    if (setOriginalPath)
    {
        CloudNode cloudNode;
        string cloudNodePath;
        if (lookupCloudNode(us.mConfig.mRemoteNode, cloudNode, &cloudNodePath, nullptr, nullptr, nullptr, nullptr, Syncs::FOLDER_ONLY)
            &&  us.mConfig.mOriginalPathOfRemoteRootNode != cloudNodePath)
        {
            us.mConfig.mOriginalPathOfRemoteRootNode = cloudNodePath;
            ensureDriveOpenedAndMarkDirty(us.mConfig.mExternalDrivePath);
        }
    }

    error e;
    {
        // todo: even better thead safety
        lock_guard<mutex> g(mClient.nodeTreeMutex);
        std::tie(e, us.mConfig.mError, us.mConfig.mWarning) = mClient.checkSyncConfig(us.mConfig);
        us.mConfig.mEnabled = e == API_OK && us.mConfig.mError == NO_SYNC_ERROR;
    }

    if (e)
    {
        // error and enable flag were already changed
        LOG_debug << "Enablesync checks resulted in error: " << e;

        us.mConfig.mRunState = us.mConfig.mDatabaseExists ? SyncRunState::Suspend : SyncRunState::Disable;

        us.changedConfigState(true, true);
        if (completion) completion(e, us.mConfig.mError, backupId);
        return;
    }

    // Does this sync already contain an ignore file?
    if (!hasIgnoreFile(us.mConfig))
    {
        // Create a new chain so that we can add custom rules if necessary.
        unique_ptr<DefaultFilterChain> resultIfDfc;
        unique_ptr<string_vector> resultIfMegaignoreDefault;
        confirmOrCreateDefaultMegaignore(!us.mConfig.mLegacyExclusionsIneligigble, resultIfDfc, resultIfMegaignoreDefault);
        assert(resultIfDfc || resultIfMegaignoreDefault);

        bool writeMegaignoreFailed = false;

        if (resultIfDfc)
        {
            // We are using default rules, or legacy rules for this sync

            // Do we have a custom rule to apply?
            if (!excludedPath.empty())
            {
                resultIfDfc->excludePath(excludedPath);
            }

            // Try and create the missing ignore file.  Not synced by default
            if (!resultIfDfc->create(us.mConfig.mLocalPath, true, *fsaccess, false))
            {
                LOG_debug << "Failed to create ignore file for sync without one at: " << us.mConfig.mLocalPath;
                writeMegaignoreFailed = true;
            }
        }
        else
        {
            // We are copying .megaignore.default but adding one more excluded path into it

            // Do we have a custom rule to apply?
            if (!excludedPath.empty())
            {
                string temp = excludedPath;
                LocalPath::utf8_normalize(&temp);
                auto targetPath = LocalPath::fromAbsolutePath(std::move(temp));
                size_t index;
                if (us.mConfig.mLocalPath.isContainingPathOf(targetPath, &index))
                {
                    // Path exclusions should be relative to the .megaignore file
                    resultIfMegaignoreDefault->push_back("-p:" + targetPath.subpathFrom(index).toPath(false));
                }
            }

            string wholefile = string("\xEF\xBB\xBF", 3);        // utf8-BOM
            for (auto& line : *resultIfMegaignoreDefault)
            {
#ifdef WIN32
                wholefile += line + "\r\n";
#else
                wholefile += line + "\n";
#endif
            }

            auto filePath = us.mConfig.mLocalPath;
            filePath.appendWithSeparator(IGNORE_FILE_NAME, false);
            auto fa = fsaccess->newfileaccess(false);
            writeMegaignoreFailed = true;
            if (fa->fopen(filePath, false, true, FSLogging::logOnError))
            {
                if (fa->fwrite((const byte*)wholefile.data(), (unsigned)wholefile.size(), 0))
                {
                    writeMegaignoreFailed = false;

                    LOG_debug << "Applied .megaignore from default: " << wholefile;
                }
            }
        }

        if (writeMegaignoreFailed)
        {
            // for backups, it's ok to be backup up read-only folders.
            // for syncs, we can't sync if we can't bring changes back

            if (us.mConfig.isBackup())
            {
                LOG_debug << "As it's a Backup, continuing without .megaigore for: " << us.mConfig.mLocalPath;
            }
            else
            {
                us.mConfig.mError = COULD_NOT_CREATE_IGNORE_FILE;
                us.mConfig.mEnabled = false;
                us.mConfig.mRunState = us.mConfig.mDatabaseExists ? SyncRunState::Suspend : SyncRunState::Disable;

                us.changedConfigState(true, true);

                if (completion)
                    completion(API_EWRITE, us.mConfig.mError, backupId);

                return;
            }
        }

        // Engine-generated ignore files should be invisible.
        if (!writeMegaignoreFailed)
        {
            // Generate path to ignore file.
            auto path = us.mConfig.mLocalPath;

            path.appendWithSeparator(IGNORE_FILE_NAME, false);

            // Try and make the ignore file invisible.
            fsaccess->setFileHidden(path);
        }
    }

    us.mConfig.mError = NO_SYNC_ERROR;
    us.mConfig.mEnabled = true;
    us.mConfig.mRunState = SyncRunState::Loading;
    us.mConfig.mLegacyExclusionsIneligigble = true;

    // If we're a backup sync...
    if (us.mConfig.isBackup())
    {
        auto& config = us.mConfig;

        auto firstTime = config.mBackupState == SYNC_BACKUP_NONE;
        auto isExternal = config.isExternal();

        if (firstTime || isExternal)
        {
            // Then we must come up in mirroring mode.
            LOG_verbose << "Backup Sync " << toHandle(config.mBackupId)
                        << " starting in mirroring mode"
                        << " [firstTime = " << firstTime << ", isExternal = " << isExternal << "]";
            us.mConfig.mBackupState = SYNC_BACKUP_MIRROR;
        }
    }

    us.changedConfigState(true, true);
    mHeartBeatMonitor->updateOrRegisterSync(us);

    startSync_inThread(us, completion, logname);
    us.mNextHeartbeat->updateSPHBStatus(us);
}

bool Syncs::checkSyncRemoteLocationChange(SyncConfig& config,
                                          const bool exists,
                                          const std::string& cloudPath)
{
    assert(onSyncThread());
    if (!exists)
    {
        if (!config.mRemoteNode.isUndef())
        {
            config.mRemoteNode = NodeHandle();
        }
        return false;
    }
    if (cloudPath == config.mOriginalPathOfRemoteRootNode)
    {
        return false;
    }
    LOG_debug << "Sync root path changed!  Was: " << config.mOriginalPathOfRemoteRootNode
              << " now: " << cloudPath;
    config.mOriginalPathOfRemoteRootNode = cloudPath;
    return true;
}

void Syncs::changeSyncRemoteRoot(const handle backupId,
                                 std::shared_ptr<const Node>&& newRootNode,
                                 std::function<void(error, SyncError)>&& completionForClient)
{
    assert(!onSyncThread());

    // We need to change to the syncs thread to run the missing validations and commit the change
    queueSync(
        [this,
         backupId,
         newRootNode = std::move(newRootNode),
         completionForClientWrapped =
             wrapToRunInClientThread(std::move(completionForClient), FromAnyThread::yes)]() mutable
        {
            changeSyncRemoteRootInThread(backupId,
                                         std::move(newRootNode),
                                         std::move(completionForClientWrapped));
        },
        "changeSyncRemoteRoot");
}

void Syncs::changeSyncRemoteRootInThread(const handle backupId,
                                         std::shared_ptr<const Node>&& newRootNode,
                                         std::function<void(error, SyncError)>&& completion)
{
    assert(onSyncThread());

    lock_guard<std::recursive_mutex> guard(mSyncVecMutex);
    const auto it = std::find_if(std::begin(mSyncVec),
                                 std::end(mSyncVec),
                                 [backupId](const auto& unifSync)
                                 {
                                     return unifSync && unifSync->mConfig.mBackupId == backupId;
                                 });
    if (it == std::end(mSyncVec))
    {
        LOG_err << "There are no syncs with the given backupId";
        return completion(API_EARGS, UNKNOWN_ERROR);
    }

    const auto& unifSync = (*it);
    auto& config = unifSync->mConfig;
    if (config.isBackup())
    {
        LOG_err << "Trying to change remote root of a backup sync. Operation not supported yet";
        return completion(API_EARGS, UNKNOWN_ERROR);
    }

    const auto newRootNH = newRootNode->nodeHandle();
    if (config.mRemoteNode == newRootNH)
    {
        LOG_err << "The given node to set as new root is already the root of the sync";
        return completion(API_EEXIST, UNKNOWN_ERROR);
    }

    const auto& syncs = unifSync->syncs;
    const auto currentDbPath = config.getSyncDbPath(*syncs.fsaccess, syncs.mClient);
    const auto currentRootNH = std::exchange(config.mRemoteNode, newRootNH);

    if (!commitConfigToDb(config))
    {
        config.mRemoteNode = currentRootNH;
        LOG_err
            << "Couldn't commit the configuration into the database, cancelling remote root change";
        return completion(API_EWRITE, SYNC_CONFIG_WRITE_FAILURE);
    }

    const auto renameDbAndNotifyServer =
        [&config, &unifSync, &syncs, &currentDbPath, newRootNH, this]()
    {
        if (currentDbPath)
        {
            // Move db to the new expected location
            const auto newDbFileName =
                config.getSyncDbStateCacheName(config.mLocalPathFsid, newRootNH, syncs.mClient.me);

            std::filesystem::path newDbPath{
                syncs.mClient.dbaccess
                    ->databasePath(*syncs.fsaccess, newDbFileName, DbAccess::DB_VERSION)
                    .rawValue()};

            std::filesystem::rename(*currentDbPath, newDbPath);
        }
        mHeartBeatMonitor->updateOrRegisterSync(*unifSync);
    };

    if (const bool syncRunning = (unifSync->mSync != nullptr); syncRunning)
    {
        unifSync->suspendSync();
        renameDbAndNotifyServer();
        unifSync->resumeSync(
            [completion = std::move(completion)](error err, SyncError serr, handle)
            {
                if (err)
                    LOG_err << "Error resuming sync after remote root change: " << err
                            << ", Sync err: " << serr;
                completion(API_OK, NO_SYNC_ERROR);
            });
    }
    else
    {
        renameDbAndNotifyServer();
        completion(API_OK, NO_SYNC_ERROR);
    }
}

void Syncs::changeSyncLocalRoot(const handle backupId,
                                LocalPath&& newValidLocalRootPath,
                                std::function<void(error, SyncError)>&& completionForClient)
{
    assert(!onSyncThread());

    // We need to change to the syncs thread to run the missing validations and commit the change
    queueSync(
        [this,
         backupId,
         newPath = std::move(newValidLocalRootPath),
         completionForClientWrapped =
             wrapToRunInClientThread(std::move(completionForClient), FromAnyThread::yes)]() mutable
        {
            changeSyncLocalRootInThread(backupId,
                                        std::move(newPath),
                                        std::move(completionForClientWrapped));
        },
        "changeSyncLocalRoot");
}

void Syncs::changeSyncLocalRootInThread(const handle backupId,
                                        LocalPath&& newValidLocalRootPath,
                                        std::function<void(error, SyncError)>&& completion)
{
    assert(onSyncThread());

    lock_guard<std::recursive_mutex> guard(mSyncVecMutex);
    const auto it = std::find_if(std::begin(mSyncVec),
                                 std::end(mSyncVec),
                                 [backupId](const auto& unifSync)
                                 {
                                     return unifSync && unifSync->mConfig.mBackupId == backupId;
                                 });
    if (it == std::end(mSyncVec))
    {
        LOG_err << "There are no syncs with the given backupId";
        return completion(API_EARGS, UNKNOWN_ERROR);
    }

    const auto& unifSync = *it;
    const bool syncWasRunning = unifSync->mSync != nullptr;
    if (syncWasRunning)
        unifSync->suspendSync();

    const auto exitResumingIfNeeded =
        [syncWasRunning, completion = std::move(completion), &unifSync](error e, SyncError se)
    {
        if (!syncWasRunning)
            return completion(e, se);
        unifSync->resumeSync(
            [completion = std::move(completion), e, se](error err, SyncError serr, handle)
            {
                if (err)
                    LOG_err << "Error resuming sync after local root change: " << err
                            << ", Sync err: " << serr;
                completion(e, se);
            });
    };

    if (const auto syncErr = unifSync->changeConfigLocalRoot(newValidLocalRootPath);
        syncErr != NO_SYNC_ERROR)
    {
        const error apiErr = syncErr == SYNC_CONFIG_WRITE_FAILURE ? API_EWRITE : API_EARGS;
        return exitResumingIfNeeded(apiErr, syncErr);
    }
    mHeartBeatMonitor->updateOrRegisterSync(*unifSync);
    exitResumingIfNeeded(API_OK, NO_SYNC_ERROR);
}

void Syncs::manageRemoteRootLocationChange(Sync& sync) const
{
    // Currently, we don't support movements for backup roots
    if (sync.isBackup())
    {
        LOG_err << "Remote root node move/rename is not expected to take place for backup syncs";
        assert(false);
        sync.changestate(SyncError::BACKUP_MODIFIED, false, true, false);
        return;
    }
    // we need to check if the node in its new location is syncable
    const auto& config = sync.getConfig();
    const auto [e, syncError] = std::invoke(
        [this, &config]() -> std::pair<error, SyncError>
        {
            std::lock_guard g(mClient.nodeTreeMutex);
            return mClient.isnodesyncable(mClient.mNodeManager.getNodeByHandle(config.mRemoteNode),
                                          true);
        });
    if (e)
    {
        LOG_debug << "Node is not syncable after moving to a new location: " << syncError;
        sync.changestate(syncError, false, true, true);
    }
    else
    {
        // Notify the change in the root path
        mClient.app->syncupdate_remote_root_changed(config);
    }
}

void Syncs::startSync_inThread(UnifiedSync& us,
                               std::function<void(error, SyncError, handle)> completion,
                               const string& logname)
{
    assert(onSyncThread());
    assert(!us.mSync);

    auto fail = [&us, &completion](Error e, SyncError se) -> void {
        us.changeState(se, false, true, true);
        us.mSync.reset();
        LOG_debug << "Final error for sync start: " << e;
        if (completion) completion(e, us.mConfig.mError, us.mConfig.mBackupId);
    };

    us.mConfig.mRunState = SyncRunState::Loading;
    us.changedConfigState(false, true);

    SyncError constructResult = NO_SYNC_ERROR;
    us.mSync.reset(new Sync(us, logname, constructResult));

    if (constructResult != NO_SYNC_ERROR)
    {
        LOG_err << "Sync creation failed, syncerr: " << constructResult;
        return fail(API_EFAILED, constructResult);
    }

    debugLogHeapUsage();

    us.mSync->purgeStaleDownloads();

    // this was already set in the Sync constructor
    assert(us.mConfig.mRunState == SyncRunState::Run);

    us.changedConfigState(false, true);

    // Make sure we could open the state cache database.
    if (us.mSync->shouldHaveDatabase() && !us.mSync->statecachetable)
    {
        LOG_err << "Unable to open state cache database.";
        return fail(API_EFAILED, UNABLE_TO_OPEN_DATABASE);
    }
    else if (us.mSync->localroot->watch(us.mConfig.getLocalPath(), UNDEF) != WR_SUCCESS)
    {
        LOG_err << "Unable to add a watch for the sync root: "
                << us.mConfig.getLocalPath();

        return fail(API_EFAILED, UNABLE_TO_ADD_WATCH);
    }

    ensureDriveOpenedAndMarkDirty(us.mConfig.mExternalDrivePath);
    mSyncFlags->isInitialPass = true;

    if (completion) completion(API_OK, us.mConfig.mError, us.mConfig.mBackupId);
}

void UnifiedSync::changedConfigState(bool save, bool notifyApp)
{
    assert(syncs.onSyncThread());

    if (mConfig.stateFieldsChanged())
    {
        LOG_debug << "Sync " << toHandle(mConfig.mBackupId)
                  << " now in runState: " << int(mConfig.mRunState)
                  << " enabled: " << mConfig.mEnabled << " error: " << mConfig.mError
                  << " (isBackup: " << mConfig.isBackup() << ")";

        if (save)
        {
            syncs.ensureDriveOpenedAndMarkDirty(mConfig.mExternalDrivePath);
        }

        if (notifyApp && !mConfig.mRemovingSyncBySds)
        {
            assert(syncs.onSyncThread());
            syncs.mClient.app->syncupdate_stateconfig(mConfig);
        }
    }
}

Syncs::Syncs(MegaClient& mc):
    waiter(new WAIT_CLASS),
    mClient(mc),
    fsaccess(std::make_unique<FSACCESS_CLASS>()),
    mSyncFlags(new SyncFlags),
    mScanService(new ScanService()),
    mThrottlingManager(std::make_shared<UploadThrottlingManager>())
{
    assert(mThrottlingManager && "Throttling manager should be valid in the Syncs constructor!");

    fsaccess->initFilesystemNotificationSystem();

    mHeartBeatMonitor.reset(new BackupMonitor(*this));
    syncThread = std::thread(
        [this]()
        {
            syncLoop();
        });
}

Syncs::~Syncs()
{
    assert(!onSyncThread());

    // null function is the signal to end the thread
    syncThreadActions.pushBack(QueuedSyncFunc(nullptr, ""));
    waiter->notify();
    if (syncThread.joinable()) syncThread.join();
}

void Syncs::syncRun(std::function<void()> f, const string& actionName)
{
    assert(!onSyncThread());
    std::promise<bool> synchronous;
    syncThreadActions.pushBack(QueuedSyncFunc([&]()
        {
            f();
            synchronous.set_value(true);
        }, actionName));

    mSyncFlags->earlyRecurseExitRequested = true;
    waiter->notify();
    synchronous.get_future().get();
}

void Syncs::queueSync(std::function<void()>&& f, const string& actionName)
{
    assert(!onSyncThread());
    syncThreadActions.pushBack(QueuedSyncFunc(std::move(f), actionName));
    mSyncFlags->earlyRecurseExitRequested = true;
    waiter->notify();
}

void Syncs::queueClient(std::function<void(MegaClient&, TransferDbCommitter&)>&& f,
                        [[maybe_unused]] bool fromAnyThread)
{
    assert(onSyncThread() || fromAnyThread);
    clientThreadActions.pushBack(std::move(f));
    mClient.waiter->notify();
}

void Syncs::getSyncProblems(std::function<void(unique_ptr<SyncProblems>)> completion,
                            bool completionInClient)
{
    using MC = MegaClient;
    using DBTC = TransferDbCommitter;

    if (completionInClient)
    {
        completion = [this, completion](unique_ptr<SyncProblems> problems) {
            SyncProblems* rawPtr = problems.release();
            queueClient([completion, rawPtr](MC&, DBTC&) mutable {
                completion(unique_ptr<SyncProblems>(rawPtr));
            });
        };
    }

    queueSync(
        [this, completion]() mutable
        {
            unique_ptr<SyncProblems> problems(new SyncProblems);
            getSyncProblems_inThread(*problems);
            completion(std::move(problems));
        },
        "getSyncProblems");
}

void Syncs::getSyncProblems_inThread(SyncProblems& problems)
{
    assert(onSyncThread());

    problems.mStallsDetected = stallsDetected(problems.mStalls);
    problems.mConflictsDetected = conflictsDetected(problems.mConflictsMap);

    // Try to present just one item for a move/rename, instead of two.
    // We may have generated two items, one for the source node
    // and one for the target node.   Most paths will match between the two.
    // If for some reason we only know one side of the move/rename, we will keep that item.

    auto combinePathProblems = [](const SyncStallEntry& soEntry, SyncStallEntry& siEntry)
    {
        // We always keep the PathProblem present on any of the two stalls
        // If we have two PathProblems, one of them should be caused by an unresolved area (waiting for moves, etc), so we keep the more meaningful one (which could be solvable)
        if (soEntry.localPath1.problem != PathProblem::NoProblem &&
            (siEntry.localPath1.problem == PathProblem::NoProblem || siEntry.localPath1.problem == PathProblem::DestinationPathInUnresolvedArea))
        {
            siEntry.localPath1.problem = soEntry.localPath1.problem;
        }
        if (soEntry.localPath2.problem != PathProblem::NoProblem &&
            (siEntry.localPath2.problem == PathProblem::NoProblem || siEntry.localPath2.problem == PathProblem::DestinationPathInUnresolvedArea))
        {
            siEntry.localPath2.problem = soEntry.localPath2.problem;
        }
        if (soEntry.cloudPath1.problem != PathProblem::NoProblem &&
            (siEntry.cloudPath1.problem == PathProblem::NoProblem || siEntry.cloudPath1.problem == PathProblem::DestinationPathInUnresolvedArea))
        {
            siEntry.cloudPath1.problem = soEntry.cloudPath1.problem;
        }
        if (soEntry.cloudPath2.problem != PathProblem::NoProblem &&
            (siEntry.cloudPath2.problem == PathProblem::NoProblem || siEntry.cloudPath2.problem == PathProblem::DestinationPathInUnresolvedArea))
        {
            siEntry.cloudPath2.problem = soEntry.cloudPath2.problem;
        }
    };

    for (auto& syncStallInfoMapsPair : problems.mStalls.syncStallInfoMaps)
    {
        auto& syncStallInfoMaps = syncStallInfoMapsPair.second;
        for (auto si = syncStallInfoMaps.local.begin();
                si != syncStallInfoMaps.local.end();
                ++si)
        {
            if (si->second.reason == SyncWaitReason::MoveOrRenameCannotOccur)
            {
                auto so = syncStallInfoMaps.local.find(si->second.localPath2.localPath);
                if (so != syncStallInfoMaps.local.end())
                {
                    if (so != si &&
                        so->second.reason == SyncWaitReason::MoveOrRenameCannotOccur &&
                        so->second.localPath1.localPath == si->second.localPath1.localPath &&
                        so->second.localPath2.localPath == si->second.localPath2.localPath &&
                        so->second.cloudPath1.cloudPath == si->second.cloudPath1.cloudPath)
                    {
                        combinePathProblems(so->second, si->second);
                        if (si->second.cloudPath2.cloudPath.empty())
                        {
                            // if we know the destination in one, make sure we keep it
                            si->second.cloudPath2.cloudPath = so->second.cloudPath2.cloudPath;
                        }
                        // other iterators are not invalidated in std::map
                        syncStallInfoMaps.local.erase(so);
                    }
                }
            }
        }

        for (auto si = syncStallInfoMaps.cloud.begin();
            si != syncStallInfoMaps.cloud.end();
            ++si)
        {
            if (si->second.reason == SyncWaitReason::MoveOrRenameCannotOccur)
            {
                auto so = syncStallInfoMaps.cloud.find(si->second.cloudPath2.cloudPath);
                if (so != syncStallInfoMaps.cloud.end())
                {
                    if (so != si &&
                        so->second.reason == SyncWaitReason::MoveOrRenameCannotOccur &&
                        so->second.cloudPath1.cloudPath == si->second.cloudPath1.cloudPath &&
                        so->second.cloudPath2.cloudPath == si->second.cloudPath2.cloudPath &&
                        so->second.localPath1.localPath == si->second.localPath1.localPath)
                    {
                        combinePathProblems(so->second, si->second);
                        if (si->second.localPath2.localPath.empty())
                        {
                            // if we know the destination in one, make sure we keep it
                            si->second.localPath2.localPath = so->second.localPath2.localPath;
                        }
                        // other iterators are not invalidated in std::map
                        syncStallInfoMaps.cloud.erase(so);
                    }
                }
            }
        }
    }
}

void Syncs::getSyncStatusInfo(handle backupID,
                              SyncStatusInfoCompletion completion,
                              bool completionInClient)
{
    // No completion? No work to be done!
    if (!completion)
        return;

    // Convenience.
    using DBTC = TransferDbCommitter;
    using MC = MegaClient;
    using SV = vector<SyncStatusInfo>;

    // Is it up to the client to call the completion function?
    if (completionInClient)
        completion = [completion, this](SV info) {
            // Delegate to the user's completion function.
            queueClient([completion, info = std::move(info)](MC&, DBTC&) mutable {
                completion(std::move(info));
            });
        };

    // Queue the request on the sync thread.
    queueSync([backupID, completion, this]() {
        getSyncStatusInfoInThread(backupID, std::move(completion));
    }, "getSyncStatusInfo");
}

void Syncs::getSyncStatusInfoInThread(handle backupID,
                                      SyncStatusInfoCompletion completion)
{
    // Make sure we're running on the right thread.
    assert(onSyncThread());

    // Make sure no one's changing the syncs beneath our feet.
    lock_guard<std::recursive_mutex> guard(mSyncVecMutex);

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
            info.mTransferCounts = mSync.threadSafeState->transferCounts();

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
                info.mTotalSyncedBytes += static_cast<size_t>(node.syncedFingerprint.size);

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

SyncConfigVector Syncs::configsForDrive(const LocalPath& drive) const
{
    assert(onSyncThread() || !onSyncThread());

    lock_guard<std::recursive_mutex> guard(mSyncVecMutex);

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

SyncController::SyncController() = default;

SyncController::~SyncController() = default;

bool SyncController::deferPutnode(const LocalPath&) const
{
    return true;
}

bool SyncController::deferPutnodeCompletion(const LocalPath&) const
{
    return true;
}

bool SyncController::deferUpload(const LocalPath&) const
{
    return true;
}

void Syncs::injectSyncSensitiveData(SyncSensitiveData data)
{
    syncRun([data = std::move(data), this]() {
        // Nothing should've been injected yet.
        assert(SymmCipher::isZeroKey(syncKey.key, sizeof(syncKey.key)));
        assert(!mSyncConfigIOContext);
        assert(!mSyncConfigStore);

        // Inject state cache key (sync key.)
        syncKey.setkey(reinterpret_cast<const byte*>(data.stateCacheKey.data()));

        // Construct IO context.
        mSyncConfigIOContext.reset(
          new SyncConfigIOContext(*fsaccess,
                                  data.jscData.authenticationKey,
                                  data.jscData.cipherKey,
                                  data.jscData.fileName,
                                  rng));
    }, __func__);
}

SyncConfigVector Syncs::getConfigs(bool onlyActive) const
{
    assert(onSyncThread() || !onSyncThread());

    lock_guard<std::recursive_mutex> guard(mSyncVecMutex);

    SyncConfigVector v;
    for (auto& s : mSyncVec)
    {
        if (s->mSync
            || !onlyActive)
        {
            v.push_back(s->mConfig);
        }
    }
    return v;
}

handle Syncs::getSyncIdContainingActivePath(const LocalPath& lp) const
{
    assert(onSyncThread() || !onSyncThread());

    lock_guard<std::recursive_mutex> guard(mSyncVecMutex);

    SyncConfigVector v;
    for (auto& s : mSyncVec)
    {
        if (s->mSync)
        {
            if (s->mConfig.mLocalPath.isContainingPathOf(lp))
            {
                auto debrisPath = s->mConfig.mLocalPath;
                debrisPath.appendWithSeparator(LocalPath::fromRelativePath(DEBRISFOLDER), false);
                if (debrisPath.isContainingPathOf(lp))
                {
                    return UNDEF;
                }
                else
                {
                    return s->mConfig.mBackupId;
                }
            }
        }
    }
    return UNDEF;
}

bool Syncs::configById(handle backupId, SyncConfig& configResult) const
{
    assert(!onSyncThread());

    lock_guard<std::recursive_mutex> guard(mSyncVecMutex);

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

void Syncs::backupCloseDrive(const LocalPath& drivePath, std::function<void(Error)> clientCallback)
{
    assert(!onSyncThread());
    assert(clientCallback);

    queueSync(
        [this, drivePath, clientCallback]()
        {
            Error e = backupCloseDrive_inThread(drivePath);
            queueClient(
                [clientCallback, e](MegaClient&, TransferDbCommitter&)
                {
                    clientCallback(e);
                });
        },
        "backupCloseDrive");
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

    auto syncsOnDrive = selectedSyncConfigs(
      [&](SyncConfig& config, Sync*)
      {
          return config.mExternalDrivePath == drivePath;
      });

    for (auto& sc : syncsOnDrive)
    {
        SyncConfig removed;
        unloadSyncByBackupID(sc.mBackupId, sc.mEnabled, removed);
    }

    return result;
}

void Syncs::backupOpenDrive(const LocalPath& drivePath, std::function<void(Error)> clientCallback)
{
    assert(!onSyncThread());
    assert(clientCallback);

    queueSync(
        [this, drivePath, clientCallback]()
        {
            Error e = backupOpenDrive_inThread(drivePath);
            queueClient(
                [clientCallback, e](MegaClient&, TransferDbCommitter&)
                {
                    clientCallback(e);
                });
        },
        "backupOpenDrive");
}

error Syncs::backupOpenDrive_inThread(const LocalPath& drivePath)
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
            lock_guard<std::recursive_mutex> guard(mSyncVecMutex);

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
                // Create the unified sync.
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
             << " as we couldn't open its config database: " << drivePath;

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

        lock_guard<std::recursive_mutex> guard(mSyncVecMutex);
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

    }, "getSyncedNodeForLocalPath");
    return result;
}

treestate_t Syncs::getSyncStateForLocalPath(handle backupId, const LocalPath& lp)
{
    assert(!onSyncThread());

    // mLocalNodeChangeMutex must already be locked!!
    // we never have mSyncVecMutex and then lock mLocalNodeChangeMutex
    lock_guard<std::recursive_mutex> guard(mSyncVecMutex);
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

bool Syncs::getSyncStateForLocalPath(const LocalPath& lp, treestate_t& ts, nodetype_t& nt, SyncConfig& sc)
{
    assert(onSyncThread());
    for (auto& us : mSyncVec)
    {
        if (us->mSync && us->mConfig.mLocalPath.isContainingPathOf(lp))
        {
            if (LocalNode* match = us->mSync->localnodebypath(nullptr, lp, nullptr, nullptr, true))
            {
                ts = match->checkTreestate(false);
                nt = match->type;
                sc = us->mConfig;
                return true;
            }
            return false;
        }
    }
    return false;
}

error Syncs::syncConfigStoreAdd(const SyncConfig& config)
{
    assert(!onSyncThread());

    error result = API_OK;
    syncRun([&](){ syncConfigStoreAdd_inThread(config, [&](error e){ result = e; }); }, "syncConfigStoreAdd");
    return result;
}

void Syncs::moveToSyncDebrisByBackupID(const string& path, handle backupId, std::function<void (Error)> completion, std::function<void (Error)> completionInClient)
{
    auto moveToDebris = [this, path, backupId, completion, completionInClient]()
    {
        assert(onSyncThread());

        lock_guard<std::recursive_mutex> guard(mSyncVecMutex);
        Sync* sync = nullptr;
        error e = API_ENOENT;
        for (auto& s : mSyncVec)
        {
            if (s->mSync && s->mConfig.mBackupId == backupId)
            {
                sync = s->mSync.get();
            }
        }

        if (sync)
        {
            e = sync->movetolocaldebris(LocalPath::fromAbsolutePath(path)) ? API_OK : API_EINTERNAL;
        }

        if (completion)
        {
            completion(e);
        }

        if (completionInClient)
        {
            queueClient([completionInClient, e](MegaClient& , TransferDbCommitter&)
            {
                completionInClient(e);
            });
        }
    };

    if (onSyncThread())
    {
        moveToDebris();
    }
    else
    {
        queueSync([moveToDebris]()
        {
            moveToDebris();
        }, "Move to node to derbis");
    }
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
                      << i->mLocalPath;

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

    auto failed = mSyncConfigStore->writeDirtyDrives(getConfigs(false));

    if (failed.empty()) return true;

    LOG_err << "Failed to flush "
             << failed.size()
             << " drive(s).";

    // Disable syncs present on drives that we couldn't write.
    auto nDisabled = 0u;

    for (auto& drivePath : failed)
    {
        // Determine which syncs are present on this drive.
        auto configs = configsForDrive(drivePath);

        // Disable those that aren't already disabled.
        for (auto& config : configs)
        {
            // Already disabled? Nothing to do.
            //
            // dgw: This is what prevents an infinite flush cycle.
            if (!config.mEnabled)
                continue;

            // Disable the sync.
            disableSyncByBackupId(config.mBackupId,
                                  SYNC_CONFIG_WRITE_FAILURE,
                                  false,
                                  true,
                                  nullptr);

            ++nDisabled;
        }
    }

    LOG_warn << "Disabled"
             << nDisabled
             << " sync(s) on "
             << failed.size()
             << " drive(s).";

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
            // note that the sync root path might not be available now, and we might start the
            // sync later if the drive appears (and its suspension reason was that path disappearing)
            for (auto& c: configs)
            {
                handle root_fsid = c.mLocalPathFsid;
                if (root_fsid == UNDEF)
                {
                    // backward compatibilty for when we didn't store the fsid in serialized config
                    auto fas = fsaccess->newfileaccess(false);
                    if (fas->fopen(c.mLocalPath, true, false, FSLogging::logOnError))
                    {
                        root_fsid = fas->fsid;
                    }
                }

                if (root_fsid != UNDEF)
                {
                    string dbname = c.getSyncDbStateCacheName(root_fsid, c.mRemoteNode, mClient.me);

                    // Note, we opened dbaccess in thread-safe mode

                    // If the user is upgrading from NO SRW to SRW, we rename the DB files to the new SRW version.
                    // However, if there are db files from a previous SRW version (i.e., the user downgraded from SRW to NO SRW and then upgraded again to SRW)
                    // we need to remove the SRW db files. The flag DB_OPEN_FLAG_RECYCLE is used for this purpose.
                    int dbFlags = DB_OPEN_FLAG_TRANSACTED; // Unused
                    if (DbAccess::LEGACY_DB_VERSION == DbAccess::LAST_DB_VERSION_WITHOUT_SRW)
                    {
                        dbFlags |= DB_OPEN_FLAG_RECYCLE;
                    }
                    LocalPath dbPath;
                    c.mDatabaseExists = mClient.dbaccess->checkDbFileAndAdjustLegacy(*fsaccess, dbname, dbFlags, dbPath);
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
            auto completion = bind(&putComplete, std::move(context), _1, _2);

            // Create and initiate request.
            auto* request = new CommandBackupPut(&client, info, std::move(completion));
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
                    auto* request = new CommandBackupRemove(&client, i->mBackupId, nullptr);
                    client.reqs.add(request);
                    // don't wait for the cleanup to notify the client about failure of import
                    // (error/success of cleanup is irrelevant for the app)
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
                    // So we can wait for the sync to be added.
                    std::promise<void> waiter;

                    // Called when the engine has added the sync.
                    auto completion = [&waiter](error, SyncError, handle) {
                        waiter.set_value();
                    };

                    // Add the new sync, optionally enabling it.
                    syncs.appendNewSync(config,
                                        false,
                                        std::move(completion),
                                        false,
                                        config.mName);

                    // Wait for this sync to be added.
                    waiter.get_future().get();
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
        lock_guard<std::recursive_mutex> guard(mSyncVecMutex);

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
    ContextPtr context = std::make_unique<Context>();

    context->mClient = &mClient;
    context->mCompletion = std::move(completion);
    context->mConfigs = std::move(configs);
    context->mConfig = context->mConfigs.begin();
    context->mDeviceHash = mClient.getDeviceidHash();
    context->mSyncs = this;

    if (context->mDeviceHash.empty())
    {
        LOG_err << "Failed to get Device ID while importing sync configs";
        completion(API_EARGS);
        return;
    }

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
                 << config.mLocalPath;
        return;
    }

    string localPath = config.mLocalPath.toPath(false);
    string remotePath;
    const string& name = config.mName;
    const char* type = SyncConfig::synctypename(config.mSyncType);

    if (const auto node = mClient.nodeByHandle(config.mRemoteNode))
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
    config.mFilesystemFingerprint.reset();
    config.mLocalPath = LocalPath::fromAbsolutePath(localPath);
    config.mName = std::move(name);
    config.mOriginalPathOfRemoteRootNode = remotePath;
    config.mWarning = NO_SYNC_WARNING;

    // Set node handle if possible.
    if (const auto root = mClient.nodeByPath(remotePath.c_str()))
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

    return mSyncConfigIOContext.get();
}

template<typename... Arguments, typename... Parameters>
bool Syncs::defer(bool (SyncController::*predicate)(Parameters...) const,
                  Arguments&&... arguments) const
{
    // Consult controller if available.
    if (auto controller = syncController())
        return (*controller.*predicate)(std::forward<Arguments>(arguments)...);

    // Otherwise assume we shouldn't defer any activity.
    return false;
}

bool Syncs::hasImmediateStall(const SyncStallInfo& stalls) const
{
    std::lock_guard<std::mutex> guard(mImmediateStallLock);

    if (mHasImmediateStall)
        return mHasImmediateStall(stalls);

    return stalls.hasImmediateStallReason();
}

bool Syncs::isImmediateStall(const SyncStallEntry& entry) const
{
    std::lock_guard<std::mutex> guard(mImmediateStallLock);

    if (mIsImmediateStall)
        return mIsImmediateStall(entry);

    return entry.alertUserImmediately;
}

bool Syncs::deferPutnode(const LocalPath& path) const
{
    return defer(&SyncController::deferPutnode, path);
}

bool Syncs::deferPutnodeCompletion(const LocalPath& path) const
{
    return defer(&SyncController::deferPutnodeCompletion, path);
}

bool Syncs::hasSyncController() const
{
    std::lock_guard<std::mutex> guard(mSyncControllerLock);

    // Reference to controller has grown stale.
    if (mSyncController.expired())
        return mSyncController.reset(), false;

    // Reference to controller is still live.
    return true;
}

bool Syncs::deferUpload(const LocalPath& path) const
{
    return defer(&SyncController::deferUpload, path);
}

void Syncs::setHasImmediateStall(HasImmediateStallPredicate predicate)
{
    std::lock_guard<std::mutex> guard(mImmediateStallLock);

    mHasImmediateStall = std::move(predicate);
}

void Syncs::setIsImmediateStall(IsImmediateStallPredicate predicate)
{
    std::lock_guard<std::mutex> guard(mImmediateStallLock);

    mIsImmediateStall = std::move(predicate);
}

void Syncs::setSyncController(SyncControllerPtr controller)
{
    std::lock_guard<std::mutex> guard(mSyncControllerLock);

    mSyncController = controller;
}

SyncControllerPtr Syncs::syncController() const
{
    std::lock_guard<std::mutex> guard(mSyncControllerLock);

    // Return controller if it's still alive.
    if (auto controller = mSyncController.lock())
        return controller;

    // Clear controller if it's stale.
    mSyncController.reset();

    return nullptr;
}

bool Syncs::isSyncStalled(handle backupId) const
{
    assert(onSyncThread());

    return syncStallState && stallReport.isSyncStalled(backupId);
}

LocalNode* Syncs::findMoveFromLocalNode(const shared_ptr<LocalNode::RareFields::MoveInProgress>& moveTo)
{
    assert(onSyncThread());

    // There should never be many moves in progress so we can iterate the entire thing
    // Pointers are safe to access because destruction of these LocalNodes removes them from the set
    for (auto ln : mMoveInvolvedLocalNodes)
    {
        assert(ln->rareRO().moveFromHere || ln->rareRO().moveToHere);
        if (auto& moveFrom = ln->rareRO().moveFromHere)
        {
            if (moveFrom == moveTo)
            {
                return ln;
            }
        }
    }
    return nullptr;
}

void Syncs::clear_inThread(bool reopenStoreAfter)
{
    assert(onSyncThread());

    assert(!mSyncConfigStore);

    mSyncConfigStore.reset();
    {
        lock_guard<std::recursive_mutex> guard(mSyncVecMutex);
        mSyncVec.clear();
    }
    mNumSyncsActive = 0;
    if (!reopenStoreAfter)
    {
        mSyncConfigIOContext.reset();
        syncKey.setkey(SymmCipher::zeroiv);
    }
    stallReport = SyncStallInfo();
    triggerHandles.clear();
    localnodeByScannedFsid.clear();
    localnodeBySyncedFsid.clear();
    localnodeByNodeHandle.clear();
    mSyncFlags.reset(new SyncFlags);
    mHeartBeatMonitor.reset(new BackupMonitor(*this));
    mFileChangingCheckState.clear();
    mMoveInvolvedLocalNodes.clear();

    if (syncscanstate)
    {
        assert(onSyncThread());
        syncscanstate = false;
        mClient.app->syncupdate_scanning(false);
    }

    if (syncBusyState)
    {
        assert(onSyncThread());
        syncBusyState = false;
        mClient.app->syncupdate_syncing(false);
    }

    syncStallState = false;
    syncConflictState = false;
    totalSyncStalls.store(0);
    totalSyncConflicts.store(0);

    totalLocalNodes = 0;

    mSyncsLoaded = false;
    mSyncsResumed = false;
}

void Syncs::appendNewSync(const SyncConfig& c, bool startSync, std::function<void(error, SyncError, handle)> completion, bool completionInClient, const string& logname, const string& excludedPath)
{
    assert(!onSyncThread());
    assert(c.mBackupId != UNDEF);

    auto clientCompletion = [this, completion](error e, SyncError se, handle backupId)
    {
        queueClient(
            [e, se, backupId, completion](MegaClient&, TransferDbCommitter&)
            {
                if (completion)
                    completion(e, se, backupId);
            });
    };

    queueSync([=]()
    {
        appendNewSync_inThread(c, startSync, completionInClient ? clientCompletion : completion, logname, excludedPath);
    }, "appendNewSync");
}

void Syncs::appendNewSync_inThread(const SyncConfig& c, bool startSync, std::function<void(error, SyncError, handle)> completion, const string& logname, const string& excludedPath)
{
    assert(onSyncThread());

    // Get our hands on the sync config store.
    auto* store = syncConfigStore();

    // Can we get our hands on the config store?
    if (!store)
    {
        LOG_err << "Unable to add backup "
            << c.mLocalPath
            << " on "
            << c.mExternalDrivePath
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
                    << c.mLocalPath
                    << " on "
                    << c.mExternalDrivePath
                    << " as we could not read its config database.";

            if (completion)
                completion(API_EFAILED, c.mError, c.mBackupId);

            return;
        }
    }

    {
        lock_guard<std::recursive_mutex> guard(mSyncVecMutex);
        mSyncVec.push_back(unique_ptr<UnifiedSync>(new UnifiedSync(*this, c)));
    }

    ensureDriveOpenedAndMarkDirty(c.mExternalDrivePath);

    mClient.app->sync_added(c);

    if (!startSync)
    {
        if (completion) completion(API_OK, c.mError, c.mBackupId);
        return;
    }

    enableSyncByBackupId_inThread(c.mBackupId, true, completion, logname, excludedPath);
}

Sync* Syncs::runningSyncByBackupIdForTests(handle backupId) const
{
    assert(!onSyncThread());
    // returning a Sync* is not really thread safe but the tests are using these directly currently.  So long as they only browse the Sync while nothing changes, it should be ok

    lock_guard<std::recursive_mutex> guard(mSyncVecMutex);
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

    lock_guard<std::recursive_mutex> guard(mSyncVecMutex);
    for (auto& s : mSyncVec)
    {
        if (s->mConfig.mBackupId == backupId)
            return c = s->mConfig, true;
    }

    return false;
}

void Syncs::transferPauseFlagsUpdated(bool downloadsPaused, bool uploadsPaused)
{
    assert(!onSyncThread());

    bool unchanged = mDownloadsPaused == downloadsPaused &&
                        mUploadsPaused == uploadsPaused;

    mDownloadsPaused = downloadsPaused;
    mUploadsPaused = uploadsPaused;
    mTransferPauseFlagsChanged = mTransferPauseFlagsChanged || !unchanged;
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
    syncRun([&](){ purgeRunningSyncs_inThread(); }, "purgeRunningSyncs");
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
        queueClient(
            [completion, e](MegaClient&, TransferDbCommitter&)
            {
                completion(e);
            });
    };
    queueSync([this, backupId, newname, clientCompletion]()
        {
            renameSync_inThread(backupId, newname, clientCompletion);
        }, "renameSync");
}

void Syncs::renameSync_inThread(handle backupId, const string& newname, std::function<void(Error e)> completion)
{
    assert(onSyncThread());

    lock_guard<std::recursive_mutex> guard(mSyncVecMutex);

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
            SyncConfigVector v = getConfigs(false);

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
        }, "disableSyncs");
}

void Syncs::disableSyncByBackupId(handle backupId, SyncError syncError, bool newEnabledFlag, bool keepSyncDb, std::function<void()> completion)
{
    assert(!onSyncThread());
    queueSync([this, backupId, syncError, newEnabledFlag, keepSyncDb, completion]()
    {
            disableSyncByBackupId_inThread(backupId, syncError, newEnabledFlag, keepSyncDb, completion);
    }, "disableSyncByBackupId");
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

            // if we are logging out, we don't need to bother the user about
            // syncs stopping, the user expects everything to stop
            bool notifyApp = !mClient.loggingout;

            us.changeState(syncError, newEnabledFlag, notifyApp, keepSyncDb); //This will cause the later deletion of Sync (not MegaSyncPrivate) object

            mHeartBeatMonitor->updateOrRegisterSync(us);
        }
    }
    if (completion) completion();
}

SyncConfigVector Syncs::selectedSyncConfigs(std::function<bool(SyncConfig&, Sync*)> selector) const
{
    SyncConfigVector selected;

    lock_guard<std::recursive_mutex> guard(mSyncVecMutex);

    for (size_t i = 0; i < mSyncVec.size(); ++i)
    {
        if (selector(mSyncVec[i]->mConfig, mSyncVec[i]->mSync.get()))
        {
            selected.emplace_back(mSyncVec[i]->mConfig);
        }
    }

    return selected;
}

std::function<void(MegaClient&, TransferDbCommitter&)>
    Syncs::prepareSdsCleanupForBackup(UnifiedSync& us, const vector<pair<handle, int>>& sds)
{
    us.sdsUpdateInProgress.reset(new bool(true));

    LOG_debug << "SDS: preparing sds command attribute for sync/backup "
              << toHandle(us.mConfig.mBackupId) << " on node " << us.mConfig.mRemoteNode
              << " for execution after update requested by sds";

    return [remoteNode = us.mConfig.mRemoteNode,
            backupId = us.mConfig.mBackupId,
            remainingSds = sds,
            boolsptr = us.sdsUpdateInProgress](MegaClient& mc, TransferDbCommitter&) mutable
    {
        if (std::shared_ptr<Node> node = mc.nodeByHandle(remoteNode))
        {
            remainingSds.erase(std::remove_if(remainingSds.begin(),
                                              remainingSds.end(),
                                              [&backupId](const pair<handle, int>& sdsRequest)
                                              {
                                                  return sdsRequest.first == backupId;
                                              }));

            mc.setattr(
                node,
                attr_map(Node::sdsId(), Node::toSdsString(remainingSds)),
                [boolsptr](NodeHandle handle, Error result)
                {
                    LOG_debug << "SDS: Attribute updated on " << handle << " result: " << result;
                    *boolsptr = false;
                },
                true);
        }
    };
}

bool Syncs::processPauseResumeSyncBySds(UnifiedSync& us, vector<pair<handle, int>>& sdsBackups)
{
    assert(onSyncThread());

    if (us.sdsUpdateInProgress && *us.sdsUpdateInProgress)
    {
        return false;
    }

    // find the last SDS request to Pause or Resume current sync
    auto sdsPauseResumeRequest = std::find_if(
        sdsBackups.rbegin(),
        sdsBackups.rend(),
        [id = us.mConfig.mBackupId](const pair<handle, int>& v)
        {
            return v.first == id && (v.second == CommandBackupPut::ACTIVE ||
                                     v.second == CommandBackupPut::TEMPORARY_DISABLED);
        });

    if (sdsPauseResumeRequest == sdsBackups.rend())
    {
        return false;
    }

    auto clientRemoveSdsEntryFunction = prepareSdsCleanupForBackup(us, sdsBackups);

    if ((sdsPauseResumeRequest->second == CommandBackupPut::ACTIVE && us.mConfig.mRunState == SyncRunState::Run) ||
        (sdsPauseResumeRequest->second == CommandBackupPut::TEMPORARY_DISABLED && us.mConfig.mRunState > SyncRunState::Run))
    {
        // no need to change state, just do the clean-up
        queueClient(std::move(clientRemoveSdsEntryFunction));
    }

    else if (sdsPauseResumeRequest->second == CommandBackupPut::ACTIVE)
    {
        // switch to Active
        enableSyncByBackupId_inThread(
            us.mConfig.mBackupId,
            true,
            [clientRemoveSdsEntryFunction, this](error err, SyncError serr, handle h) mutable
            {
                if (err)
                {
                    LOG_err << "Failed to set sync/backup " << h << " as Active. Error " << err
                            << ". SyncError: " << serr;
                }
                else
                {
                    LOG_info << "Set sync/backup " << h << " as Active.";
                }
                queueClient(std::move(clientRemoveSdsEntryFunction)); // do the cleanup
            },
            "");
    }

    else if (sdsPauseResumeRequest->second == CommandBackupPut::TEMPORARY_DISABLED)
    {
        // switch to Pause
        disableSyncByBackupId_inThread(
            us.mConfig.mBackupId,
            NO_SYNC_ERROR,
            false,
            true,
            [h = us.mConfig.mBackupId, clientRemoveSdsEntryFunction, this]() mutable
            {
                LOG_info << "Set sync/backup " << h << " as Paused.";
                queueClient(std::move(clientRemoveSdsEntryFunction)); // do the cleanup
            });
    }

    return true; // confirm the state update request for the given sync
}

// process backup is removed by SDS if there is any
//
// case 1: Backup is moved from backup center to the cloud node
//    we'll receive sds delete and the backup root node is moved in action packets
// case 2: Backup is deleted from backup center
//    we'll receive sds delete and the backup root node is deleted in action packets
//    The node deleteion could appear first or after sds delete due to async
// @return true if removing sync by sds
bool Syncs::processRemovingSyncBySds(UnifiedSync& us, bool foundRootNode, vector<pair<handle, int>>& sdsBackups)
{
    assert(onSyncThread());

    // prevent the reentry due to aync nature
    if (us.mConfig.mRemovingSyncBySds)
    {
        return true;
    }

    if (!foundRootNode && us.mConfig.isBackup())
    {
        LOG_debug << "Backup root node no longer exists " << toHandle(us.mConfig.mBackupId);
        deregisterThenRemoveSyncBySds(us, nullptr);
        return true;
    }

    // find any SDS request to Delete current sync
    if ((!us.sdsUpdateInProgress || !*us.sdsUpdateInProgress) &&
        std::find(sdsBackups.begin(),
                  sdsBackups.end(),
                  pair{us.mConfig.mBackupId, static_cast<int>(CommandBackupPut::DELETED)}) !=
            sdsBackups.end())
    {
        LOG_debug << "SDS: command received to stop sync " << toHandle(us.mConfig.mBackupId);

        auto clientRemoveSdsEntryFunction = prepareSdsCleanupForBackup(us, sdsBackups);

        deregisterThenRemoveSyncBySds(us, clientRemoveSdsEntryFunction);
        return true;
    }

    return false;
}

void Syncs::deregisterThenRemoveSyncBySds(UnifiedSync& us, std::function<void(MegaClient&, TransferDbCommitter&)> clientRemoveSdsEntryFunction)
{
    assert(onSyncThread());

    us.mConfig.mRemovingSyncBySds = true;
    if (Sync* sync = us.mSync.get())
    {
        // prevent the sync doing anything more before we delete it
        sync->changestate(NO_SYNC_ERROR, false, false, false);
    }
    auto backupId = us.mConfig.mBackupId;
    queueClient(
        [backupId, clientRemoveSdsEntryFunction](MegaClient& mc, TransferDbCommitter&)
        {
            mc.syncs.deregisterThenRemoveSync(backupId, nullptr, clientRemoveSdsEntryFunction);
        });
}

void Syncs::deregisterThenRemoveSyncById(handle backupId, std::function<void(Error)>&& completion)
{
    assert(!onSyncThread());
    queueSync(
        [this, backupId]()
        {
            lock_guard<std::recursive_mutex> guard(mSyncVecMutex);
            if (const auto it = std::find_if(std::begin(mSyncVec),
                                             std::end(mSyncVec),
                                             [backupId](const auto& us)
                                             {
                                                 return us && us->mConfig.mBackupId == backupId;
                                             });
                it != std::end(mSyncVec))
                (*it)->changeState(NO_SYNC_ERROR, false, false, false);
        },
        "removeSyncFromDb");
    deregisterThenRemoveSync(backupId, std::move(completion), {});
}

void Syncs::deregisterThenRemoveSync(handle backupId, std::function<void(Error)> completion, std::function<void(MegaClient&, TransferDbCommitter&)> clientRemoveSdsEntryFunction)
{
    assert(!onSyncThread());

    // Try and deregister this sync's backup ID first.
    // If later removal operations fail, the heartbeat record will be resurrected

    LOG_debug << "Deregistering backup ID: " << toHandle(backupId);

    {
        // since we are only setting flags, we can actually do this off-thread
        // (but using mSyncVecMutex and hidden inside Syncs class)
        lock_guard<std::recursive_mutex> guard(mSyncVecMutex);
        for (size_t i = 0; i < mSyncVec.size(); ++i)
        {
            auto& config = mSyncVec[i]->mConfig;
            if (config.mBackupId == backupId)
            {
                // prevent any sp or sphb messages being queued after
                config.mSyncDeregisterSent = true;
            }
        }
    }

    // use queueClient since we are not certain to be locked on client thread
    queueClient([backupId, completion, this, clientRemoveSdsEntryFunction](MegaClient& mc, TransferDbCommitter&){

        mc.reqs.add(new CommandBackupRemove(&mc, backupId,
                [backupId, completion, this, clientRemoveSdsEntryFunction](Error e){
                    if (e)
                    {
                        // de-registering is not critical - we continue anyway
                        LOG_warn << "API error deregisterig sync " << toHandle(backupId) << ":" << e;
                    }

                    queueSync(
                        [=]()
                        {
                            removeSyncAfterDeregistration_inThread(backupId,
                                                                   std::move(completion),
                                                                   clientRemoveSdsEntryFunction);
                        },
                        "deregisterThenRemoveSync");
                }));
    }, true);

}

void Syncs::removeSyncAfterDeregistration_inThread(handle backupId, std::function<void(Error)> clientCompletion, std::function<void(MegaClient&, TransferDbCommitter&)> clientRemoveSdsEntryFunction)
{
    assert(onSyncThread());

    Error e = API_OK;
    SyncConfig configCopy;
    if (unloadSyncByBackupID(backupId, false, configCopy))
    {
        mClient.app->sync_removed(configCopy);
        mSyncConfigStore->markDriveDirty(configCopy.mExternalDrivePath);

        // lastly, send the command to remove the sds entry from the (former) sync root Node's attributes
        if (clientRemoveSdsEntryFunction)
        {
            queueClient(std::move(clientRemoveSdsEntryFunction));
        }
    }
    else
    {
        e = API_EEXIST;
    }

    if (clientCompletion)
    {
        // this case for if we didn't need to deregister anything
        queueClient([clientCompletion, e](MegaClient&, TransferDbCommitter&){ clientCompletion(e); });
    }
}

bool Syncs::unloadSyncByBackupID(handle id, bool newEnabledFlag, SyncConfig& configCopy)
{
    assert(onSyncThread());
    LOG_debug << "Unloading sync: " << toHandle(id);

    for (auto i = mSyncVec.size(); i--; )
    {
        if (mSyncVec[i]->mConfig.mBackupId == id)
        {
            if (auto& syncPtr = mSyncVec[i]->mSync)
            {
                // if it was running, the app gets a callback saying it's no longer active
                // SYNC_CANCELED is a special value that means we are shutting it down without changing config
                mSyncVec[i]->changeState(UNLOADING_SYNC, newEnabledFlag, false, true);
                assert(!syncPtr->statecachetable);
                syncPtr.reset(); // deletes sync
            }

            configCopy = mSyncVec[i]->mConfig;

            // the sync config is not affected by this operation; it should already be up to date on disk (or be pending)
            // we don't call sync_removed back since the sync is not deleted
            // we don't unregister from the backup/sync heartbeats as the sync can be resumed later

            lock_guard<std::recursive_mutex> guard(mSyncVecMutex);
            mSyncVec.erase(mSyncVec.begin() + static_cast<long>(i));
            return true;
        }
    }

    return false;
}

void Syncs::prepareForLogout(bool keepSyncsConfigFile, std::function<void()> clientCompletion)
{
    queueSync([=](){ prepareForLogout_inThread(keepSyncsConfigFile, clientCompletion); }, "prepareForLogout");
}

void Syncs::prepareForLogout_inThread(bool keepSyncsConfigFile, std::function<void()> clientCompletion)
{
    assert(onSyncThread());

    if (keepSyncsConfigFile)
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
    else // if logging out and syncs won't be kept...
    {
        // regardless of that, we de-register all syncs/backups in Backup Centre
        for (auto& us : mSyncVec)
        {
            std::function<void()> onFinalDeregister = nullptr;
            if (us.get() == mSyncVec.back().get())
            {
                // this is the last one, so we'll arrange clientCompletion
                // to run after it completes.  Earlier de-registers must finish first
                onFinalDeregister = std::move(clientCompletion);
                clientCompletion = nullptr;
            }

            us->mConfig.mSyncDeregisterSent = true;
            auto backupId = us->mConfig.mBackupId;
            queueClient(
                [backupId, onFinalDeregister](MegaClient& mc, TransferDbCommitter&)
                {
                    mc.reqs.add(new CommandBackupRemove(&mc,
                                                        backupId,
                                                        [onFinalDeregister](Error)
                                                        {
                                                            if (onFinalDeregister)
                                                                onFinalDeregister();
                                                        }));
                });
        }
    }

    if (clientCompletion)
    {
        // this case for if we didn't need to deregister anything
        queueClient([clientCompletion](MegaClient&, TransferDbCommitter&){ clientCompletion(); });
    }
}


void Syncs::locallogout(bool removecaches, bool keepSyncsConfigFile, bool reopenStoreAfter)
{
    assert(!onSyncThread());
    syncRun([=](){ locallogout_inThread(removecaches, keepSyncsConfigFile, reopenStoreAfter); }, "locallogout");
}

void Syncs::locallogout_inThread(bool removecaches, bool keepSyncsConfigFile, bool reopenStoreAfter)
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
    for (auto& sc : getConfigs(false))
    {
        SyncConfig removed;
        unloadSyncByBackupID(sc.mBackupId, false, removed);
    }
    assert(mSyncVec.empty());

    // make sure we didn't resurrect the store, singleton style
    assert(!mSyncConfigStore);

    clear_inThread(reopenStoreAfter);
    mExecutingLocallogout = false;

    if (reopenStoreAfter)
    {
        SyncConfigVector configs;
        syncConfigStoreLoad(configs);
    }
}

bool Syncs::commitConfigToDb(const SyncConfig& config)
{
    assert(onSyncThread());
    ensureDriveOpenedAndMarkDirty(config.mExternalDrivePath);
    return syncConfigStoreFlush();
}

void Syncs::ensureDriveOpenedAndMarkDirty(const LocalPath& externalDrivePath)
{
    assert(onSyncThread());

    if (auto* store = syncConfigStore())
    {
        // If the app hasn't opened this drive itself, then we open it now (loads any syncs that
        // already exist there)
        if (!externalDrivePath.empty() && !store->driveKnown(externalDrivePath))
        {
            backupOpenDrive_inThread(externalDrivePath);
        }
        store->markDriveDirty(externalDrivePath);
    }
}

void Syncs::loadSyncConfigsOnFetchnodesComplete(bool resetSyncConfigStore)
{
    assert(!onSyncThread());

    if (mSyncsLoaded) return;
    mSyncsLoaded = true;

    queueSync([this, resetSyncConfigStore]()
        {
            loadSyncConfigsOnFetchnodesComplete_inThread(resetSyncConfigStore);
        }, "loadSyncConfigsOnFetchnodesComplete");
}

void Syncs::resumeSyncsOnStateCurrent()
{
    assert(!onSyncThread());

    // Double check the client only calls us once (per session) for this
    assert(!mSyncsResumed);
    if (mSyncsResumed) return;
    mSyncsResumed = true;

    queueSync([this]()
        {
            resumeSyncsOnStateCurrent_inThread();
        }, "resumeSyncsOnStateCurrent");
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

    if (error e = syncConfigStoreLoad(configs))
    {
        LOG_warn << "syncConfigStoreLoad failed: " << e;
        mClient.app->syncs_restored(SYNC_CONFIG_READ_FAILURE);
        return;
    }

    // There should be no syncs yet.
    assert(mSyncVec.empty());

    {
        lock_guard<std::recursive_mutex> guard(mSyncVecMutex);
        for (auto& config : configs)
        {
            mSyncVec.push_back(unique_ptr<UnifiedSync>(new UnifiedSync(*this, config)));
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
                if (lookupCloudNode(unifiedSync->mConfig.mRemoteNode, cloudNode, &cloudNodePath, nullptr, nullptr, nullptr, nullptr, Syncs::FOLDER_ONLY))
                {
                    unifiedSync->mConfig.mOriginalPathOfRemoteRootNode = cloudNodePath;
                    ensureDriveOpenedAndMarkDirty(unifiedSync->mConfig.mExternalDrivePath);
                }
            }

            if (unifiedSync->mConfig.getEnabled())
            {

#ifdef __APPLE__
                unifiedSync->mConfig.mFilesystemFingerprint.reset(); //for certain MacOS, fsfp seems to vary when restarting. we set it to 0, so that it gets recalculated
#endif
                LOG_debug << "Resuming cached sync: " << toHandle(unifiedSync->mConfig.mBackupId) << " " << unifiedSync->mConfig.getLocalPath() << " fsfp= " << unifiedSync->mConfig.mFilesystemFingerprint.toString() << " error = " << unifiedSync->mConfig.mError;

                enableSyncByBackupId_inThread(
                    unifiedSync->mConfig.mBackupId,
                    false,
                    [&unifiedSync](error, SyncError se, handle backupId)
                    {
                        LOG_debug << "Sync autoresumed: " << toHandle(backupId) << " "
                                  << unifiedSync->mConfig.getLocalPath() << " fsfp= "
                                  << unifiedSync->mConfig.mFilesystemFingerprint.toString()
                                  << " error = " << se;
                    },
                    "");
            }
            else
            {
                LOG_debug << "Sync loaded (but not resumed): " << toHandle(unifiedSync->mConfig.mBackupId) << " " << unifiedSync->mConfig.getLocalPath() << " fsfp= " << unifiedSync->mConfig.mFilesystemFingerprint.toString() << " error = " << unifiedSync->mConfig.mError;
            }
        }
    }

    mClient.app->syncs_restored(NO_SYNC_ERROR);
}

void Sync::recursiveCollectNameConflicts(list<NameConflict>* conflicts, size_t* count, size_t* limit)
{
    assert(syncs.onSyncThread());

    assert(conflicts || count);
    FSNode rootFsNode(localroot->getLastSyncedFSDetails());
    SyncRow row{ &cloudRoot, localroot.get(), &rootFsNode };
    SyncPath pathBuffer(syncs, localroot->localname, cloudRootPath);
    size_t dummyCount = 0;
    size_t dummyMax = std::numeric_limits<size_t>::max();
    recursiveCollectNameConflicts(row, pathBuffer, conflicts, count ? *count : dummyCount, limit ? *limit : dummyMax);
}

void Sync::purgeStaleDownloads()
{
    // Convenience.
    using MC = MegaClient;
    using DBTC = TransferDbCommitter;

    // Make sure we're running on the right thread.
    assert(syncs.onSyncThread());

    auto globPath = localdebris;

    // Get our hands on this sync's temporary directory.
    globPath.appendWithSeparator(LocalPath::fromRelativePath("tmp"), true);

    // Generate glob pattern to match all temporary downloads.
    globPath.appendWithSeparator(LocalPath::fromRelativePath(".*.mega"), true);

    // Remainder of work takes place on the client thread.
    //
    // The idea here is to prevent the client from starting any new
    // transfers while we're busy purging temporary files.
    syncs.queueClient([globPath](MC& client, DBTC&) mutable {
        // Figure out which temporaries are currently present.
        auto dirAccess = client.fsaccess->newdiraccess();
        auto paths = set<LocalPath>();

        if (!dirAccess->dopen(&globPath, nullptr, true))
            return;

        LocalPath path;
        LocalPath name;
        nodetype_t type;

        while (dirAccess->dnext(path, name, false, &type))
        {
            if (type == FILENODE)
                paths.emplace(name);
        }

        // Filter out paths that are still "alive."
        for (auto& i : client.multi_cachedtransfers[GET])
        {
            if (!i.second->localfilename.empty())
                paths.erase(i.second->localfilename);
        }

        // Remove "dead" temporaries.
        for (const auto& tempPath: paths)
            client.fsaccess->unlinklocal(tempPath);
    });
}

void SyncRow::inferOrCalculateChildSyncRows(bool wasSynced, vector<SyncRow>& childRows, vector<FSNode>& fsInferredChildren, vector<FSNode>& fsChildren, vector<CloudNode>& cloudChildren,
                bool belowRemovedFsNode, fsid_localnode_map& localnodeByScannedFsid)
{
    // This is the function that determines the list of syncRows that recursiveSync() will operate on for this folder.
    // Each SyncRow a (CloudNodes, SyncNodes, FSNodes) tuple that all match up by name (taking escapes/case into account)
    // For the case of a folder that contains already-synced content, and in which nothing changed, we can "infer" the set
    // much more efficiently, by generating it from SyncNodes alone.
    // Otherwise we need to calculate it, which involves sorting the three sets and matching them up.
    // An additional complication is that we try to save RAM by not storing the scanned FSNodes for this folder
    // If we can regenerate the list of FSNodes from the LocalNodes, then we would have done that, and so we
    // need to recreate that list.  So our extra RAM usage is just for the folders on the stack, and not the entire tree.
    // (Plus for those SyncNode folders where regeneration wouldn't match the FSNodes (yet))
    // (SyncNode = LocalNode, we'll rename LocalNode eventually)

    if (wasSynced && !belowRemovedFsNode &&
        !syncNode->lastFolderScan && syncNode->syncAgain < TREE_ACTION_HERE &&   // if fully matching, we would have removed the fsNode vector to save space
        syncNode->sync->inferRegeneratableTriplets(cloudChildren, *syncNode, fsInferredChildren, childRows))
    {
        // success, the already sorted and aligned triplets were inferred
        // and the results were filled in: childRows, cloudChildren, fsInferredChildren
    }
    else
    {
        // Effective children are from the last scan, if present.
        vector<FSNode>* effectiveFsChildren = belowRemovedFsNode ? nullptr : syncNode->lastFolderScan.get();

        if (!effectiveFsChildren)
        {
            // Otherwise, we can reconstruct the filesystem entries from the LocalNodes
            fsChildren.reserve(syncNode->children.size() + 50);  // leave some room for others to be added in syncItem()

            for (auto &childIt : syncNode->children)
            {
                assert(childIt.first == childIt.second->localname);
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


void Sync::recursiveCollectNameConflicts(SyncRow& row, SyncPath& fullPath, list<NameConflict>* ncs, size_t& count, size_t& limit)
{
    assert(syncs.onSyncThread());

    assert(row.syncNode);
    if (!row.syncNode->conflictsDetected())
    {
        return;
    }

    if (count >= limit)
    {
        return;
    }

    // Get sync triplets.
    vector<SyncRow> childRows;
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
            if (ncs)
            {
                NameConflict nc;

                if (childRow.hasCloudPresence())
                    nc.cloudPath = fullPath.cloudPath;

                if (childRow.hasLocalPresence())
                    nc.localPath = fullPath.localPath;

                // Only meaningful if there are no cloud clashes.
                if (auto* c = childRow.cloudNode)
                    nc.clashingCloud.emplace_back(c->name, c->handle);

                // Only meaningful if there are no local clashes.
                if (auto* f = childRow.fsNode)
                    nc.clashingLocalNames.emplace_back(f->localname);

                for (auto* c : childRow.cloudClashingNames)
                    nc.clashingCloud.emplace_back(c->name, c->handle);

                for (auto* f : childRow.fsClashingNames)
                    nc.clashingLocalNames.emplace_back(f->localname);

                ncs->emplace_back(std::move(nc));
            }
            if (++count >= limit)
            {
                break;
            }
        }

        // recurse after dealing with all items, so any renames within the folder have been completed
        if (childRow.syncNode && childRow.syncNode->type == FOLDERNODE)
        {
            auto syncPathRestore = makeScopedSyncPathRestorer(fullPath);

            if (!fullPath.appendRowNames(childRow, mFilesystemType) ||
                localdebris.isContainingPathOf(fullPath.localPath))
            {
                // This is a legitimate case; eg. we only had a syncNode and it is removed in resolve_delSyncNode
                // Or if this is the debris folder, ignore it
                continue;
            }

            recursiveCollectNameConflicts(childRow, fullPath, ncs, count, limit);
        }
    }
}

bool SyncRow::hasClashes() const
{
    return !cloudClashingNames.empty() || !fsClashingNames.empty();
}

bool SyncRow::hasCloudPresence() const
{
    return cloudNode || !cloudClashingNames.empty();
}

bool SyncRow::hasLocalPresence() const
{
    return fsNode || !fsClashingNames.empty();
}

const LocalPath& SyncRow::comparisonLocalname() const
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

SyncRowType SyncRow::type() const
{
    auto c = static_cast<unsigned>(cloudNode != nullptr);
    auto s = static_cast<unsigned>(syncNode != nullptr);
    auto f = static_cast<unsigned>(fsNode != nullptr);

    return static_cast<SyncRowType>(c * 4 + s * 2 + f);
}

ExclusionState SyncRow::exclusionState(const CloudNode& node) const
{
    assert(syncNode);
    assert(syncNode->type > FILENODE);

    return syncNode->exclusionState(node.name,
                                    node.type,
                                    node.fingerprint.size);
}

ExclusionState SyncRow::exclusionState(const FSNode& node) const
{
    assert(syncNode);
    assert(syncNode->type > FILENODE);

    return syncNode->exclusionState(node.localname,
                                    node.type,
                                    node.fingerprint.size);
}

ExclusionState SyncRow::exclusionState(const LocalPath& name, nodetype_t type, m_off_t size) const
{
    assert(syncNode);
    assert(syncNode->type != FILENODE);

    return syncNode->exclusionState(name, type, size);
}

bool SyncRow::hasCaseInsensitiveCloudNameChange() const
{
    // only call this if the sync is mCaseInsensitive
    return fsNode && syncNode && cloudNode &&
        syncNode->namesSynchronized &&
        0 != compareUtf(syncNode->localname, true, cloudNode->name, true, false) &&
        0 == compareUtf(syncNode->localname, true, cloudNode->name, true, true) &&
        0 != compareUtf(fsNode->localname, true, cloudNode->name, true, false);
}

bool SyncRow::hasCaseInsensitiveLocalNameChange() const
{
    // only call this if the sync is mCaseInsensitive
    return fsNode && syncNode && cloudNode &&
        syncNode->namesSynchronized &&
        0 != compareUtf(syncNode->localname, true, fsNode->localname, true, false) &&
        0 == compareUtf(syncNode->localname, true, fsNode->localname, true, true) &&
        0 != compareUtf(cloudNode->name, true, fsNode->localname, true, false);
}

bool SyncRow::isLocalOnlyIgnoreFile() const
{
    return isIgnoreFile() && syncNode &&
           syncNode->parent &&
           syncNode->parent->rareRO().filterChain &&
           !syncNode->parent->rareRO().filterChain->mSyncThisMegaignore;
}

bool SyncRow::isIgnoreFile() const
{
    struct Predicate
    {
        bool operator()(const CloudNode& node) const
        {
            // So to avoid ambiguity.
            const string& name = IGNORE_FILE_NAME;

            return node.type == FILENODE
                   && !platformCompareUtf(node.name, true, name, false);
        }

        bool operator()(const FSNode& node) const
        {
            const LocalPath& name = IGNORE_FILE_NAME;

            return node.type == FILENODE
                   && !platformCompareUtf(node.localname, true, name, false);
        }
    } predicate;

    if (auto* s = syncNode)
        return s->isIgnoreFile();

    if (auto* f = fsNode)
        return predicate(*f);

    if (!fsClashingNames.empty())
        return predicate(*fsClashingNames.front());

    if (auto* c = cloudNode)
        return predicate(*c);

    if (!cloudClashingNames.empty())
        return predicate(*cloudClashingNames.front());

    return false;
}

bool SyncRow::isNoName() const
{
    // TODO: Notify the app about the presence os NoName nodes (the stall issue has been removed after SDK-3859)

    // Can't be a no-name triplet if we have clashing filesystem names.
    if (!fsClashingNames.empty())
        return false;

    // Could be a no-name triplet if we have clashing cloud names.
    if (!cloudClashingNames.empty())
        return cloudClashingNames.front()->name.empty();

    // Could be a no-name triplet if we have a cloud node.
    if (cloudNode && cloudNode->name.empty())
    {
        if (fsNode || syncNode)
        {
            LOG_debug << "Considering a cloudNode to be NO_NAME with either a fsNode or syncNode: "
                    << "fsNode: " << string(fsNode ? "true" : "false")
                    << "fsNode: " << string(syncNode ? "true" : "false");
        }
        return true;
    }
    return false;
}

void Sync::combineTripletSet(vector<SyncRow>::iterator a, vector<SyncRow>::iterator b) const
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

    // match up elements that are still present and were already synced
    vector<SyncRow>::iterator lastFullySynced = b;
    vector<SyncRow>::iterator lastNotFullySynced = b;
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

            // is this row fully synced already? if so, put it aside in case there are more syncNodes
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
    if (syncNode_nfs_count >= 2)
    {
        LOG_err << "syncNode_nfs_count(" << syncNode_nfs_count << ") >= 2! This should not happen!";
        assert(false && "syncNode_nfs_count >= 2, please figure out how we got into that state");
    }

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
                SYNC_verbose_timed << syncname << "Conflicting filesystem name: "
                                   << targetrow->fsNode->localname;
                targetrow->fsClashingNames.push_back(targetrow->fsNode);
                targetrow->fsNode = nullptr;
            }
            if (targetrow->fsNode ||
                !targetrow->fsClashingNames.empty())
            {
                SYNC_verbose_timed << syncname << "Conflicting filesystem name: "
                                   << i->fsNode->localname;
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
            if (!targetrow->cloudNode || !targetrow->cloudNode->name.empty()) // Avoid NO_NAME nodes to be considered
            {
                if (targetrow->cloudNode &&
                    !(targetrow->syncNode &&
                    targetrow->syncNode->syncedCloudNodeHandle
                    == targetrow->cloudNode->handle))
                {
                    SYNC_verbose_timed << syncname << "Conflicting cloud name: "
                                       << targetrow->cloudNode->name;
                    targetrow->cloudClashingNames.push_back(targetrow->cloudNode);
                    targetrow->cloudNode = nullptr;
                }
                if (targetrow->cloudNode ||
                    !targetrow->cloudClashingNames.empty())
                {
                    SYNC_verbose_timed << syncname << "Conflicting cloud name: "
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
        assert(i == targetrow || i->empty() || (i->cloudNode && (i->cloudNode->name.empty())));
    }
#endif
}

auto Sync::computeSyncTriplets(vector<CloudNode>& cloudNodes, const LocalNode& syncParent, vector<FSNode>& fsNodes) const -> vector<SyncRow>
{
    assert(syncs.onSyncThread());

    CodeCounter::ScopeTimer rst(syncs.mClient.performanceStats.computeSyncTripletsTime);

    vector<SyncRow> triplets;
    triplets.reserve(cloudNodes.size() + syncParent.children.size() + fsNodes.size());

    for (auto& cn : cloudNodes)          triplets.emplace_back(&cn, nullptr, nullptr);
    for (auto& sn : syncParent.children) triplets.emplace_back(nullptr, sn.second, nullptr);
    for (auto& fsn : fsNodes)            triplets.emplace_back(nullptr, nullptr, &fsn);

    auto tripletCompare = [this](const SyncRow& lhs, const SyncRow& rhs) -> int {
        // Sanity.
        assert(!lhs.fsNode || !lhs.fsNode->localname.empty());
        assert(!rhs.fsNode || !rhs.fsNode->localname.empty());
        assert(!lhs.syncNode || !lhs.syncNode->localname.empty());
        assert(!rhs.syncNode || !rhs.syncNode->localname.empty());

        // Although it would be great to efficiently compare cloud names in utf8 directly against filesystem names
        // in utf16, without any conversions or copied and manipulated strings, unfortunately we have
        // a few obstacles to that.  Mainly, that the utf8 encoding can differ - especially on Mac
        // where they normalize the names that go to the filesystem, but with a different normalization
        // than we chose for the Node names.  In order to compare these effectively and efficiently
        // we pretty much have to first duplicate and convert both strings to a single utf8 normalization first.

        if (lhs.cloudNode)
        {
            if (rhs.cloudNode)
            {
                return compareUtf(lhs.cloudNode->name, true, rhs.cloudNode->name, true, mCaseInsensitive);
            }
            else if (rhs.syncNode)
            {
                return compareUtf(lhs.cloudNode->name, true, rhs.syncNode->toName_of_localname, false, mCaseInsensitive);
            }
            else // rhs.fsNode
            {
                return compareUtf(lhs.cloudNode->name, true, rhs.fsNode->toName_of_localname(*syncs.fsaccess), false, mCaseInsensitive);
            }
        }
        else if (lhs.syncNode)
        {
            if (rhs.cloudNode)
            {
                return compareUtf(lhs.syncNode->toName_of_localname, false, rhs.cloudNode->name, true, mCaseInsensitive);
            }
            else if (rhs.syncNode)
            {
                return compareUtf(lhs.syncNode->toName_of_localname, false, rhs.syncNode->toName_of_localname, false, mCaseInsensitive);
            }
            else // rhs.fsNode
            {
                return compareUtf(lhs.syncNode->toName_of_localname, false, rhs.fsNode->toName_of_localname(*syncs.fsaccess), false, mCaseInsensitive);
            }
        }
        else // lhs.fsNode
        {
            if (rhs.cloudNode)
            {
                return compareUtf(lhs.fsNode->toName_of_localname(*syncs.fsaccess), false, rhs.cloudNode->name, true, mCaseInsensitive);
            }
            else if (rhs.syncNode)
            {
                return compareUtf(lhs.fsNode->toName_of_localname(*syncs.fsaccess), false, rhs.syncNode->toName_of_localname, false, mCaseInsensitive);
            }
            else // rhs.fsNode
            {
                return compareUtf(lhs.fsNode->toName_of_localname(*syncs.fsaccess), false, rhs.fsNode->toName_of_localname(*syncs.fsaccess), false, mCaseInsensitive);
            }
        }
    };

    std::sort(triplets.begin(), triplets.end(),
           [=](const SyncRow& lhs, const SyncRow& rhs)
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

    auto newEnd = std::remove_if(triplets.begin(), triplets.end(), [](SyncRow& row){ return row.empty(); });
    triplets.erase(newEnd, triplets.end());

    return triplets;
}

bool Sync::inferRegeneratableTriplets(vector<CloudNode>& cloudChildren, const LocalNode& syncParent, vector<FSNode>& inferredFsNodes, vector<SyncRow>& inferredRows) const
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


static IndexPairVector computeSyncSequences(vector<SyncRow>& children)
{
    // No children, no work to be done.
    if (children.empty())
        return IndexPairVector();

    CodeCounter::ScopeTimer rst(computeSyncSequencesStats);

    // Separate our children into those that are ignore files and those that are not.
    auto i = std::partition(children.begin(), children.end(), [](const SyncRow& child) {
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

bool Sync::recursiveSync(SyncRow& row, SyncPath& fullPath, bool belowRemovedCloudNode, bool belowRemovedFsNode, unsigned depth)
{
    assert(syncs.onSyncThread());

    // in case of sync failing while we recurse
    if (getConfig().mError) return false;

    assert(row.syncNode);
    assert(row.syncNode->type > FILENODE);
    assert(row.syncNode->getLocalPath() == fullPath.localPath);

    if (depth + mCurrentRootDepth == MAX_CLOUD_DEPTH - 1)
    {
        ProgressingMonitor monitor(*this, row, fullPath);

        LOG_debug << "Attempting to synchronize overly deep directory: "
                  << logTriplet(row, fullPath)
                  << ": Effective depth is "
                  << depth + mCurrentRootDepth;

        monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
            SyncWaitReason::SyncItemExceedsSupportedTreeDepth, true, true,
            {row.cloudHandleOpt(), fullPath.cloudPath},
            {},
            {fullPath.localPath},
            {}));

        return true;
    }

    // nothing to do for this subtree? Skip traversal
    if (!(row.syncNode->scanRequired() || row.syncNode->mightHaveMoves() || row.syncNode->syncRequired()))
    {
        //SYNC_verbose << syncname << "No scanning/moving/syncing needed at " << logTriplet(row, fullPath);
        return true;
    }

    SYNC_verbose_timed << syncname << (belowRemovedCloudNode ? "belowRemovedCloudNode " : "")
                       << (belowRemovedFsNode ? "belowRemovedFsNode " : "")
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
            ProgressingMonitor monitor(*this, row, fullPath);
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
        vector<SyncRow> childRows;
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

        SyncRow* ignoreRow = !childRows.empty() &&
                              childRows.front().isIgnoreFile() ?
                             &childRows.front() : nullptr;

        FSNode* ignoreFile = ignoreRow ? ignoreRow->fsNode : nullptr;

        bool invalidateExclusions = false;

        if (ignoreRow && !row.syncNode->rareRO().filterChain)
        {
            // instantiate filterChain as soon as we have a row, even if we don't have a
            // local file yet, in order to prevent any sync steps for other things until
            // we get the local file by download, or stall if there are cloud duplicates etc.
            LOG_debug << syncname << ".megaignore row detected inside " << logTriplet(row, fullPath);
            row.syncNode->rare().filterChain.reset(new FilterChain);
            invalidateExclusions = true;
        }
        if (!ignoreRow && row.syncNode->rareRO().filterChain)
        {
            LOG_debug << syncname << ".megaignore row disappeared inside " << logTriplet(row, fullPath);
            row.syncNode->rare().filterChain.reset();
            invalidateExclusions = true;
        }
        if (ignoreRow && !ignoreFile &&
            row.syncNode->rareRO().filterChain &&
            row.syncNode->rare().filterChain->mLoadSucceeded)
        {
            LOG_debug << syncname << ".megaignore file disappeared inside " << logTriplet(row, fullPath);
            row.syncNode->rare().filterChain->mFingerprint = FileFingerprint();
            row.syncNode->rare().filterChain->mLoadSucceeded = false;
            invalidateExclusions = true;
        }
        if (ignoreFile && row.syncNode->rareRO().filterChain)
        {
            if (row.syncNode->rareRO().filterChain->mFingerprint != ignoreFile->fingerprint)
            {
                LOG_debug << syncname << "loading .megaignore file inside " << logTriplet(row, fullPath);
                auto ignorepath = fullPath.localPath;
                ignorepath.appendWithSeparator(IGNORE_FILE_NAME, true);
                bool ok = row.syncNode->loadFilters(ignorepath);
                if (ok != !row.syncNode->rareRO().badlyFormedIgnoreFilePath)
                {
                    if (ok)
                    {
                        row.syncNode->rare().badlyFormedIgnoreFilePath.reset();
                    }
                    else
                    {
                        row.syncNode->rare().badlyFormedIgnoreFilePath.reset(new LocalNode::RareFields::BadlyFormedIgnore(ignorepath, this));
                        syncs.badlyFormedIgnoreFilePaths.push_back(row.syncNode->rare().badlyFormedIgnoreFilePath);
                    }
                }
                invalidateExclusions = true;
            }
        }

        if (invalidateExclusions)
        {
            row.syncNode->setRecomputeExclusionState(false, false);
        }


        // Here is where we loop over the syncRows for this folder.
        // We must process things in a particular order
        //    - first, check all rows for involvement in moves (case 0)
        //      such a row is excluded from further checks
        //    - second, check all remaining rows for sync actions on that row (case 1)
        //    - third, recurse into each folder (case 2)
        //      there are various reasons we may skip that, based on flags set by earlier steps
        //    - zeroth, we perform first->third for any .megaignore file before any others
        //      as any change involving .megaignore may affect anything else we do
        PerFolderLogSummaryCounts pflsc;

        for (auto& sequence : sequences)
        {
            // The main three steps: moves, node itself, recurse
            for (unsigned step = 0; step < 3; ++step)
            {
                if (step == 2 &&
                    sequence.second == sequences.back().second &&
                    !belowRemovedCloudNode && !belowRemovedFsNode)
                {
                    string message;
                    if (pflsc.report(message))
                    {
                        SYNC_verbose_timed << syncname << message << " out of folder items:" << childRows.size();
                    }
                }

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
                                syncs.setScannedFsidReused(fsfp(), f->fsid);
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

                        if (s->exclusionState() == ES_EXCLUDED)
                        {
                            if (!s->children.empty())
                            {
                                // We keep the immediately excluded node (parent folder is not excluded), but remove anything below it
                                LOG_debug << syncname << "Removing " << s->children.size() << " child LocalNodes from excluded " << s->getLocalPath();
                                vector<LocalNode*> cs;
                                cs.reserve(s->children.size());
                                for (auto& child: s->children)
                                {
                                    cs.push_back(child.second);
                                }
                                // this technique might seem a bit roundabout, but deletion will cause these to
                                // remove themselves from s->children. // we can't have that happening while we iterate that map.
                                for (auto p : cs)
                                {
                                    // deletion this way includes statecachedel
                                    delete p;
                                }
                            }
                            if (s->transferSP)
                            {
                                s->resetTransfer(nullptr);
                            }
                            s->checkTreestate(true);
                            continue;
                        }
                    }

                    auto syncPathRestore = makeScopedSyncPathRestorer(fullPath);

                    if (!fullPath.appendRowNames(childRow, mFilesystemType) ||
                        localdebris.isContainingPathOf(fullPath.localPath))
                    {
                        // This is a legitimate case; eg. we only had a syncNode and it is removed in resolve_delSyncNode
                        // Or if this is the debris folder, ignore it
                        continue;
                    }

                    if (childRow.syncNode)
                    {
                        #ifdef DEBUG
                            auto p = childRow.syncNode->getLocalPath();
                            assert(0 == compareUtf(p, true, fullPath.localPath, true, mCaseInsensitive));
                        #endif
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

                        // moved from the end of syncItem_checkMoves.  So we can check ignore files also, as those skip move processing
                        if ((syncHere || belowRemovedCloudNode || belowRemovedFsNode) &&
                            syncItem_checkFilenameClashes(childRow, row, fullPath))
                        {
                            row.syncNode->setSyncAgain(false, true, false);
                            break;
                        }

                        if (belowRemovedCloudNode)
                        {
                            // when syncing/scanning below a removed cloud node, we just want to collect up scan fsids
                            // and make syncNodes to visit, so we can be sure of detecting all the moves,
                            // in particular contradictory moves.
                            if (childRow.type() == SRT_XXF && row.exclusionState(*childRow.fsNode) == ES_INCLUDED)
                            {
                                makeSyncNode_fromFS(childRow, row, fullPath, false);
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
                            if (!syncItem(childRow, row, fullPath, pflsc))
                            {
                                if (childRow.syncNode && childRow.syncNode->type != FOLDERNODE)
                                {
                                    childRow.syncNode->setSyncAgain(true, true, false);
                                }
                                else
                                {
                                    row.syncNode->setSyncAgain(false, true, false);
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
                            childRow.syncNode->type > FILENODE && (
                                (childRow.recurseBelowRemovedCloudNode &&
                                 (childRow.syncNode->scanRequired() || childRow.syncNode->syncRequired()))
                                ||
                                (childRow.recurseBelowRemovedFsNode &&
                                 childRow.syncNode->syncRequired())
                                ||
                                (recurseHere &&
                                !childRow.suppressRecursion &&
                                //!childRow.syncNode->deletedFS &&   we should not check this one, or we won't remove the LocalNode
                                //childRow.syncNode->rareRO().removeNodeHere.expired() && // this is shared_ptr now, not weak_ptr.  And checked early in checkForCompletedCloudMovedToDebris
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
                        break;

                    }

                    if (!childRow.fsNode && childRow.syncNode)
                    {
                        // if there's no local file/folder, we can't scan
                        // avoid the case of large folder upload, deleted while uploading
                        // that then persists in thinking subtree needs scanning
                        childRow.syncNode->scanAgain = TREE_RESOLVED;
                        childRow.syncNode->parentSetScanAgain = false;
                    }

                }
            }

            //if (ignoreRow && ignoreRow->syncNode /*&& ignoreRow->syncNode->transferSP*/)
            //{
            //    if (!row.syncNode->rareRO().filterChain ||
            //        !row.syncNode->rareRO().filterChain->mLoadSucceeded)
            //    {
            //        // we can't calculate what's included yet.  Let the download complete
            //        // and come back when the .megaignore is present and well-formed
            //        break;
            //    }
            //}
        }

        if (!anyNameConflicts)
        {
            // here childRows still contains pointers into lastFolderScan, fsAddedSiblings etc
            row.syncNode->clearRegeneratableFolderScan(fullPath, childRows);
        }

        // If we still don't match the known fs state, and if we added any FSNodes that
        // aren't part of our scan data (and we think we don't need another scan),
        // add them to the scan data to avoid re-fingerprinting no the next folder scan
        if (!row.fsAddedSiblings.empty())
        {
            auto& scan = row.syncNode->lastFolderScan;
            if (scan && row.syncNode->scanAgain < TREE_ACTION_HERE)
            {
                scan->reserve(scan->size() + row.fsAddedSiblings.size());
                for (auto& ptr: row.fsAddedSiblings)
                {
                    scan->push_back(std::move(ptr));
                }
                row.fsAddedSiblings.clear();
            }
        }
    }

    // Recompute our LocalNode flags from children
    // Flags for this row could have been set during calls to the node
    // If we skipped a child node this time (or if not), the set-parent
    // flags let us know if future actions are needed at this level
    for (auto& child : row.syncNode->children)
    {
        assert(child.first == child.second->localname);

        if (child.second->exclusionState() == ES_EXCLUDED)
        {
            continue;
        }

        if (child.second->type > FILENODE)
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

    // keep sync overlay icons up to date as we recurse (including the sync root node)
    row.syncNode->checkTreestate(true);

    SYNC_verbose_timed << syncname << (belowRemovedCloudNode ? "belowRemovedCloudNode " : "")
                << "Exiting folder with "
                << row.syncNode->scanAgain  << "-"
                << row.syncNode->checkMovesAgain << "-"
                << row.syncNode->syncAgain << " ("
                << row.syncNode->conflicts << ") at "
                << fullPath.syncPath;

    return !earlyExit;
}

string Sync::logTriplet(const SyncRow& row, const SyncPath& fullPath) const
{
    ostringstream s;
    s << " triplet:" <<
        " " << (row.cloudNode ? fullPath.cloudPath : "(null)") <<
        " " << (row.syncNode ? fullPath.syncPath : "(null)") <<
        " " << (row.fsNode ? fullPath.localPath.toPath(false):"(null)");
    return s.str();
}

bool Sync::syncItem_checkMoves(SyncRow& row, SyncRow& parentRow, SyncPath& fullPath,
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

    // Does this row have a sync node?
    if (auto* s = row.syncNode)
    {
        // Is this row part of an ongoing upload?
        if (auto u = std::dynamic_pointer_cast<SyncUpload_inClient>(s->transferSP))
        {
            // Is it waiting for a putnodes request to complete?
            if (u->putnodesStarted)
            {
                if (!u->wasPutnodesCompleted)
                {
                    LOG_debug << "Waiting for putnodes to complete, defer move checking: "
                              << logTriplet(row, fullPath);

                    // Then come back later.
                    return false;
                }
                else
                {
                    bool rowResult = false;
                    if (processCompletedUploadFromHere(row, parentRow, fullPath, rowResult, u))
                    {
                        return rowResult;
                    }
                }
            }
        }
    }

    // Under some circumstances on sync startup, our shortname records can be out of date.
    // If so, we adjust for that here, as the directories are scanned
    if (row.syncNode && row.fsNode && row.fsNode->shortname)
    {
        if ((!row.syncNode->slocalname ||
            *row.syncNode->slocalname != *row.fsNode->shortname) &&
            row.syncNode->localname != *row.fsNode->shortname)
        {
            LOG_warn << syncname
                     << "Updating slocalname: " << *row.fsNode->shortname
                     << " at " << fullPath.localPath
                     << " was " << (row.syncNode->slocalname ? row.syncNode->slocalname->toPath(false) : "(null)")
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

    // Are we dealing with a no-name triplet?
    if (row.isNoName())
    {
        LOG_debug << syncname
                  << "No name triplets here. "
                  << "Excluding this triplet from sync for now. "
                  << logTriplet(row, fullPath);

        return true;
    }

    if (row.fsNode && isDoNotSyncFileName(row.fsNode->toName_of_localname(*syncs.fsaccess)))
    {
        // don't consider these for moves
        return true;
    }

    // Don't perform any moves until we know the row's exclusion state.
    if ((row.cloudNode && parentRow.exclusionState(*row.cloudNode) == ES_UNKNOWN) ||
        (row.fsNode && parentRow.exclusionState(*row.fsNode) == ES_UNKNOWN))
    {
        row.itemProcessed = true;
        row.suppressRecursion = true;

        return true;
    }

    bool rowResult;
    if (checkForCompletedFolderCreateHere(row, parentRow, fullPath, rowResult))
    {
        row.itemProcessed = true;
        return rowResult;
    }

    if (checkForCompletedCloudMoveToHere(row, parentRow, fullPath, rowResult))
    {
        row.itemProcessed = true;
        return rowResult;
    }

    if (checkForCompletedCloudMovedToDebris(row, parentRow, fullPath, rowResult))
    {
        row.itemProcessed = true;
        return rowResult;
    }

    // Don't try and synchronize special files.
    if (checkSpecialFile(row, parentRow, fullPath))
    {
        row.itemProcessed = true;
        return false;
    }

    // First deal with detecting local moves/renames and propagating correspondingly
    // Independent of the syncItem() combos below so we don't have duplicate checks in those.
    // Be careful of unscannable folder entries which may have no detected type or fsid.
    // todo: We also have to consider the possibility this item was moved locally and remotely,
    // and possibly from different locations, or even possibly from the same location.

    if (row.fsNode &&
        (!row.syncNode ||
          row.syncNode->fsid_lastSynced == UNDEF ||
          row.syncNode->fsid_lastSynced != row.fsNode->fsid ||
          (mCaseInsensitive && row.hasCaseInsensitiveLocalNameChange())))
    {
        if (checkLocalPathForMovesRenames(row, parentRow, fullPath, rowResult, belowRemovedCloudNode))
        {
            row.itemProcessed = true;
            return rowResult;
        }
    }

    if (row.cloudNode &&
        (!row.syncNode ||
          row.syncNode->syncedCloudNodeHandle.isUndef() ||
          row.syncNode->syncedCloudNodeHandle != row.cloudNode->handle ||
          (mCaseInsensitive && row.hasCaseInsensitiveCloudNameChange())))
    {
        if (checkCloudPathForMovesRenames(row, parentRow, fullPath, rowResult, belowRemovedFsNode))
        {
            row.itemProcessed = true;
            return rowResult;
        }
    }

    return false;
}

bool Sync::syncItem_checkBackupCloudNameClash(SyncRow& row,
                                              SyncRow& /*parentRow*/,
                                              SyncPath& fullPath)
{
    assert(isBackup() == threadSafeState->mCanChangeVault);
    if (!isBackup()) return false;

    if (!row.cloudClashingNames.empty())
    {
        // Duplicates like this should not occur for backups.  However, before SRW, it could occur due to bugs, races etc.
        // Since stall resolution at the app level won't work, as it can't alter the Vault, we have to address it here
        // Choose one to delete (to debris) - avoid the one that is marked as synced already, if there is one

        if (row.syncNode)
        {
            if (auto& rn = row.syncNode->rare().removeNodeHere)
            {
                if (!rn->failed && !rn->succeeded)
                {
                    return true;  // concentrate on the delete until that is done
                }
                LOG_debug << syncname << "[Sync::syncItem_checkBackupCloudNameClash] Completed duplicate backup cloud item to cloud sync debris but some dupes remain (" << rn->succeeded << "): " << fullPath.cloudPath << logTriplet(row, fullPath);
                rn.reset();
            }
        }

        // choose which one to remove
        NodeHandle avoidHandle = row.syncNode ? row.syncNode->syncedCloudNodeHandle : NodeHandle();
        CloudNode* cn = nullptr;
        auto consider = [&](CloudNode* c){
            if (c && c->handle != avoidHandle)
            {
                if (!cn) cn = c;
            }
        };
        for (auto& i : row.cloudClashingNames)
        {
            consider(i);
        }
        consider(row.cloudNode);

        // set up the operation and pass it to the client thread, track the operation by removeNodeHere
        if (cn)
        {
            LOG_debug << syncname << "[Sync::syncItem_checkBackupCloudNameClash] Moving duplicate backup cloud item to cloud sync debris: " << cn->handle << " " << cn->name << " " << fullPath.cloudPath << logTriplet(row, fullPath);
            bool fromInshare = inshare;
            auto debrisNodeHandle = cn->handle;

            auto deletePtr = std::make_shared<LocalNode::RareFields::DeleteToDebrisInProgress>();
            deletePtr->pathDeleting = fullPath.cloudPath;
            bool canChangeVault = threadSafeState->mCanChangeVault;

            syncs.queueClient(
                [debrisNodeHandle, fromInshare, deletePtr, canChangeVault](MegaClient& mc,
                                                                           TransferDbCommitter&)
                {
                    if (auto n = mc.nodeByHandle(debrisNodeHandle))
                    {
                        mc.movetosyncdebris(
                            n.get(),
                            fromInshare,
                            [deletePtr](NodeHandle, Error e)
                            {
                                LOG_debug << "[Sync::syncItem_checkBackupCloudNameClash] Sync "
                                             "backup duplicate delete to sync debris completed: "
                                          << e << " " << deletePtr->pathDeleting;

                                if (e)
                                    deletePtr->failed = true;
                                else
                                    deletePtr->succeeded = true;
                            },
                            canChangeVault);
                    }
                });

            if (row.syncNode)
            {
                // Remember that the delete is going on, so we don't do anything else until that resolves
                // We will detach the synced-fsid side on final completion of this operation.  If we do so
                // earier, the logic will evaluate that updated state too soon, perhaps resulting in downsync.
                row.syncNode->rare().removeNodeHere = deletePtr;
            }
            else
            {
                LOG_debug << "[Sync::syncItem_checkBackupCloudNameClash] No row.syncNode -> avoid assigning deletePtr to row.syncNode->rare().removeNodeHere";
            }
            return true;
        }
    }
    return false;  // no backup node removal needed
}

bool Sync::syncItem_checkFilenameClashes(SyncRow& row, SyncRow& parentRow, SyncPath& fullPath)
{
    // Avoid syncing nodes that have multiple clashing names
    // Except if we previously had a folder (just itself) synced, allow recursing into that one.
    if (!row.fsClashingNames.empty() || !row.cloudClashingNames.empty())
    {

        if (row.isNoName())
        {
            SYNC_verbose_timed << syncname << "no name Multiple name clashes here. Excluding this node from sync for now."
                               << logTriplet(row, fullPath);
            return false;
        }

        if (syncItem_checkBackupCloudNameClash(row, parentRow, fullPath))
        {
            return false;
        }

        if (row.syncNode) row.syncNode->setContainsConflicts(true, false, false);
        else parentRow.syncNode->setContainsConflicts(false, true, false);

        if (row.cloudNode && row.syncNode && row.fsNode &&
            row.syncNode->type == FOLDERNODE &&
            row.cloudNode->handle == row.syncNode->syncedCloudNodeHandle &&
            row.fsNode->fsid != UNDEF && row.fsNode->fsid == row.syncNode->fsid_lastSynced)
        {
            SYNC_verbose << syncname << "Name clashes at this already-synced folder.  We will sync nodes below though." << logTriplet(row, fullPath);
            return false;
        }

        //// Is this clash due to multiple ignore files being present in the cloud?
        //auto isIgnoreFileClash = [](const SyncRow& row) {
        //    // Any clashes in the cloud?
        //    if (row.cloudClashingNames.empty())
        //        return false;

        //    // Any clashes on the local disk?
        //    if (!row.fsClashingNames.empty())
        //        return false;

        //    if (!(row.fsNode || row.syncNode))
        //        return false;

        //    // Row represents an ignore file?
        //    return row.isIgnoreFile();
        //};

        //if (isIgnoreFileClash(row))
        //    return false;

        SYNC_verbose_timed << syncname << "Multiple name clashes here. Excluding this node from sync for now."
                           << logTriplet(row, fullPath);

        if (row.syncNode)
        {
            row.syncNode->scanAgain = TREE_RESOLVED;
            row.syncNode->checkMovesAgain = TREE_RESOLVED;
            row.syncNode->syncAgain = TREE_RESOLVED;
        }

        row.itemProcessed = true;
        row.suppressRecursion = true;

        return true;
    }

    return false;
}

bool Sync::syncItem_checkDownloadCompletion(SyncRow& row, SyncRow& parentRow, SyncPath& fullPath)
{
    assert(syncs.onSyncThread());

    auto downloadPtr = std::dynamic_pointer_cast<SyncDownload_inClient>(row.syncNode->transferSP);
    if (!downloadPtr) return true;

    ProgressingMonitor monitor(*this, row, fullPath);

    if (downloadPtr->wasTerminated)
    {
        const bool keepSyncItem = handleTerminatedDownloads(row, fullPath, *downloadPtr, monitor);
        downloadPtr->terminatedReasonAlreadyKnown = true;
        return keepSyncItem;
    }
    if (downloadPtr->wasCompleted)
    {
        assert(downloadPtr->downloadDistributor);

        // Convenience.
        auto& fsAccess = *syncs.fsaccess;

        // Clarity.
        auto& targetPath = fullPath.localPath;

        // Try and move/rename the downloaded file into its new home.
        bool nameTooLong = false;
        bool transientError = false;

        if (row.fsNode && downloadPtr->okToOverwriteFF.isvalid)
        {
            if (row.fsNode->fingerprint != downloadPtr->okToOverwriteFF)
            {
                LOG_debug << syncname << "Sync download cannot overwrite unexpected file at " << logTriplet(row, fullPath);
                monitor.noResult();
                return true;  // continue matching and see if we stall due to changes on both sides
            }
        }

        std::function<void()> afterDistributed = []() { };

        // Have we downloaded an ignore file?
        if (row.isIgnoreFile())
        {
            // Convenience.
            auto noLogging = FSLogging::noLogging;

            // Was the existing ignore file (if any) hidden?
            if (syncs.fsaccess->isFileHidden(targetPath, noLogging))
                afterDistributed = std::bind(FileSystemAccess::setFileHidden,
                                             std::cref(targetPath),
                                             noLogging);
        }

        if (downloadPtr->downloadDistributor->distributeTo(targetPath, fsAccess, FileDistributor::MoveReplacedFileToSyncDebris, transientError, nameTooLong, this))
        {
            assert(FSNode::debugConfirmOnDiskFingerprintOrLogWhy(fsAccess, targetPath, *downloadPtr));

            // Perform after-distribution task.
            afterDistributed();

            // Move was successful.
            SYNC_verbose << syncname << "Download complete, moved file to final destination." << logTriplet(row, fullPath);

            // Download was moved into place.
            downloadPtr->wasDistributed = true;

            // No longer necessary as the transfer's complete.
            row.syncNode->resetTransfer(nullptr);

            // Let the engine know the file exists, even if it hasn't detected it yet.
            //
            // This is necessary as filesystem events may be delayed.
            if (auto fsNode = FSNode::fromPath(fsAccess, targetPath, true, FSLogging::logOnError))  // skip case check. We will check for exact match ourselves
            {
                if (fsNode->localname != targetPath.leafName())
                {
                    row.syncNode->localFSCannotStoreThisName = true;
                    row.syncNode->rare().localFSRenamedToThisName = fsNode->localname;
                    return true;  // carry on with checkItem() - this syncNode will cause a stall until cloud rename
                }

                // Make this new fsNode part of our sync data structure
                parentRow.fsAddedSiblings.emplace_back(std::move(*fsNode));
                row.fsNode = &parentRow.fsAddedSiblings.back();
                row.syncNode->slocalname = row.fsNode->cloneShortname();
            }
            else
            {
                row.syncNode->localFSCannotStoreThisName = true;
                return true;  // carry on with checkItem() - this syncNode will cause a stall until cloud rename
            }

            // Mark the row as synced with the original Node downloaded, so that
            // we can chain any cloud moves/renames that occurred in the meantime
            row.syncNode->setSyncedFsid(row.fsNode->fsid, syncs.localnodeBySyncedFsid, row.fsNode->localname, row.fsNode->cloneShortname());
            row.syncNode->syncedFingerprint = row.fsNode->fingerprint;
            row.syncNode->setSyncedNodeHandle(downloadPtr->h);
            statecacheadd(row.syncNode);
        }
        else if (nameTooLong)
        {
            SYNC_verbose << syncname
                            << "Download complete but the target's name is too long: "
                            << logTriplet(row, fullPath);

            monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
                SyncWaitReason::DownloadIssue, true, true,
                {downloadPtr->h, fullPath.cloudPath},
                {},
                {downloadPtr->downloadDistributor->distributeFromPath()},
                {fullPath.localPath, PathProblem::NameTooLongForFilesystem}));

            // Leave the transfer intact so we don't reattempt the download.
            // Also allow the syncItem logic to continue, to resolve via user change, eg delete the cloud node
            return true;
        }
        else
        {
            // (Transient?) error while moving download into place.
            SYNC_verbose_timed << syncname << "Download complete, but filesystem move error."
                               << logTriplet(row, fullPath);

            // Let the monitor know what we're up to.
            monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
                SyncWaitReason::DownloadIssue, true, true,
                {downloadPtr->h, fullPath.cloudPath},
                {},
                {downloadPtr->getLocalname()},
                {fullPath.localPath, PathProblem::FilesystemErrorDuringOperation}));

            // Also allow the syncItem logic to continue, to resolve via user change, eg delete the cloud node
            return true;
        }
    }
    return true;  // carry on with checkItem()
}

bool Sync::handleTerminatedDownloads(const SyncRow& row,
                                     const SyncPath& fullPath,
                                     const SyncDownload_inClient& downloadFile,
                                     ProgressingMonitor& monitor)
{
    switch (downloadFile.mError)
    {
        case API_EKEY:
            return handleTerminatedDownloadsDueMAC(row, fullPath, downloadFile, monitor);
        case API_EBLOCKED:
            return handleTerminatedDownloadsDueBlocked(row, fullPath, downloadFile, monitor);
        case API_EWRITE:
            return handleTerminatedDownloadsDueWritePerms(row, fullPath, downloadFile);
        default:
            return handleTerminatedDownloadsDueUnknown(row, fullPath, downloadFile, monitor);
    }
}

bool Sync::handleTerminatedDownloadsDueMAC(const SyncRow& row,
                                           const SyncPath& fullPath,
                                           const SyncDownload_inClient& downloadFile,
                                           ProgressingMonitor& monitor) const
{
    if (!downloadFile.terminatedReasonAlreadyKnown)
    {
        SYNC_verbose << syncname << "Download was terminated due to MAC verification failure: "
                     << logTriplet(row, fullPath);
    }

    monitor.waitingLocal(
        downloadFile.getLocalname(),
        SyncStallEntry(SyncWaitReason::DownloadIssue,
                       true,
                       true,
                       {downloadFile.h, fullPath.cloudPath, PathProblem::MACVerificationFailure},
                       {},
                       {downloadFile.getLocalname(), PathProblem::MACVerificationFailure},
                       {fullPath.localPath}));

    const bool keepStalling = row.cloudNode && row.cloudNode->handle == downloadFile.h;
    return !keepStalling;
}

bool Sync::handleTerminatedDownloadsDueBlocked(const SyncRow& row,
                                               const SyncPath& fullPath,
                                               const SyncDownload_inClient& downloadFile,
                                               ProgressingMonitor& monitor) const
{
    if (!downloadFile.terminatedReasonAlreadyKnown)
    {
        SYNC_verbose
            << syncname
            << "Download was terminated due to file being blocked (taken down because of ToS): "
            << logTriplet(row, fullPath);
    }

    monitor.waitingCloud(
        fullPath.cloudPath,
        SyncStallEntry(SyncWaitReason::DownloadIssue,
                       true,
                       true,
                       {downloadFile.h, fullPath.cloudPath, PathProblem::CloudNodeIsBlocked},
                       {},
                       {},
                       {}));
    return true;
}

bool Sync::handleTerminatedDownloadsDueWritePerms(const SyncRow& row,
                                                  const SyncPath& fullPath,
                                                  const SyncDownload_inClient& downloadFile)
{
    if (!downloadFile.terminatedReasonAlreadyKnown)
    {
        SYNC_verbose << syncname
                     << "Download was terminated due to API_EWRITE (problem with the temporary "
                        "directory?): "
                     << logTriplet(row, fullPath);
    }
    tmpfa.reset();
    // remove the download record so we re-evaluate what to do
    row.syncNode->resetTransfer(nullptr);
    return true;
}

bool Sync::handleTerminatedDownloadsDueUnknown(const SyncRow& row,
                                               const SyncPath& fullPath,
                                               const SyncDownload_inClient& downloadFile,
                                               ProgressingMonitor& monitor) const
{
    if (!downloadFile.terminatedReasonAlreadyKnown)
    {
        SYNC_verbose << syncname << "Download was terminated due to an unhandled reason (error: "
                     << downloadFile.mError << ") " << logTriplet(row, fullPath);
    }
    monitor.waitingCloud(
        fullPath.cloudPath,
        SyncStallEntry(SyncWaitReason::DownloadIssue,
                       true,
                       true,
                       {downloadFile.h, fullPath.cloudPath, PathProblem::UnknownDownloadIssue},
                       {},
                       {},
                       {}));
    assert(false && "Unhandled situation! Investigate the reason and handle it properly");
    const bool cloudNodeHasChanged = row.cloudNode && row.cloudNode->handle != downloadFile.h;
    const bool localFileIsNewer = row.fsNode && row.cloudNode &&
                                  row.fsNode->fingerprint.mtime > row.cloudNode->fingerprint.mtime;
    const bool executeRestOfSyncItem = cloudNodeHasChanged || localFileIsNewer;
    return executeRestOfSyncItem;
}

struct DifferentValueDetector_nodetype
{
    nodetype_t value = TYPE_UNKNOWN;

    // returns false if any different values are detected
    bool combine(nodetype_t v)
    {
        if (value == TYPE_UNKNOWN)
        {
            value = v;
            return true;
        }
        else if (v == TYPE_UNKNOWN)
        {
            return true;
        }
        else return v == value;
    }
};

bool Sync::syncItem(SyncRow& row, SyncRow& parentRow, SyncPath& fullPath, PerFolderLogSummaryCounts& pflsc)
{
    CodeCounter::ScopeTimer rst(syncs.mClient.performanceStats.syncItem);

    assert(syncs.onSyncThread());

    if (row.syncNode && row.fsNode &&
       (row.fsNode->type == TYPE_UNKNOWN || row.fsNode->fsid == UNDEF ||
       (row.fsNode->type == FILENODE && !row.fsNode->fingerprint.isvalid)))
    {
        SYNC_verbose_timed << "File lost permissions and we can't identify or fingerprint it anymore: "
                           << logTriplet(row, fullPath);

        ProgressingMonitor monitor(*this, row, fullPath);
        monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
            SyncWaitReason::FileIssue, false, false,
            {},
            {},
            {fullPath.localPath, PathProblem::CannotFingerprintFile},
            {}));

        return false;
    }

    if (row.isNoName())
    {
        LOG_debug << syncname
                  << "[syncItem] No name triplets here. "
                  << "Excluding this triplet from sync for now. "
                  << logTriplet(row, fullPath);

        return true;
    }

    // Check for files vs folders,  we can't upload a file over a folder etc.
    // Turns out we need to let moves be detected first, hence this block is moved from
    // there, otherwise a delete of a moved node could happen
    // Hence this block is moved from syncItem_checkMoves
    DifferentValueDetector_nodetype typeDiffDetect;
    if ((row.cloudNode && !typeDiffDetect.combine(row.cloudNode->type)) ||
        (row.syncNode && !typeDiffDetect.combine(row.syncNode->type)) ||
        (row.fsNode && !typeDiffDetect.combine(row.fsNode->type)))
    {

        if ((row.cloudNode && row.cloudNode->type == TYPE_DONOTSYNC) ||
            (row.fsNode && row.fsNode->type == TYPE_DONOTSYNC))
        {
            // don't do anything for these- eg. files auto generated by win/mac filesystem browsers
            // Note that historic syncs may have many of these in the cloud already
            return true;
        }

        SYNC_verbose << syncname << "File vs folder detected in this row: " << fullPath.localPath;

        // we already know these are not move-involved.  So, allow resetting the type of the syncNode
        if (row.syncNode)
        {
            nodetype_t resetType = TYPE_UNKNOWN;

            if (!row.fsNode && row.cloudNode)
            {
                SYNC_verbose << syncname << "Resetting sync node type for no fsNode: " << row.cloudNode->type << fullPath.localPath;
                resetType = row.cloudNode->type;
            }

            if (!row.cloudNode && row.fsNode)
            {
                SYNC_verbose << syncname << "Resetting sync node type for no cloudNode: " << row.fsNode->type << fullPath.localPath;
                resetType = row.fsNode->type;
            }

            if (row.cloudNode && row.fsNode && row.cloudNode->type == row.fsNode->type)
            {
                SYNC_verbose << syncname << "Resetting sync node type to cloud/fs type: " << row.cloudNode->type << fullPath.localPath;
                resetType = row.cloudNode->type;
            }

            if (resetType != TYPE_UNKNOWN)
            {
                row.syncNode->type = resetType;
                row.syncNode->setSyncedFsid(UNDEF, syncs.localnodeBySyncedFsid, row.syncNode->localname, row.syncNode->cloneShortname());
                row.syncNode->setSyncedNodeHandle(NodeHandle());
                statecacheadd(row.syncNode);
                return false;
            }
        }

        bool cloudSideChange = row.cloudNode && row.syncNode && row.cloudNode->type != row.syncNode->type;

        ProgressingMonitor monitor(*this, row, fullPath);
        monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
            SyncWaitReason::FolderMatchedAgainstFile, true, cloudSideChange,
            {row.cloudHandleOpt(), fullPath.cloudPath}, {},
            {fullPath.localPath}, {}));

        return false;
    }


    unsigned confirmDeleteCount = 0;
    if (row.syncNode)
    {
        // reset the count pre-emptively in case we don't choose SRT_XSX
        confirmDeleteCount = row.syncNode->confirmDeleteCount;
        if (confirmDeleteCount > 0)
        {
            row.syncNode->confirmDeleteCount = 0;
        }

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
            // Move pending?
            if (!s->rare().movePendingFrom.expired())
            {
                // Don't do anything until the move has completed.
                return false;
            }

            // Move in progress?
            if (auto& moveFromHere = s->rare().moveFromHere)
            {
                if (s->rare().moveToHere == moveFromHere &&
                    (moveFromHere->succeeded || moveFromHere->failed))
                {
                    // rename of this node (case insensitive but case changed)?
                    moveFromHere.reset();
                    s->rare().moveToHere.reset();
                    s->updateMoveInvolvement();
                }
                else if (moveFromHere->failed || moveFromHere->syncCodeProcessedResult)
                {
                    // Move's completed.
                    moveFromHere.reset();
                    s->updateMoveInvolvement();
                }
                else if (moveFromHere.use_count() == 1)
                {
                    SYNC_verbose << "Removing orphaned moveFromHere pointer at: " << logTriplet(row, fullPath);
                    moveFromHere.reset();
                    s->updateMoveInvolvement();
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
                if (s->rare().moveToHere)
                {
                    assert(!s->rare().moveToHere->failed);
                    assert(!s->rare().moveToHere->syncCodeProcessedResult);
                    assert(s->rare().moveToHere->succeeded);

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

            // ignore files cannot be ignored
            assert(!s->isIgnoreFile());

            // Let transfers complete.
            removable &= !s->transferSP;

            // Keep the node (as ignored) but purge the children
            if (removable)
            {
                // Extra sanity.
                assert(!s->rareRO().moveFromHere);
                assert(!s->rareRO().moveToHere);

                if (!s->children.empty())
                {
                    LOG_debug << syncname << "syncItem removing child LocalNodes from excluded " << s->getLocalPath();
                    vector<LocalNode*> cs;
                    cs.resize(s->children.size());
                    for (auto& i : s->children)
                    {
                        cs.push_back(i.second);
                    }
                    for (auto p : cs)
                    {
                        delete p;
                    }
                }


            }
            return true; // consider it synced (ie, do not revisit)
        }
    }

    // Check blocked status.  Todo:  figure out use blocked flag clearing
    if (!row.syncNode && row.fsNode && (
        row.fsNode->isBlocked || row.fsNode->type == TYPE_UNKNOWN) &&
        parentRow.syncNode->exclusionState(row.fsNode->localname, TYPE_UNKNOWN, -1) == ES_INCLUDED)
    {
        // so that we can checkForScanBlocked() immediately below
        if (!makeSyncNode_fromFS(row, parentRow, fullPath, false))
        {
            row.suppressRecursion = true;
            row.itemProcessed = true;
            return false;
        }
    }
    if (row.syncNode &&
        row.syncNode->checkForScanBlocked(row.fsNode))
    {
        row.suppressRecursion = true;
        row.itemProcessed = true;
        return false;
    }

    if (row.syncNode && row.syncNode->transferSP)
    {
        if (!syncItem_checkDownloadCompletion(row, parentRow, fullPath))
        {
            return false;
        }
    }

    auto rowType = row.type();

    if (row.syncNode && rowType != SRT_XSX &&
        row.syncNode->localFSCannotStoreThisName)
    {
        LocalPath fsReportPath;
        if (!row.syncNode->rareRO().localFSRenamedToThisName.empty())
        {
            fsReportPath = fullPath.localPath.parentPath();
            fsReportPath.appendWithSeparator(row.syncNode->rareRO().localFSRenamedToThisName, true);
        }

        ProgressingMonitor monitor(*this, row, fullPath);
        monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
            SyncWaitReason::FileIssue, false, false,
            {row.cloudHandleOpt(), fullPath.cloudPath, PathProblem::FilesystemCannotStoreThisName},
            {},
            {fsReportPath, PathProblem::FilesystemCannotStoreThisName},
            {}));

        return false;
    }

    switch (rowType)
    {
    case SRT_CSF:
    {
        CodeCounter::ScopeTimer csfTime(syncs.mClient.performanceStats.syncItemCSF);

        // Are we part of a move and was our source a download-in-progress?
        resolve_checkMoveDownloadComplete(row, fullPath);

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

            return resolve_rowMatched(row, parentRow, fullPath, pflsc);
        }

        if (cloudEqual)
        {
            // filesystem changed, put the change
            return resolve_upsync(row, parentRow, fullPath, pflsc);
        }

        if (fsEqual)
        {
            if (isBackup())
            {
                LOG_warn << "CSF with cloud node change and this is a BACKUP!"
                         << " Local file will be upsynced to fix the mismatched cloud node."
                         << " Triplet: " << logTriplet(row, fullPath);
                assert(false && "CSF with cloud node change should not happen for a backup!");
            }
            else
            {
                // cloud has changed, get the change
                return resolve_downsync(row, parentRow, fullPath, true, pflsc);
            }
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

        if (isBackup())
        {
            // for backups, we only change the cloud
            LOG_warn << "CSF for a BACKUP with CloudNode != SyncNode != FSNode -> resolve upsync "
                        "to avoid user intervention"
                     << " " << logTriplet(row, fullPath);
            return resolve_upsync(row, parentRow, fullPath, pflsc);
        }

        // both changed, so we can't decide without the user's help
        return resolve_userIntervention(row, fullPath);
    }
    case SRT_XSF:
    {
        CodeCounter::ScopeTimer xsfTime(syncs.mClient.performanceStats.syncItemXSF);

        if (row.syncNode->type == TYPE_DONOTSYNC ||
            row.isLocalOnlyIgnoreFile() ||
            row.isNoName())
        {
            // we do not upload do-not-sync files (eg. system+hidden, on windows)
            return true;
        }

        if (!row.syncNode->syncedCloudNodeHandle.isUndef()
            && row.syncNode->fsid_lastSynced != UNDEF
            && row.syncNode->fsid_lastSynced == row.fsNode->fsid
            // on disk, content could be changed without an fsid change
            && syncEqual(*row.fsNode, *row.syncNode))
        {
            if (isBackup())
            {
                // for backups, we only change the cloud
                LOG_warn << "XSF with cloud node gone when this item was synced before and this is "
                            "a BACKUP!"
                         << " This will result on the backup being disabled."
                         << " Triplet: " << logTriplet(row, fullPath);
                assert(false && "XSF - cloud item not present for a previously "
                                "synced item should not happen for a backup!");
            }
            else
            {
                // used to be fully synced and the fs side still has that version
                // remove in the fs (if not part of a move)
                return resolve_cloudNodeGone(row, parentRow, fullPath);
            }
        }

        // either
        //  - cloud item did not exist before; upsync
        //  - the fs item has changed too; upsync (this lets users recover from the both sides changed state - user deletes the one they don't want anymore)
        return resolve_upsync(row, parentRow, fullPath, pflsc);
    }
    case SRT_CSX:
    {
        CodeCounter::ScopeTimer csxTime(syncs.mClient.performanceStats.syncItemCSX);

        // local item not present
        if (isBackup())
        {
            // make the cloud the same as local
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
        else if (row.syncNode->fsid_lastSynced == UNDEF)
        {
            // fs item did not exist before; downsync
            return resolve_downsync(row, parentRow, fullPath, false, pflsc);
        }
        else if (row.syncNode->syncedCloudNodeHandle != row.cloudNode->handle)
        {
            // the cloud item has changed too; downsync (this lets users recover from both sides changed state - user deletes the one they don't want anymore)
            return resolve_downsync(row, parentRow, fullPath, false, pflsc);
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
            assert(false && "This case is not expected to happen");

            return false;
        }
    }
    case SRT_XSX:
    {
        CodeCounter::ScopeTimer xsxTime(syncs.mClient.performanceStats.syncItemXSX);

        // local and cloud disappeared; remove sync item also
        return resolve_delSyncNode(row, parentRow, fullPath, confirmDeleteCount);
    }
    case SRT_CXF:
    {
        CodeCounter::ScopeTimer cxfTime(syncs.mClient.performanceStats.syncItemCXF);

        // we have to check both, due to the size parameter
        auto cloudside = parentRow.exclusionState(row.fsNode->localname, row.fsNode->type, row.fsNode->fingerprint.size);
        auto localside = parentRow.exclusionState(row.fsNode->localname, row.cloudNode->type, row.cloudNode->fingerprint.size); // use fsNode's name for convenience, size is what matters

        if (cloudside == ES_EXCLUDED || localside == ES_EXCLUDED)
        {
            LOG_verbose << "CXF case is excluded by size: " << row.fsNode->fingerprint.size << " vs " << row.cloudNode->fingerprint.size << logTriplet(row, fullPath);
            return true;
        }

        if (cloudside != ES_INCLUDED || localside != ES_INCLUDED)
        {
            LOG_verbose << "Exclusion state unknown, come back later: " << int(cloudside) << " vs " << int(localside) << logTriplet(row, fullPath);
            return false;
        }

        // Item exists locally and remotely but we haven't synced them previously
        // If they are equal then join them with a Localnode. Othewise report to user.
        // The original algorithm would compare mtime, and if that was equal then size/crc

        if (bool isSyncEqual = syncEqual(*row.cloudNode, *row.fsNode); isSyncEqual || isBackup())
        {
            // In both cases we create the sync node from local to do the upsync later
            return resolve_makeSyncNode_fromFS(row, parentRow, fullPath, false);
        }
        else
        {
            return resolve_userIntervention(row, fullPath);
        }
    }
    case SRT_XXF:
    {
        CodeCounter::ScopeTimer xxfTime(syncs.mClient.performanceStats.syncItemXXF);

        // Don't create a sync node for this file unless we know that it's included.
        if (parentRow.exclusionState(*row.fsNode) != ES_INCLUDED)
            return true;

        // Item exists locally only. Check if it was moved/renamed here, or Create
        // If creating, next run through will upload it
        return resolve_makeSyncNode_fromFS(row, parentRow, fullPath, false);
    }
    case SRT_CXX:
    {
        CodeCounter::ScopeTimer cxxTime(syncs.mClient.performanceStats.syncItemCXX);

        // Don't create sync nodes unless we know the row is included.
        if (parentRow.exclusionState(*row.cloudNode) != ES_INCLUDED)
            return true;

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

bool Sync::resolve_checkMoveDownloadComplete(SyncRow& row, SyncPath& fullPath)
{
    // Convenience.
    auto target = row.syncNode;

    // Are we part of an ongoing move?
    auto movePtr = target->rareRO().moveToHere;

    // No part of an ongoing move.
    if (!movePtr)
        return false;

    // Are we still associated with the move source?
    const NodeMatchByFSIDAttributes moveNodeAttributes{movePtr->sourceType,
                                                       *movePtr->sourceFsfp,
                                                       cloudRootOwningUser,
                                                       movePtr->sourceFingerprint};
    const auto [sourceExclusionUnknown, source] =
        syncs.findLocalNodeBySyncedFsid(movePtr->sourceFsid,
                                        moveNodeAttributes,
                                        fullPath.localPath);

    if (sourceExclusionUnknown)
    {
        LOG_debug << "In resolve_checkMoveDownloadComplete, download source's exclusion state is unknown.  at: " << logTriplet(row, fullPath);
    }

    // No longer associated with the move source.
    if (!source)
        return false;

    // Sanity check.
    if (source->rareRO().moveFromHere != movePtr)
    {
        // This can happen if we see the user moved local node N to node P.
        // We start this corresponding cloud move.  But in the meantime,
        // the user has locally moved N again
        LOG_debug << "Move source does not match the movePtr anymore. (" << source->getLocalPath() << ") at: " << logTriplet(row, fullPath);
        row.syncNode->rare().moveToHere->syncCodeProcessedResult = true;  // a visit to the source node with the corresponding moveFromHere ptr will now remove it
        row.syncNode->rare().moveToHere.reset();
        row.syncNode->updateMoveInvolvement();
        return false;
    }

    // Is the source part of an ongoing download?
    auto download = std::dynamic_pointer_cast<SyncDownload_inClient>(source->transferSP);

    // Not part of an ongoing download.
    if (!download)
        return false;

    LOG_debug << "Completing move of in-progress download: "
              << logTriplet(row, fullPath);

    // Consider the move complete.
    source->moveContentTo(target, fullPath.localPath, true);
    source->setSyncedFsid(UNDEF, syncs.localnodeBySyncedFsid, source->localname, source->cloneShortname());
    source->setSyncedNodeHandle(NodeHandle());
    source->sync->statecacheadd(source);

    source->rare().moveFromHere->syncCodeProcessedResult = true;
    source->rare().moveFromHere.reset();
    source->trimRareFields();
    source->updateMoveInvolvement();

    target->rare().moveToHere->syncCodeProcessedResult = true;
    target->rare().moveToHere.reset();
    target->trimRareFields();
    target->updateMoveInvolvement();

    // Consider us synced with the local disk.
    target->setSyncedFsid(movePtr->sourceFsid, syncs.localnodeBySyncedFsid, row.fsNode->localname, row.fsNode->cloneShortname());
    target->syncedFingerprint = movePtr->sourceFingerprint;
    target->sync->statecacheadd(target);

    // Terminate the transfer if we're not a match with the local disk.
    if (row.fsNode->fsid != movePtr->sourceFsid
        || row.fsNode->fingerprint != movePtr->sourceFingerprint)
    {
        LOG_debug << "Move-target no longer matches move-source: "
                  << logTriplet(row, fullPath);

        target->resetTransfer(nullptr);
    }

    // We should now be good to go.
    return true;
}

bool Sync::resolve_checkMoveComplete(SyncRow& row, SyncRow& /*parentRow*/, SyncPath& fullPath)
{
    // Confirm that the move details are the same as recorded (LocalNodes may have changed or been deleted by now, etc.
    auto movePtr = row.syncNode->rare().moveToHere;

    LOG_debug << syncname << "Checking move source/target by fsid " << toHandle(movePtr->sourceFsid);

    const NodeMatchByFSIDAttributes moveNodeAttributes{movePtr->sourceType,
                                                       *movePtr->sourceFsfp,
                                                       cloudRootOwningUser,
                                                       movePtr->sourceFingerprint};
    const auto [sourceExclusionUnknown, sourceSyncNode] =
        syncs.findLocalNodeBySyncedFsid(movePtr->sourceFsid,
                                        moveNodeAttributes,
                                        fullPath.localPath);

    if (sourceExclusionUnknown)
    {
        LOG_debug << "In resolve_checkMoveComplete, move source's exclusion state is unknown.  at: " << logTriplet(row, fullPath);
    }

    if (sourceSyncNode)
    {
        LOG_debug << syncname << "Sync cloud move/rename from : " << sourceSyncNode->getCloudPath(true) << " resolved here! " << logTriplet(row, fullPath);

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
        sourceSyncNode->updateMoveInvolvement();

        // If this node was repurposed for the move, rather than the normal case of creating a fresh one, we remove the old content if it was a folder
        // We have to do this after all processing of sourceSyncNode, in case the source was (through multiple operations) one of the subnodes about to be removed.
        // TODO: however, there is a risk of name collisions - probably we should use a multimap for LocalNode::children.
        for (auto& oldc : movePtr->priorChildrenToRemove)
        {
            for (auto& c : row.syncNode->children)
            {
                if (c.first == oldc.first && c.second == oldc.second)
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
    row.syncNode->updateMoveInvolvement();

    return sourceSyncNode != nullptr;
}

bool Sync::resolve_rowMatched(SyncRow& row, SyncRow& parentRow, SyncPath& fullPath, PerFolderLogSummaryCounts& pflsc)
{
    assert(syncs.onSyncThread());

    // these comparisons may need to be adjusted for UTF, escapes
    assert(row.syncNode->fsid_lastSynced != row.fsNode->fsid || 0 == compareUtf(row.syncNode->localname, true, row.fsNode->localname, true, mCaseInsensitive));
    assert(row.syncNode->fsid_lastSynced == row.fsNode->fsid || 0 == compareUtf(row.syncNode->localname, true, row.fsNode->localname, true, mCaseInsensitive));

    assert((!!row.syncNode->slocalname == !!row.fsNode->shortname) &&
            (!row.syncNode->slocalname ||
            (*row.syncNode->slocalname == *row.fsNode->shortname)));

    if (row.syncNode->fsid_lastSynced != row.fsNode->fsid ||
        row.syncNode->syncedCloudNodeHandle != row.cloudNode->handle ||
        row.syncNode->localname != row.fsNode->localname)
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

        if (mCaseInsensitive &&
            0 == compareUtf(row.syncNode->localname, true, row.fsNode->localname, true, true) &&
            0 != compareUtf(row.syncNode->localname, true, row.fsNode->localname, true, false))
        {
            SYNC_verbose << "Updating LocalNode localname case to match fs: " << row.fsNode->localname << " at " << logTriplet(row, fullPath);
        }

        row.syncNode->setSyncedFsid(row.fsNode->fsid, syncs.localnodeBySyncedFsid, row.fsNode->localname, row.fsNode->cloneShortname());
        row.syncNode->setSyncedNodeHandle(row.cloudNode->handle);

        row.syncNode->syncedFingerprint = row.fsNode->fingerprint;

        if (row.syncNode->transferSP)
        {
            LOG_debug << "Clearing transfer for matched row at " << logTriplet(row, fullPath);
            row.syncNode->resetTransfer(nullptr);
        }

        if (mCaseInsensitive && !row.syncNode->namesSynchronized &&
            0 == compareUtf(row.cloudNode->name, true, row.fsNode->localname, true, false))
        {
            // name is equal (taking escaping and case into account) so going forward, also propagate renames that only change case
            assert(row.fsNode->localname == row.syncNode->localname);
            row.syncNode->namesSynchronized = true;
        }

        statecacheadd(row.syncNode);
        ProgressingMonitor monitor(*this, row, fullPath); // not stalling
    }
    else
    {
        if (!pflsc.alreadySyncedCount)
        {
            // This line is too verbose when debugging large syncs.  Instead, report the already-synced count in the containing folder
            SYNC_verbose_timed << syncname << "Row was already synced" << logTriplet(row, fullPath);
        }
        pflsc.alreadySyncedCount += 1;
    }

    if (row.syncNode->type == FILENODE)
    {
        row.syncNode->scanAgain = TREE_RESOLVED;
        row.syncNode->checkMovesAgain = TREE_RESOLVED;
        row.syncNode->syncAgain = TREE_RESOLVED;
    }

    return true;
}

bool Sync::resolve_makeSyncNode_fromFS(SyncRow& row, SyncRow& parentRow, SyncPath& fullPath, bool considerSynced)
{
    makeSyncNode_fromFS(row, parentRow, fullPath, considerSynced);

    // the row is not in sync, so we return false
    // future visits will make more steps towards getting in sync
    return false;
}

bool Sync::makeSyncNode_fromFS(SyncRow& row, SyncRow& parentRow, SyncPath& fullPath, bool considerSynced)
{
    // this version of the function returns true/false depending on whether it was able to make the SyncNode.
    assert(syncs.onSyncThread());
    ProgressingMonitor monitor(*this, row, fullPath);

    if (row.fsNode->type == FILENODE && !row.fsNode->fingerprint.isvalid)
    {
        SYNC_verbose_timed << "We can't create a LocalNode yet without a FileFingerprint: "
                           << logTriplet(row, fullPath);

        // we couldn't get the file crc yet (opened by another proecess, etc)
        monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
            SyncWaitReason::FileIssue, false, false,
            {},
            {},
            {fullPath.localPath, PathProblem::CannotFingerprintFile},
            {}));

        return false;
    }

    // this really is a new node: add
    LOG_debug << syncname << "Creating LocalNode from FS with fsid " << toHandle(row.fsNode->fsid) << " at: " << fullPath.localPath << logTriplet(row, fullPath);

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

    if (row.syncNode->type > FILENODE)
    {
        row.syncNode->setScanAgain(false, true, true, 0);
    }

    statecacheadd(row.syncNode);

    // success making the LocalNode/SyncNode
    return true;
}

bool Sync::resolve_makeSyncNode_fromCloud(SyncRow& row, SyncRow& parentRow, SyncPath& fullPath, bool considerSynced)
{
    assert(syncs.onSyncThread());
    ProgressingMonitor monitor(*this, row, fullPath);

    SYNC_verbose << syncname << "Creating LocalNode from Cloud at: " << fullPath.cloudPath << logTriplet(row, fullPath);

    assert(row.syncNode == nullptr);
    row.syncNode = new LocalNode(this);

    if (row.cloudNode->type == FILENODE)
    {
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
        assert(row.cloudNode->fingerprint.isvalid);
        row.syncNode->setSyncedNodeHandle(row.cloudNode->handle);
    }
    if (row.syncNode->type > FILENODE)
    {
        row.syncNode->setSyncAgain(false, true, true);
    }
    statecacheadd(row.syncNode);
    row.syncNode->setSyncAgain(true, false, false);

    return false;
}

bool Sync::resolve_delSyncNode(SyncRow& row, SyncRow& parentRow, SyncPath& fullPath, unsigned deleteCounter)
{
    assert(syncs.onSyncThread());
    ProgressingMonitor monitor(*this, row, fullPath);

    if (row.syncNode->hasRare())
    {
        // We should never reach this function if pendingFrom is live.
        assert(row.syncNode->rareRO().movePendingFrom.expired());

        if (row.syncNode->rare().moveToHere &&
            row.syncNode->rare().moveToHere->inProgress())
        {
            SYNC_verbose_timed << syncname << "Not deleting with still-moving/renaming source node to:"
                               << logTriplet(row, fullPath);
            return false;
        }

        if (row.syncNode->rare().moveFromHere &&
            !row.syncNode->rare().moveFromHere->syncCodeProcessedResult)
        {
            SYNC_verbose_timed << syncname << "Not deleting still-moving/renaming source node from:"
                               << logTriplet(row, fullPath);

            monitor.waitingCloud(fullPath.cloudPath, SyncStallEntry(
                SyncWaitReason::MoveOrRenameCannotOccur, false, false,
                {row.cloudHandleOpt(), fullPath.cloudPath},
                {NodeHandle(), "", PathProblem::DestinationPathInUnresolvedArea},
                {fullPath.localPath},
                {LocalPath(), PathProblem::DestinationPathInUnresolvedArea}));

            return false;
        }
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


    if (auto u = std::dynamic_pointer_cast<SyncUpload_inClient>(row.syncNode->transferSP))
    {
        if (u->putnodesStarted && !u->wasPutnodesCompleted)
        {
            // if we delete the LocalNode now, then the appearance of the uploaded file will cause a download which would be incorrect
            // if it hadn't started putnodes, it would be ok to delete (which should also cancel the transfer)
            SYNC_verbose << "This LocalNode is a candidate for deletion, but was also uploading a file and putnodes is in progress." << logTriplet(row, fullPath);
            return false;
        }
    }

    // setting on the first pass, or restoring on subsequent (part of the auto-reset if it's no longer routed to _delSyncNode)
    row.syncNode->confirmDeleteCount = 1;

    if (deleteCounter == 0)
    {
        // make sure we've done a full pass including the rest of this iteration over the node trees,
        // to see if there is any other relevant info available (such as, this is a move rather than delete)
        SYNC_verbose << "This LocalNode is a candidate for deletion, we'll confirm on the next pass." << logTriplet(row, fullPath);

        // whenever this node is visited and we don't call resolve_delSyncNode(), this is reset to 0
        // this prevents flop-flopping into and out of stall state if this is the only problem node
        return false;
    }

    if (row.syncNode->moveAppliedToLocal)
    {
        // we detected a cloud move from here, and applied the corresponding local move.  Ok to delete this LocalNode.
        SYNC_verbose << syncname << "Deleting Localnode (moveAppliedToLocal)" << logTriplet(row, fullPath);
    }
    else if (row.syncNode->deletedFS)
    {
        // we detected a local deletion here, and applied that deletion in the cloud too.  Ok to delete this LocalNode.
        SYNC_verbose << syncname << "Deleting Localnode (deletedFS)" << logTriplet(row, fullPath);
    }
    else if (mMovesWereComplete)
    {
        // Since moves are complete, we can remove this LocalNode now, it can't be part of any move anymore
        // Up to that point, we should not remove it or we won't be able to detect clashing moves of the same node.
        SYNC_verbose << syncname << "Deleting Localnode (movesWereComplete)" << logTriplet(row, fullPath);
    }
    else
    {
        // is the old Node or FSNode somewhere else now?  If we can't find them, then this node can't have been moved
        bool fsNodeIsElsewhere = false;
        bool cloudNodeIsElsewhere = false;
        string fsElsewhereLocation;

        if (!mScanningWasComplete)
        {
            fsNodeIsElsewhere = true;
        }
        else if (row.syncNode->fsid_lastSynced != UNDEF)
        {
            const NodeMatchByFSIDAttributes syncNodeAttributes{row.syncNode->type,
                                                               fsfp(),
                                                               cloudRootOwningUser,
                                                               row.syncNode->syncedFingerprint};
            const auto [sourceFsidExclusionUnknown, fsElsewhere] =
                syncs.findLocalNodeByScannedFsid(row.syncNode->fsid_lastSynced,
                                                 syncNodeAttributes,
                                                 fullPath.localPath);
            if (fsElsewhere)
            {
                fsNodeIsElsewhere = true;
                fsElsewhereLocation = fsElsewhere->getCloudPath(false);
                SYNC_verbose << "LocalNode considered for deletion, but fsNode is elsewhere: "
                             << fsElsewhere->getLocalPath() << logTriplet(row, fullPath);
            }
            else if (sourceFsidExclusionUnknown)
            {
                fsNodeIsElsewhere = true;
                SYNC_verbose << "LocalNode considered for deletion, but fsNode is elsewhere with "
                                "unknown exclusion state: "
                             << logTriplet(row, fullPath);
            }
        }

        CloudNode cloudNode;
        string cloudNodePath;
        bool isInTrash = false;
        bool nodeIsInActiveSync = false, nodeIsDefinitelyExcluded = false;
        bool found = syncs.lookupCloudNode(row.syncNode->syncedCloudNodeHandle, cloudNode, &cloudNodePath, &isInTrash, &nodeIsInActiveSync, &nodeIsDefinitelyExcluded, nullptr, Syncs::EXACT_VERSION);
        if (found && !isInTrash && nodeIsInActiveSync && !nodeIsDefinitelyExcluded)
        {
            cloudNodeIsElsewhere = true;
            SYNC_verbose_timed << "LocalNode considered for deletion, but cloud Node is elsewhere: " << cloudNodePath
                               << logTriplet(row, fullPath);
        }

        if (fsNodeIsElsewhere && cloudNodeIsElsewhere && fsElsewhereLocation != cloudNodePath)
        {
            SYNC_verbose_timed << "LocalNode considered for deletion, but it is the source of moves to different locations: " << cloudNodePath
                               << " " << fsElsewhereLocation
                               << logTriplet(row, fullPath);

            // Moves are not complete yet so we can't be sure this node is not the source of two
            // inconsistent moves, one local and one remote.  Keep for now until all moves are resolved.
            monitor.waitingCloud(fullPath.cloudPath, SyncStallEntry(
                SyncWaitReason::DeleteWaitingOnMoves, false, false,
                {row.cloudHandleOpt(), fullPath.cloudPath},
                {},
                {fullPath.localPath},
                {LocalPath()}));

            return false;
        }
    }

    if (row.syncNode->deletedFS)
    {
        if (row.syncNode->type == FOLDERNODE)
        {
            LOG_debug << syncname << "Sync - local folder deletion detected: " << fullPath.localPath;
        }
        else
        {
            LOG_debug << syncname << "Sync - local file deletion detected: " << fullPath.localPath;
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

bool Sync::resolve_upsync(SyncRow& row, SyncRow& parentRow, SyncPath& fullPath, PerFolderLogSummaryCounts& pflsc)
{
    assert(syncs.onSyncThread());
    ProgressingMonitor monitor(*this, row, fullPath);

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

    // Convenience.
    auto deferred = [&](const char* message,
                        bool (Syncs::*predicate)(const LocalPath&) const,
                        PathProblem problem) {
        // Activity isn't deferred.
        if (!(syncs.*predicate)(fullPath.localPath))
            return false;

        // Let debuggers know why weren't not performing the activity.
        LOG_debug << syncname
                  << message
                  << " "
                  << fullPath.localPath
                  << logTriplet(row, fullPath);

        // Try and attempt the action later.
        row.syncNode->setSyncAgain(false, true, false);

        // Emit a special stall for observers to detect.
        monitor.waitingLocal(fullPath.localPath,
                             SyncStallEntry(SyncWaitReason::UploadIssue,
                                            false,
                                            false,
                                            {NodeHandle(), fullPath.cloudPath, problem},
                                            {},
                                            {fullPath.localPath, problem},
                                            {}));

        // Activity's been deferred.
        return true;
    }; // deferred

    if (row.fsNode->type == FILENODE)
    {
        // upload the file if we're not already uploading it
        if (!row.syncNode->transferResetUnlessMatched(PUT, row.fsNode->fingerprint))
        {
            // if we are in the putnodes stage of a transfer though, then
            // wait for that to finish and then re-evaluate
            return false;
        }

        shared_ptr<SyncUpload_inClient> existingUpload = std::dynamic_pointer_cast<SyncUpload_inClient>(row.syncNode->transferSP);

        if (existingUpload && !existingUpload->putnodesStarted)
        {
            // keep the name and target folder details current:

            // if it's just a case change in a case insensitive name, use the updated uppercase/lowercase
            bool onlyCaseChanged = mCaseInsensitive && row.cloudNode &&
                  0 == compareUtf(row.cloudNode->name, true, row.fsNode->localname, true, true);

            // if we were already matched with a name that is not exactly the same as toName(), keep using it
            string nodeName = !row.cloudNode || onlyCaseChanged
                                ? row.fsNode->localname.toName(*syncs.fsaccess)
                                : row.cloudNode->name;

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
                LOG_debug << syncname << "Sync - local file addition detected: " << fullPath.localPath;

                //if (checkIfFileIsChanging(*row.fsNode, fullPath.localPath))
                //{
                //    LOG_debug << syncname
                //              << "Waiting for file to stabilize before uploading: "
                //              << fullPath.localPath.toPath();

                //    monitor.waitingLocal(fullPath.localPath,
                //                         LocalPath(),
                //                         string(),
                //                         SyncWaitReason::WaitingForFileToStopChanging);

                //    return false;
                //}

                // Ask the controller if we should defer uploading this file.
                if (deferred("Upload deferred by controller",
                             &Syncs::deferUpload,
                             PathProblem::UploadDeferredByController))
                    return false;

                LOG_debug << syncname << "Uploading file " << fullPath.localPath << logTriplet(row, fullPath);
                assert(row.syncNode->scannedFingerprint.isvalid); // LocalNodes for files always have a valid fingerprint
                assert(row.syncNode->scannedFingerprint == row.fsNode->fingerprint);

                // if it's just a case change in a case insensitive name, use the updated uppercase/lowercase
                bool onlyCaseChanged = mCaseInsensitive && row.cloudNode &&
                    0 == compareUtf(row.cloudNode->name, true, row.fsNode->localname, true, true);

                // if we were already matched with a name that is not exactly the same as toName(), keep using it
                string nodeName = !row.cloudNode || onlyCaseChanged
                    ? row.fsNode->localname.toName(*syncs.fsaccess)
                    : row.cloudNode->name;

                auto upload = std::make_shared<SyncUpload_inClient>(parentRow.cloudNode->handle,
                    fullPath.localPath, nodeName, row.fsNode->fingerprint, threadSafeState,
                    row.fsNode->fsid, row.fsNode->localname, inshare);

                const NodeHandle displaceHandle =
                    row.cloudNode ? row.cloudNode->handle : NodeHandle();
                const bool uploadSentToClientQueue = row.syncNode->queueClientUpload(
                    upload,
                    UseLocalVersioningFlag,
                    nodeName == ".megaignore",
                    displaceHandle); // we'll take care of versioning ourselves ( we take over the
                                     // putnodes step below)

                if (uploadSentToClientQueue)
                {
                    LOG_debug << syncname << "Sync - sending file " << fullPath.localPath;
                }
                else
                {
                    LOG_debug << syncname
                              << "[UploadThrottle] Sync - file exceeded the limit of uploads "
                                 "without throttling ("
                              << syncs.maxUploadsBeforeThrottle() << ")"
                              << ". Added to delayed queue: " << fullPath.localPath;
                }
            }
            else
            {
                SYNC_verbose_timed << syncname << "Parent cloud folder to upload to doesn't exist yet"
                                   << logTriplet(row, fullPath);
                row.syncNode->setSyncAgain(true, false, false);

                monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
                    SyncWaitReason::UploadIssue, false, false,
                    {NodeHandle(), fullPath.cloudPath, PathProblem::ParentFolderDoesNotExist},
                    {},
                    {fullPath.localPath},
                    {}));

            }
        }
        else if (existingUpload->wasCompleted && !existingUpload->putnodesStarted)
        {
            // We issue putnodes from the sync thread like this because localnodes may have moved/renamed in the meantime
            // And consider that the old target parent node may not even exist anymore

            // Should we defer the putnodes until later?
            if (deferred("Putnode deferred by controller",
                         &Syncs::deferPutnode,
                         PathProblem::PutnodeDeferredByController))
                return false;

            existingUpload->putnodesStarted = true;

            SYNC_verbose << syncname << "Queueing putnodes for completed upload" << logTriplet(row, fullPath);

            threadSafeState->addExpectedUpload(parentRow.cloudNode->handle, existingUpload->name, existingUpload);

            NodeHandle displaceHandle = row.cloudNode ? row.cloudNode->handle : NodeHandle();
            auto isInshare = inshare;

            std::function<void(MegaClient&)> signalPutnodesBegin;

            if (parentRow.cloudNode &&
                existingUpload->h != parentRow.cloudNode->handle)
            {
                LOG_verbose << "Adjusting the target folder for moved upload, was " << existingUpload->h << " now " << parentRow.cloudNode->handle;
                existingUpload->h = parentRow.cloudNode->handle;
            }

            bool canChangeVault = threadSafeState->mCanChangeVault;
            syncs.queueClient(
                [existingUpload, displaceHandle, isInshare, signalPutnodesBegin, canChangeVault](
                    MegaClient& mc,
                    TransferDbCommitter&)
                {
                    std::shared_ptr<Node> displaceNode = mc.nodeByHandle(displaceHandle);
                    if (displaceNode && mc.versions_disabled)
                    {
                        MegaClient* c = &mc;
                        mc.movetosyncdebris(displaceNode.get(), isInshare,

                            // after the old node is out of the way, we wll putnodes
                            [c, existingUpload, signalPutnodesBegin](NodeHandle, Error){
                                if (signalPutnodesBegin)
                                    signalPutnodesBegin(*c);

                                existingUpload->sendPutnodesOfUpload(c, NodeHandle());
                            }, canChangeVault);

                        // putnodes will be executed after or simultaneous with the
                        // move to sync debris
                        return;
                    }

                    // the case where we are making versions, or not displacing something with the same name
                    if (signalPutnodesBegin)
                        signalPutnodesBegin(mc);

                    existingUpload->sendPutnodesOfUpload(&mc, displaceNode ? displaceNode->nodeHandle() : NodeHandle());
                });
        }
        else if (existingUpload->wasPutnodesCompleted)
        {
            // Only reset the transfer if the putnode's completion hasn't been deferred.
            // This is necessary to prevent an infinite upload-loop in some cases.
            if (syncs.deferPutnodeCompletion(fullPath.localPath))
                return false;

            assert(!existingUpload->putnodesFailed ||
                   existingUpload->putnodesResultHandle.isUndef());
            if (existingUpload->putnodesFailed)
            {
                SYNC_verbose << syncname
                             << "Upload from here failed, reset for reevaluation in resolve_upsync."
                             << logTriplet(row, fullPath);

                row.syncNode->bypassThrottlingNextTime(syncs.maxUploadsBeforeThrottle());
            }
            else
            {
                row.syncNode->increaseUploadCounter();
                SYNC_verbose
                    << syncname
                    << "Putnodes complete. Detaching upload in resolve_upsync. [Num uploads: "
                    << row.syncNode->uploadCounter() << "]" << logTriplet(row, fullPath);
            }

            row.syncNode->resetTransfer(nullptr);
            return false; // revisit in case of further changes
        }
        else if (existingUpload->putnodesStarted)
        {
            SYNC_verbose << syncname << "Upload's putnodes already in progress" << logTriplet(row, fullPath);
        }
        else
        {
            if (!pflsc.alreadyUploadingCount)
            {
                if (existingUpload->wasStarted)
                {
                    SYNC_verbose << syncname << "Upload already in progress"
                                 << logTriplet(row, fullPath);
                }
                else
                {
                    SYNC_verbose_timed << syncname
                                       << "Upload already waiting in the throttling queue"
                                       << logTriplet(row, fullPath);
                }
            }
            pflsc.alreadyUploadingCount += 1;
        }
    }
    else if (row.fsNode->type == FOLDERNODE)
    {
        if (row.syncNode->hasRare() && row.syncNode->rare().createFolderHere)
        {
            SYNC_verbose << syncname << "Create cloud folder already in progress" << logTriplet(row, fullPath);
        }
        else
        {
            if (parentRow.cloudNode)
            {
                // there can't be a matching cloud node in this row (for folders), so just toName() is correct
                string foldername = row.syncNode->toName_of_localname;

                LOG_verbose << syncname << "Creating cloud node for: " << fullPath.localPath << " as " << foldername << logTriplet(row, fullPath);
                // while the operation is in progress sync() will skip over the parent folder

                bool canChangeVault = threadSafeState->mCanChangeVault;
                NodeHandle targethandle = parentRow.cloudNode->handle;
                auto createFolderPtr = std::make_shared<LocalNode::RareFields::CreateFolderInProgress>(row.fsNode->fsid);
                row.syncNode->rare().createFolderHere = createFolderPtr;
                syncs.queueClient(
                    [foldername, targethandle, createFolderPtr, canChangeVault](
                        MegaClient& mc,
                        TransferDbCommitter&)
                    {
                        vector<NewNode> nn(1);
                        mc.putnodes_prepareOneFolder(&nn[0], foldername, canChangeVault);
                        mc.putnodes(targethandle,
                                    NoVersioning,
                                    std::move(nn),
                                    nullptr,
                                    0,
                                    canChangeVault,
                                    {}, // customerIpPort
                                    [createFolderPtr](const Error& e,
                                                      targettype_t,
                                                      vector<NewNode>& v,
                                                      bool /*targetOverride*/,
                                                      int /*tag*/,
                                                      const map<string, string>& /*fileHandles*/)
                                    {
                                        if (!e && !v.empty())
                                        {
                                            createFolderPtr->succeededHandle.set6byte(
                                                v[0].mAddedHandle);
                                        }
                                        if (createFolderPtr->succeededHandle.isUndef())
                                        {
                                            createFolderPtr->failed = true;
                                        }
                                    });
                    });
            }
            else
            {
                SYNC_verbose << "Delay creating cloud node until parent cloud node exists: " << fullPath.localPath << logTriplet(row, fullPath);
                row.syncNode->setSyncAgain(true, false, false);

                monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
                    SyncWaitReason::CannotCreateFolder, false, false,
                    {NodeHandle(), fullPath.cloudPath, PathProblem::ParentFolderDoesNotExist},
                    {},
                    {fullPath.localPath},
                    {}));
            }
        }
        // we may not see some moves/renames until the entire folder structure is created.
        row.syncNode->setCheckMovesAgain(true, false, false);     // todo: double check - might not be needed for the wait case? might cause a stall?
    }
    else if (row.fsNode->type == TYPE_DONOTSYNC)
    {
        // This is the sort of thing that we should not sync, but not complain about either
        // consider it synced.
        monitor.noResult();
        return true;
    }
    else // unknown/special
    {
        monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
            SyncWaitReason::FileIssue, false, false,
            {NodeHandle(), fullPath.cloudPath},
            {},
            {fullPath.localPath, PathProblem::DetectedSpecialFile},
            {}));
    }
    return false;
}

bool Sync::resolve_downsync(SyncRow& row,
                            SyncRow& parentRow,
                            SyncPath& fullPath,
                            [[maybe_unused]] bool alreadyExists,
                            PerFolderLogSummaryCounts& pflsc)
{
    assert(syncs.onSyncThread());
    ProgressingMonitor monitor(*this, row, fullPath);

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
        LOG_err << "Something triggered a download for a backup! Cancelling it";
        assert(false && "Something triggered a download for a backup!");
        row.syncNode->resetTransfer(nullptr);
        return false;
    }

    if (row.cloudNode->type == FILENODE)
    {
        // download the file if we're not already downloading
        // if (alreadyExists), we will move the target to the trash when/if download completes //todo: check

        if (!row.cloudNode->fingerprint.isvalid)
        {
            // if the cloud fingerprint is not valid, then the local mtime can't be set properly
            // and we'll have all sorts of matching problems, probably re-upload, etc
            // or the download will get cancelled for different fingerprint, and cycle, etc
            monitor.waitingCloud(fullPath.cloudPath, SyncStallEntry(
                SyncWaitReason::DownloadIssue, false, true,
                {row.cloudNode->handle, fullPath.cloudPath, PathProblem::CloudNodeInvalidFingerprint},
                {},
                {parentRow.fsNode ? fullPath.localPath : LocalPath()},
                {}));

            return false;
        }

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

                // Do we have enough space on disk for this file?
                {
                    auto size = row.cloudNode->fingerprint.size;

                    assert(size >= 0);

                    if (syncs.fsaccess->availableDiskSpace(mLocalPath) <= size)
                    {
                        LOG_debug << syncname
                                  << "Insufficient space available for download: "
                                  << logTriplet(row, fullPath);

                        changestate(INSUFFICIENT_DISK_SPACE, false, true, true);

                        return false;
                    }
                }

                // FIXME: to cover renames that occur during the
                // download, reconstruct localname in complete()
                LOG_debug << syncname << "Start sync download: " << row.syncNode << logTriplet(row, fullPath);
                LOG_debug << syncname << "Sync - requesting file " << fullPath.localPath;

                createDebrisTmpLockOnce();

                bool downloadFirst = fullPath.localPath.leafName().toPath(false) == ".megaignore";

                // download to tmpfaPath (folder debris/tmp). We will rename/mv it to correct location (updated if necessary) after that completes
                row.syncNode->queueClientDownload(std::make_shared<SyncDownload_inClient>(*row.cloudNode,
                    fullPath.localPath, inshare, threadSafeState, row.fsNode ? row.fsNode->fingerprint : FileFingerprint()
                    ), downloadFirst);

            }
            // terminated and completed transfers are checked for early in syncItem()
            else
            {
                if (!pflsc.alreadyDownloadingCount)
                {
                    SYNC_verbose << syncname << "Download already in progress -> completed: "
                                 << downloadPtr->wasCompleted << " terminated: "
                                 << downloadPtr->wasTerminated << " requester abandoned: "
                                 << downloadPtr->wasRequesterAbandoned << " -> " << logTriplet(row, fullPath);
                }
                pflsc.alreadyDownloadingCount += 1;
            }
        }
        else
        {
            SYNC_verbose_timed << "Delay starting download until parent local folder exists: " << fullPath.cloudPath
                               << logTriplet(row, fullPath);
            row.syncNode->setSyncAgain(true, false, false);

            monitor.waitingCloud(fullPath.cloudPath, SyncStallEntry(
                SyncWaitReason::DownloadIssue, false, true,
                {row.cloudNode->handle, fullPath.cloudPath},
                {},
                {fullPath.localPath, PathProblem::ParentFolderDoesNotExist},
                {}));
        }
    }
    else // FOLDERNODE
    {
        assert(!alreadyExists); // if it did we would have matched it

        if (parentRow.fsNode)
        {
            LOG_verbose << syncname << "Sync - executing local folder creation at: " << fullPath.localPath << logTriplet(row, fullPath);

            assert(!isBackup());
            if (syncs.fsaccess->mkdirlocal(fullPath.localPath, false, true))
            {
                assert(row.syncNode);
                assert(row.syncNode->localname == fullPath.localPath.leafName());

                // Update our records of what we know is on disk for this (parent) LocalNode.
                // This allows the next level of folders to be created too

                auto fa = syncs.fsaccess->newfileaccess(false);
                if (fa->fopen(fullPath.localPath, true, false, FSLogging::logOnError))
                {
                    auto fsnode = FSNode::fromFOpened(*fa, fullPath.localPath, *syncs.fsaccess);

                    // Mark other nodes with this FSID as having their FSID reused.
                    syncs.setSyncedFsidReused(fsfp(), fsnode->fsid);
                    syncs.setScannedFsidReused(fsfp(), fsnode->fsid);

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
                    parentRow.syncNode->scanDelayUntil = std::max<dstime>(parentRow.syncNode->scanDelayUntil, syncs.waiter->ds + 1);
                }
                else
                {
                    LOG_warn << syncname << "Failed to fopen folder straight after creation - revisit in 5s. " << fullPath.localPath << logTriplet(row, fullPath);
                    row.syncNode->setScanAgain(true, false, false, 50);
                }
            }
            else if (syncs.fsaccess->target_name_too_long)
            {
                LOG_warn << syncname
                         << "Unable to create target folder as its name is too long "
                         << fullPath.localPath
                         << logTriplet(row, fullPath);

                assert(row.syncNode);

                monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
                    SyncWaitReason::CannotCreateFolder, true, true,
                    {row.cloudNode->handle, fullPath.cloudPath},
                    {},
                    {fullPath.localPath, PathProblem::NameTooLongForFilesystem},
                    {}));
            }
            else
            {
                // let's consider this case as blocked too, alert the user
                LOG_warn << syncname << "Unable to create folder " << fullPath.localPath << logTriplet(row, fullPath);
                assert(row.syncNode);

                monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
                    SyncWaitReason::CannotCreateFolder, false, true,
                    {row.cloudNode->handle, fullPath.cloudPath},
                    {},
                    {fullPath.localPath, PathProblem::FilesystemErrorDuringOperation},
                    {}));

            }
        }
        else
        {
            SYNC_verbose_timed << "Delay creating local folder until parent local folder exists: " << fullPath.localPath
                               << logTriplet(row, fullPath);
            row.syncNode->setSyncAgain(true, false, false);

            monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
                SyncWaitReason::CannotCreateFolder, false, true,
                {row.cloudNode->handle, fullPath.cloudPath},
                {},
                {fullPath.localPath, PathProblem::ParentFolderDoesNotExist},
                {}));

        }

        // we may not see some moves/renames until the entire folder structure is created.
        row.syncNode->setCheckMovesAgain(true, false, false);  // todo: is this still right for the watiing case
    }
    return false;
}



bool Sync::resolve_userIntervention(SyncRow& row, SyncPath& fullPath)
{
    assert(syncs.onSyncThread());
    ProgressingMonitor monitor(*this, row, fullPath);

    assert(!isBackup() && "resolve_userIntervention should never be called for a backup!");

    if (row.syncNode)
    {
        bool immediateStall = true;

        if (row.syncNode->hasRare())
        {
            if (row.syncNode->rare().moveFromHere) immediateStall = false;
            if (row.syncNode->rare().moveToHere) immediateStall = false;
        }

        if (row.syncNode->transferSP)
        {
            if (immediateStall)
            {
                // eg if it's a simple upload (no moves involved), and the cloud side changes to something different during upload
                // since it doesn't seem right if we detect the stall and yet we can see the upload keeps progressing
                row.syncNode->resetTransfer(nullptr);
            }
        }

        SYNC_verbose_timed << "Both sides mismatch: "
                           << "Cloud -> mtime: " << row.cloudNode->fingerprint.mtime << ", "
                           << "size: " << row.cloudNode->fingerprint.size << ". "
                           << "SyncNode -> mtime: " << row.syncNode->syncedFingerprint.mtime << ", "
                           << "size: " << row.syncNode->syncedFingerprint.size << ". "
                           << "Local -> mtime: " << row.fsNode->fingerprint.mtime << ", "
                           << "size: " << row.fsNode->fingerprint.size << ". "
                           << "Immediate: " << immediateStall << " at "
                           << logTriplet(row, fullPath);

        monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
            SyncWaitReason::LocalAndRemoteChangedSinceLastSyncedState_userMustChoose, immediateStall, true,
            {row.cloudNode->handle, fullPath.cloudPath},
            {},
            {fullPath.localPath},
            {}));
    }
    else
    {
        SYNC_verbose_timed << "Both sides unsynced: "
                           << "Cloud -> mtime: " << row.cloudNode->fingerprint.mtime << ", "
                           << "size: " << row.cloudNode->fingerprint.size << ". "
                           << "Local -> mtime: " << row.fsNode->fingerprint.mtime << ", "
                           << "size: " << row.fsNode->fingerprint.size << ". "
                           << "At " << logTriplet(row, fullPath);

        monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
            SyncWaitReason::LocalAndRemotePreviouslyUnsyncedDiffer_userMustChoose, true, true,
            {row.cloudNode->handle, fullPath.cloudPath},
            {},
            {fullPath.localPath},
            {}));
    }
    return false;
}

bool Sync::resolve_cloudNodeGone(SyncRow& row, SyncRow& parentRow, SyncPath& fullPath)
{
    assert(!isBackup() && "This method is not allowed to be called on backups");
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

        // Is NO_NAMES ? Then ignore
        if (row.isNoName())
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
                                      nullptr,
                                      Syncs::LATEST_VERSION_ONLY);

        // Remote doesn't exist under an active sync or is excluded.
        if (!found || !active || nodeIsDefinitelyExcluded)
            return MT_NONE;

        // Does the remote represent an ignore file?
        if (cloudNode.isIgnoreFile())
        {
            // Then we know it can't be a move target.
            return MT_NONE;
        }

        // We need to discard NO_NAME nodes.
        // Not only the current cloudNode: it could happen that a node has been moved into a undecryptable/NO_NAMEd path. So we must check parents too.
        const std::string NO_KEY_SUFFIX = "NO_KEY";
        if (cloudPath.find(NO_KEY_SUFFIX) != std::string::npos)
        {
            SYNC_verbose << syncname << "[cloudNodeGone] [isPossibleCloudMoveSource] There is a NO_KEY in the cloud node path. Look for NO_NAMEd nodes [cloudNode.name = '" << cloudNode.name << "', cloudPath = '" << cloudPath << "']";
            do
            {
                if (cloudNode.name.empty())
                {
                    // Unnamed/undecryptable node, we know it cannot be considered a move target (for our local sync, so we need to discard the moving nodes and move the files to local debris)
                    assert((cloudPath.length() >= NO_KEY_SUFFIX.length() &&
                            !cloudPath.compare(cloudPath.length() - NO_KEY_SUFFIX.length(),
                                               NO_KEY_SUFFIX.length(),
                                               NO_KEY_SUFFIX)) &&
                           "Cloud node name empty, but the path does not contain NO_KEY word");
                    SYNC_verbose << syncname
                                << "[cloudNodeGone] Cloud Node is a NO_NAME/NO_KEY (undecryptable node) or a child of a NO_NAME/NO_KEY, it cannot be a move target!!!! "
                                << logTriplet(row, fullPath);
                    return MT_NONE;
                }
                if (cloudNode.parentType > FILENODE && !cloudNode.parentHandle.isUndef())
                {
                    SYNC_verbose << syncname << "[cloudNodeGone] [isPossibleCloudMoveSource] looking for NO_NAME parents [cloudNode.name = '" << cloudNode.name << "', cloudPath = '" << cloudPath << "']";
                    found = syncs.lookupCloudNode(cloudNode.parentHandle,
                                                cloudNode,
                                                &cloudPath,
                                                nullptr,
                                                &active,
                                                &nodeIsDefinitelyExcluded,
                                                nullptr,
                                                Syncs::LATEST_VERSION_ONLY);
                }
                else
                {
                    found = false;
                }
            } while (found && active && !nodeIsDefinitelyExcluded);
            LOG_warn << "[cloudNodeGone] There is a NO_KEY word in the cloudPath, but no unnamed cloudNode has been found"; // This could happen, for example, we could have a file or folder named MARIANO_KEY
        }
        else
        {
            // Just in case the NO_KEY word changes
            if (cloudNode.name.empty())
            {
                LOG_warn << "[cloudNodeGone] There isn't a NO_KEY word present in the cloudPath, "
                            "but the cloudNode name is empty!";
                assert(false && "There isn't a NO_KEY word present in the cloudPath, but the "
                                "cloudNode name is empty!");
            }
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

        // It's in an active sync and it's not excluded
        return MT_PENDING;
    };

    assert(syncs.onSyncThread());
    ProgressingMonitor monitor(*this, row, fullPath);

    string cloudPath;

    if (auto mt = isPossibleCloudMoveSource(cloudPath))
    {
        row.syncNode->setCheckMovesAgain(true, false, false);

        row.syncNode->trimRareFields();

        if (mt == MT_UNDERWAY)
        {
            SYNC_verbose_timed << syncname
                               << "Node is a cloud move/rename source, move is under way: "
                               << logTriplet(row, fullPath);
            row.suppressRecursion = true;
        }
        else
        {
            SYNC_verbose_timed << syncname
                               << "Letting move destination node process this first (cloud node is at "
			                   << cloudPath
                               << "): "
                               << logTriplet(row, fullPath);
        }

        monitor.waitingCloud(fullPath.cloudPath, SyncStallEntry(
            SyncWaitReason::MoveOrRenameCannotOccur, false, true,
            {row.cloudHandleOpt(), fullPath.cloudPath},
            {NodeHandle(), cloudPath},
            {fullPath.localPath},
            {LocalPath(), PathProblem::DestinationPathInUnresolvedArea}));
    }
    else if (row.syncNode->deletedFS)
    {
        SYNC_verbose << syncname << "FS item already removed: " << logTriplet(row, fullPath);
        monitor.noResult();
    }
    else if (mMovesWereComplete)
    {
        if (movetolocaldebris(fullPath.localPath))
        {
            LOG_debug << syncname << "Moved local item to local sync debris: " << fullPath.localPath << logTriplet(row, fullPath);
            row.syncNode->setScanAgain(true, false, false, 0);
            row.syncNode->scanAgain = TREE_RESOLVED;

            // don't let revisits do anything until the tree is cleaned up
            row.syncNode->deletedFS = true;
        }
        else
        {
            monitor.waitingCloud(fullPath.cloudPath, SyncStallEntry(
                SyncWaitReason::CannotPerformDeletion, false, true,
                {NodeHandle(), fullPath.cloudPath, PathProblem::DeletedOrMovedByUser},
                {},
                {fullPath.localPath, PathProblem::MoveToDebrisFolderFailed},
                {}));

            LOG_err << syncname << "Failed to move to local debris:  " << fullPath.localPath;
            // todo: do we need some sort of delay before retry on the next go-round?
        }
    }
    else
    {
        // todo: but, nodes are always current before we call recursiveSync - shortcut this case for nodes?
        SYNC_verbose_timed << syncname << "Wait for scanning+moving to finish before removing local node: "
                           << logTriplet(row, fullPath);
        row.syncNode->setSyncAgain(true, false, false); // make sure we revisit (but don't keep checkMoves set)
        if (parentRow.cloudNode)
        {

            monitor.waitingCloud(fullPath.cloudPath, SyncStallEntry(
                SyncWaitReason::DeleteWaitingOnMoves, false, true,
                {NodeHandle(), fullPath.cloudPath, PathProblem::DeletedOrMovedByUser},
                {},
                {fullPath.localPath},
                {}));
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

std::pair<bool, LocalNode*> Syncs::findLocalNodeBySyncedFsid(
    const handle fsid,
    const NodeMatchByFSIDAttributes& targetNodeAttributes,
    const LocalPath& originalPathForLogging,
    std::function<bool(const LocalNode&)> extraCheck,
    std::function<void(LocalNode*)> onFingerprintMismatchDuringPutnodes) const
{
    assert(onSyncThread());

    FindLocalNodeByFSIDPredicate predicate(fsid,
                                           ScannedOrSyncedContext::SYNCED,
                                           targetNodeAttributes,
                                           originalPathForLogging,
                                           extraCheck,
                                           onFingerprintMismatchDuringPutnodes);

    return findLocalNodeByFsid(localnodeBySyncedFsid, std::move(predicate));
}

std::pair<bool, LocalNode*>
    Syncs::findLocalNodeByScannedFsid(const handle fsid,
                                      const NodeMatchByFSIDAttributes& targetNodeAttributes,
                                      const LocalPath& originalPathForLogging,
                                      std::function<bool(const LocalNode&)> extraCheck) const
{
    assert(onSyncThread());

    FindLocalNodeByFSIDPredicate predicate(fsid,
                                           ScannedOrSyncedContext::SCANNED,
                                           targetNodeAttributes,
                                           originalPathForLogging,
                                           extraCheck);

    return findLocalNodeByFsid(localnodeByScannedFsid, std::move(predicate));
}

void Syncs::setSyncedFsidReused(const fsfp_t& fsfp, const handle fsid)
{
    assert(onSyncThread());
    for (auto range = localnodeBySyncedFsid.equal_range(fsid);
         range.first != range.second;
         ++range.first)
    {
        if (range.first->second->sync->fsfp() == fsfp)
            range.first->second->fsidSyncedReused = true;
    }
}

void Syncs::setScannedFsidReused(const fsfp_t& fsfp, const handle fsid)
{
    assert(onSyncThread());
    for (auto range = localnodeByScannedFsid.equal_range(fsid);
        range.first != range.second;
        ++range.first)
    {
        if (range.first->second->sync->fsfp() == fsfp)
            range.first->second->fsidScannedReused = true;
    }
}

bool Syncs::findLocalNodeByNodeHandle(NodeHandle h, LocalNode*& sourceSyncNodeOriginal, LocalNode*& sourceSyncNodeCurrent, bool& unsureDueToIncompleteScanning, bool& unsureDueToUnknownExclusionMoveSource)
{
    // find where the node was (based on synced local file presence)
    // and where it is now (synced local file absent at the corresponding path)

    // consider these cases.
    // 1. normal move of cloud node.  original location still has local file, move-to location (here) has none
    //    Only one node found for that case, with local file.
    // 2. move of cloud node, and original local file was separately moved elsewhere.
    //    Original location does not have the local file, new location has it (but that location is not here)
    //    Two nodes found.

    sourceSyncNodeOriginal = nullptr;
    sourceSyncNodeCurrent = nullptr;
    unsureDueToIncompleteScanning = false;
    unsureDueToUnknownExclusionMoveSource = false;

    assert(onSyncThread());
    if (h.isUndef()) return false;

    auto range = localnodeByNodeHandle.equal_range(h);

    for (auto it = range.first; it != range.second; ++it)
    {
        switch (it->second->exclusionState())
        {
        case ES_INCLUDED: break;
        case ES_UNKNOWN:  LOG_verbose << mClient.clientname << "findLocalNodeByNodeHandle - unknown exclusion with that handle " << h << " at: " << it->second->getLocalPath();
                          unsureDueToUnknownExclusionMoveSource = true;
                          continue;
        case ES_EXCLUDED: continue;
        default: assert(false); continue;
        }

        // check the file/folder actually exists (with same fsid) on disk for this LocalNode
        LocalPath lp = it->second->getLocalPath();

        if (it->second->fsid_lastSynced != UNDEF &&
            it->second->fsid_lastSynced == fsaccess->fsidOf(lp, false, false, FSLogging::logExceptFileNotFound))
        {
            sourceSyncNodeCurrent = it->second;
        }
        else
        {
            sourceSyncNodeOriginal = it->second;
        }
    }

    if (!sourceSyncNodeCurrent && sourceSyncNodeOriginal && sourceSyncNodeOriginal->fsid_lastSynced != UNDEF)
    {
        // see if we can find where the local side went, so we can report a move clash
        const NodeMatchByFSIDAttributes sourceSyncNodeOriginalAttributes{
            sourceSyncNodeOriginal->type,
            sourceSyncNodeOriginal->sync->fsfp(),
            sourceSyncNodeOriginal->sync->cloudRootOwningUser,
            sourceSyncNodeOriginal->syncedFingerprint};
        std::tie(unsureDueToUnknownExclusionMoveSource, sourceSyncNodeCurrent) =
            findLocalNodeByScannedFsid(sourceSyncNodeOriginal->fsid_lastSynced,
                                       sourceSyncNodeOriginalAttributes,
                                       sourceSyncNodeOriginal->getLocalPath());

        if (unsureDueToUnknownExclusionMoveSource)
        {
            LOG_verbose << mClient.clientname
                        << "findLocalNodeByNodeHandle - unknown exclusion after fsid lookup";
        }

        if (!sourceSyncNodeCurrent && !sourceSyncNodeOriginal->sync->scanningWasComplete())
        {
            unsureDueToIncompleteScanning = true;
        }
    }

    if (sourceSyncNodeCurrent && !sourceSyncNodeOriginal)
    {
        // normal case, simple cloud side move only.  Current and original local location should be the same
        sourceSyncNodeOriginal = sourceSyncNodeCurrent;
    }

    return sourceSyncNodeCurrent && sourceSyncNodeOriginal;
}

std::optional<bool> Sync::checkIfFileIsChanging(const FSNode& fsNode, const LocalPath& fullPath)
{
    assert(syncs.onSyncThread());
    assert(fsNode.type == FILENODE);
    Syncs::FileChangingState& state = syncs.mFileChangingCheckState[fullPath];

    const auto getResult =
        [wasInitialized = state.isInitialized()](const bool waitforupdate) -> std::optional<bool>
    {
        return wasInitialized ? std::optional{waitforupdate} : std::nullopt;
    };

    const m_time_t currentsecs = m_time();
    if (!state.updatedfileinitialts)
    {
        state.updatedfileinitialts = currentsecs;
    }

    const bool fileUpdatedBeforeNow = currentsecs >= state.updatedfileinitialts;
    if (!fileUpdatedBeforeNow)
    {
        LOG_warn << syncname << "checkIfFileIsChanging: File check started in the future";
        syncs.mFileChangingCheckState.erase(fullPath);
        return getResult(false);
    }

    const bool isMaxDelayExceeded =
        currentsecs - state.updatedfileinitialts > Sync::FILE_UPDATE_MAX_DELAY_SECS;
    if (isMaxDelayExceeded)
    {
        syncs.queueClient(
            [](MegaClient& mc, TransferDbCommitter&)
            {
                mc.sendevent(99438, "Timeout waiting for file update", 0);
            });

        syncs.mFileChangingCheckState.erase(fullPath);
        return getResult(false);
    }

    auto prevfa = syncs.fsaccess->newfileaccess(false);
    const bool canOpenFile = prevfa->fopen(fullPath, FSLogging::logOnError);
    if (!canOpenFile)
    {
        if (prevfa->retry)
        {
            LOG_debug << syncname
                      << "checkIfFileIsChanging: The file in the origin is temporarily blocked. "
                         "Waiting...";
            return getResult(true);
        }
        else
        {
            LOG_debug << syncname
                      << "checkIfFileIsChanging: There isn't anything in the origin path";
            syncs.mFileChangingCheckState.erase(fullPath);
            return getResult(false);
        }
    }
    LOG_debug << syncname << "checkIfFileIsChanging: File detected in the origin of a move";

    const bool fileCheckedInFuture = currentsecs < state.updatedfilets;
    fileCheckedInFuture ?
        LOG_warn << syncname << "checkIfFileIsChanging: File checked in the future" :
        LOG_debug << syncname << "checkIfFileIsChanging: The file size seems stable";

    const bool fileSizeChangedTooRecently =
        (currentsecs - state.updatedfilets) < (Sync::FILE_UPDATE_DELAY_DS / 10);
    if (!fileCheckedInFuture && fileSizeChangedTooRecently)
    {
        LOG_verbose << syncname << "checkIfFileIsChanging: currentsecs = " << currentsecs
                    << "  lastcheck = " << state.updatedfilets << "  currentsize = " << prevfa->size
                    << "  lastsize = " << state.updatedfilesize;
        LOG_debug << "checkIfFileIsChanging: The file size changed too recently. Waiting "
                  << currentsecs - state.updatedfilets << " ds for " << fsNode.localname;
        return getResult(true);
    }

    const bool fileSizeIsChanging = state.updatedfilesize != prevfa->size;
    if (!fileCheckedInFuture && fileSizeIsChanging)
    {
        LOG_verbose << "checkIfFileIsChanging: " << syncname << " currentsecs = " << currentsecs
                    << "  lastcheck = " << state.updatedfilets << "  currentsize = " << prevfa->size
                    << "  lastsize = " << state.updatedfilesize;
        LOG_debug
            << "checkIfFileIsChanging: The file size has changed since the last check. Waiting...";
        state.updatedfilesize = prevfa->size;
        state.updatedfilets = currentsecs;
        return getResult(true);
    }

    const bool fileModifiedInFuture = currentsecs < prevfa->mtime;
    if (fileModifiedInFuture)
    {
        LOG_warn << syncname << "checkIfFileIsChanging: File modified in the future";
        syncs.mFileChangingCheckState.erase(fullPath);
        return getResult(false);
    }

    const bool fileModifiedIsStable =
        currentsecs - prevfa->mtime >= (Sync::FILE_UPDATE_DELAY_DS / 10);
    if (fileModifiedIsStable)
    {
        LOG_debug << syncname << "checkIfFileIsChanging: The modification time seems stable.";
        syncs.mFileChangingCheckState.erase(fullPath);
        return getResult(false);
    }

    LOG_verbose << "checkIfFileIsChanging:" << syncname << "currentsecs = " << currentsecs
                << "  mtime = " << prevfa->mtime;
    LOG_debug << "checkIfFileIsChanging:" << syncname << "File modified too recently. Waiting...";
    return getResult(true);
}

bool Sync::resolve_fsNodeGone(SyncRow& row, SyncRow& /*parentRow*/, SyncPath& fullPath)
{
    assert(syncs.onSyncThread());
    ProgressingMonitor monitor(*this, row, fullPath);

    const auto [unsureOfMovedLocalNodeDueToUnknownExclusions, movedLocalNode] = std::invoke(
        [&syncs = std::as_const(syncs),
         &cloudRootOwningUser = std::as_const(cloudRootOwningUser),
         &fsfp = std::as_const(fsfp()),
         &syncNode = std::as_const(*row.syncNode),
         &fullPath = std::as_const(fullPath)]() -> std::pair<bool, LocalNode*>
        {
            // Ignore files aren't subject to the usual move processing.
            if (!syncNode.fsidSyncedReused && syncNode.isIgnoreFile())
            {
                auto extraCheck = [&syncNode](const LocalNode& n)
                {
                    return &n != &syncNode && !n.isIgnoreFile();
                };

                const NodeMatchByFSIDAttributes syncNodeAttributes{syncNode.type,
                                                                   fsfp,
                                                                   cloudRootOwningUser,
                                                                   syncNode.syncedFingerprint};
                return syncs.findLocalNodeByScannedFsid(syncNode.fsid_lastSynced,
                                                        syncNodeAttributes,
                                                        fullPath.localPath,
                                                        std::move(extraCheck));
            }
            return {false, nullptr};
        });

    if (unsureOfMovedLocalNodeDueToUnknownExclusions)
    {
        row.syncNode->setCheckMovesAgain(true, false, false);

        SYNC_verbose_timed << syncname << "This file/folder was probably moved, but the destination is currently exclusion-unknown: "
                           << logTriplet(row, fullPath);

        monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
            SyncWaitReason::MoveOrRenameCannotOccur, false, false,
            {row.cloudHandleOpt(), fullPath.cloudPath},
            {NodeHandle(), movedLocalNode->getCloudPath(false), PathProblem::DestinationPathInUnresolvedArea},
            {fullPath.localPath},
            {movedLocalNode->getLocalPath()}));
    }
    else if (movedLocalNode)
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
            SYNC_verbose_timed << syncname << "This file/folder was moved, letting destination node at "
                               << movedLocalNode->getLocalPath() << " process this first: "
                               << logTriplet(row, fullPath);
        }
        // todo: do we need an equivalent to row.recurseToScanforNewLocalNodesOnly = true;  (in resolve_cloudNodeGone)

        monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
            SyncWaitReason::MoveOrRenameCannotOccur, false, false,
            {row.cloudHandleOpt(), fullPath.cloudPath},
            {NodeHandle(), movedLocalNode->getCloudPath(false), PathProblem::DestinationPathInUnresolvedArea},
            {fullPath.localPath},
            {movedLocalNode->getLocalPath()}));

        // make sure we do visit the parent folder of that node so the move can be processed
        if (movedLocalNode->parent && !movedLocalNode->parent->syncRequired())
        {
            SYNC_verbose << syncname << "Ensuring we visit the move-target node parent: " << movedLocalNode->getLocalPath() << ". At " << logTriplet(row, fullPath);
            movedLocalNode->setSyncAgain(true, true, false);
        }

    }
    else if (!mScanningWasComplete &&
             !row.isIgnoreFile())  // ignore files do not participate in move logic
    {
        SYNC_verbose_timed << syncname << "Wait for scanning to finish before confirming fsid "
                           << toHandle(row.syncNode->fsid_lastSynced) << " deleted or moved: "
                           << logTriplet(row, fullPath);

        monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
            SyncWaitReason::DeleteOrMoveWaitingOnScanning, false, false,
            {row.cloudHandleOpt(), fullPath.cloudPath},
            {},
            {fullPath.localPath, PathProblem::DeletedOrMovedByUser},
            {}));
    }
    else if (mMovesWereComplete ||
             row.isIgnoreFile())  // ignore files do not participate in move logic
    {
        if (!row.syncNode->rareRO().removeNodeHere)
        {
            // We need to be sure before sending to sync trash.  If we have received
            // a lot of delete notifications, but not yet the corrsponding add that makes it a move
            // then it would be a mistake.  Give the filesystem 2 seconds to deliver that one.
            // On windows at least, under some circumstance, it may first deliver many deletes for the subfolder in a reverse depth first order
            bool timeToBeSure = syncs.waiter->ds - lastFSNotificationTime > 20;

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
                    bool canChangeVault = threadSafeState->mCanChangeVault;

                    syncs.queueClient(
                        [debrisNodeHandle, fromInshare, deletePtr, canChangeVault](
                            MegaClient& mc,
                            TransferDbCommitter&)
                        {
                            if (auto n = mc.nodeByHandle(debrisNodeHandle))
                            {
                                if (n->parent && n->parent->type == FILENODE)
                                {
                                    // if we decided to remove a file, but it turns out not to be
                                    // the latest version of that file, abandon the action
                                    // and let the sync recalculate
                                    LOG_debug << "Sync delete was out of date, there is a more "
                                                 "recent version of the file. "
                                              << debrisNodeHandle << " " << n->displaypath();
                                    return;
                                }

                                mc.movetosyncdebris(
                                    n.get(),
                                    fromInshare,
                                    [deletePtr](NodeHandle, Error e)
                                    {
                                        LOG_debug << "Sync delete to sync debris completed: " << e
                                                  << " " << deletePtr->pathDeleting;

                                        if (e)
                                            deletePtr->failed = true;
                                        else
                                            deletePtr->succeeded = true;
                                    },
                                    canChangeVault);
                            }
                        });

                    // Remember that the delete is going on, so we don't do anything else until that resolves
                    // We will detach the synced-fsid side on final completion of this operation.  If we do so
                    // earier, the logic will evaluate that updated state too soon, perhaps resulting in downsync.
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
        SYNC_verbose_timed << syncname << "Wait for moves to finish before confirming fsid "
                           << toHandle(row.syncNode->fsid_lastSynced)
                           << " deleted: " << logTriplet(row, fullPath);

        monitor.waitingLocal(fullPath.localPath, SyncStallEntry(
            SyncWaitReason::DeleteWaitingOnMoves, false, false,
            {row.cloudHandleOpt(), fullPath.cloudPath},
            {},
            {fullPath.localPath, PathProblem::DeletedOrMovedByUser},
            {}));
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

std::future<size_t> Syncs::triggerPeriodicScanEarly(handle backupID)
{
    // Cause periodic-scan syncs to scan now (waiting for the next periodic scan is impractical for tests)
    // For this backupId or for all periodic scan syncs if backupId == UNDEF
    assert(!onSyncThread());

    auto indiscriminate = backupID == UNDEF;
    auto notifier = std::make_shared<std::promise<size_t>>();
    auto result = notifier->get_future();

    queueSync([backupID, indiscriminate, notifier, this]() {
        lock_guard<std::recursive_mutex> guard(mSyncVecMutex);
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
    }, "triggerPeriodicScanEarly");

    return result;
}

void Syncs::triggerSync(const LocalPath& lp, bool scan)
{
    assert(!onSyncThread());

    if (mClient.fetchingnodes) return;  // on start everything needs scan+sync anyway

    lock_guard<mutex> g(triggerMutex);
    auto& entry = triggerLocalpaths[lp];
    if (scan) entry = true;
}

void Syncs::triggerSync(NodeHandle h, bool recurse)
{
    assert(!onSyncThread());

    if (mClient.fetchingnodes) return;  // on start everything needs scan+sync anyway

    lock_guard<mutex> g(triggerMutex);
    auto& entry = triggerHandles[h];
    if (recurse) entry = true;
}

void Syncs::processTriggerLocalpaths()
{
    // Mark nodes to be scanned because upload transfers failed.
    // This may save us trying to immediately start the same failed upload
    assert(onSyncThread());

    map<LocalPath, bool> triggers;
    {
        lock_guard<mutex> g(triggerMutex);
        triggers.swap(triggerLocalpaths);
    }

    if (mSyncVec.empty()) return;

    for (auto& lp : triggers)
    {
        for (auto& us : mSyncVec)
        {
            if (Sync* sync = us->mSync.get())
            {
                if (LocalNode* triggerLn = sync->localnodebypath(nullptr, lp.first, nullptr, nullptr, false))
                {
                    if (lp.second)
                    {
                        LOG_debug << "Scan trigger by path received for " << triggerLn->getLocalPath();
                        triggerLn->setScanAgain(false, true, false, 0);
                    }
                    else
                    {
                        LOG_debug << "Sync trigger by path received for " << triggerLn->getLocalPath();
                        triggerLn->setSyncAgain(false, true, false);
                    }
                }
            }
        }
    }
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
                bool found = lookupCloudNode(h, cloudNode, &cloudNodePath, &isInTrash, nullptr, nullptr, nullptr, Syncs::EXACT_VERSION);
                if (found && !isInTrash)
                {
                    // if the parent is a file, then it's just old versions being mentioned in the actionpackets, ignore
                    if (cloudNode.parentType > FILENODE && !cloudNode.parentHandle.isUndef())
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

void Syncs::setdefaultfilepermissions(int permissions)
{
    queueSync(
        [this, permissions]() {
            fsaccess->setdefaultfilepermissions(permissions);
        },
        "setdefaultfilepermissions");
}

void Syncs::setdefaultfolderpermissions(int permissions)
{
    queueSync(
        [this, permissions]() {
            fsaccess->setdefaultfolderpermissions(permissions);
        },
        "setdefaultfolderpermissions");
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

handle SyncConfigStore::driveID(const LocalPath& drivePath) const
{
    auto i = mKnownDrives.find(drivePath);

    if (i != mKnownDrives.end())
        return i->second.driveID;

    assert(false && "Drive should be known!");

    return UNDEF;
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
    driveInfo.drivePath = drivePath;

    if (isExternal)
    {
        driveInfo.driveID = mIOContext.driveID(drivePath);

        if (driveInfo.driveID == UNDEF)
        {
            LOG_err << "Failed to retrieve drive ID for: "
                    << drivePath;

            return API_EREAD;
        }
    }

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
            else
            {
                LOG_debug << "SyncConfigStore::read returned: " << int(result);
            }
        }
    }
    else
    {
        LOG_debug << "getSlotsInOrder returned: " << int(result);
    }

    if (result != API_EREAD)
    {
        mKnownDrives[drivePath] = driveInfo;
    }

    return result;
}


error SyncConfigStore::write(const LocalPath& drivePath, const SyncConfigVector& configs)
{
#ifndef NDEBUG
    for (const auto& config : configs)
    {
        assert(equal(config.mExternalDrivePath, drivePath));
    }
#endif

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
                     << drivePath << " error " << e;
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
                     << drivePath << " error " << e;

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
        LOG_debug << "mIOContext read failed";
        return API_EREAD;
    }

    JSON reader(data);

    if (!mIOContext.deserialize(dbp, configs, reader, slot, isExternal))
    {
        LOG_debug << "mIOContext deserialize failed";
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
            config.mLocalPath = LocalPath::fromRelativePath(config.mLocalPath.toPath(false));

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
                    << drivePath
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
    auto path = dbFilePath(dbPath, slot);

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
    const auto TYPE_SYNCS = makeNameid("sy");

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

handle SyncConfigIOContext::driveID(const LocalPath& drivePath) const
{
    handle result = UNDEF;

    readDriveId(mFsAccess, drivePath, result);

    return result;
}

FileSystemAccess& SyncConfigIOContext::fsAccess() const
{
    return mFsAccess;
}

error SyncConfigIOContext::getSlotsInOrder(const LocalPath& dbPath,
                                           vector<unsigned int>& confSlots)
{
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
        const char suffix = filePath.toPath(false).back();

        // Skip invalid suffixes.
        if (!is_digit(static_cast<unsigned>(suffix)))
        {
            continue;
        }

        // Determine file's modification time.
        if (!fileAccess->fopen(filePath, FSLogging::logOnError))
        {
            // Couldn't stat file.
            continue;
        }

        // Record this slot-time pair.
        unsigned int slot = static_cast<unsigned>(suffix - 0x30); // convert char to int
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
              << path;

    // Try and open the file for reading.
    auto fileAccess = mFsAccess.newfileaccess(false);

    if (!fileAccess->fopen(path, true, false, FSLogging::logOnError))
    {
        // Couldn't open the file for reading.
        LOG_err << "Unable to open config DB for reading: "
                << path;

        return API_EREAD;
    }

    // Try and read the data from the file.
    string d;

    if (!fileAccess->fread(&d, static_cast<unsigned>(fileAccess->size), 0, 0x0, FSLogging::logOnError))
    {
        // Couldn't read the file.
        LOG_err << "Unable to read config DB: "
                << path;

        return API_EREAD;
    }

    // Try and decrypt the data.
    if (!decrypt(d, data))
    {
        // Couldn't decrypt the data.
        LOG_err << "Unable to decrypt config DB: "
                << path;

        return API_EREAD;
    }

    LOG_debug << "Config DB successfully read from disk: "
              << path
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
                 << path;

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
              << dbPath
              << " / "
              << slot;

    // Try and create the backup configuration directory.
    if (!(mFsAccess.mkdirlocal(path, false, false) || mFsAccess.target_exists))
    {
        LOG_err << "Unable to create config DB directory: "
                << dbPath;

        // Couldn't create the directory and it doesn't exist.
        return API_EWRITE;
    }

    // Generate the rest of the path.
    path = dbFilePath(dbPath, slot);

    // Open the file for writing.
    auto fileAccess = mFsAccess.newfileaccess(false);

    if (!fileAccess->fopen(path, false, true, FSLogging::logOnError))
    {
        // Couldn't open the file for writing.
        LOG_err << "Unable to open config DB for writing: "
                << path;

        return API_EWRITE;
    }

    // Ensure the file is empty.
    if (!fileAccess->ftruncate())
    {
        // Couldn't truncate the file.
        LOG_err << "Unable to truncate config DB: "
                << path;

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
                << path;

        return API_EWRITE;
    }

    LOG_debug << "Config DB successfully written to disk: "
              << path
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
        LOG_err << "Unable to decrypt JSON sync config: "
                << "File's too small ("
                << in.size()
                << " vs. "
                << METADATA_LENGTH
                << ")";

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
        LOG_err << "Unable to decrypt JSON sync config: "
                << "HMAC mismatch";

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
    const auto TYPE_BACKUP_ID       = makeNameid("id");
    const auto TYPE_BACKUP_STATE    = makeNameid("bs");
    const auto TYPE_CHANGE_METHOD   = makeNameid("cm");
    const auto TYPE_ENABLED         = makeNameid("en");
    const auto TYPE_FILESYSTEM_FP   = makeNameid("fp");
    const auto TYPE_FILESYSTEM_FU   = makeNameid("fu");
    const auto TYPE_ROOT_FSID       = makeNameid("rf");
    const auto TYPE_LAST_ERROR      = makeNameid("le");
    const auto TYPE_LAST_WARNING    = makeNameid("lw");
    const auto TYPE_NAME            = makeNameid("n");
    const auto TYPE_SCAN_INTERVAL   = makeNameid("si");
    const auto TYPE_SOURCE_PATH     = makeNameid("sp");
    const auto TYPE_SYNC_TYPE       = makeNameid("st");
    const auto TYPE_TARGET_HANDLE   = makeNameid("th");
    const auto TYPE_TARGET_PATH     = makeNameid("tp");
    const auto TYPE_LEGACY_INELIGIB = makeNameid("li");

    // Temporary storage.
    std::uint64_t fsFingerprint = 0;
    std::string   fsUUID;

    // Assume legacy exclusions are eligible.
    config.mLegacyExclusionsIneligigble = false;

    for ( ; ; )
    {
        switch (reader.getnameid())
        {
        case EOO:
            // Populate fingerprint.
            config.mFilesystemFingerprint =
              fsfp_t(fsFingerprint, std::move(fsUUID));

            // success if we reached the end of the object
            return *reader.pos == '}';

        case TYPE_CHANGE_METHOD:
            config.mChangeDetectionMethod =
              static_cast<ChangeDetectionMethod>(reader.getint32());
            break;

        case TYPE_ENABLED:
            config.mEnabled = reader.getbool();
            break;

        case TYPE_FILESYSTEM_FP:
            fsFingerprint = reader.getfsfp();
            break;

        case TYPE_FILESYSTEM_FU:
            reader.storebinary(&fsUUID);
            break;

        case TYPE_ROOT_FSID:
            config.mLocalPathFsid = reader.gethandle(sizeof(handle));
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

        case TYPE_LEGACY_INELIGIB:
            config.mLegacyExclusionsIneligigble = reader.getbool();
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
    if (!mCipher.cbc_encrypt_pkcs_padding(&data, iv, &d))
    {
        LOG_err << "Failed to encrypt file.";
        return d;
    }

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
    auto sourcePath = config.mLocalPath.toPath(false);

    // Strip drive path from source.
    if (config.isExternal())
    {
        auto drivePath = config.mExternalDrivePath.toPath(false);
        sourcePath.erase(0, drivePath.size());
    }

    writer.beginobject();
    writer.arg("id", config.mBackupId, sizeof(handle));
    writer.arg_B64("sp", sourcePath);
    writer.arg_B64("n", config.mName);
    writer.arg_B64("tp", config.mOriginalPathOfRemoteRootNode);
    writer.arg_fsfp("fp", config.mFilesystemFingerprint.fingerprint());
    writer.arg_B64("fu", config.mFilesystemFingerprint.uuid());
    writer.arg("rf", config.mLocalPathFsid, sizeof(handle));
    writer.arg("th", config.mRemoteNode);
    writer.arg("le", config.mError);
    writer.arg("lw", config.mWarning);
    writer.arg("st", config.mSyncType);
    writer.arg("en", config.mEnabled);
    writer.arg("bs", config.mBackupState);
    writer.arg("cm", config.mChangeDetectionMethod);
    writer.arg("si", config.mScanIntervalSec);
    writer.arg("li", config.mLegacyExclusionsIneligigble);
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
        waiter->bumpds();

        // Flush changes made to internal configs.
        syncConfigStoreFlush();

        mHeartBeatMonitor->beat();

        // Aim to wait at least one second between recursiveSync traversals, keep CPU down.
        // If traversals are very long, have a fair wait between (up to 5 seconds)
        // If something happens that means the sync needs attention, the waiter
        // should be woken up by a waiter->notify() call, and we break out of this wait
        if (!skipWait)
        {
            auto waitDs = 10 + std::min<unsigned>(lastRecurseMs, 10000)/200;
            if (mClient.statecurrent && waitDs != 10)
            {
                LOG_verbose << "starting sync wait, delay " << waitDs;
            }
            waiter->init(waitDs);
            waiter->wakeupby(fsaccess.get(), Waiter::NEEDEXEC);
            waiter->wait();
            if (mClient.statecurrent && waitDs != 10)
            {
                LOG_verbose << "sync wait complete";
            }
        }
        skipWait = false;
        lastRecurseMs = 0;

        fsaccess->checkevents(waiter.get());

        // reset flag now, if it gets set then we speed back to processsing syncThreadActions
        mSyncFlags->earlyRecurseExitRequested = false;

        // execute any requests from the MegaClient
        waiter->bumpds();
        QueuedSyncFunc f;
        while (syncThreadActions.popFront(f))
        {
            if (!f.first)
            {
                // null function is the signal to end the thread
                // Be sure to flush changes made to internal configs.
                syncConfigStoreFlush();
                return;
            }

            if (!f.second.empty())
            {
                LOG_debug << "Sync thread executing request: " << f.second;
            }

            f.first();
        }

        waiter->bumpds();

        // Process filesystem notifications.
        for (auto& us : mSyncVec)
        {
            if (Sync* sync = us->mSync.get())
            {
                if (sync->dirnotify)
                {
                    sync->procscanq();
                }
            }
        }

        processTriggerHandles();
        processTriggerLocalpaths();

        waiter->bumpds();

        // We must have actionpacketsCurrent so that any LocalNode created can straight away indicate if it matched a Node
        // check this before we check if the sync root nodes exist etc, in case a mid-session fetchnodes is going on
        if (!mClient.actionpacketsCurrent)
        {
            if (mClient.statecurrent)
            {
                LOG_verbose << "not ready to sync recurse, actionpackets are changing nodes";
            }
            continue;
        }

        // verify filesystem fingerprints, disable deviating syncs
        // (this covers mountovers, some device removals and some failures)
        for (auto& us : mSyncVec)
        {
            vector<pair<handle, int>> sdsBackups;
            CloudNode cloudNode;
            string cloudRootPath;
            bool inTrash = false;
            unsigned rootDepth;
            bool foundRootNode = lookupCloudNode(us->mConfig.mRemoteNode,
                                                    cloudNode,
                                                    &cloudRootPath,
                                                    &inTrash,
                                                    nullptr,
                                                    nullptr,
                                                    &rootDepth,
                                                    Syncs::FOLDER_ONLY,
                                                    nullptr,
                                                    &sdsBackups);

            if (processRemovingSyncBySds(*us.get(), foundRootNode, sdsBackups))
            {
                continue;
            }
            else if (foundRootNode && processPauseResumeSyncBySds(*us.get(), sdsBackups))
            {
                continue;
            }

            if (Sync* sync = us->mSync.get())
            {
                if (us->mConfig.mError != NO_SYNC_ERROR)
                {
                    continue;
                }

                auto fa = fsaccess->newfileaccess();
                if (fa->fopen(sync->localroot->localname, true, false, FSLogging::logOnError, nullptr, true))
                {
                    if (fa->type != FOLDERNODE)
                    {
                        LOG_err << "Sync local root folder is not a folder: " << sync->localroot->localname;
                        sync->changestate(INVALID_LOCAL_TYPE, false, true, true);
                        continue;
                    }
                    else if (fa->fsid != sync->localroot->fsid_lastSynced)
                    {
                        LOG_err << "Sync local root folder fsid has changed for " << sync->localroot->localname << ": "
                                << fa->fsid << " was: " << sync->localroot->fsid_lastSynced;
                        sync->changestate(MISMATCH_OF_ROOT_FSID, false, true, true);
                        continue;
                    }
                }
                else
                {
                    LOG_err << "Sync local root folder could not be opened: " << sync->localroot->localname;
                    sync->changestate(LOCAL_PATH_UNAVAILABLE, false, true, true);
                    continue;
                }


                auto expectedFsfp = sync->fsfp();
                assert(expectedFsfp);

                if (expectedFsfp)
                {
                    auto computedFsfp = fsaccess->fsFingerprint(sync->localroot->localname);
                    if (computedFsfp && computedFsfp != expectedFsfp)
                    {
                        LOG_err << "Local filesystem mismatch. Previous: "
                                << expectedFsfp.toString()
                                << "  Current: "
                                << computedFsfp.toString();
                        sync->changestate(LOCAL_FILESYSTEM_MISMATCH, false, true, true);
                        continue;
                    }
                }

                sync->cloudRoot = cloudNode;
                sync->cloudRootPath = cloudRootPath;
                sync->mCurrentRootDepth = rootDepth;

                const bool remoteRootHasChanged =
                    checkSyncRemoteLocationChange(us->mConfig, foundRootNode, sync->cloudRootPath);

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
                else if (remoteRootHasChanged)
                {
                    manageRemoteRootLocationChange(*sync);
                }
            }
            else if (us->mConfig.mRunState == SyncRunState::Suspend &&
                     (us->mConfig.mError == LOCAL_PATH_UNAVAILABLE ||
                      us->mConfig.mError == LOCAL_PATH_TEMPORARY_UNAVAILABLE))
            {
                // If we shut the sync down before because the local path wasn't available (yet)
                // And it's safe to resume the sync because it's in Suspend (rather than disable)
                // then we can auto-restart it, if the path becomes available (eg, network drive was
                // slow to mount, user plugged in USB, etc)

                auto computedFsfp = fsaccess->fsFingerprint(us->mConfig.mLocalPath);
                auto expectedFsfp = us->mConfig.mFilesystemFingerprint;

                auto fa = fsaccess->newfileaccess();
                if (fa->fopen(us->mConfig.mLocalPath, true, false, FSLogging::logExceptFileNotFound, nullptr, true))
                {
                    if (fa->type != FOLDERNODE)
                    {
                        LOG_err << "Sync path is available again but is not a folder: " << us->mConfig.mLocalPath;
                    }
                    // todo: we don't keep the fsid of the root folder in config, but maybe we should?
                    //else if (fa->fsid != us->mConfig.mLocalPathFsid)
                    //{
                    //    LOG_err << "Sync path is available again but is not the same folder, by fsid: " << us->mConfig.mLocalPath;
                    //}
                    else if (computedFsfp && expectedFsfp && computedFsfp != expectedFsfp)
                    {
                        LOG_err << "Sync path is available again but is not the same filesystem, by id.  Old: "
                                << expectedFsfp.toString()
                                << " New: "
                                << computedFsfp.toString()
                                << " Path: "
                                << us->mConfig.mLocalPath;
                    }
                    else
                    {
                        LOG_debug << "Auto-starting sync that was suspended when the local path was unavailable: " << us->mConfig.mLocalPath;
                        enableSyncByBackupId_inThread(us->mConfig.mBackupId, false, nullptr, "", "");
                    }
                }
            }
        };

        stopSyncsInErrorState();

        // Clear the context if the associated sync is no longer active.
        mIgnoreFileFailureContext.reset(*this);

        if (syncStallState &&
            (waiter->ds < mSyncFlags->recursiveSyncLastCompletedDs + 10) &&
            (waiter->ds > mSyncFlags->recursiveSyncLastCompletedDs) &&
            !lastLoopEarlyExit &&
            !mSyncVec.empty())
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
            auto scanningWasCompletePreviously = mSyncFlags->scanningWasComplete && !mSyncFlags->isInitialPass;
            mSyncFlags->scanningWasComplete = checkSyncsScanningWasComplete_inThread();
            mSyncFlags->reachableNodesAllScannedLastPass = mSyncFlags->reachableNodesAllScannedThisPass && !mSyncFlags->isInitialPass;
            mSyncFlags->reachableNodesAllScannedThisPass = true;
            auto allSyncsMovesWereComplete = checkSyncsMovesWereComplete();
            mSyncFlags->movesWereComplete = scanningWasCompletePreviously && allSyncsMovesWereComplete;
            mSyncFlags->noProgress = mSyncFlags->reachableNodesAllScannedLastPass;
            if (mSyncFlags->noProgress) mSyncFlags->stall.setNoProgress();
            if (!scanningWasCompletePreviously || !mSyncFlags->scanningWasComplete || !mSyncFlags->reachableNodesAllScannedLastPass || !allSyncsMovesWereComplete || !mSyncFlags->movesWereComplete)
            {
                SYNCS_verbose_timed << "[SyncLoop] scanningWasCompletePreviously = " << scanningWasCompletePreviously
                            << ", scanningWasComplete = " << mSyncFlags->scanningWasComplete
                            << ", reachableNodesAllScannedLastPass = " << mSyncFlags->reachableNodesAllScannedLastPass
                            << ", allSyncsMovesWereComplete = " << allSyncsMovesWereComplete
                            << ", movesWereComplete = " << mSyncFlags->movesWereComplete
                            << ", noProgress = " << mSyncFlags->noProgress;
            }
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
                        continue;
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
                    SyncRow row{&sync->cloudRoot, sync->localroot.get(), &rootFsNode};

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

                if (!us->mConfig.mFinishedInitialScanning &&
                    !sync->localroot->scanRequired())
                {
                    LOG_debug << "Finished initial sync scan at " << sync->localroot->getLocalPath();
                    us->mConfig.mFinishedInitialScanning = true;
                }

                // send stats to the app, per sync
                PerSyncStats counts;
                counts.scanning = sync->localroot->scanRequired();
                counts.syncing = sync->localroot->mightHaveMoves() ||
                                 sync->localroot->syncRequired();
                sync->threadSafeState->getSyncNodeCounts(counts.numFiles, counts.numFolders);
                SyncTransferCounts stc = sync->threadSafeState->transferCounts();
                counts.numUploads = static_cast<int32_t>(stc.mUploads.mPending);
                counts.numDownloads = static_cast<int32_t>(stc.mDownloads.mPending);
                if (us->lastReportedDisplayStats != counts)
                {
                    mClient.app->syncupdate_stats(us->mConfig.mBackupId, counts);
                    us->lastReportedDisplayStats = counts;
                }
            }
        }

        if (mTransferPauseFlagsChanged.load())
        {
            mTransferPauseFlagsChanged = false;

            lock_guard<std::recursive_mutex> guard(mSyncVecMutex);
            for (auto& us : mSyncVec)
            {
                mHeartBeatMonitor->updateOrRegisterSync(*us);
            }
        }

#ifdef MEGA_MEASURE_CODE
        rst.complete();
#endif
        lastRecurseMs = unsigned(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::high_resolution_clock::now() - recurseStart).count());

        const int noProgressCountLoggingFrequency = 500; // Log this every 500 counts
        if (!skippedForScanning && !earlyExit && mSyncFlags->noProgressCount && (mSyncFlags->noProgressCount % noProgressCountLoggingFrequency == 0))
        {
            LOG_verbose << "recursiveSync took ms: " << lastRecurseMs
                        << (skippedForScanning ? " (" + std::to_string(skippedForScanning)+ " skipped due to ongoing scanning)" : "")
                        << (mSyncFlags->noProgressCount ? " no progress count: " + std::to_string(mSyncFlags->noProgressCount) : "")
                        << (earlyExit ? " (earlyExit)" : "");
        }


        waiter->bumpds();
        mSyncFlags->recursiveSyncLastCompletedDs = waiter->ds;

        if (skippedForScanning > 0)
        {
            // avoid flip-flopping on stall state if the stalled paths were inside a sync that is still scanning
            earlyExit = true;
        }

        if (earlyExit)
        {
            unsetSyncsScanningWasComplete_inThread();
            mSyncFlags->scanningWasComplete = false;
            mSyncFlags->reachableNodesAllScannedThisPass = false;
        }
        else
        {
            mSyncFlags->isInitialPass = false;

            // Process name conflicts
            processSyncConflicts();

            // Process scanning updates
            bool anySyncScanning = isAnySyncScanning_inThread();
            if (anySyncScanning != syncscanstate)
            {
                assert(onSyncThread());
                syncscanstate = anySyncScanning;
                mClient.app->syncupdate_scanning(anySyncScanning);
            }

            // Process syncing updates
            bool anySyncBusy = isAnySyncSyncing();
            if (anySyncBusy != syncBusyState)
            {
                assert(onSyncThread());
                syncBusyState = anySyncBusy;
                mClient.app->syncupdate_syncing(anySyncBusy);
            }

            // Process stall issues
            processSyncStalls();

            ++completedPassCount;
        }

        // Process throttling queue
        processDelayedUploads();

        lastLoopEarlyExit = earlyExit;
    }
}

bool Syncs::isAnySyncSyncing() const
{
    assert(onSyncThread());

    lock_guard<std::recursive_mutex> guard(mSyncVecMutex);

    for (auto& us : mSyncVec)
    {
        if (Sync* sync = us->mSync.get())
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
    return false;
}

bool Syncs::isAnySyncScanning_inThread() const
{
    assert(onSyncThread());

    lock_guard<std::recursive_mutex> guard(mSyncVecMutex);

    for (auto& us : mSyncVec)
    {
        if (Sync* sync = us->mSync.get())
        {
            if (sync->isSyncScanning())
            {
                return true;
            }
        }
    }
    return false;
}

bool Syncs::checkSyncsScanningWasComplete_inThread()
{
    assert(onSyncThread());

    lock_guard<std::recursive_mutex> guard(mSyncVecMutex);

    bool allSyncsScanningWereComplete = true;
    for (auto& us : mSyncVec)
    {
        if (Sync* sync = us->mSync.get())
        {
            allSyncsScanningWereComplete &= sync->checkScanningWasComplete();
        }
    }
    return allSyncsScanningWereComplete;
}

void Syncs::unsetSyncsScanningWasComplete_inThread()
{
    assert(onSyncThread());

    lock_guard<std::recursive_mutex> guard(mSyncVecMutex);

    for (auto& us : mSyncVec)
    {
        if (Sync* sync = us->mSync.get())
        {
            sync->unsetScanningWasComplete();
        }
    }
}

bool Syncs::checkSyncsMovesWereComplete()
{
    assert(onSyncThread());

    lock_guard<std::recursive_mutex> guard(mSyncVecMutex);

    bool allSyncsMovesWereComplete = true;
    for (auto& us : mSyncVec)
    {
        if (Sync* sync = us->mSync.get())
        {
            allSyncsMovesWereComplete &= sync->checkMovesWereComplete();
        }
    }
    return allSyncsMovesWereComplete;
}

void Syncs::setSyncsNeedFullSync(bool andFullScan, bool andReFingerprint, handle backupId)
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
                    if (andReFingerprint)
                    {
                        us->mSync->localroot->setSubtreeNeedsRefingerprint();
                    }
                }
            }
        }
    }, "setSyncsNeedFullSync");
}

bool Syncs::conflictsDetected(SyncIDtoConflictInfoMap& conflicts)
{
    assert(onSyncThread());
    size_t totalConflicts{};
    for (auto& us: mSyncVec)
    {
        if (Sync* sync = us->mSync.get(); sync && sync->localroot->conflictsDetected())
        {
            auto [it, success] = conflicts.emplace(us->mConfig.mBackupId, list<NameConflict>());
            if (!success)
            {
                assert(false);
                LOG_err << "[Syncs::conflictsDetected()] cannot add entry at conflicts map "
                           "with BackUpId: "
                        << toHandle(us->mConfig.mBackupId);
                conflicts.clear();
                break;
            }
            sync->recursiveCollectNameConflicts(&it->second);
            totalConflicts += it->second.size();
        }
    }
    // Disable sync conflicts update flag
    mClient.app->syncupdate_totalconflicts(false);
    // totalSyncConflicts is set by conflictsDetectedCount, whose count is limited by the previous
    // number of conflicts + 1, in order to avoid extra recursive operations as the full count is
    // not needed This updates the counter to the real number of conflicts, so we avoid incremental
    // updates later (from previous_conflicts_size + 1 to actual_conflicts_size)
    totalSyncConflicts.store(totalConflicts);
    return totalConflicts > 0;
}

size_t Syncs::conflictsDetectedCount(size_t limit) const
{
    assert(onSyncThread());

    size_t count = 0;
    for (auto& us : mSyncVec)
    {
        if (Sync* sync = us->mSync.get())
        {
            if (sync->localroot->conflictsDetected())
            {
                if (limit == 1)
                {
                    count = limit;
                    break;
                }
                sync->recursiveCollectNameConflicts(nullptr, &count, limit ? &limit : nullptr);
                if (count >= limit) break;
            }
        }
    }
    return count;
}

void Syncs::collectSyncNameConflicts(handle backupId, std::function<void(list<NameConflict>&& nc)> completion, bool completionInClient)
{
    assert(!onSyncThread());

    auto clientCompletion = [this, completion](list<NameConflict>&& nc)
    {
        shared_ptr<list<NameConflict>> ncptr(new list<NameConflict>(std::move(nc)));
        queueClient(
            [completion, ncptr](MegaClient&, TransferDbCommitter&)
            {
                if (completion)
                    completion(std::move(*ncptr));
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
                    us->mSync->recursiveCollectNameConflicts(&nc);
                }
            }
            finalcompletion(std::move(nc));
        }, "collectSyncNameConflicts");
}

bool Syncs::stallsDetected(SyncStallInfo& stallInfo)
{
    assert(onSyncThread());

    if (syncStallState) // If in stall state, there must be either lack of progress or immediate alerts
    {
        for (auto& [id, syncStallInfoMap] : stallReport.syncStallInfoMaps)
        {
            if (syncStallInfoMap.hasProgressLack())
            {
                for (auto& r: syncStallInfoMap.cloud) stallInfo.syncStallInfoMaps[id].cloud.insert(r);
                for (auto& r: syncStallInfoMap.local) stallInfo.syncStallInfoMaps[id].local.insert(r);
            }
            else
            {
                for (auto& r: syncStallInfoMap.cloud)
                {
                    if (r.second.alertUserImmediately) stallInfo.syncStallInfoMaps[id].cloud.insert(r);
                }
                for (auto& r: syncStallInfoMap.local)
                {
                    if (r.second.alertUserImmediately) stallInfo.syncStallInfoMaps[id].local.insert(r);
                }
            }
        }
        // Update totalSyncStalls just in case it is different since last check
        totalSyncStalls.store(stallInfo.size());
    }

    // Disable sync stalls update flag
    mClient.app->syncupdate_totalstalls(false);
    return syncStallState;
}

size_t Syncs::stallsDetectedCount() const
{
    assert(onSyncThread());

    if (!syncStallState)
    {
        return 0;
    }

    auto reportableSize = stallReport.reportableSize();
    if (reportableSize <= 0)
    {
        LOG_warn << "[Syncs::stallsDetectedCount()] reportableSize (" << reportableSize << ") is not positive! [real size = " << stallReport.size() << "]";
        assert(hasImmediateStall(stallReport)); // If in sync stall state, there must be reportable size (unless there is a custom hasImmediateStall predicate for tests)
    }
    return reportableSize;
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

void Syncs::processSyncConflicts()
{
    assert(onSyncThread());

    bool conflictsNow =
        conflictsDetectedCount(1) > 0; // We only need to know if we have at least 1 conflict
    if (conflictsNow != syncConflictState)
    {
        assert(onSyncThread());
        syncConflictState = conflictsNow;
        mClient.app->syncupdate_totalconflicts(false);
        mClient.app->syncupdate_conflicts(conflictsNow);
        if (conflictsNow)
        {
            assert(!totalSyncConflicts.load());
            auto conflictsCount = conflictsDetectedCount();
            totalSyncConflicts.store(conflictsCount);
            lastSyncConflictsCount = std::chrono::steady_clock::now();
        }
        else
        {
            totalSyncConflicts.store(0);
        }
    }
    else if (conflictsNow && !mClient.app->isSyncStalledChanged() &&
            ((std::chrono::steady_clock::now() - lastSyncConflictsCount) >= MIN_DELAY_BETWEEN_SYNC_STALLS_OR_CONFLICTS_COUNT))
    {
        auto minDelayBetweenConflictsCount = std::min(std::max(MIN_DELAY_BETWEEN_SYNC_STALLS_OR_CONFLICTS_COUNT,
                                                                std::chrono::milliseconds(totalSyncConflicts.load() * 10)), // 1 second for every 100 conflicts
                                                        MAX_DELAY_BETWEEN_SYNC_STALLS_OR_CONFLICTS_COUNT);
        if ((std::chrono::steady_clock::now() - lastSyncConflictsCount) >= minDelayBetweenConflictsCount)
        {
            auto updatedTotalSyncsConflict = conflictsDetectedCount(totalSyncConflicts.load() + 1); // We only need to know either if there are now less conflicts or at least one conflict more than before
            if (totalSyncConflicts.load() != updatedTotalSyncsConflict)
            {
                assert(onSyncThread());
                mClient.app->syncupdate_totalconflicts(true);
                LOG_info << mClient.clientname << "Sync conflicting paths state app update notified [previousSyncConflictsTotal = " << totalSyncConflicts.load() << ", updatedTotalSyncsConflict = " << updatedTotalSyncsConflict << "]";
                totalSyncConflicts.store(updatedTotalSyncsConflict);
            }
            lastSyncConflictsCount = std::chrono::steady_clock::now();
        }
    }
}

void Syncs::processSyncStalls()
{
    assert(onSyncThread());

    bool stalled = syncStallState;
    {
        for (auto i = badlyFormedIgnoreFilePaths.begin(); i != badlyFormedIgnoreFilePaths.end(); )
        {
            if (auto lp = i->lock())
            {
                if (lp->sync)
                {
                    auto& affectedBackupId = lp->sync->getConfig().mBackupId;
                    assert(affectedBackupId != UNDEF);
                    mSyncFlags->stall.waitingLocal(affectedBackupId, lp->localPath, SyncStallEntry(
                        SyncWaitReason::FileIssue, true, false,
                        {},
                        {},
                        {lp->localPath, PathProblem::IgnoreFileMalformed},
                        {}));
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
                if (sbp->localNode->exclusionState() == ES_EXCLUDED)
                {
                    // the user ignored the path in response to the stall item, probably
                    i = scanBlockedPaths.erase(i);
                    continue;
                }

                if (sbp->scanBlockedTimer.armed())
                {
                    if (sbp->folderUnreadable)
                    {
                        LOG_verbose << "Scan blocked timer elapsed, trigger folder rescan: " << sbp->scanBlockedLocalPath;
                        sbp->localNode->setScanAgain(false, true, false, 0);
                        sbp->scanBlockedTimer.backoff(); // wait increases exponentially (up to 10 mins) until we succeed
                    }
                    else
                    {
                        LOG_verbose << "Locked file fingerprint timer elapsed, trigger folder rescan: " << sbp->scanBlockedLocalPath;
                        sbp->localNode->setScanAgain(false, true, false, 0);
                        sbp->scanBlockedTimer.backoff(300); // limited wait (30s) as the user will expect it uploaded fairly soon after they stop editing it
                    }
                }

                auto affectedBackupId = sbp->sync ? sbp->sync->getConfig().mBackupId : UNDEF;
                assert(affectedBackupId != UNDEF);

                if (sbp->folderUnreadable)
                {
                    mSyncFlags->stall.waitingLocal(affectedBackupId, sbp->scanBlockedLocalPath, SyncStallEntry(
                        SyncWaitReason::FileIssue, true, false,
                        {},
                        {},
                        {sbp->scanBlockedLocalPath, PathProblem::FilesystemErrorListingFolder},
                        {}));
                }
                else
                {
                    assert(sbp->filesUnreadable);
                    LOG_verbose << "Locked file(s) fingerprint inside this path (resulting in scan blocked path): " << sbp->scanBlockedLocalPath;
                }
                ++i;
            }
            else i = scanBlockedPaths.erase(i);
        }

        // Update progress counters
        mSyncFlags->stall.updateNoProgress();

        lock_guard<mutex> g(stallReportMutex);
        // Update stall report with the stalls created during the loop
        stallReport.moveFromButKeepCountersAndClearObsoleteKeys(mSyncFlags->stall);

        // Check immediate stalls and progress lack stalls
        bool immediateStall = hasImmediateStall(stallReport);
        bool progressLackStall = stallReport.hasProgressLackStall()
                                      && mSyncFlags->reachableNodesAllScannedThisPass;

        stalled = !stallReport.empty()
                    && (immediateStall || progressLackStall);

        if (stalled)
        {
            for (auto& [syncId, syncStallInfoMap] : stallReport.syncStallInfoMaps)
            {
                bool syncProgressLackStall = syncStallInfoMap.hasProgressLack();
                for (auto& p : syncStallInfoMap.cloud) if (syncProgressLackStall || isImmediateStall(p.second)) { SYNCS_verbose_timed << "Stall detected! stalled node path (" << syncWaitReasonDebugString(p.second.reason) << "): " << p.first << " [backupId = " << syncId << "]"; }
                for (auto& p : syncStallInfoMap.local) if (syncProgressLackStall || isImmediateStall(p.second)) { SYNCS_verbose_timed << "Stall detected! stalled local path (" << syncWaitReasonDebugString(p.second.reason) << "): " << p.first << " [backupId = " << syncId << "]"; }
            }
        }
    }

    if (stalled != syncStallState)
    {
        assert(onSyncThread());
        syncStallState = stalled;
        mClient.app->syncupdate_totalstalls(false);
        mClient.app->syncupdate_stalled(stalled);
        if (stalled)
        {
            assert(!totalSyncStalls.load());
            totalSyncStalls.store(stallsDetectedCount());
            lastSyncStallsCount = std::chrono::steady_clock::now();
        }
        else
        {
            totalSyncStalls.store(0);
        }
        LOG_warn << mClient.clientname << "Stall state app notified: " << stalled;
    }
    else if (stalled && !mClient.app->isSyncStalledChanged() &&
            ((std::chrono::steady_clock::now() - lastSyncStallsCount) >= MIN_DELAY_BETWEEN_SYNC_STALLS_OR_CONFLICTS_COUNT))
    {
        auto updatedTotalSyncsStalls = stallsDetectedCount();
        if (totalSyncStalls.load() != updatedTotalSyncsStalls)
        {
            assert(onSyncThread());
            mClient.app->syncupdate_totalstalls(true);
            LOG_warn << mClient.clientname << "Stall state app update notified [previousSyncStallsTotal = " << totalSyncStalls.load() << ", updatedTotalSyncsStalls = " << updatedTotalSyncsStalls << "]";
            totalSyncStalls.store(updatedTotalSyncsStalls);
        }
        lastSyncStallsCount = std::chrono::steady_clock::now();
    }
}

void Syncs::proclocaltree(LocalNode* n, LocalTreeProc* tp)
{
    assert(onSyncThread());

    if (n->type > FILENODE)
    {
        for (localnode_map::iterator it = n->children.begin(); it != n->children.end(); )
        {
            assert(it->first == it->second->localname);
            LocalNode *child = it->second;
            it++;
            proclocaltree(child, tp);
        }
    }

    tp->proc(*n->sync->syncs.fsaccess, n);
}

bool Syncs::isCloudNodeInShare(const CloudNode& cn)
{
    lock_guard g(mClient.nodeTreeMutex);
    std::shared_ptr<Node> n = mClient.mNodeManager.getNodeByHandle(cn.handle);
    return n && n->matchesOrHasAncestorMatching(
                    [](const Node& node) -> bool
                    {
                        return node.inshare != nullptr;
                    });
}

bool Syncs::lookupCloudNode(NodeHandle h, CloudNode& cn, string* cloudPath, bool* isInTrash,
        bool* nodeIsInActiveSyncQuery,
        bool* nodeIsDefinitelyExcluded,
        unsigned* depth,
        WhichCloudVersion whichVersion,
        handle* owningUser,
        vector<pair<handle, int>>* sdsBackups)
{
    // we have to avoid doing these lookups when the client thread might be changing the Node tree
    // so we use the mutex to prevent access during that time - which is only actionpacket processing.
    assert(onSyncThread());
    assert(!nodeIsDefinitelyExcluded || nodeIsInActiveSyncQuery); // if you ask if it's excluded, you must ask if it's in sync too

    if (h.isUndef()) return false;

    vector<pair<NodeHandle, Sync*>> activeSyncHandles;
    vector<pair<std::shared_ptr<Node>, Sync*>> activeSyncRoots;

    if (nodeIsInActiveSyncQuery)
    {
        *nodeIsInActiveSyncQuery = false;

        for (auto & us : mSyncVec)
        {
            if (us->mSync && !us->mConfig.mError)
            {
                activeSyncHandles.emplace_back(us->mConfig.mRemoteNode, us->mSync.get());
            }
        }
    }

    lock_guard<mutex> g(mClient.nodeTreeMutex);

    if (nodeIsInActiveSyncQuery)
    {
        for (auto & rh : activeSyncHandles)
        {
            if (std::shared_ptr<Node> rn = mClient.mNodeManager.getNodeByHandle(rh.first))
            {
                activeSyncRoots.emplace_back(rn, rh.second);
            }
        }
    }

    if (std::shared_ptr<Node> n = mClient.mNodeManager.getNodeByHandle(h))
    {
        switch (whichVersion)
        {
            case EXACT_VERSION:
                break;

            case LATEST_VERSION:
                {
                    std::shared_ptr<Node> m = n->latestFileVersion();
                    if (m != n)
                    {
                        auto& syncs = *this;
                        SYNC_verbose << "Looking up Node " << n->nodeHandle() << " chose latest version " << m->nodeHandle();
                        n = m;
                    }
                }
                break;

            case LATEST_VERSION_ONLY:
                if (n->type != FILENODE)
                    break;

                if (n->parent && n->parent->type == FILENODE)
                    return false;

                break;

            case FOLDER_ONLY:
                assert(n->type > FILENODE);
                break;
        }

        if (isInTrash)
        {
            *isInTrash = n->firstancestor()->nodeHandle() == mClient.mNodeManager.getRootNodeRubbish();
        }

        if (cloudPath) *cloudPath = n->displaypath();

        if (depth) *depth = n->depth();

        if (sdsBackups) *sdsBackups = n->getSdsBackups();

        cn = CloudNode(*n);

        if (nodeIsInActiveSyncQuery)
        {
            auto it = std::find_if(activeSyncRoots.begin(), activeSyncRoots.end(),
                          [n](const pair<std::shared_ptr<Node>, Sync *> &rn) { return n->isbelow(rn.first.get()); });
            if (it != activeSyncRoots.end())
            {
                *nodeIsInActiveSyncQuery = true;
                if (nodeIsDefinitelyExcluded) *nodeIsDefinitelyExcluded = isDefinitelyExcluded(*it, n);
            }
        }

        if (owningUser)
        {
            if (auto& inshare = n->firstancestor()->inshare)
            {
                *owningUser = inshare->user->userhandle;
            }
            else
            {
                *owningUser = mClient.me;
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
    if (std::shared_ptr<Node> n = mClient.mNodeManager.getNodeByHandle(h))
    {
        assert(n->type > FILENODE);
        assert(!n->parent || n->parent->type > FILENODE);

        sharedNode_list nl = mClient.mNodeManager.getChildren(n.get());
        cloudChildren.reserve(nl.size());

        for (auto& c : nl)
        {
            cloudChildren.push_back(*c);
            assert(cloudChildren.back().parentHandle == h);
        }
        return true;
    }
    return false;
}

bool Syncs::isDefinitelyExcluded(const pair<std::shared_ptr<Node>, Sync*>& root, std::shared_ptr<const Node> child)
{
    // Make sure we're on the sync thread.
    assert(onSyncThread());

    // Make sure we're looking at the latest version of child.
    child = child->latestFileVersion();

    // Sanity.
    assert(child->isbelow(root.first.get()));

    // Determine the trail from root to child.
    vector<pair<NodeHandle, string>> trail;

    for (auto node = child; node != root.first; node = node->parent)
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
        if (node->toName_of_localname != i->second)
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

Sync* Syncs::syncContainingPath(const LocalPath& path)
{
    auto predicate = [&path](const UnifiedSync& us) {
        return us.mConfig.mLocalPath.isContainingPathOf(path);
    };

    return syncMatching(std::move(predicate));
}

Sync* Syncs::syncContainingPath(const string& path)
{
    auto predicate = [&path](const UnifiedSync& us) {
        return IsContainingCloudPathOf(
                 us.mConfig.mOriginalPathOfRemoteRootNode,
                 path);
    };

    return syncMatching(std::move(predicate));
}

void Syncs::ignoreFileLoadFailure(const Sync& sync, const LocalPath& path)
{
    // We should never be asked to report multiple ignore file failures.
    assert(mIgnoreFileFailureContext.mBackupID == UNDEF);

    // Record the failure.
    mIgnoreFileFailureContext.mBackupID = sync.getConfig().mBackupId;
    mIgnoreFileFailureContext.mPath = path;
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
        auto root = mClient.mNodeManager.getNodeByHandle(config.mRemoteNode);

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

bool Sync::PerFolderLogSummaryCounts::report(string& s)
{
    if (alreadySyncedCount)
    {
        s += " alreadySynced:" + std::to_string(alreadySyncedCount);
    }
    if (alreadyUploadingCount)
    {
        s += " alreadyUploading:" + std::to_string(alreadyUploadingCount);
    }
    if (alreadyDownloadingCount)
    {
        s += " alreadyDownloading:" + std::to_string(alreadyDownloadingCount);
    }
    return !s.empty();
}

// Syncs Upload Throttling

void Syncs::processDelayedUploads()
{
    assertThrottlingManagerIsValid();

    const auto queueUploadToClient = [this](DelayedSyncUpload&& delayedSyncUpload)
    {
        if (!delayedSyncUpload.mWeakUpload.lock())
        {
            LOG_warn << "[UploadThrottle] Upload is no longer valid";
            return;
        }

        queueClient(
            [delayedSyncUpload = std::move(delayedSyncUpload)](MegaClient& mc,
                                                               TransferDbCommitter& committer)
            {
                if (auto upload =
                        delayedSyncUpload.mWeakUpload.lock()) // Check again if upload is valid
                {
                    LOG_debug << "[UploadThrottle] Sync - Sending delayed file "
                              << upload->getLocalname();
                    clientUpload(mc,
                                 committer,
                                 upload,
                                 delayedSyncUpload.mVersioningOption,
                                 delayedSyncUpload.mQueueFirst,
                                 delayedSyncUpload.mOvHandleIfShortcut);
                }
                else
                {
                    LOG_warn << "[UploadThrottle] Upload no longer valid inside queueClient";
                }
            });
    };

    mThrottlingManager->processDelayedUploads(queueUploadToClient);
}

void Syncs::addToDelayedUploads(DelayedSyncUpload&& delayedUpload)
{
    assertThrottlingManagerIsValid();
    mThrottlingManager->addToDelayedUploads(std::move(delayedUpload));
}

std::chrono::seconds Syncs::uploadCounterInactivityExpirationTime() const
{
    assertThrottlingManagerIsValid();
    return mThrottlingManager->uploadCounterInactivityExpirationTime();
}

std::chrono::seconds Syncs::throttleUpdateRate() const
{
    assertThrottlingManagerIsValid();
    return mThrottlingManager->throttleUpdateRate();
}

unsigned Syncs::maxUploadsBeforeThrottle() const
{
    assertThrottlingManagerIsValid();
    return mThrottlingManager->maxUploadsBeforeThrottle();
}

void Syncs::setThrottleUpdateRate(const std::chrono::seconds throttleUpdateRate,
                                  std::function<void(const error)>&& completion)
{
    assert(!onSyncThread());

    queueSync(
        [this,
         throttleUpdateRate,
         completionForClientWrapped =
             wrapToRunInClientThread(std::move(completion), FromAnyThread::yes)]() mutable
        {
            assertThrottlingManagerIsValid();
            const error result =
                mThrottlingManager->setThrottleUpdateRate(throttleUpdateRate) ? API_OK : API_EARGS;
            completionForClientWrapped(result);
        },
        "setThrottleUpdateRate");
}

void Syncs::setMaxUploadsBeforeThrottle(const unsigned maxUploadsBeforeThrottle,
                                        std::function<void(const error)>&& completion)
{
    assert(!onSyncThread());

    queueSync(
        [this,
         maxUploadsBeforeThrottle,
         completionForClientWrapped =
             wrapToRunInClientThread(std::move(completion), FromAnyThread::yes)]() mutable
        {
            assertThrottlingManagerIsValid();
            const error result =
                mThrottlingManager->setMaxUploadsBeforeThrottle(maxUploadsBeforeThrottle) ?
                    API_OK :
                    API_EARGS;
            completionForClientWrapped(result);
        },
        "setMaxUploadsBeforeThrottle");
}

void Syncs::uploadThrottleValues(
    std::function<void(const std::chrono::seconds, const unsigned)>&& completion)
{
    assert(!onSyncThread());

    queueSync(
        [this,
         completionForClientWrapped =
             wrapToRunInClientThread(std::move(completion), FromAnyThread::yes)]() mutable
        {
            assertThrottlingManagerIsValid();

            const auto updateRateInSecondsLimit = mThrottlingManager->throttleUpdateRate();
            const auto maxUploadsBeforeThrottleLimit =
                mThrottlingManager->maxUploadsBeforeThrottle();

            completionForClientWrapped(updateRateInSecondsLimit, maxUploadsBeforeThrottleLimit);
        },
        "uploadThrottleValues");
}

void Syncs::uploadThrottleValuesLimits(std::function<void(ThrottleValueLimits&&)>&& completion)
{
    assert(!onSyncThread());

    queueSync(
        [this,
         completionForClientWrapped =
             wrapToRunInClientThread(std::move(completion), FromAnyThread::yes)]() mutable
        {
            assertThrottlingManagerIsValid();

            completionForClientWrapped(mThrottlingManager->throttleValueLimits());
        },
        "uploadThrottleValuesLimits");
}

void Syncs::checkSyncUploadsThrottled(std::function<void(const bool)>&& completion)
{
    assert(!onSyncThread());

    queueSync(
        [this,
         completionForClientWrapped =
             wrapToRunInClientThread(std::move(completion), FromAnyThread::yes)]() mutable
        {
            assertThrottlingManagerIsValid();

            completionForClientWrapped(mThrottlingManager->anyDelayedUploads());
        },
        "checkSyncUploadsThrottled");
}

void Syncs::setThrottlingManager(std::shared_ptr<IUploadThrottlingManager> uploadThrottlingManager,
                                 std::function<void(const error)>&& completion)
{
    assert(!onSyncThread());

    queueSync(
        [this,
         uploadThrottlingManager = std::move(uploadThrottlingManager),
         completionForClientWrapped =
             wrapToRunInClientThread(std::move(completion), FromAnyThread::yes)]() mutable
        {
            if (!uploadThrottlingManager)
            {
                completionForClientWrapped(API_EARGS);
            }

            LOG_debug << "[Syncs::setThrottlingManager] Setting a new Throttling Manager. All "
                         "previous stored values will be lost";
            mThrottlingManager = std::move(uploadThrottlingManager);
            completionForClientWrapped(API_OK);
        },
        "setThrottlingManager");
}

void Syncs::assertThrottlingManagerIsValid() const
{
    assert(onSyncThread());

    if (mThrottlingManager)
        return;

    LOG_err << "Syncs upload throttling manager is not valid and it should! It is expected to be "
               "initialized from Syncs constructor";
    assert(false && "Syncs upload throttling manager should always be valid!!!");
}

} // namespace

#endif
