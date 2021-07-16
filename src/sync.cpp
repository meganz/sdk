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

ScanService::ScanService(Waiter& waiter)
  : mCookie(std::make_shared<Cookie>(waiter))
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

auto ScanService::queueScan(LocalPath targetPath, bool followSymlinks, map<LocalPath, FSNode>&& priorScanChildren) -> RequestPtr
{
    // Create a request to represent the scan.
    auto request = std::make_shared<ScanRequest>(mCookie, followSymlinks, targetPath, move(priorScanChildren));

    // Queue request for processing.
    mWorker->queue(request);

    return request;
}

ScanService::ScanRequest::ScanRequest(std::shared_ptr<Cookie> cookie,
                                      bool followSymLinks,
                                      LocalPath targetPath,
                                      map<LocalPath, FSNode>&& priorScanChildren)
  : mCookie(move(cookie))
  , mComplete(false)
  , mFollowSymLinks(followSymLinks)
  , mKnown(move(priorScanChildren))
  , mResults()
  , mTargetPath(std::move(targetPath))
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
          request->mTargetPath.toPath(*mFsAccess);

        LOG_verbose << "Directory scan begins: " << targetPath;

        // Process the request.
        scan(request);

        // Mark the request as complete.
        request->mComplete = true;

        LOG_verbose << "Directory scan ended: " << targetPath;

        // Do we still have someone to notify?
        auto cookie = request->mCookie.lock();

        if (cookie)
        {
            // Yep, let them know the request is complete.
            cookie->completed();
        }
        else
        {
            LOG_debug << "No waiter, discarding "
                      << request->mResults.size()
                      << " scan result(s).";
        }
    }
}

FSNode ScanService::Worker::interrogate(DirAccess& iterator,
                                        const LocalPath& name,
                                        LocalPath& path,
                                        ScanRequest& request)
{
    auto reuseFingerprint =
      [](const FSNode& lhs, const FSNode& rhs)
      {
          return lhs.type == rhs.type
                 && lhs.fsid == rhs.fsid
                 && lhs.fingerprint.mtime == rhs.fingerprint.mtime
                 && rhs.fingerprint.size == rhs.fingerprint.size;
      };

    FSNode result;
    auto& known = request.mKnown;

    // Always record the name.
    result.localname = name;
    result.name = name.toName(*mFsAccess);

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
                      << path.toPath(*mFsAccess);
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
        }

        return result;
    }

    // Couldn't open the file.
    LOG_warn << "Error opening file: " << path.toPath(*mFsAccess);

    // File's blocked if the error is transient.
    result.isBlocked = fileAccess->retry;

    // Warn about the blocked file.
    if (result.isBlocked)
    {
        LOG_warn << "File blocked: " << path.toPath(*mFsAccess);
    }

    return result;
}

// Really we only have one worker despite the vector of threads - maybe we should just have one
// regardless of multiple clients too - there is only one filesystem after all (but not singleton!!)
CodeCounter::ScopeStats ScanService::syncScanTime = { "folderScan" };

void ScanService::Worker::scan(ScanRequestPtr request)
{
    CodeCounter::ScopeTimer rst(syncScanTime);

    // Have we been passed a valid target path?
    auto fileAccess = mFsAccess->newfileaccess();
    auto path = request->mTargetPath;

    if (!fileAccess->fopen(path, true, false))
    {
        LOG_debug << "Scan target does not exist: "
                  << path.toPath(*mFsAccess);
        return;
    }

    // Does the path denote a directory?
    if (fileAccess->type != FOLDERNODE)
    {
        LOG_debug << "Scan target is not a directory: "
                  << path.toPath(*mFsAccess);
        return;
    }

    std::unique_ptr<DirAccess> dirAccess(mFsAccess->newdiraccess());
    LocalPath name;

    // Can we open the directory?
    if (!dirAccess->dopen(&path, fileAccess.get(), false))
    {
        LOG_debug << "Unable to iterate scan target: "
                  << path.toPath(*mFsAccess);
        return;
    }

    // Process each file in the target.
    std::vector<FSNode> results;

    while (dirAccess->dnext(path, name, request->mFollowSymLinks))
    {
        ScopedLengthRestore restorer(path);
        path.appendWithSeparator(name, false);

        // Learn everything we can about the file.
        auto info = interrogate(*dirAccess, name, path, *request);
        results.emplace_back(std::move(info));
    }

    // No need to keep this data around anymore.
    request->mKnown.clear();

    // Publish the results.
    request->mResults = std::move(results);
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

string SyncPath::localPath_utf8()
{
    return localPath.toPath(*syncs.fsaccess);
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
        localPath.appendWithSeparator(LocalPath::fromName(row.cloudNode->name, *syncs.fsaccess, filesystemType), true);
    }
    else if (!row.cloudClashingNames.empty() || !row.fsClashingNames.empty())
    {
        // so as not to mislead in logs etc
        localPath.appendWithSeparator(LocalPath::fromName("<<<clashing>>>", *syncs.fsaccess, filesystemType), true);
    }
    else
    {
        // this is a legitimate case; eg. we only had a syncNode and it is removed in resolve_delSyncNode
        return false;
    }

    // add to cloudPath
    cloudPath += "/";
    if (row.cloudNode)
    {
        cloudPath += row.cloudNode->name;
    }
    else if (row.syncNode)
    {
        cloudPath += row.syncNode->name;
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
        syncPath += row.syncNode->name;
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
    , mLocalFingerprint(localFingerprint)
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
           && mLocalFingerprint == rhs.mLocalFingerprint
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

NodeHandle SyncConfig::getRemoteNode() const
{
    return mRemoteNode;
}

void SyncConfig::setRemoteNode(NodeHandle remoteNode)
{
    mRemoteNode = remoteNode;
}

handle SyncConfig::getLocalFingerprint() const
{
    return mLocalFingerprint;
}

void SyncConfig::setLocalFingerprint(fsfp_t fingerprint)
{
    mLocalFingerprint = fingerprint;
}

SyncConfig::Type SyncConfig::getType() const
{
    return mSyncType;
}


SyncError SyncConfig::getError() const
{
    return mError;
}

void SyncConfig::setError(SyncError value)
{
    mError = value;
}

handle SyncConfig::getBackupId() const
{
    return mBackupId;
}

void SyncConfig::setBackupId(const handle &backupId)
{
    mBackupId = backupId;
}

bool SyncConfig::isBackup() const
{
    return mSyncType == TYPE_BACKUP;
}

bool SyncConfig::isExternal() const
{
    return !mExternalDrivePath.empty();
}

bool SyncConfig::errorOrEnabledChanged()
{
    bool changed = mError != mKnownError ||
                   mEnabled != mKnownEnabled;

    if (changed)
    {
        mKnownError = mError;
        mKnownEnabled = mEnabled;
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
    case LOCAL_FINGERPRINT_MISMATCH:
        return "Local fingerprint mismatch";
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

const char* SyncConfig::syncstatename(const syncstate_t state)
{
    switch (state)
    {
    case SYNC_DISABLED:
        return "DISABLED";
    case SYNC_FAILED:
        return "FAILED";
    case SYNC_CANCELED:
        return "CANCELED";
    case SYNC_INITIALSCAN:
        return "INITIALSCAN";
    case SYNC_ACTIVE:
        return "ACTIVE";
    default:
        return "UNKNOWN";
    }
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

// new Syncs are automatically inserted into the session's syncs list
// and a full read of the subtree is initiated
Sync::Sync(UnifiedSync& us, const string& cdebris,
           const LocalPath& clocaldebris, Node* remotenode, bool cinshare, const string& logname)
: syncs(us.syncs)
, localroot(new LocalNode)
, mUnifiedSync(us)
, syncscanbt(us.syncs.rng)
{
    assert(cdebris.empty() || clocaldebris.empty());
    assert(!cdebris.empty() || !clocaldebris.empty());

    isnetwork = false;
    inshare = cinshare;
    tmpfa = NULL;
    syncname = logname; // can be updated to be more specific in logs
    //initializing = true;

    localnodes[FILENODE] = 0;
    localnodes[FOLDERNODE] = 0;

    state = SYNC_INITIALSCAN;

    //fullscan = true;
    //scanseqno = 0;

    mLocalPath = mUnifiedSync.mConfig.getLocalPath();

    if (mUnifiedSync.mConfig.isBackup())
    {
        mUnifiedSync.mConfig.setBackupState(SYNC_BACKUP_MIRROR);
    }

    mFilesystemType = syncs.fsaccess->getlocalfstype(mLocalPath);

    localroot->init(this, FOLDERNODE, NULL, mLocalPath, nullptr);  // the root node must have the absolute path.  We don't store shortname, to avoid accidentally using relative paths.
    localroot->setSyncedNodeHandle(remotenode->nodeHandle(), remotenode->displayname());
    localroot->setScanAgain(false, true, true, 0);
    localroot->setCheckMovesAgain(false, true, true);
    localroot->setSyncAgain(false, true, true);

    if (!cdebris.empty())
    {
        debris = cdebris;
        localdebrisname = LocalPath::fromPath(debris, *syncs.fsaccess);
        localdebris = localdebrisname;
        localdebris.prependWithSeparator(mLocalPath);
    }
    else
    {
        localdebrisname = clocaldebris.leafName();
        localdebris = clocaldebris;
    }
    // notifications may be queueing from this moment
    dirnotify.reset(syncs.fsaccess->newdirnotify(*localroot, mLocalPath, &syncs.waiter));

    // set specified fsfp or get from fs if none
    const auto cfsfp = mUnifiedSync.mConfig.getLocalFingerprint();
    if (cfsfp)
    {
        fsfp = cfsfp;
    }
    else
    {
        fsfp = dirnotify->fsfingerprint();
    }

    fsstableids = dirnotify->fsstableids();
    LOG_info << "Filesystem IDs are stable: " << fsstableids;

    // Always create a watch for the root node.
    localroot->watch(mLocalPath, UNDEF);

#ifdef __APPLE__
    // Assume FS events are relative to the sync root.
    mFsEventsPath = mLocalPath.platformEncoded();

    // Are we running on Catalina or newer?
    if (macOSmajorVersion() >= 19)
    {
        auto root = "/System/Volumes/Data" + mFsEventsPath;

        LOG_debug << "MacOS 10.15+ filesystem detected.";
        LOG_debug << "Checking FSEvents path: " << root;

        // Check for presence of volume metadata.
        if (auto fd = open(root.c_str(), O_RDONLY); fd >= 0)
        {
            // Make sure it's actually about the root.
            if (char buf[MAXPATHLEN]; fcntl(fd, F_GETPATH, buf) >= 0)
            {
                // Awesome, let's use the FSEvents path.
                mFsEventsPath = root;
            }

            close(fd);
        }
        else
        {
            LOG_debug << "Unable to open FSEvents path.";
        }

        // Safe as root is strictly larger.
        auto usingEvents = mFsEventsPath.size() == root.size();

        LOG_debug << "Using "
                  << (usingEvents ? "FSEvents" : "standard")
                  << " paths for detecting filesystem notifications: "
                  << mFsEventsPath;
    }
#endif

    // load LocalNodes from cache (only for internal syncs)

// todo: assuming for now, dbaccess is thread safe

    if (syncs.mClient.dbaccess && !us.mConfig.isExternal())
    {
        // open state cache table
        handle tableid[3];
        string dbname;

        auto fas = syncs.fsaccess->newfileaccess(false);

        if (fas->fopen(mLocalPath, true, false))
        {
            tableid[0] = fas->fsid;
            tableid[1] = remotenode->nodehandle;
            tableid[2] = syncs.mClient.me;

            dbname.resize(sizeof tableid * 4 / 3 + 3);
            dbname.resize(Base64::btoa((byte*)tableid, sizeof tableid, (char*)dbname.c_str()));

            statecachetable.reset(syncs.mClient.dbaccess->open(syncs.rng, *syncs.fsaccess, dbname));

            localroot->fsid_lastSynced = fas->fsid;
            readstatecache();
        }
    }
    else
    {
        // we still need the fsid of the synced folder
        auto fas = syncs.fsaccess->newfileaccess(false);

        if (fas->fopen(mLocalPath, true, false))
        {
            localroot->fsid_lastSynced = fas->fsid;
        }
    }
}

Sync::~Sync()
{
    // must be set to prevent remote mass deletion while rootlocal destructor runs
    mDestructorRunning = true;

    // unlock tmp lock
    tmpfa.reset();


    // The database is closed; deleting localnodes will not remove them
    statecachetable.reset();

    // This will recursively delete all LocalNodes in the sync.
    // If they have transfers associated, the SyncUpload_inClient and <tbd> will have their wasRequesterAbandoned flag set true
    localroot.reset();
}

bool Sync::backupModified()
{
    changestate(SYNC_DISABLED, BACKUP_MODIFIED, false, true);
    return false;
}

bool Sync::isBackup() const
{
    return getConfig().isBackup();
}

bool Sync::isBackupAndMirroring() const
{
    return isBackup() &&
           getConfig().getBackupState() == SYNC_BACKUP_MIRROR;
}

bool Sync::isBackupMonitoring() const
{
    return getConfig().getBackupState() == SYNC_BACKUP_MONITOR;
}

void Sync::setBackupMonitoring()
{
    auto& config = getConfig();

    assert(config.getBackupState() == SYNC_BACKUP_MIRROR);

    config.setBackupState(SYNC_BACKUP_MONITOR);

    syncs.saveSyncConfig(config);
}

bool Sync::active() const
{
    switch (state)
    {
    case SYNC_ACTIVE:
    case SYNC_INITIALSCAN:
        return true;
    default:
        break;
    }

    return false;
}

void Sync::setSyncPaused(bool pause)
{
    syncPaused = pause;
    syncs.mSyncFlags->isInitialPass = true;
}

bool Sync::isSyncPaused() {
    return syncPaused;
}

Node* Sync::cloudRoot()
{
// todo: fix this
    return syncs.mClient.nodeByHandle(localroot->syncedCloudNodeHandle);
}

void Sync::addstatecachechildren(uint32_t parent_dbid, idlocalnode_map* tmap, LocalPath& localpath, LocalNode *p, int maxdepth)
{
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

        l->init(this, l->type, p, localpath, std::move(shortname));

#ifdef DEBUG
        if (fsid != UNDEF)
        {
            auto fa = syncs.fsaccess->newfileaccess(false);
            if (fa->fopen(localpath))  // exists, is file
            {
                auto sn = syncs.fsaccess->fsShortname(localpath);
                if (!(!l->localname.empty() &&
                    ((!l->slocalname && (!sn || l->localname == *sn)) ||
                    (l->slocalname && sn && !l->slocalname->empty() && *l->slocalname != l->localname && *l->slocalname == *sn))))
                {
                    // This can happen if a file was moved elsewhere and moved back before the sync restarts.
                    // We'll refresh slocalname while scanning.
                    LOG_warn << "Shortname mismatch on LocalNode load!" <<
                        " Was: " << (l->slocalname ? l->slocalname->toPath(*syncs.fsaccess) : "(null") <<
                        " Now: " << (sn ? sn->toPath(*syncs.fsaccess) : "(null") <<
                        " at " << localpath.toPath(*syncs.fsaccess);
                }
            }
        }
#endif

        l->parent_dbid = parent_dbid;
        l->syncedFingerprint.size = size;
        l->setSyncedFsid(fsid, syncs.localnodeBySyncedFsid, l->localname);
        l->setSyncedNodeHandle(l->syncedCloudNodeHandle, l->name);

        if (!l->slocalname_in_db)
        {
            statecacheadd(l);
            if (insertq.size() > 50000)
            {
                cachenodes();  // periodically output updated nodes with shortname updates, so people who restart megasync still make progress towards a fast startup
            }
        }

        if (maxdepth)
        {
            addstatecachechildren(l->dbid, tmap, localpath, l, maxdepth - 1);
        }

        assert(l->localname.empty() || l->name.empty() || parent_dbid == UNDEF ||
            0 == compareUtf(l->localname, true, l->name, false, true));
    }
}

bool Sync::readstatecache()
{
    if (statecachetable && state == SYNC_INITIALSCAN)
    {
        string cachedata;
        idlocalnode_map tmap;
        uint32_t cid;
        LocalNode* l;

        statecachetable->rewind();

        // bulk-load cached nodes into tmap
        while (statecachetable->next(&cid, &cachedata, &syncs.syncKey))
        {
            if ((l = LocalNode::unserialize(this, &cachedata)))
            {
                l->dbid = cid;
                tmap.insert(pair<int32_t,LocalNode*>(l->parent_dbid,l));
            }
        }

        // recursively build LocalNode tree, set scanseqnos to sync's current scanseqno
        addstatecachechildren(0, &tmap, localroot->localname, localroot.get(), 100);
        cachenodes();

        //// trigger a single-pass full scan to identify deleted nodes
        //fullscan = true;
        //scanseqno++;

        localroot->setScanAgain(false, true, true, 0);

        return true;
    }

    return false;
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
    if (state == SYNC_CANCELED)
    {
        return;
    }

    // Always queue the update even if we don't have a state cache.
    //
    // The reasoning here is that our integration tests regularly check the
    // size of these queues to determine whether a sync is or is not idle.
    //
    // The same reasoning applies to statecacheadd(...) below.
    insertq.erase(l);

    if (l->dbid)
    {
        deleteq.insert(l->dbid);
    }
}

// insert LocalNode into DB cache
void Sync::statecacheadd(LocalNode* l)
{
    if (state == SYNC_CANCELED)
    {
        return;
    }

    if (l->dbid)
    {
        deleteq.erase(l->dbid);
    }

    insertq.insert(l);
}

void Sync::cachenodes()
{
    // Purge the queues if we have no state cache.
    if (!statecachetable)
    {
        deleteq.clear();
        insertq.clear();
    }

    if ((state == SYNC_ACTIVE || (state == SYNC_INITIALSCAN /*&& insertq.size() > 100*/)) && (deleteq.size() || insertq.size()))
    {
        LOG_debug << syncname << "Saving LocalNode database with " << insertq.size() << " additions and " << deleteq.size() << " deletions";
        statecachetable->begin();

        // deletions
        for (set<uint32_t>::iterator it = deleteq.begin(); it != deleteq.end(); it++)
        {
            statecachetable->del(*it);
        }

        deleteq.clear();

        // additions - we iterate until completion or until we get stuck
        bool added;

        do {
            added = false;

            for (set<LocalNode*>::iterator it = insertq.begin(); it != insertq.end(); )
            {
                if ((*it)->type == TYPE_UNKNOWN)
                {
                    insertq.erase(it++);
                }
                else if ((*it)->parent->dbid || (*it)->parent == localroot.get())
                {
                    statecachetable->put(MegaClient::CACHEDLOCALNODE, *it, &syncs.syncKey);
                    insertq.erase(it++);
                    added = true;
                }
                else it++;
            }
        } while (added);

        statecachetable->commit();

        if (insertq.size())
        {
            LOG_err << "LocalNode caching did not complete";
        }
    }
}

void Sync::changestate(syncstate_t newstate, SyncError newSyncError, bool newEnableFlag, bool notifyApp)
{
    auto& config = getConfig();

    // See explanation in disableSelectedSyncs(...).
    newEnableFlag &= !(config.isBackup() && newstate < SYNC_INITIALSCAN);

    config.setError(newSyncError);
    config.setEnabled(newEnableFlag);

    if (newstate != state)
    {
        auto oldstate = state;
        state = newstate;

        if (notifyApp)
        {
            bool wasActive = oldstate == SYNC_ACTIVE || oldstate == SYNC_INITIALSCAN;
            bool nowActive = newstate == SYNC_ACTIVE;
            if (wasActive != nowActive)
            {
                mUnifiedSync.syncs.mClient.app->syncupdate_active(config.getBackupId(), nowActive);
            }
        }
    }

    if (newstate != SYNC_CANCELED)
    {
        mUnifiedSync.changedConfigState(notifyApp);
    }
}

// walk localpath and return corresponding LocalNode and its parent
// localpath must be relative to l or start with the root prefix if l == NULL
// localpath must be a full sync path, i.e. start with localroot->localname
// NULL: no match, optionally returns residual path
LocalNode* Sync::localnodebypath(LocalNode* l, const LocalPath& localpath, LocalNode** parent, LocalPath* outpath)
{
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
    if (!tmpfa)
    {
        tmpfa = syncs.fsaccess->newfileaccess();

        int i = 3;
        while (i--)
        {
            LOG_verbose << "Creating sync tmp folder";
            LocalPath localfilename = localdebris;
            syncs.fsaccess->mkdirlocal(localfilename, true);

            LocalPath tmpname = LocalPath::fromName("tmp", *syncs.fsaccess, mFilesystemType);
            localfilename.appendWithSeparator(tmpname, true);
            syncs.fsaccess->mkdirlocal(localfilename);

            tmpfaPath = localfilename;

            // lock it
            LocalPath lockname = LocalPath::fromName("lock", *syncs.fsaccess, mFilesystemType);
            localfilename.appendWithSeparator(lockname, true);

            if (tmpfa->fopen(localfilename, false, true))
            {
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
    }
}


/// todo:   things to figure out where to put them in new system:
///
// no fsid change detected or overwrite with unknown file:
//if (fa->mtime != l->mtime || fa->size != l->size)
//{
//    if (fa->fsidvalid && l->fsid != fa->fsid)
//    {
//        l->setfsid(fa->fsid, client->fsidnode);
//    }
//
//    m_off_t dsize = l->size > 0 ? l->size : 0;
//
//    l->genfingerprint(fa.get());
//
//    client->app->syncupdate_local_file_change(this, l, path.c_str());
//
//    DBTableTransactionCommitter committer(client->tctable);
//    client->stopxfer(l, &committer); // TODO:  can we use one committer for all the files in the folder?  Or for the whole recursion?
//    l->bumpnagleds();
//    l->deleted = false;
//
//    client->syncactivity = true;
//
//    statecacheadd(l);
//
//    fa.reset();
//
//    if (isnetwork && l->type == FILENODE)
//    {
//        LOG_debug << "Queueing extra fs notification for modified file";
//        dirnotify->notify(DirNotify::EXTRA, NULL, LocalPath(*localpathNew));
//    }
//    return l;
//}
//    }
//    else
//    {
//    // (we tolerate overwritten folders, because we do a
//    // content scan anyway)
//    if (fa->fsidvalid && fa->fsid != l->fsid)
//    {
//        l->setfsid(fa->fsid, client->fsidnode);
//        newnode = true;
//    }
//    }

//client->app->syncupdate_local_folder_addition(this, l, path.c_str());
//                else
//                {
//                if (fa->fsidvalid && l->fsid != fa->fsid)
//                {
//                    l->setfsid(fa->fsid, client->fsidnode);
//                }
//
//                if (l->genfingerprint(fa.get()))
//                {
//                    changed = true;
//                    l->bumpnagleds();
//                    l->deleted = false;
//                }
//
//                if (newnode)
//                {
//                    client->app->syncupdate_local_file_addition(this, l, path.c_str());
//                }
//                else if (changed)
//                {
//                    client->app->syncupdate_local_file_change(this, l, path.c_str());
//                    DBTableTransactionCommitter committer(client->tctable); // TODO:  can we use one committer for all the files in the folder?  Or for the whole recursion?
//                    client->stopxfer(l, &committer);
//                }
//
//                if (newnode || changed)
//                {
//                    statecacheadd(l);
//                }
//                }
//            }
//        }
//
//        if (changed || newnode)
//        {
//            if (isnetwork && l->type == FILENODE)
//            {
//                LOG_debug << "Queueing extra fs notification for new file";
//                dirnotify->notify(DirNotify::EXTRA, NULL, LocalPath(*localpathNew));
//            }
//
//            client->syncactivity = true;
//        }
//    }
//    else
//    {
//    LOG_warn << "Error opening file";
//    if (fa->retry)
//    {
//        // fopen() signals that the failure is potentially transient - do
//        // nothing and request a recheck
//        LOG_warn << "File blocked. Adding notification to the retry queue: " << path;
//        dirnotify->notify(DirNotify::RETRY, ll, LocalPath(*localpathNew));
//        client->syncfslockretry = true;
//        client->syncfslockretrybt.backoff(SCANNING_DELAY_DS);
//        client->blockedfile = *localpathNew;
//    }
//    else if (l)
//    {
//        // immediately stop outgoing transfer, if any
//        if (l->transfer)
//        {
//            DBTableTransactionCommitter committer(client->tctable); // TODO:  can we use one committer for all the files in the folder?  Or for the whole recursion?
//            client->stopxfer(l, &committer);
//        }
//
//        client->syncactivity = true;
//
//        // in fullscan mode, missing files are handled in bulk in deletemissing()
//        // rather than through setnotseen()
//        if (!fullscan)
//        {
//            l->setnotseen(1);
//        }
//    }

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

        if (sf.reachableNodesAllScannedLastPass &&
            sf.reachableNodesAllScannedThisPass &&
            sf.noProgressCount > 10)
        {
            for (auto i = sf.stalledNodePaths.begin(); i != sf.stalledNodePaths.end(); )
            {
                if (IsContainingCloudPathOf(i->first, cloudPath))
                {
                    // we already have a parent or ancestor listed
                    return;
                }
                else if (IsContainingCloudPathOf(cloudPath, i->first))
                {
                    // this new path is the parent or ancestor
                    i = sf.stalledNodePaths.erase(i);
                }
                else ++i;
            }
            sf.stalledNodePaths[cloudPath].reason = r;
            sf.stalledNodePaths[cloudPath].involvedCloudPath = cloudPath2;
            sf.stalledNodePaths[cloudPath].involvedLocalPath = localpath;
        }
    }

    void waitingLocal(const LocalPath& localPath, const LocalPath& localPath2, const string& cloudPath, SyncWaitReason r)
    {
        // the caller has a local path that an operation is in progress for, or can't be dealt with yet.
        // update our list of subtree roots containing such paths
        resolved = true;

        if (sf.reachableNodesAllScannedLastPass &&
            sf.reachableNodesAllScannedThisPass &&
            sf.noProgressCount > 10)
        {
            for (auto i = sf.stalledLocalPaths.begin(); i != sf.stalledLocalPaths.end(); )
            {
                if (i->first.isContainingPathOf(localPath))
                {
                    // we already have a parent or ancestor listed
                    return;
                }
                else if (localPath.isContainingPathOf(i->first))
                {
                    // this new path is the parent or ancestor
                    i = sf.stalledLocalPaths.erase(i);
                }
                else ++i;
            }
            sf.stalledLocalPaths[localPath].reason = r;
            sf.stalledLocalPaths[localPath].involvedLocalPath = localPath2;
            sf.stalledLocalPaths[localPath].involvedCloudPath = cloudPath;
        }
    }

    void noResult()
    {
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
    // no cloudNode at this row.  Check if this node is where a filesystem item moved to.

    if (row.fsNode->isSymlink)
    {
        LOG_debug << syncname << "checked path is a symlink, blocked: " << fullPath.localPath_utf8();
        row.syncNode->setUseBlocked();    // todo:   move earlier?  no syncnode here
        rowResult = false;
        return true;
    }
    else if (row.syncNode && row.syncNode->type != row.fsNode->type)
    {
        LOG_debug << syncname << "checked path does not have the same type, blocked: " << fullPath.localPath_utf8();
        row.syncNode->setUseBlocked();
        rowResult = false;
        return true;
    }
    else
    {
        //// mark as present
        //row.syncNode->setnotseen(0); // todo: do we need this - prob not always right now

        // we already checked fsid differs before calling

        SYNC_verbose << "Is this a local move destination, by fsid " << toHandle(row.fsNode->fsid) << " at " << logTriplet(row, fullPath);

        // was the file overwritten by moving an existing file over it?
        if (LocalNode* sourceSyncNode = syncs.findLocalNodeBySyncedFsid(row.fsNode->fsid, row.fsNode->type, row.fsNode->fingerprint, this, nullptr))   // todo: maybe null for sync* to detect moves between sync?
        {
            assert(parentRow.syncNode);
            ProgressingMonitor monitor(syncs);

            if (!row.syncNode)
            {
                resolve_makeSyncNode_fromFS(row, parentRow, fullPath, false);
                assert(row.syncNode);
            }

            row.syncNode->setCheckMovesAgain(true, false, false);

            // logic to detect files being updated in the local computer moving the original file
            // to another location as a temporary backup
            if (sourceSyncNode->type == FILENODE &&
                checkIfFileIsChanging(*row.fsNode, sourceSyncNode->getLocalPath()))
            {
                // if we revist here and the file is still the same after enough time, we'll move it
                monitor.waitingLocal(sourceSyncNode->getLocalPath(), LocalPath(), string(), SyncWaitReason::WatiingForFileToStopChanging);
                rowResult = false;
                return true;
            }

            // Is there something in the way at the move destination?
            string nameOverwritten;
            if (row.cloudNode)
            {
                SYNC_verbose << syncname << "Move detected by fsid, but something else with that name is already here in the cloud. Type: " << row.cloudNode->type
                    << " new path: " << fullPath.localPath_utf8()
                    << " old localnode: " << sourceSyncNode->localnodedisplaypath(*syncs.fsaccess)
                    << logTriplet(row, fullPath);

                // but, is is ok to overwrite that thing?  If that's what happened locally to a synced file, and the cloud item was also synced and is still there, then it's legit
                // overwriting a folder like that is not possible so far as I know
                bool legitOverwrite = row.syncNode->type == FILENODE &&
                                      row.syncNode->fsid_lastSynced != UNDEF &&
                                      row.syncNode->syncedCloudNodeHandle == row.cloudNode->handle;

                if (legitOverwrite)
                {
                    SYNC_verbose << syncname << "Move is a legit overwrite of a synced file, so we overwrite that in the cloud also." << logTriplet(row, fullPath);
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

//            if (!sourceSyncNode->moveSourceApplyingToCloud && !belowRemovedCloudNode)
//            {
//                assert(!sourceSyncNode->moveTargetApplyingToCloud);

                LOG_debug << syncname << "Move detected by fsid. Type: " << sourceSyncNode->type
                    << " new path: " << fullPath.localPath_utf8()
                    << " old localnode: " << sourceSyncNode->localnodedisplaypath(*syncs.fsaccess)
                    << logTriplet(row, fullPath);
//                sourceSyncNode->moveSourceApplyingToCloud = true;
//            }
            row.suppressRecursion = true;   // wait until we have moved the other LocalNodes below this one

            // is it a move within the same folder?  (ie, purely a rename?)
            syncRow* sourceRow = nullptr;
            if (sourceSyncNode->parent == row.syncNode->parent
                && row.rowSiblings)
            {
                // then prevent any other action on this node for now
                // if there is a new matching fs name, we'll need this node to be renamed, then a new one will be created
                for (syncRow& r : *row.rowSiblings)
                {
                    // skip the syncItem() call for this one this time (so no upload until move is resolved)
                    if (r.syncNode == sourceSyncNode) sourceRow = &r;
                }
            }

            // we don't want the source LocalNode to be visited until the move completes
            // because it might see a new file with the same name, and start an
            // upload attached to that LocalNode (which would create a wrong version chain in the account)
            // TODO: consider alternative of preventing version on upload completion - probably resulting in much more complicated name matching though
            if (sourceRow)
            {
                sourceRow->itemProcessed = true;
                sourceRow->syncNode->setSyncAgain(true, false, false);
            }

            // Although we have detected a move locally, there's no guarantee the cloud side
            // is complete, the corresponding parent folders in the cloud may not match yet.
            // ie, either of sourceCloudNode or targetCloudNode could be null here.
            // So, we have a heirarchy of statuses:
            //    If any scan flags anywhere are set then we can't be sure a missing fs node isn't a move
            //    After all scanning is done, we need one clean tree traversal with no moves detected
            //    before we can be sure we can remove nodes or upload/download.

            //todo: detach from Nodes ?

            CloudNode sourceCloudNode, targetCloudNode;
            string sourceCloudNodePath, targetCloudNodePath;
            bool foundSourceCloudNode = syncs.lookupCloudNode(sourceSyncNode->syncedCloudNodeHandle, sourceCloudNode, &sourceCloudNodePath);
            bool foundTargetCloudNode = syncs.lookupCloudNode(parentRow.syncNode->syncedCloudNodeHandle, targetCloudNode, &targetCloudNodePath);

//todo:
            //if (sourceCloudNode && !sourceCloudNode->mPendingChanges.empty())
            //{
            //    // come back again later when there isn't already a command in progress
            //    SYNC_verbose << syncname << "Actions are already in progress for " << sourceCloudNode->displaypath() << logTriplet(row, fullPath);
            //    if (parentRow.syncNode) parentRow.syncNode->setSyncAgain(false, true, false);
            //    rowResult = false;
            //    return true;
            //}

            if (foundSourceCloudNode && foundTargetCloudNode)
            {
                if (belowRemovedCloudNode)
                {
                    LOG_debug << syncname << "Move destination detected for fsid " << toHandle(row.fsNode->fsid) << " but we are belowRemovedCloudNode, must wait for resolution at: " << fullPath.cloudPath << logTriplet(row, fullPath);;
                    monitor.waitingLocal(fullPath.localPath, sourceSyncNode->getLocalPath(), fullPath.cloudPath, SyncWaitReason::ApplyMoveNeedsOtherSideParentFolderToExist);
                    row.syncNode->setSyncAgain(true, false, false);
                }
                else
                {

                    if (row.cloudNode && row.cloudNode->handle != sourceCloudNode.handle)
                    {
                        LOG_debug << syncname << "Moving node to debris for replacement: " << fullPath.cloudPath << logTriplet(row, fullPath);

                        bool inshareFlag = inshare;
                        auto deleteHandle = row.cloudNode->handle;
                        syncs.clientThreadActions.pushBack([deleteHandle, inshareFlag](MegaClient& mc, DBTableTransactionCommitter& committer)
                            {
                                if (auto n = mc.nodeByHandle(deleteHandle))
                                {
                                    mc.movetosyncdebris(n, inshareFlag);
                                    mc.execsyncdeletions();
                                }
                            });
                    }

                    auto movePtr = std::make_shared<LocalNode::RareFields::MoveInProgress>();
                    //auto sourceHandle = sourceCloudNode.handle;

                    string newName = row.fsNode->localname.toName(*syncs.fsaccess);
                    if (newName == sourceCloudNode.name ||
                        sourceSyncNode->localname == row.fsNode->localname)
                    {
                        // if it wasn't renamed locally, or matches the target anyway
                        // then don't change the name
                        newName.clear();
                    }

                    //auto targetLocationFsid = row.fsNode->fsid;
                    //auto targetLocationType = row.fsNode->type;
                    //auto targetLocationFingerprint = row.fsNode->fingerprint;

                    //LocalNode* oldSourceSyncNode = sourceSyncNode;
                    //LocalNode* oldTargetSyncNode = row.syncNode;

                    if (sourceCloudNode.parentHandle == targetCloudNode.handle && newName.empty())
                    {

                        LOG_debug << sourceSyncNode->sync->syncname << "Sync cloud move/rename has completed: " << sourceCloudNodePath;

                        // Now that the move has completed, it's ok (and necessary) to resume visiting the source node
                        if (sourceRow) sourceRow->itemProcessed = true;

                        // remove fsid (and handle) from source node, so we don't detect
                        // that as a move source anymore
                        sourceSyncNode->setSyncedFsid(UNDEF, syncs.localnodeBySyncedFsid, sourceSyncNode->localname);
                        sourceSyncNode->setSyncedNodeHandle(NodeHandle(), sourceSyncNode->name);

                        // Move all the LocalNodes under the source node to the new location
                        // We can't move the source node itself as the recursive callers may be using it
                        sourceSyncNode->moveContentTo(row.syncNode, fullPath.localPath, true);

                        row.syncNode->setScanAgain(true, true, true, 0);
                        sourceSyncNode->setScanAgain(true, false, false, 0);

                        // Check for filename anomalies.  Only report if we really do succeed with the rename
                        if (!newName.empty() && newName != nameOverwritten)
                        {
                            auto anomalyType = isFilenameAnomaly(fullPath.localPath.leafName(), newName, sourceCloudNode.type);
                            if (anomalyType != FILENAME_ANOMALY_NONE)
                            {
                                auto local  = fullPath.localPath_utf8();
                                auto remote =  targetCloudNodePath + "/" + newName;
                                syncs.clientThreadActions.pushBack([anomalyType, local, remote](MegaClient& mc, DBTableTransactionCommitter& committer)
                                    {
                                        mc.filenameAnomalyDetected(anomalyType, local, remote);
                                    });
                            }
                        }

                        rowResult = false; // one more future visit to double check everything
                        return true; // job done for this node for now
                    }

                    if (sourceCloudNode.parentHandle == targetCloudNode.handle && !newName.empty())
                    {
                        // send the command to change the node name
                        LOG_debug << syncname
                                  << "Renaming node: " << sourceCloudNodePath
                                  << " to " << newName  << logTriplet(row, fullPath);

                        auto renameHandle = sourceCloudNode.handle;
                        syncs.clientThreadActions.pushBack([renameHandle, newName, movePtr](MegaClient& mc, DBTableTransactionCommitter& committer)
                            {
                                if (auto n = mc.nodeByHandle(renameHandle))
                                {
                                    mc.setattr(n, attr_map('n', newName), [movePtr](NodeHandle, Error){
                                        // movePtr.reset();  // kept alive until completion - then the sync code knows it's finished
                                    });
                                }
                            });

                        //row.syncNode->moveTargetApplyingToCloud = true;
                        row.syncNode->rare().moveFromHere = movePtr;
                        sourceSyncNode->rare().moveToHere = movePtr;

                        LOG_debug << "Sync - local rename/move " << sourceSyncNode->getLocalPath().toPath(*syncs.fsaccess) << " -> " << fullPath.localPath.toPath(*syncs.fsaccess);

                        rowResult = false;
                        return true;
                    }
                    else
                    {
                        // send the command to move the node
                        LOG_debug << syncname << "Moving node: " << sourceCloudNodePath
                                  << " to " << targetCloudNodePath
                                  << (newName.empty() ? "" : (" as " + newName).c_str()) << logTriplet(row, fullPath);

                        syncs.clientThreadActions.pushBack([sourceCloudNode, targetCloudNode, newName, movePtr](MegaClient& mc, DBTableTransactionCommitter& committer)
                        {
                            auto fromNode = mc.nodeByHandle(sourceCloudNode.handle);
                            auto toNode = mc.nodeByHandle(targetCloudNode.handle);

                            if (fromNode && toNode)
                            {
                                auto err = mc.rename(fromNode, toNode,
                                            SYNCDEL_NONE,
                                            sourceCloudNode.parentHandle,
                                            newName.empty() ? nullptr : newName.c_str(),
                                            [movePtr](NodeHandle, Error){
                                                //movePtr.reset();  // movePtr kept alive until completion
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
                                    //changestate(SYNC_FAILED, COULD_NOT_MOVE_CLOUD_NODES, false, true);
                                }
                            }

                            // movePtr.reset();  // kept alive until completion - then the sync code knows it's finished
                        });

                        // command sent, now we wait for the actinpacket updates, later we will recognise
                        // the row as synced from fsNode, cloudNode and update the syncNode from those
                        LOG_debug << "Sync - local rename/move " << sourceSyncNode->getLocalPath().toPath(*syncs.fsaccess) << " -> " << fullPath.localPath.toPath(*syncs.fsaccess);

                        //assert(sourceSyncNode->moveSourceApplyingToCloud);
                        //row.syncNode->moveTargetApplyingToCloud = true;
                        row.syncNode->rare().moveFromHere = movePtr;
                        sourceSyncNode->rare().moveToHere = movePtr;

                        row.suppressRecursion = true;

                        row.syncNode->setSyncAgain(true, true, false); // keep visiting this node

                        rowResult = false;
                        return true;
                    }
                }
            }
            else
            {
                // eg. cloud parent folder not synced yet (maybe Localnode is created, but not handle matched yet)
                if (!foundSourceCloudNode) SYNC_verbose << syncname << "Source cloud node doesn't exist yet" << logTriplet(row, fullPath);
                if (!foundTargetCloudNode) SYNC_verbose << syncname << "Target parent cloud node doesn't exist yet" << logTriplet(row, fullPath);

                monitor.waitingLocal(fullPath.localPath, sourceSyncNode->getLocalPath(), fullPath.cloudPath, SyncWaitReason::ApplyMoveNeedsOtherSideParentFolderToExist);

                row.suppressRecursion = true;
                rowResult = false;
                return true;
            }


            // todo: adjust source sourceLocalNode so that it is treated as a deletion


            //LOG_debug << "File move/overwrite detected";

            //// delete existing LocalNode...
            //delete row.syncNode;      // todo:  CAUTION:  this will queue commands to remove the cloud node
            //row.syncNode = nullptr;

            //// ...move remote node out of the way...
            //client->execsyncdeletions();   // todo:  CAUTION:  this will send commands to remove the cloud node

            //// ...and atomically replace with moved one
            //client->app->syncupdate_local_move(this, sourceLocalNode, fullPath.toPath(*client->fsaccess).c_str());

            //// (in case of a move, this synchronously updates l->parent and l->node->parent)
            //sourceLocalNode->setnameparent(parentRow.syncNode, &fullPath, client->fsaccess->fsShortname(fullPath), true);

            //// mark as seen / undo possible deletion
            //sourceLocalNode->setnotseen(0);  // todo: do we still need this?

            //statecacheadd(sourceLocalNode);

            //rowResult = false;
            //return true;
        }
    }
    //else
    //{
    //    // !row.syncNode

    //    // rename or move of existing node?
    //    if (row.fsNode->isSymlink)
    //    {
    //        LOG_debug << "checked path is a symlink, blocked: " << fullPath.toPath(*client->fsaccess);
    //        row.syncNode->setUseBlocked();    // todo:   move earlier?  no syncnode here
    //        rowResult = false;
    //        return true;
    //    }
    //    else if (LocalNode* sourceLocalNode = client->findLocalNodeByFsid(*row.fsNode, *this))
    //    {
    //        LOG_debug << syncname << "Move detected by fsid. Type: " << sourceLocalNode->type << " new path: " << fullPath.toPath(*client->fsaccess) << " old localnode: " << sourceLocalNode->localnodedisplaypath(*client->fsaccess);

    //        // logic to detect files being updated in the local computer moving the original file
    //        // to another location as a temporary backup
    //        if (sourceLocalNode->type == FILENODE &&
    //            client->checkIfFileIsChanging(*row.fsNode, sourceLocalNode->getLocalPath(true)))
    //        {
    //            // if we revist here and the file is still the same after enough time, we'll move it
    //            rowResult = false;
    //            return true;
    //        }

    //        client->app->syncupdate_local_move(this, sourceLocalNode, fullPath.toPath(*client->fsaccess).c_str());

    //        // (in case of a move, this synchronously updates l->parent
    //        // and l->node->parent)
    //        sourceLocalNode->setnameparent(parentRow.syncNode, &fullPath, client->fsaccess->fsShortname(fullPath), false);

              /*      // Has the move (rename) resulted in a filename anomaly?
                    if (Node* node = it->second->node)
                    {
                        auto type = isFilenameAnomaly(*localpathNew, node);

                        if (type != FILENAME_ANOMALY_NONE)
                        {
                            auto localPath = localpathNew->toPath();
                            auto remotePath = node->displaypath();

                            client->filenameAnomalyDetected(type, localPath, remotePath);
                        }
                    }*/

    //        // make sure that active PUTs receive their updated filenames
    //        client->updateputs();

    //        statecacheadd(sourceLocalNode);

    //        // unmark possible deletion
    //        sourceLocalNode->setnotseen(0);    // todo: do we still need this?

    //        // immediately scan folder to detect deviations from cached state
    //        if (fullscan && sourceLocalNode->type == FOLDERNODE)
    //        {
    //            sourceLocalNode->setFutureScan(true, true);
    //        }
    //    }
    //}
    return false;
 }


 #ifdef DEBUG
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
    if (row.syncNode && row.syncNode->type != row.cloudNode->type)
    {
        LOG_debug << syncname << "checked node does not have the same type, blocked: " << fullPath.cloudPath;
        row.syncNode->setUseBlocked();
        row.suppressRecursion = true;
        rowResult = false;
        return true;
    }
    else if (LocalNode* sourceSyncNode = syncs.findLocalNodeByNodeHandle(row.cloudNode->handle))
    {
        if (sourceSyncNode == row.syncNode) return false;

        // It's a move or rename

        if (isBackup())
        {
            // Backups must not change the local
            changestate(SYNC_FAILED, BACKUP_MODIFIED, false, true);
            rowResult = false;
            return true;
        }

        ProgressingMonitor monitor(syncs);

        assert(parentRow.syncNode);
        if (parentRow.syncNode) parentRow.syncNode->setCheckMovesAgain(false, true, false);
        if (row.syncNode) row.syncNode->setCheckMovesAgain(true, false, false);

        sourceSyncNode->treestate(TREESTATE_SYNCING);
        if (row.syncNode) row.syncNode->treestate(TREESTATE_SYNCING);

        LocalPath sourcePath = sourceSyncNode->getLocalPath();

        //todo: detach nodes?
        Node* oldCloudParent = sourceSyncNode->parent ?
                               syncs.mClient.nodeByHandle(sourceSyncNode->parent->syncedCloudNodeHandle) :
                               nullptr;

        // is there already something else at the target location though?
        if (row.fsNode)
        {
            SYNC_verbose << syncname << "Move detected by nodehandle, but something else with that name is already here locally. Type: " << row.fsNode->type
                << " moved node: " << fullPath.cloudPath
                << " old parent: " << (oldCloudParent ? oldCloudParent->displaypath() : "?")
                << logTriplet(row, fullPath);

            row.syncNode->setCheckMovesAgain(false, true, false);
            monitor.waitingCloud(fullPath.cloudPath, sourceSyncNode->getCloudPath(), fullPath.localPath, SyncWaitReason::ApplyMoveIsBlockedByExistingItem);
            rowResult = false;
            return true;
        }

        if (!sourceSyncNode->moveApplyingToLocal && !belowRemovedFsNode && parentRow.cloudNode)
        {
            LOG_debug << syncname << "Move detected by nodehandle. Type: " << sourceSyncNode->type
                << " moved node: " << fullPath.cloudPath
                << " old parent: " << (oldCloudParent ? oldCloudParent->displaypath() : "?")
                << logTriplet(row, fullPath);

            LOG_debug << "Sync - remote move " << fullPath.cloudPath <<
                " from " << (oldCloudParent ? oldCloudParent->displayname() : "?") <<
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
                auto localPath = fullPath.localPath_utf8();
                syncs.clientThreadActions.pushBack([type, localPath, remotePath](MegaClient& mc, DBTableTransactionCommitter& committer)
                    {
                        mc.filenameAnomalyDetected(type, localPath, remotePath);
                    });
            }
        }

        // is it a move within the same folder?  (ie, purely a rename?)
        syncRow* sourceRow = nullptr;
        if (sourceSyncNode->parent == parentRow.syncNode
            && row.rowSiblings
            && !sourceSyncNode->moveAppliedToLocal)
        {
            // then prevent any other action on this node for now
            // if there is a new matching fs name, we'll need this node to be renamed, then a new one will be created
            for (syncRow& r : *row.rowSiblings)
            {
                // skip the syncItem() call for this one this time (so no upload until move is resolved)
                if (r.syncNode == sourceSyncNode) sourceRow = &r;
            }
        }

        // we don't want the source LocalNode to be visited until after the move completes, and we revisit with rescanned folder data
        // because it might see a new file with the same name, and start a download, keeping the row instead of removing it
        if (sourceRow)
        {
            sourceRow->itemProcessed = true;
            sourceRow->syncNode->setScanAgain(true, false, false, 0);
        }

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

        if (syncs.fsaccess->renamelocal(sourcePath, fullPath.localPath))
        {
            // todo: move anything at this path to sync debris first?  Old algo didn't though

            // check filesystem is not changing fsids as a result of rename
            assert(sourceSyncNode->fsid_lastSynced == debug_getfsid(fullPath.localPath, *syncs.fsaccess));

            LOG_debug << "Sync - local rename/move " << sourceSyncNode->getLocalPath().toPath(*syncs.fsaccess) << " -> " << fullPath.localPath.toPath(*syncs.fsaccess);

            if (!row.syncNode)
            {
                resolve_makeSyncNode_fromCloud(row, parentRow, fullPath, false);
                assert(row.syncNode);
            }

            // remove fsid (and handle) from source node, so we don't detect
            // that as a move source anymore
            sourceSyncNode->setSyncedFsid(UNDEF, syncs.localnodeBySyncedFsid, sourceSyncNode->localname);
            sourceSyncNode->setSyncedNodeHandle(NodeHandle(), sourceSyncNode->name);

            sourceSyncNode->moveContentTo(row.syncNode, fullPath.localPath, true);
            sourceSyncNode->moveAppliedToLocal = true;

            sourceSyncNode->setScanAgain(true, false, false, 0);
            row.syncNode->setScanAgain(true, false, false, 0);

            rowResult = false;
            return true;
        }
        else if (syncs.fsaccess->transient_error)
        {
            LOG_warn << "transient error moving folder: " << sourcePath.toPath(*syncs.fsaccess) << logTriplet(row, fullPath);
            row.syncNode->setUseBlocked();   // todo: any interaction with checkMovesAgain ? might we stall if the transient error never resovles?
            row.suppressRecursion = true;
            sourceSyncNode->moveApplyingToLocal = false;
            rowResult = false;
            return true;
        }
        else
        {
            SYNC_verbose << "Move to here delayed since local parent doesn't exist yet: " << sourcePath.toPath(*syncs.fsaccess) << logTriplet(row, fullPath);
            monitor.waitingCloud(fullPath.cloudPath, sourceSyncNode->getCloudPath(), fullPath.localPath, SyncWaitReason::ApplyMoveNeedsOtherSideParentFolderToExist);
            rowResult = false;
            return true;
        }
    }
    return false;
}



//bool Sync::checkValidNotification(int q, Notification& notification)
//{
//    // This code moved from filtering before going on notifyq, to filtering after when it's thread-safe to do so
//
//    if (q == DirNotify::DIREVENTS || q == DirNotify::EXTRA)
//    {
//        Notification next;
//        while (dirnotify->notifyq[q].peekFront(next)
//            && next.localnode == notification.localnode && next.path == notification.path)
//        {
//            dirnotify->notifyq[q].popFront(next);  // this is the only thread removing from the queue so it will be the same item
//            if (!notification.timestamp || !next.timestamp)
//            {
//                notification.timestamp = 0;  // immediate
//            }
//            else
//            {
//                notification.timestamp = std::max(notification.timestamp, next.timestamp);
//            }
//            LOG_debug << "Next notification repeats, skipping duplicate";
//        }
//    }
//
//    if (notification.timestamp && /*!initializing &&*/ q == DirNotify::DIREVENTS)
//    {
//        LocalPath tmppath;

//        if (notification.localnode)
//        {
//            if (node == (LocalNode*)~0) return false;
//
//            tmppath = notification.localnode->getLocalPath();
//        }
//
//        if (!notification.path.empty())
//        {
//            tmppath.appendWithSeparator(notification.path, false, client->fsaccess->localseparator);
//        }
//
//        attr_map::iterator ait;
//        auto fa = client->fsaccess->newfileaccess(false);
//        bool success = fa->fopen(tmppath, false, false);
//        LocalNode *ll = localnodebypath(notification.localnode, notification.path);
//        if ((!ll && !success && !fa->retry) // deleted file
//            || (ll && success && ll->node && ll->node->localnode == ll
//                && (ll->type != FILENODE || (*(FileFingerprint *)ll) == (*(FileFingerprint *)ll->node))
//                && (ait = ll->node->attrs.map.find('n')) != ll->node->attrs.map.end()
//                && ait->second == ll->name
//                && fa->fsidvalid && fa->fsid == ll->fsid && fa->type == ll->type
//                && (ll->type != FILENODE || (ll->mtime == fa->mtime && ll->size == fa->size))))
//        {
//            LOG_debug << "Self filesystem notification skipped";
//            return false;
//        }
//    }
//    return true;
//}

dstime Sync::procextraq()
{
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
                      << notification.path.toPath(*syncs.fsaccess);
            continue;
        }

        // How long has it been since the notification was queued?
        auto elapsed = Waiter::ds - notification.timestamp;

        // Is it ready to be processed?
        if (elapsed < EXTRA_SCANNING_DELAY_DS)
        {
            // We'll process the notification later.
            queue.unpopFront(notification);

            return delay;
        }

        LOG_verbose << "Processing extra fs notification: "
                    << notification.path.toPath(*syncs.fsaccess);

        LocalPath remainder;
        LocalNode* match;
        LocalNode* nearest;

        match = localnodebypath(node, notification.path, &nearest, &remainder);

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
            SYNC_verbose << "Trigger scan flag by delayed notification on " << nearest->localnodedisplaypath(*syncs.fsaccess);
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
    NotificationDeque& queue = dirnotify->fsEventq;

    if (queue.empty())
    {
        return NEVER;
    }

    LOG_verbose << "Marking sync tree with filesystem notifications: "
                << queue.size();

    Notification notification;
    dstime delay = NEVER;

    while (queue.popFront(notification))
    {
        // Skip invalidated notifications.
        if (notification.invalidated())
        {
            LOG_debug << syncname << "Notification skipped: "
                      << notification.path.toPath(*syncs.fsaccess);
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
        LocalNode* match;
        LocalNode* nearest;
        LocalNode* node = notification.localnode;

        match = localnodebypath(node, notification.path, &nearest, &remainder);

        // Notify the parent of reachable nodes.
        if (match && match->parent)
        {
            nearest = match->parent;
        }

        // Make sure we actually have someone to notify.
        if (!nearest)
        {
            // Should we reschedule this notification?
            continue;
        }

        // Let the parent know it needs to perform a scan.
#ifdef DEBUG
        //if (nearest->scanAgain < TREE_ACTION_HERE)
        {
            SYNC_verbose << "Trigger scan flag by fs notification on " << nearest->localnodedisplaypath(*syncs.fsaccess);
        }
#endif

        nearest->setScanAgain(false, true, !remainder.empty(), SCANNING_DELAY_DS);

        // Queue an extra notification if we're a network sync.
        if (isnetwork)
        {
            LOG_verbose << "Queuing extra notification for: "
                        << notification.path.toPath(*syncs.fsaccess);

            dirnotify->notify(dirnotify->fsDelayedNetworkEventq,
                              node,
                              std::move(notification.path));
        }

        // How long the caller should wait before syncing.
        delay = SCANNING_DELAY_DS;
    }

    return delay;
}

//// todo: do we still need this?
//// delete all child LocalNodes that have been missing for two consecutive scans (*l must still exist)
//void Sync::deletemissing(LocalNode* l)
//{
//    LocalPath path;
//    std::unique_ptr<FileAccess> fa;
//    for (localnode_map::iterator it = l->children.begin(); it != l->children.end(); )
//    {
//        if (scanseqno-it->second->scanseqno > 1)
//        {
//            if (!fa)
//            {
//                fa = client->fsaccess->newfileaccess();
//            }
//            client->unlinkifexists(it->second, fa.get(), path);
//            delete it++->second;
//        }
//        else
//        {
//            deletemissing(it->second);
//            it++;
//        }
//    }
//}


bool Sync::movetolocaldebris(LocalPath& localpath)
{
    assert(!isBackup());

    char buf[32];
    struct tm tms;
    string day, localday;
    bool havedir = false;
    struct tm* ptm = m_localtime(m_time(), &tms);

    for (int i = -3; i < 100; i++)
    {
        ScopedLengthRestore restoreLen(localdebris);

        if (i == -2 || i > 95)
        {
            LOG_verbose << syncname << "Creating local debris folder";
            syncs.fsaccess->mkdirlocal(localdebris, true);
        }

        sprintf(buf, "%04d-%02d-%02d", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday);

        if (i >= 0)
        {
            sprintf(strchr(buf, 0), " %02d.%02d.%02d.%02d", ptm->tm_hour,  ptm->tm_min, ptm->tm_sec, i);
        }

        day = buf;
        localdebris.appendWithSeparator(LocalPath::fromPath(day, *syncs.fsaccess), true);

        if (i > -3)
        {
            LOG_verbose << syncname << "Creating daily local debris folder";
            havedir = syncs.fsaccess->mkdirlocal(localdebris, false) || syncs.fsaccess->target_exists;
        }

        localdebris.appendWithSeparator(localpath.subpathFrom(localpath.getLeafnameByteIndex(*syncs.fsaccess)), true);

        syncs.fsaccess->skip_errorreport = i == -3;  // we expect a problem on the first one when the debris folders or debris day folders don't exist yet
        if (syncs.fsaccess->renamelocal(localpath, localdebris, false))
        {
            syncs.fsaccess->skip_errorreport = false;
            return true;
        }
        syncs.fsaccess->skip_errorreport = false;

        if (syncs.fsaccess->transient_error)
        {
            return false;
        }

        if (havedir && !syncs.fsaccess->target_exists)
        {
            return false;
        }
    }

    return false;
}

// todo: move this to client-side?
m_off_t Sync::getInflightProgress()
{
    m_off_t progressSum = 0;

    for (auto tslot : syncs.mClient.tslots)
    {
        for (auto file : tslot->transfer->files)
        {
            if (auto ln = dynamic_cast<LocalNode*>(file))
            {
                if (ln->sync == this)
                {
                    progressSum += tslot->progressreported;
                }
            }
            else if (auto sfg = dynamic_cast<SyncDownload_inClient*>(file))
            {
            //todo:
                //if (sfg->localNode.sync == this)
                //{
                //    progressSum += tslot->progressreported;
                //}
            }
        }
    }

    return progressSum;
}


UnifiedSync::UnifiedSync(Syncs& s, const SyncConfig& c)
    : syncs(s), mConfig(c)
{
    mNextHeartbeat.reset(new HeartBeatSyncInfo());
}


void Syncs::enableSync(UnifiedSync& us, bool resetFingerprint, bool notifyApp, std::function<void(error)> completion)
{
    // Called from client thread
    assert(!us.mSync);
    us.mConfig.mError = NO_SYNC_ERROR;

    if (resetFingerprint)
    {
        us.mConfig.setLocalFingerprint(0); //This will cause the local filesystem fingerprint to be recalculated
    }

    LocalPath rootpath;
    std::unique_ptr<FileAccess> openedLocalFolder;
    Node* remotenode;
    bool inshare, isnetwork;

    error e = mClient.checkSyncConfig(us.mConfig, rootpath, openedLocalFolder, remotenode, inshare, isnetwork);

    if (e)
    {
        // error and enable flag were already changed
        us.changedConfigState(notifyApp);
        completion(e);
        return;
    }

    us.mConfig.mError = NO_SYNC_ERROR;
    us.mConfig.mEnabled = true;

    string debris = DEBRISFOLDER;
    auto localdebris = LocalPath();

    auto clientCompletion = [this, notifyApp, &us, completion](error e){
        us.changedConfigState(notifyApp);
        if (!e) mHeartBeatMonitor->updateOrRegisterSync(us);
        if (completion) completion(e);
    };

    syncThreadActions.pushBack([this, &us, debris, localdebris, remotenode, inshare, isnetwork, rootpath, clientCompletion]()
        {
            startSync_inThread(us, debris, localdebris, remotenode, inshare, isnetwork, rootpath, clientCompletion);
        });
}

bool Syncs::updateSyncRemoteLocation(UnifiedSync& us, Node* n, bool forceCallback)
{
    bool changed = false;
    if (n)
    {
        auto newpath = n->displaypath();
        if (newpath != us.mConfig.mOriginalPathOfRemoteRootNode)
        {
            us.mConfig.mOriginalPathOfRemoteRootNode = newpath;
            changed = true;
        }

        if (us.mConfig.getRemoteNode() != n->nodehandle)
        {
            us.mConfig.setRemoteNode(NodeHandle().set6byte(n->nodehandle));
            changed = true;
        }
    }
    else //unset remote node: failed!
    {
        if (!us.mConfig.getRemoteNode().isUndef())
        {
            us.mConfig.setRemoteNode(NodeHandle());
            changed = true;
        }
    }

    if (changed || forceCallback)
    {
        mClient.app->syncupdate_remote_root_changed(us.mConfig);
    }

    //persist
    saveSyncConfig(us.mConfig);

    return changed;
}

void Syncs::startSync_inThread(UnifiedSync& us, const string& debris, const LocalPath& localdebris,
    Node* remotenode, bool inshare, bool isNetwork, const LocalPath& rootpath,
    std::function<void(error)> clientCompletion)
{

    auto prevFingerprint = us.mConfig.getLocalFingerprint();

    assert(!us.mSync);
    us.mSync.reset(new Sync(us, debris, localdebris, remotenode, inshare, "")); // todo: what to set default logname to
    us.mConfig.setLocalFingerprint(us.mSync->fsfp);

    error e;
    if (prevFingerprint && prevFingerprint != us.mConfig.getLocalFingerprint())
    {
        LOG_err << "New sync local fingerprint mismatch. Previous: " << prevFingerprint
            << "  Current: " << us.mConfig.getLocalFingerprint();
        us.mSync->changestate(SYNC_FAILED, LOCAL_FINGERPRINT_MISMATCH, false, true);
        us.mConfig.mError = LOCAL_FINGERPRINT_MISMATCH;
        us.mConfig.mEnabled = false;
        us.mSync.reset();
        e = API_EFAILED;
    }
    else
    {
        us.mSync->isnetwork = isNetwork;

        saveSyncConfig(us.mConfig);
        mSyncFlags->isInitialPass = true;
        e = API_OK;
    }

    clientThreadActions.pushBack([e, clientCompletion](MegaClient& mc, DBTableTransactionCommitter& committer)
        {
            clientCompletion(e);
        });
}

void UnifiedSync::changedConfigState(bool notifyApp)
{
    if (mConfig.errorOrEnabledChanged())
    {
        LOG_debug << "Sync " << toHandle(mConfig.mBackupId) << " enabled/error changed to " << mConfig.mEnabled << "/" << mConfig.mError;

        syncs.saveSyncConfig(mConfig);
        if (notifyApp)
        {
            syncs.mClient.app->syncupdate_stateconfig(mConfig.getBackupId());
        }
        syncs.mClient.abortbackoff(false);
    }
}

Syncs::Syncs(MegaClient& mc)
  : mClient(mc)
  , fsaccess(new FSACCESS_CLASS)
  , mSyncFlags(new SyncFlags)
  , mScanService(new ScanService(waiter))
{
    mHeartBeatMonitor.reset(new BackupMonitor(&mClient));

    syncKey.setkey(mc.key.key);

    syncThread = std::thread([this]() { syncLoop(); });
}

Syncs::~Syncs()
{
    exitSyncLoop = true;
    if (syncThread.joinable()) syncThread.join();
}

SyncConfigVector Syncs::configsForDrive(const LocalPath& drive) const
{
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
    SyncConfigVector v;
    for (auto& s : mSyncVec)
    {
        v.push_back(s->mConfig);
    }
    return v;
}

error Syncs::backupCloseDrive(LocalPath drivePath)
{
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

    // Ensure the drive path is in normalized form.
    drivePath = NormalizeAbsolute(drivePath);

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
      });

    return result;
}

error Syncs::backupOpenDrive(LocalPath drivePath)
{
    // Is the drive path valid?
    if (drivePath.empty())
    {
        return API_EARGS;
    }

    // Convenience.
    auto& fsAccess = *mClient.fsaccess;

    // Can we get our hands on the config store?
    auto* store = syncConfigStore();

    if (!store)
    {
        LOG_err << "Couldn't restore "
                << drivePath.toPath(fsAccess)
                << " as there is no config store.";

        // Nope and we can't do anything without it.
        return API_EINTERNAL;
    }

    // Ensure the drive path is in normalized form.
    drivePath = NormalizeAbsolute(drivePath);

    // Has this drive already been opened?
    if (store->driveKnown(drivePath))
    {
        LOG_debug << "Skipped restore of "
                  << drivePath.toPath(fsAccess)
                  << " as it has already been opened.";

        // Then we don't have to do anything.
        return API_EEXIST;
    }

    SyncConfigVector configs;

    // Try and open the database on the drive.
    auto result = store->read(drivePath, configs);

    // Try and restore the backups in the database.
    if (result == API_OK)
    {
        LOG_debug << "Attempting to restore backup syncs from "
                  << drivePath.toPath(fsAccess);

        size_t numRestored = 0;

        // Create a unified sync for each backup config.
        for (const auto& config : configs)
        {
            // Make sure there aren't any syncs with this backup id.
            if (syncConfigByBackupId(config.mBackupId))
            {
                LOG_err << "Skipping restore of backup "
                        << config.mLocalPath.toPath(fsAccess)
                        << " on "
                        << drivePath.toPath(fsAccess)
                        << " as a sync already exists with the backup id "
                        << toHandle(config.mBackupId);

                continue;
            }

            // Create the unified sync.
            mSyncVec.emplace_back(new UnifiedSync(*this, config));

            // Track how many configs we've restored.
            ++numRestored;
        }

        // Log how many backups we could restore.
        LOG_debug << "Restored "
                  << numRestored
                  << " out of "
                  << configs.size()
                  << " backup(s) from "
                  << drivePath.toPath(fsAccess);

        return API_OK;
    }

    // Couldn't open the database.
    LOG_warn << "Failed to restore "
             << drivePath.toPath(fsAccess)
             << " as we couldn't open its config database.";

    return result;
}

SyncConfigStore* Syncs::syncConfigStore()
{
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

error Syncs::syncConfigStoreAdd(const SyncConfig& config)
{
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
        return API_EINTERNAL;
    }

    SyncConfigVector configs;
    bool known = store->driveKnown(LocalPath());

    // Load current configs from disk.
    auto result = store->read(LocalPath(), configs);

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

    return result;
}

bool Syncs::syncConfigStoreDirty()
{
    return mSyncConfigStore && mSyncConfigStore->dirty();
}

bool Syncs::syncConfigStoreFlush()
{
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
    size_t nFailed = failed.size();

    disableSelectedSyncs(
      [&](SyncConfig& config, Sync*)
      {
          // But only if they're not already disabled.
          if (!config.getEnabled()) return false;

          auto matched = failed.count(config.mExternalDrivePath);

          return matched > 0;
      },
      SYNC_CONFIG_WRITE_FAILURE,
      false, [=](size_t disabled){

        LOG_warn << "Disabled "
                 << disabled
                 << " sync(s) on "
                 << nFailed
                 << " drive(s).";
      });
    return false;
}

error Syncs::syncConfigStoreLoad(SyncConfigVector& configs)
{
    LOG_debug << "Attempting to load internal sync configs from disk.";

    auto result = API_EAGAIN;

    // Can we get our hands on the internal sync config database?
    if (auto* store = syncConfigStore())
    {
        // Try and read the internal database from disk.
        result = store->read(LocalPath(), configs);

        if (result == API_ENOENT || result == API_OK)
        {
            LOG_debug << "Loaded "
                      << configs.size()
                      << " internal sync config(s) from disk.";

            return API_OK;
        }
    }

    LOG_err << "Couldn't load internal sync configs from disk: "
            << result;

    return result;
}

string Syncs::exportSyncConfigs(const SyncConfigVector configs) const
{
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
    return exportSyncConfigs(configsForDrive(LocalPath()));
}

void Syncs::importSyncConfigs(const char* data, std::function<void(error)> completion)
{
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
            auto state = BackupInfoSync::getSyncState(config);
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
                    syncs.appendNewSync(config, client);
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

    // Try and translate JSON back into sync configs.
    SyncConfigVector configs;

    if (!importSyncConfigs(data, configs))
    {
        // No love. Inform the client.
        completion(API_EREAD);
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
    // Internal configs only for the time being.
    if (!config.mExternalDrivePath.empty())
    {
        LOG_warn << "Skipping export of external backup: "
                 << config.mLocalPath.toPath();
        return;
    }

    const auto& fsAccess = *mClient.fsaccess;

    string localPath = config.mLocalPath.toPath(fsAccess);
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

#ifdef _WIN32
    // Skip namespace prefix.
    if (!localPath.compare("\\\\?\\"))
    {
        localPath.erase(0, 4);
    }
#endif // _WIN32

    writer.beginobject();
    writer.arg_stringWithEscapes("localPath", localPath);
    writer.arg_stringWithEscapes("name", name);
    writer.arg_stringWithEscapes("remotePath", remotePath);
    writer.arg_stringWithEscapes("type", type);
    writer.endobject();
}

bool Syncs::importSyncConfig(JSON& reader, SyncConfig& config)
{
    static const string TYPE_LOCAL_PATH  = "localPath";
    static const string TYPE_NAME        = "name";
    static const string TYPE_REMOTE_PATH = "remotePath";
    static const string TYPE_TYPE        = "type";

    LOG_debug << "Attempting to parse config object: "
              << reader.pos;

    string localPath;
    string name;
    string remotePath;
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
    config.mLocalFingerprint = 0;
    config.mLocalPath = LocalPath::fromPath(localPath, *mClient.fsaccess);
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

bool Syncs::importSyncConfigs(const char* data, SyncConfigVector& configs)
{
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
    // Has a suitable IO context already been created?
    if (mSyncConfigIOContext)
    {
        // Yep, return a reference to it.
        return mSyncConfigIOContext.get();
    }

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
    unique_ptr<TLVstore> store(
      TLVstore::containerToTLVrecords(payload, &mClient.key));

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
      new SyncConfigIOContext(*mClient.fsaccess,
                                  std::move(authKey),
                                  std::move(cipherKey),
                                  Base64::btoa(name),
                                  mClient.rng));

    // Return a reference to the new IO context.
    return mSyncConfigIOContext.get();
}

void Syncs::clear()
{
    syncConfigStoreFlush();

    mSyncConfigStore.reset();
    mSyncConfigIOContext.reset();
    mSyncVec.clear();
    isEmpty = true;

    if (syncscanstate)
    {
        mClient.app->syncupdate_scanning(false);
        syncscanstate = false;
    }

    if (syncBusyState)
    {
        mClient.app->syncupdate_syncing(false);
        syncBusyState = false;
    }

    syncStallState = false;
    syncConflictState = false;

    totalLocalNodes = 0;
}

void Syncs::resetSyncConfigStore()
{
    mSyncConfigStore.reset();
    static_cast<void>(syncConfigStore());
}

auto Syncs::appendNewSync(const SyncConfig& c, MegaClient& mc) -> UnifiedSync*
{
    isEmpty = false;
    mSyncVec.push_back(unique_ptr<UnifiedSync>(new UnifiedSync(*this, c)));

    saveSyncConfig(c);

    return mSyncVec.back().get();
}

Sync* Syncs::runningSyncByBackupId(handle backupId) const
{
    for (auto& s : mSyncVec)
    {
        if (s->mSync && s->mConfig.getBackupId() == backupId)
        {
            return s->mSync.get();
        }
    }
    return nullptr;
}

SyncConfig* Syncs::syncConfigByBackupId(handle backupId) const
{
    for (auto& s : mSyncVec)
    {
        if (s->mConfig.getBackupId() == backupId)
        {
            return &s->mConfig;
        }
    }

    return nullptr;
}

void Syncs::forEachUnifiedSync(std::function<void(UnifiedSync&)> f)
{
    for (auto& s : mSyncVec)
    {
        f(*s);
    }
}

void Syncs::forEachRunningSync(bool includePaused, std::function<void(Sync* s)> f) const
{
    for (auto& s : mSyncVec)
    {
        if (s->mSync && (includePaused || !s->mSync->syncPaused))
        {
            f(s->mSync.get());
        }
    }
}

void Syncs::forEachRunningSyncContainingNode(Node* node, bool includePaused, std::function<void(Sync* s)> f)
{
    for (auto& s : mSyncVec)
    {
        if (s->mSync && (includePaused || !s->mSync->syncPaused))
        {
            if (s->mSync->cloudRoot() &&
                node->isbelow(s->mSync->cloudRoot()))
            {
                f(s->mSync.get());
            }
        }
    }
}

bool Syncs::forEachRunningSync_shortcircuit(bool includePaused, std::function<bool(Sync* s)> f)
{
    for (auto& s : mSyncVec)
    {
        if (s->mSync && (includePaused || !s->mSync->syncPaused))
        {
            if (!f(s->mSync.get()))
            {
                return false;
            }
        }
    }
    return true;
}

bool Syncs::hasRunningSyncs()
{
    for (auto& s : mSyncVec)
    {
        if (s->mSync) return true;
    }
    return false;
}

size_t Syncs::numRunningSyncs()
{
    size_t n = 0;
    for (auto& s : mSyncVec)
    {
        if (s->mSync) ++n;
    }
    return n;
}

unsigned Syncs::numSyncs()
{
    return unsigned(mSyncVec.size());
}

Sync* Syncs::firstRunningSync()
{
    for (auto& s : mSyncVec)
    {
        if (s->mSync) return s->mSync.get();
    }
    return nullptr;
}

void Syncs::stopCancelledFailedDisabled()
{
    for (auto& unifiedSync : mSyncVec)
    {
        if (unifiedSync->mSync && (
            unifiedSync->mSync->state == SYNC_CANCELED ||
            unifiedSync->mSync->state == SYNC_FAILED ||
            unifiedSync->mSync->state == SYNC_DISABLED))
        {
            unifiedSync->mSync.reset();
        }
    }
}

void Syncs::purgeRunningSyncs()
{
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

void Syncs::disableSyncs(SyncError syncError, bool newEnabledFlag)
{
    disableSelectedSyncs([&](SyncConfig& config, Sync*)
        {
            return config.getEnabled();
        },
        syncError,
        newEnabledFlag,
        [=](size_t nDisabled) {
            LOG_info << "Disabled " << nDisabled << " syncs. error = " << syncError;
            mClient.app->syncs_disabled(syncError);
        });
}

void Syncs::disableSelectedSyncs(std::function<bool(SyncConfig&, Sync*)> selector, SyncError syncError, bool newEnabledFlag, std::function<void(size_t)> completion)
{
    syncThreadActions.pushBack([this, selector, syncError, newEnabledFlag, completion]()
        {
            disableSelectedSyncs_inThread(selector, syncError, newEnabledFlag, completion);
        });
}

void Syncs::disableSelectedSyncs_inThread(std::function<bool(SyncConfig&, Sync*)> selector, SyncError syncError, bool newEnabledFlag, std::function<void(size_t)> completion)
{
    size_t nDisabled = 0;
    for (auto i = mSyncVec.size(); i--; )
    {
        if (selector(mSyncVec[i]->mConfig, mSyncVec[i]->mSync.get()))
        {
            if (auto sync = mSyncVec[i]->mSync.get())
            {
                sync->changestate(SYNC_DISABLED, syncError, newEnabledFlag, true); //This will cause the later deletion of Sync (not MegaSyncPrivate) object
            }
            else
            {
                // Backups should not be automatically resumed unless we can be sure
                // that mirror phase completed, and no cloud changes occurred since.
                //
                // If the backup was mirroring, the mirror may be incomplete. In that
                // case, the cloud may still contain files/folders that should not be
                // in the backup, and so we would need to restore in mirror phase, but
                // we must have the user's permission and instruction to do that.
                //
                // If the backup was monitoring when it was disabled, it's possible that
                // the cloud has changed in the meantime. If it was resumed, there may
                // be cloud changes that cause it to immediately fail.  Again we should
                // have the user's instruction to start again with mirroring phase.
                //
                // For initial implementation we are keeping things simple and reliable,
                // the user must resume the sync manually, and confirm that starting
                // with mirroring phase is appropriate.
                auto enabled = newEnabledFlag & !mSyncVec[i]->mConfig.isBackup();

                mSyncVec[i]->mConfig.setError(syncError);
                mSyncVec[i]->mConfig.setEnabled(enabled);
                mSyncVec[i]->changedConfigState(true);
            }
            nDisabled += 1;

            mHeartBeatMonitor->updateOrRegisterSync(*mSyncVec[i]);
        }
    }
    if (completion) completion(nDisabled);
}

void Syncs::removeSelectedSyncs(std::function<bool(SyncConfig&, Sync*)> selector)
{
    for (auto i = mSyncVec.size(); i--; )
    {
        if (selector(mSyncVec[i]->mConfig, mSyncVec[i]->mSync.get()))
        {
            removeSyncByIndex(i);
        }
    }
}

void Syncs::unloadSelectedSyncs(std::function<bool(SyncConfig&, Sync*)> selector)
{
    for (auto i = mSyncVec.size(); i--; )
    {
        if (selector(mSyncVec[i]->mConfig, mSyncVec[i]->mSync.get()))
        {
            unloadSyncByIndex(i);
        }
    }
}

void Syncs::purgeSyncs()
{
    assert(!onSyncThread());

    std::promise<bool> synchronous;
    syncThreadActions.pushBack([&]()
        {
            purgeSyncs();
            synchronous.set_value(true);
        });

    synchronous.get_future().get();
}


void Syncs::purgeSyncs_inThread()
{
    assert(onSyncThread());
    if (!mSyncConfigStore) return;

    // Remove all syncs.
    removeSelectedSyncs([](SyncConfig&, Sync*) { return true; });

    // Truncate internal sync config database.
    mSyncConfigStore->write(LocalPath(), SyncConfigVector());

    // Remove all drives.
    for (auto& drive : mSyncConfigStore->knownDrives())
    {
        // Never remove internal drive.
        if (!drive.empty())
        {
            // This does not flush.
            mSyncConfigStore->removeDrive(drive);
        }
    }
}

void Syncs::removeSyncByIndex(size_t index)
{
    if (index < mSyncVec.size())
    {
        if (auto& syncPtr = mSyncVec[index]->mSync)
        {
            syncPtr->changestate(SYNC_CANCELED, UNKNOWN_ERROR, false, false);

            if (syncPtr->statecachetable)
            {
                syncPtr->statecachetable->remove();
                syncPtr->statecachetable.reset();
            }
            syncPtr.reset(); // deletes sync
        }

        mSyncConfigStore->markDriveDirty(mSyncVec[index]->mConfig.mExternalDrivePath);

        // call back before actual removal (intermediate layer may need to make a temp copy to call client app)
        auto backupId = mSyncVec[index]->mConfig.getBackupId();
        mClient.app->sync_removed(backupId);

        // unregister this sync/backup from API (backup center)
        mClient.reqs.add(new CommandBackupRemove(&mClient, backupId));

        mSyncVec.erase(mSyncVec.begin() + index);

        isEmpty = mSyncVec.empty();
    }
}

void Syncs::unloadSyncByIndex(size_t index)
{
    if (index < mSyncVec.size())
    {
        if (auto& syncPtr = mSyncVec[index]->mSync)
        {
            // if it was running, the app gets a callback saying it's no longer active
            // SYNC_CANCELED is a special value that means we are shutting it down without changing config
            syncPtr->changestate(SYNC_CANCELED, UNKNOWN_ERROR, false, false);

            if (syncPtr->statecachetable)
            {
                // sync LocalNode database (if any) will be closed
                // deletion of the sync object won't affect the database
                syncPtr->statecachetable.reset();
            }
            syncPtr.reset(); // deletes sync
        }

        // the sync config is not affected by this operation; it should already be up to date on disk (or be pending)
        // we don't call sync_removed back since the sync is not deleted
        // we don't unregister from the backup/sync heartbeats as the sync can be resumed later

        mSyncVec.erase(mSyncVec.begin() + index);
        isEmpty = mSyncVec.empty();
    }
}

error Syncs::enableSyncByBackupId(handle backupId, bool resetFingerprint, UnifiedSync*& syncPtrRef)
{
    for (auto& s : mSyncVec)
    {
        if (s->mConfig.getBackupId() == backupId)
        {
            syncPtrRef = s.get();
            if (!s->mSync)
            {
                enableSync(*s, resetFingerprint, true, nullptr);
                return API_OK;
            }
            return API_EEXIST;
        }
    }
    return API_ENOENT;
}

void Syncs::saveSyncConfig(const SyncConfig& config)
{
    if (auto* store = syncConfigStore())
    {
        store->markDriveDirty(config.mExternalDrivePath);
    }
}

//// restore all configured syncs that were in a temporary error state (not manually disabled)
//void Syncs::enableResumeableSyncs()
//{
//    bool anySyncRestored = false;
//
//    for (auto& unifiedSync : mSyncVec)
//    {
//        if (!unifiedSync->mSync)
//        {
//            if (unifiedSync->mConfig.getEnabled())
//            {
//                SyncError syncError = unifiedSync->mConfig.getError();
//                LOG_debug << "Restoring sync: " << toHandle(unifiedSync->mConfig.getBackupId()) << " " << unifiedSync->mConfig.getLocalPath().toPath(*mClient.fsaccess) << " fsfp= " << unifiedSync->mConfig.getLocalFingerprint() << " old error = " << syncError;
//
//                // We should never try to resume a backup sync.
//                assert(!unifiedSync->mConfig.isBackup());
//
//                anySyncRestored |= unifiedSync->enableSync(false, true) == API_OK;
//            }
//            else
//            {
//                LOG_verbose << "Skipping restoring sync: " << unifiedSync->mConfig.getLocalPath().toPath(*mClient.fsaccess)
//                    << " enabled=" << unifiedSync->mConfig.getEnabled() << " error=" << unifiedSync->mConfig.getError();
//            }
//        }
//    }
//
//    if (anySyncRestored)
//    {
//        mClient.app->syncs_restored();
//    }
//}

void Syncs::resumeResumableSyncsOnStartup()
{
    if (mClient.loggedin() != FULLACCOUNT) return;

    SyncConfigVector configs;

    if (syncConfigStoreLoad(configs) != API_OK)
    {
        return;
    }

    // There should be no syncs yet.
    assert(mSyncVec.empty());

    for (auto& config : configs)
    {
        mSyncVec.push_back(unique_ptr<UnifiedSync>(new UnifiedSync(*this, config)));
        isEmpty = false;
    }

    for (auto& unifiedSync : mSyncVec)
    {
        if (!unifiedSync->mSync)
        {
            if (unifiedSync->mConfig.mOriginalPathOfRemoteRootNode.empty()) //should only happen if coming from old cache
            {
                auto node = mClient.nodeByHandle(unifiedSync->mConfig.getRemoteNode());
                updateSyncRemoteLocation(*unifiedSync, node, false); //updates cache & notice app of this change
                if (node)
                {
                    auto newpath = node->displaypath();
                    unifiedSync->mConfig.mOriginalPathOfRemoteRootNode = newpath;//update loaded config
                }
            }

            if (unifiedSync->mConfig.getBackupState() == SYNC_BACKUP_MIRROR)
            {
                // Should only be possible for a backup sync.
                assert(unifiedSync->mConfig.isBackup());

                // Disable only if necessary.
                if (unifiedSync->mConfig.getEnabled())
                {
                    unifiedSync->mConfig.setEnabled(false);
                    saveSyncConfig(unifiedSync->mConfig);
                }
            }

            bool hadAnError = unifiedSync->mConfig.getError() != NO_SYNC_ERROR;

            if (unifiedSync->mConfig.getEnabled())
            {
                // Right now, syncs are disabled upon all errors but, after sync-rework, syncs
                // could be kept as enabled but failed due to a temporary/recoverable error and
                // the SDK may auto-resume them if the error condition vanishes
                // (ie. an expired business account automatically disable syncs, but once
                // the user has paid, we may auto-resume).
                // TODO: remove assertion if it no longer applies:
                assert(!hadAnError);

#ifdef __APPLE__
                unifiedSync->mConfig.setLocalFingerprint(0); //for certain MacOS, fsfp seems to vary when restarting. we set it to 0, so that it gets recalculated
#endif
                LOG_debug << "Resuming cached sync: " << toHandle(unifiedSync->mConfig.getBackupId()) << " " << unifiedSync->mConfig.getLocalPath().toPath(*mClient.fsaccess) << " fsfp= " << unifiedSync->mConfig.getLocalFingerprint() << " error = " << unifiedSync->mConfig.getError();

                auto& us = *unifiedSync;
                enableSync(us, false, false, [this, &us, hadAnError](error e)
                    {
                        LOG_debug << "Sync autoresumed: " << toHandle(us.mConfig.getBackupId()) << " " << us.mConfig.getLocalPath().toPath(*mClient.fsaccess) << " fsfp= " << us.mConfig.getLocalFingerprint() << " error = " << us.mConfig.getError();
                        mClient.app->sync_auto_resume_result(us, true, hadAnError);
                    });
            }
            else
            {
                LOG_debug << "Sync loaded (but not resumed): " << toHandle(unifiedSync->mConfig.getBackupId()) << " " << unifiedSync->mConfig.getLocalPath().toPath(*mClient.fsaccess) << " fsfp= " << unifiedSync->mConfig.getLocalFingerprint() << " error = " << unifiedSync->mConfig.getError();
                mClient.app->sync_auto_resume_result(*unifiedSync, false, hadAnError);
            }
        }
    }
}

bool Sync::recursiveCollectNameConflicts(list<NameConflict>& conflicts)
{
    FSNode rootFsNode(localroot->getLastSyncedFSDetails());
    CloudNode rootCloudNode(*cloudRoot());
    syncRow row{ &rootCloudNode, localroot.get(), &rootFsNode };
    SyncPath pathBuffer(syncs, localroot->localname, cloudRoot()->displaypath()); // todo: cloudRoot
    recursiveCollectNameConflicts(row, conflicts, pathBuffer);
    return !conflicts.empty();
}

void syncRow::inferOrCalculateChildSyncRows(bool wasSynced, vector<syncRow>& childRows, vector<FSNode>& fsInferredChildren, vector<FSNode>& fsChildren, vector<CloudNode>& cloudChildren)
{
    // Effective children are from the last scan, if present.
    vector<FSNode>* effectiveFsChildren = syncNode->lastFolderScan.get();

    if (wasSynced && syncNode->sync->inferAlreadySyncedTriplets(cloudChildren, *syncNode, fsInferredChildren, childRows))
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
                if (childIt.second->fsid_lastSynced != UNDEF)
                {
                    fsChildren.emplace_back(childIt.second->getLastSyncedFSDetails());
                }
            }

            effectiveFsChildren = &fsChildren;
        }

        childRows = syncNode->sync->computeSyncTriplets(cloudChildren, *syncNode, *effectiveFsChildren);
    }
}


void Sync::recursiveCollectNameConflicts(syncRow& row, list<NameConflict>& ncs, SyncPath& fullPath)
{
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
    row.inferOrCalculateChildSyncRows(wasSynced, childRows, fsInferredChildren, fsChildren, cloudChildren);


    for (auto& childRow : childRows)
    {
        if (!childRow.cloudClashingNames.empty() ||
            !childRow.fsClashingNames.empty())
        {
            NameConflict nc;
            if (!childRow.cloudClashingNames.empty())
            {
                nc.cloudPath = row.cloudNode ? fullPath.cloudPath : "";
                for (CloudNode* n : childRow.cloudClashingNames)
                {
                    nc.clashingCloudNames.push_back(n->name);
                }
            }
            if (!childRow.fsClashingNames.empty())
            {
                nc.localPath = row.syncNode ? row.syncNode->getLocalPath() : LocalPath();
                for (FSNode* n : childRow.fsClashingNames)
                {
                    nc.clashingLocalNames.push_back(n->localname);
                }
            }
            ncs.push_back(std::move(nc));
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


auto Sync::computeSyncTriplets(vector<CloudNode>& cloudNodes, const LocalNode& syncParent, vector<FSNode>& fsNodes) const -> vector<syncRow>
{
    CodeCounter::ScopeTimer rst(syncs.mClient.performanceStats.computeSyncTripletsTime);
    //Comparator comparator(*this);
    vector<LocalNode*> localNodes;
    vector<Node*> remoteNodes;
    vector<syncRow> triplets;

    localNodes.reserve(syncParent.children.size());

    for (auto& child : syncParent.children)
    {
        localNodes.emplace_back(child.second);
    }

    bool caseInsensitive = false; // comparing to go to the cloud, differing case really is a different name

    auto fsCompareLess = [=](const FSNode& lhs, const FSNode& rhs) -> bool {
        return compareUtf(
            lhs.localname, true,
            rhs.localname, true, caseInsensitive) < 0;
    };

    auto lnCompareLess = [=](const LocalNode* lhs, const LocalNode* rhs) -> bool {
        return compareUtf(
            lhs->localname, true,
            rhs->localname, true, caseInsensitive) < 0;
    };

    auto fslnCompareSpaceship = [=](const FSNode& lhs, const LocalNode& rhs) -> int {
        return compareUtf(
            lhs.localname, true,
            rhs.localname, true, caseInsensitive);
    };

    std::sort(fsNodes.begin(), fsNodes.end(), fsCompareLess);
    std::sort(localNodes.begin(), localNodes.end(), lnCompareLess);

//#ifdef DEBUG
//    string fss, lns;
//    for (auto& i : fsNodes) fss += i.localname.toPath(*client->fsaccess) + " ";
//    for (auto i : localNodes) lns += i->localname.toPath(*client->fsaccess) + " ";
//    LOG_debug << "fs names sorted: " << fss;
//    LOG_debug << "ln names sorted: " << lns;
//#endif

    // Pair filesystem nodes with local nodes.
    {
        auto fCurr = fsNodes.begin();
        auto fEnd  = fsNodes.end();
        auto lCurr = localNodes.begin();
        auto lEnd  = localNodes.end();

        for ( ; ; )
        {
            // Determine the next filesystem node.
            auto fNext = std::upper_bound(fCurr, fEnd, *fCurr, fsCompareLess);

            // Determine the next local node.
            auto lNext = lCurr;

            if (lNext != lEnd)
            {
                // turned out not to be needed this time - keep commented for future consideration
                //if ((*lNext)->name.empty() && (*lNext)->localname.empty())
                //{
                //    // node we have detached from after a move, and it shouldn't match anything
                //    // also, there may be multiple in a single folder
                //    ++lNext;
                //}
                //else
                {
                    lNext = std::upper_bound(lCurr, lEnd, *lCurr, lnCompareLess);
                }
            }

            // By design, we should never have any conflicting local nodes.
            if (!(std::distance(lCurr, lNext) < 2))
            {
                assert(std::distance(lCurr, lNext) < 2);
            }

            auto *fsNode = fCurr != fEnd ? &*fCurr : nullptr;
            auto *localNode = lCurr != lEnd ? *lCurr : nullptr;

            // Bail, there's nothing left to pair.
            if (!(fsNode || localNode)) break;

            if (fsNode && localNode)
            {
                const auto relationship =
                    fslnCompareSpaceship(*fsNode, *localNode);

                // Non-null entries are considered equivalent.
                if (relationship < 0)
                {
                    // Process the filesystem node first.
                    localNode = nullptr;
                }
                else if (relationship > 0)
                {
                    // Process the local node first.
                    fsNode = nullptr;
                }
            }

            // Add the pair.
            triplets.emplace_back(nullptr, localNode, fsNode);

            // Mark conflicts.
            if (fsNode && std::distance(fCurr, fNext) > 1)
            {
                triplets.back().fsNode = nullptr;

                for (auto i = fCurr; i != fNext; ++i)
                {
                    LOG_debug << syncname << "Conflicting filesystem name: "
                        << i->localname.toPath(*syncs.fsaccess);

                    triplets.back().fsClashingNames.push_back(&*i);

                    if (localNode && i->fsid != UNDEF && i->fsid == localNode->fsid_lastSynced)
                    {
                        // In case of a name clash, it might be new.
                        // Do sync the subtree we were already syncing.
                        // But also complain about the clash
                        triplets.back().fsNode = &*i;
                    }
                }
            }

            fCurr = fsNode ? fNext : fCurr;
            lCurr = localNode ? lNext : lCurr;
        }
    }

    // node names going to the local FS, might clash if they only differ by case
    caseInsensitive = isCaseInsensitive(mFilesystemType);

    auto cloudCompareLess = [this, caseInsensitive](const CloudNode& lhs, const CloudNode& rhs) -> bool {
        return compareUtf(
            lhs.name, false,
            rhs.name, false, caseInsensitive) < 0;
    };

    auto rowCompareLess = [=](const syncRow& lhs, const syncRow& rhs) -> bool {
        return compareUtf(
            lhs.comparisonLocalname(), true,
            rhs.comparisonLocalname(), true, caseInsensitive) < 0;
    };

    auto cloudrowCompareSpaceship = [=](const CloudNode& lhs, const syncRow& rhs) -> int {
        // todo:  add an escape-as-we-compare option?
        // consider: local d%  upload to cloud as d%, now we need to match those up for a single syncrow.
        // consider: alternamtely d% in the cloud and local d%25 should also be considered a match.
        //auto a = LocalPath::fromName(lhs->displayname(), *client->fsaccess, mFilesystemType);

        return compareUtf(
            lhs.name, false,
            rhs.comparisonLocalname(), true, caseInsensitive);
    };

    std::sort(cloudNodes.begin(), cloudNodes.end(), cloudCompareLess);
    std::sort(triplets.begin(), triplets.end(), rowCompareLess);

//#ifdef DEBUG
//    string rns, trs;
//    for (auto i : remoteNodes) rns += i->displayname() + string(" ");
//    for (auto i : triplets) trs += i.comparisonLocalname().toPath(*client->fsaccess) + " ";
//    LOG_debug << "remote names sorted: " << rns;
//    LOG_debug << "row names sorted: " << trs;
//#endif

    // Link cloud nodes with triplets.
    {
        auto rCurr = cloudNodes.begin();
        auto rEnd = cloudNodes.end();
        size_t tCurr = 0;
        size_t tEnd = triplets.size();

        for ( ; ; )
        {
            auto rNext = rCurr;

            if (rNext != rEnd)
            {
                rNext = std::upper_bound(rCurr, rEnd, *rCurr, cloudCompareLess);
            }

            auto tNext = tCurr;

            // Compute upper bound manually.
            for ( ; tNext != tEnd; ++tNext)
            {
                // turned out not to be needed this time - keep commented for future consideration
                //if (triplets[tNext].syncNode && triplets[tNext].syncNode->name.empty() && triplets[tNext].syncNode->localname.empty())
                //{
                //    // node we have detached from after a move, and it shouldn't match anything
                //    // also, there may be multiple in a single folder
                //    ++tNext;
                //    break;
                //}

                if (rowCompareLess(triplets[tCurr], triplets[tNext]))
                {
                    break;
                }
            }

            // There should never be any conflicting triplets.
            if (tNext - tCurr >= 2)
            {
                assert(tNext - tCurr < 2);
            }

            auto* remoteNode = rCurr != rEnd ? &*rCurr : nullptr;
            auto* triplet = tCurr != tEnd ? &triplets[tCurr] : nullptr;

//#ifdef DEBUG
//            auto rn = remoteNode ? remoteNode->displayname() : "";
//#endif

            // Bail as there's nothing to pair.
            if (!(remoteNode || triplet)) break;

            if (remoteNode && triplet)
            {
                const auto relationship =
                    cloudrowCompareSpaceship(*remoteNode, *triplet);

                // Non-null entries are considered equivalent.
                if (relationship < 0)
                {
                    // Process remote node first.
                    triplet = nullptr;
                }
                else if (relationship > 0)
                {
                    // Process triplet first.
                    remoteNode = nullptr;
                }
            }

            // Have we detected a remote name conflict?
            if (remoteNode && std::distance(rCurr, rNext) > 1)
            {
                for (auto i = rCurr; i != rNext; ++i)
                {
                    LOG_debug << syncname << "Conflicting cloud name: "
                        << i->name;

                    if (triplet)
                    {
                        triplet->cloudClashingNames.push_back(&*i);

                        if (!i->handle.isUndef() &&
                            triplet->syncNode &&
                            triplet->syncNode->syncedCloudNodeHandle == i->handle)
                        {
                            // In case of a name clash, it might be new.
                            // Do sync the subtree we were already syncing.
                            // But also complain about the clash
                            triplet->cloudNode = &*i;
                        }
                    }
                }
            }
            else if (triplet)
            {
                triplet->cloudNode = remoteNode;
            }
            else
            {
                triplets.emplace_back(remoteNode, nullptr, nullptr);
            }

            if (triplet)    tCurr = tNext;
            if (remoteNode) rCurr = rNext;
        }
    }

    return triplets;
}

bool Sync::inferAlreadySyncedTriplets(vector<CloudNode>& cloudChildren, const LocalNode& syncParent, vector<FSNode>& inferredFsNodes, vector<syncRow>& inferredRows) const
{
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

        if (child.second->fsid_lastSynced == UNDEF || child.second->fsid_lastSynced != child.second->fsid_asScanned)
        {
            // on-disk is different to our LocalNode list
            return false;
        }

        inferredFsNodes.push_back(child.second->getLastSyncedFSDetails());
        inferredRows.emplace_back(node, child.second, &inferredFsNodes.back());
    }
    return true;
}

bool Sync::recursiveSync(syncRow& row, SyncPath& fullPath, bool belowRemovedCloudNode, bool belowRemovedFsNode, unsigned depth)
{
    // in case of sync failing while we recurse
    if (state < 0) return false;
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
    row.syncNode->syncAgain = TREE_RESOLVED;

    if (!row.fsNode)
    {
        row.syncNode->scanAgain = TREE_RESOLVED;
        row.syncNode->setScannedFsid(UNDEF, syncs.localnodeByScannedFsid, LocalPath());
        syncHere = row.syncNode->parent ? row.syncNode->parent->scanAgain < TREE_ACTION_HERE : true;
        recurseHere = false;  // If we need to scan, we need the folder to exist first - revisit later
    }
    else
    {
        if (row.syncNode->fsid_asScanned == UNDEF ||
            row.syncNode->fsid_asScanned != row.fsNode->fsid)
        {
            row.syncNode->scanAgain = TREE_ACTION_HERE;
            row.syncNode->setScannedFsid(row.fsNode->fsid, syncs.localnodeByScannedFsid, row.fsNode->localname);
        }
    }

    // Do we need to scan this node?
    if (row.syncNode->scanAgain >= TREE_ACTION_HERE)
    {
        syncs.mSyncFlags->reachableNodesAllScannedThisPass = false;
        syncHere = row.syncNode->processBackgroundFolderScan(row, fullPath);
    }
    else
    {
        // this will be restored at the end of the function if any nodes below in the tree need it
        row.syncNode->scanAgain = TREE_RESOLVED;
    }
    //SYNC_verbose << syncname << "sync/recurse here after scan logic: " << syncHere << recurseHere;

    if (row.syncNode->scanAgain >= TREE_ACTION_HERE)
    {
        // we must return later when we do have the scan data.
        // restore sync flag
        row.syncNode->setSyncAgain(false, true, false);
        SYNC_verbose << syncname << "Early exit from recursiveSync due to no scan data yet. " << logTriplet(row, fullPath);
        syncHere = false;
        recurseHere = false;
    }

// todo:  (if we still need it)
    //if (row.cloudNode && !row.cloudNode->mPendingChanges.empty())
    //{
    //    syncHere = false;
    //    SYNC_verbose << syncname << "Skipping synchere due to pending cloudNode commands.  recurse flag: " << recurseHere;
    //}

    bool folderSynced = syncHere; // todo: prob needs fixing

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
        bool subfoldersSynced = true;

        // Get sync triplets.
        vector<syncRow> childRows;
        vector<FSNode> fsInferredChildren;
        vector<FSNode> fsChildren;
        vector<CloudNode> cloudChildren;

        if (row.cloudNode)
        {
            syncs.lookupCloudChildren(row.cloudNode->handle, cloudChildren);
        }

        row.inferOrCalculateChildSyncRows(wasSynced, childRows, fsInferredChildren, fsChildren, cloudChildren);

        bool anyNameConflicts = false;
        for (unsigned step = 0; step < 3; ++step)
        {
            for (auto& childRow : childRows)
            {
                // in case of sync failing while we recurse
                if (state < 0) return false;

                if (!childRow.cloudClashingNames.empty() ||
                    !childRow.fsClashingNames.empty())
                {
                    anyNameConflicts = true;
                    row.syncNode->setContainsConflicts(false, true, false); // in case we don't call setItem due to !syncHere
                }
                childRow.rowSiblings = &childRows;

                assert(!row.syncNode || row.syncNode->localname.empty() || row.syncNode->name.empty() || !row.syncNode->parent ||
                       0 == compareUtf(row.syncNode->localname, true, row.syncNode->name, false, true));

                if (childRow.fsNode && childRow.syncNode &&
                    childRow.syncNode->fsid_asScanned != childRow.fsNode->fsid)
                {
                    syncs.setScannedFsidReused(childRow.fsNode->fsid, nullptr);   // todo: could put these lines first then we don't need excluded parameter?
                    childRow.syncNode->setScannedFsid(childRow.fsNode->fsid, syncs.localnodeByScannedFsid, childRow.fsNode->localname);
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
                        // todo: restore
                        //assert(0 == compareUtf(childRow.syncNode->getLocalPath(), true, fullPath.localPath, true, false));
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
                                folderSynced = false;
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
                        if (!childRow.cloudNode && !childRow.syncNode && childRow.fsNode)
                        {
                            resolve_makeSyncNode_fromFS(childRow, row, fullPath, false);
                        }
                    }
                    else if (belowRemovedFsNode)
                    {
                        // when syncing/scanning below a removed local node, we just want to
                        // and make syncNodes to visit, so we can be sure of detecting all the moves,
                        // in particular contradictroy moves.
                        if (childRow.cloudNode && !childRow.syncNode && !childRow.fsNode)
                        {
                            resolve_makeSyncNode_fromCloud(childRow, row, fullPath, false);
                        }
                    }
                    else if (syncHere && !childRow.itemProcessed)
                    {
                        // normal case: consider all the combinations
                        if (!syncItem(childRow, row, fullPath))
                        {
                            folderSynced = false;
                            row.syncNode->setSyncAgain(false, true, false);
                        }
                    }
                    break;

                case 2:
                    // third and final pass: recurse into the folders
                    if (childRow.syncNode &&
                        childRow.syncNode->type != FILENODE && (
                            (childRow.recurseBelowRemovedCloudNode &&
                             childRow.syncNode->scanRequired() || childRow.syncNode->syncRequired())
                            ||
                            (childRow.recurseBelowRemovedFsNode &&
                             childRow.syncNode->syncRequired())
                            ||
                            (recurseHere &&
                            !childRow.suppressRecursion &&
                            //!childRow.syncNode->moveSourceApplyingToCloud &&  // we may not have visited syncItem if !syncHere, but skip these if we determine they are being moved from or deleted already
                            //!childRow.syncNode->moveTargetApplyingToCloud &&
                            //!childRow.syncNode->deletedFS &&   we should not check this one, or we won't remove the LocalNode
                            !childRow.syncNode->deletingCloud)))
                    {
                        // Add watches as necessary.
                        if (childRow.fsNode)
                        {
                            childRow.syncNode->watch(fullPath.localPath, childRow.fsNode->fsid);
                        }

                        if (!recursiveSync(childRow, fullPath, belowRemovedCloudNode || childRow.recurseBelowRemovedCloudNode, belowRemovedFsNode || childRow.recurseBelowRemovedFsNode, depth+1))
                        {
                            subfoldersSynced = false;
                        }
                    }
                    break;

                }
            }
        }

        if (syncHere && folderSynced && !anyNameConflicts && !belowRemovedCloudNode)
        {
            row.syncNode->clearRegeneratableFolderScan(fullPath);
        }

        if (!subfoldersSynced) folderSynced = false;
    }

    // Recompute our LocalNode flags from children
    // Flags for this row could have been set during calls to the node
    // If we skipped a child node this time (or if not), the set-parent
    // flags let us know if future actions are needed at this level
    for (auto& child : row.syncNode->children)
    {
        if (child.second->type != FILENODE)
        {
            row.syncNode->scanAgain = updateTreestateFromChild(row.syncNode->scanAgain, child.second->scanAgain);
            row.syncNode->syncAgain = updateTreestateFromChild(row.syncNode->syncAgain, child.second->syncAgain);
        }
        row.syncNode->checkMovesAgain = updateTreestateFromChild(row.syncNode->checkMovesAgain, child.second->checkMovesAgain);
        row.syncNode->conflicts = updateTreestateFromChild(row.syncNode->conflicts, child.second->conflicts);
        row.syncNode->scanBlocked = updateTreestateFromChild(row.syncNode->scanBlocked, child.second->scanBlocked);

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

    return folderSynced;
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
    CodeCounter::ScopeTimer rst(syncs.mClient.performanceStats.syncItemTime1);

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
                     << "Updating slocalname: " << row.fsNode->shortname->toPath(*syncs.fsaccess)
                     << " at " << fullPath.localPath_utf8()
                     << " was " << (row.syncNode->slocalname ? row.syncNode->slocalname->toPath(*syncs.fsaccess) : "(null)")
                     << logTriplet(row, fullPath);
            row.syncNode->setnameparent(row.syncNode->parent, nullptr, row.fsNode->cloneShortname(), false);
        }
    }

    // Check blocked status.  Todo:  figure out use blocked flag clearing
    if (!row.syncNode && row.fsNode && (
        row.fsNode->isBlocked || row.fsNode->type == TYPE_UNKNOWN))
    {
        resolve_makeSyncNode_fromFS(row, parentRow, fullPath, false);
        if (row.syncNode->checkForScanBlocked(row.fsNode))
        {
            row.suppressRecursion = true;
            row.itemProcessed = true;
            return false;
        }
    }
    else if (row.syncNode &&
             row.syncNode->checkForScanBlocked(row.fsNode))
    {
        row.suppressRecursion = true;
        row.itemProcessed = true;
        return false;
    }


    // First deal with detecting local moves/renames and propagating correspondingly
    // Independent of the 8 combos below so we don't have duplicate checks in those.

    if (row.fsNode && (!row.syncNode || row.syncNode->fsid_lastSynced == UNDEF ||
                                        row.syncNode->fsid_lastSynced != row.fsNode->fsid))
    {
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
        LOG_verbose << syncname << "checking localnodes for synced handle " << row.cloudNode->handle;

        bool rowResult;
        if (checkCloudPathForMovesRenames(row, parentRow, fullPath, rowResult, belowRemovedFsNode))
        {
            row.itemProcessed = true;
            return rowResult;
        }
    }

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
            SYNC_verbose << "Name clashes at this already-synced folder.  We will sync below though." << logTriplet(row, fullPath);
        }
        else
        {
            LOG_debug << "Multple names clash here.  Excluding this node from sync for now." << logTriplet(row, fullPath);
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
    CodeCounter::ScopeTimer rst(syncs.mClient.performanceStats.syncItemTime2);
    bool rowSynced = false;

    // each of the 8 possible cases of present/absent for this row
    if (row.syncNode)
    {
        if (row.syncNode->upload.actualUpload) row.syncNode->upload.checkCompleted();
        //todo: if (row.syncNode->download) row.syncNode->download.checkCompleted();

        if (row.fsNode)
        {
            if (row.cloudNode)
            {
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
                    }

                    // these comparisons may need to be adjusted for UTF, escapes
                    assert(row.syncNode->fsid_lastSynced != row.fsNode->fsid || row.syncNode->localname == row.fsNode->localname);
                    assert(row.syncNode->fsid_lastSynced == row.fsNode->fsid || 0 == compareUtf(row.syncNode->localname, true, row.fsNode->localname, true, isCaseInsensitive(mFilesystemType)));
                    assert(row.syncNode->syncedCloudNodeHandle != row.cloudNode->handle || row.syncNode->name == row.cloudNode->name);
                    assert(row.syncNode->syncedCloudNodeHandle == row.cloudNode->handle || 0 == compareUtf(row.syncNode->name, true, row.cloudNode->name, true, isCaseInsensitive(mFilesystemType)));

                    assert((!!row.syncNode->slocalname == !!row.fsNode->shortname) &&
                           (!row.syncNode->slocalname ||
                           (*row.syncNode->slocalname == *row.fsNode->shortname)));

                    if (row.syncNode->fsid_lastSynced != row.fsNode->fsid ||
                        row.syncNode->syncedCloudNodeHandle != row.cloudNode->handle ||
                        row.syncNode->localname != row.fsNode->localname ||
                        row.syncNode->name != row.cloudNode->name)
                    {
                        LOG_verbose << syncname << "Row is synced, setting fsid and nodehandle" << logTriplet(row, fullPath);

                        if (row.syncNode->type == FOLDERNODE && row.syncNode->fsid_lastSynced != row.fsNode->fsid)
                        {
                            // a folder disappeared and was replaced by a different one - scan it all
                            row.syncNode->setScanAgain(false, true, true, 0);
                        }

                        row.syncNode->setSyncedFsid(row.fsNode->fsid, syncs.localnodeBySyncedFsid, row.fsNode->localname);
                        row.syncNode->setSyncedNodeHandle(row.cloudNode->handle, row.cloudNode->name);

                        row.syncNode->treestate(TREESTATE_SYNCED);

                        if (row.syncNode->type == FILENODE)
                        {
                            row.syncNode->checkMovesAgain = 0;
                            //row.syncNode->moveTargetApplyingToCloud = false;
                        }

                        statecacheadd(row.syncNode);
                        ProgressingMonitor monitor(syncs); // not stalling
                    }
                    else
                    {
                        SYNC_verbose << syncname << "Row was already synced" << logTriplet(row, fullPath);
                    }
                    rowSynced = true;
                }
                else if (cloudEqual || isBackupAndMirroring())
                {
                    // filesystem changed, put the change
                    rowSynced = resolve_upsync(row, parentRow, fullPath);
                }
                else if (fsEqual)
                {
                    // cloud has changed, get the change
                    rowSynced = resolve_downsync(row, parentRow, fullPath, true);
                }
                else
                {
                    // both changed, so we can't decide without the user's help
                    rowSynced = resolve_userIntervention(row, parentRow, fullPath);
                }
            }
            else
            {
                // cloud item absent
                if (row.syncNode->syncedCloudNodeHandle.isUndef() || isBackupAndMirroring())
                {
                    // cloud item did not exist before; upsync
                    rowSynced = resolve_upsync(row, parentRow, fullPath);
                }
                else
                {
                    // cloud item disappeared - remove locally (or figure out if it was a move, etc)
                    rowSynced = resolve_cloudNodeGone(row, parentRow, fullPath);
                }
            }
        }
        else
        {
            if (row.cloudNode)
            {
                // local item not present
                if (row.syncNode->fsid_lastSynced != UNDEF)
                {
                    // used to be synced - remove in the cloud (or detect move)
                    rowSynced = resolve_fsNodeGone(row, parentRow, fullPath);
                }
                else
                {
                    // fs item did not exist before; downsync
                    rowSynced = resolve_downsync(row, parentRow, fullPath, false);
                }
            }
            else
            {
                // local and cloud disappeared; remove sync item also
                rowSynced = resolve_delSyncNode(row, parentRow, fullPath);
            }
        }
    }

    else
    {
        if (row.fsNode)
        {
            if (row.cloudNode)
            {
                // Item exists locally and remotely but we haven't synced them previously
                // If they are equal then join them with a Localnode. Othewise report or choose greater mtime.
                if (row.fsNode->type != row.cloudNode->type)
                {
                    rowSynced = resolve_userIntervention(row, parentRow, fullPath);
                }
                else if (row.fsNode->type != FILENODE ||
                         row.fsNode->fingerprint == row.cloudNode->fingerprint)
                {
                    rowSynced = resolve_makeSyncNode_fromFS(row, parentRow, fullPath, false);
                }
                else
                {
                    rowSynced = resolve_pickWinner(row, parentRow, fullPath);
                }
            }
            else
            {
                // Item exists locally only. Check if it was moved/renamed here, or Create
                // If creating, next run through will upload it
                rowSynced = resolve_makeSyncNode_fromFS(row, parentRow, fullPath, false);
            }
        }
        else
        {
            if (row.cloudNode)
            {
                // item exists remotely only
                rowSynced = resolve_makeSyncNode_fromCloud(row, parentRow, fullPath, false);
            }
            else
            {
                // no entries - can occur when names clash, but should be caught above
                assert(false);
            }
        }
    }
    return rowSynced;
}


bool Sync::resolve_makeSyncNode_fromFS(syncRow& row, syncRow& parentRow, SyncPath& fullPath, bool considerSynced)
{
    ProgressingMonitor monitor(syncs);

    // this really is a new node: add
    LOG_debug << syncname << "Creating LocalNode from FS with fsid " << toHandle(row.fsNode->fsid) << " at: " << fullPath.localPath_utf8() << logTriplet(row, fullPath);

    assert(row.syncNode == nullptr);
    row.syncNode = new LocalNode;

    if (row.fsNode->type == FILENODE)
    {
        assert(row.fsNode->fingerprint.isvalid);
        row.syncNode->syncedFingerprint = row.fsNode->fingerprint;
    }

    row.syncNode->init(this, row.fsNode->type, parentRow.syncNode, fullPath.localPath, row.fsNode->cloneShortname());

    if (considerSynced)
    {
        row.syncNode->setSyncedFsid(row.fsNode->fsid, syncs.localnodeBySyncedFsid, row.fsNode->localname);  // todo: not sure we need this one at this stage
        row.syncNode->setScannedFsid(row.fsNode->fsid, syncs.localnodeByScannedFsid, row.fsNode->localname);
        row.syncNode->treestate(TREESTATE_SYNCED);
    }
    else
    {
        row.syncNode->treestate(TREESTATE_PENDING);
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
    ProgressingMonitor monitor(syncs);
    LOG_debug << syncname << "Creating LocalNode from Cloud at: " << fullPath.cloudPath << logTriplet(row, fullPath);

    assert(row.syncNode == nullptr);
    row.syncNode = new LocalNode;

    if (row.cloudNode->type == FILENODE)
    {
        assert(row.cloudNode->fingerprint.isvalid);
        row.syncNode->syncedFingerprint = row.cloudNode->fingerprint;
    }
    row.syncNode->init(this, row.cloudNode->type, parentRow.syncNode, fullPath.localPath, nullptr);
    if (considerSynced)
    {
        row.syncNode->setSyncedNodeHandle(row.cloudNode->handle, row.cloudNode->name);
        row.syncNode->treestate(TREESTATE_SYNCED);
    }
    else
    {
        row.syncNode->treestate(TREESTATE_PENDING);
    }
    if (row.syncNode->type != FILENODE)
    {
        row.syncNode->setSyncAgain(false, true, true);
    }
    statecacheadd(row.syncNode);
    row.syncNode->setSyncAgain(true, false, false);

    return false;
}

bool Sync::resolve_delSyncNode(syncRow& row, syncRow& parentRow, SyncPath& fullPath)
{
    ProgressingMonitor monitor(syncs);

    //if (!row.fsNode && row.syncNode->scanAgain != TREESTATE_NONE)
    //{
    //    SYNC_verbose << syncname << "Clearing scan flag for no-fsNode resolve_delSyncNode. " << logTriplet(row, fullPath);
    //    row.syncNode->scanAgain = TREESTATE_NONE;
    //}

    //if (client->mSyncFlags->movesWereComplete ||
    //    row.syncNode->moveSourceAppliedToCloud ||
    //    row.syncNode->moveAppliedToLocal ||
    //    row.syncNode->deletedFS ||
    //    row.syncNode->deletingCloud) // once the cloud delete finishes, the fs node is gone and so we visit here on the next run
    //{

        // local and cloud disappeared; remove sync item also
        //if (row.syncNode->moveSourceAppliedToCloud)
        //{
        //    SYNC_verbose << syncname << "Deleting Localnode (moveSourceAppliedToCloud)" << logTriplet(row, fullPath);
        //}
        //else
        if (row.syncNode->moveAppliedToLocal)
        {
            SYNC_verbose << syncname << "Deleting Localnode (moveAppliedToLocal)" << logTriplet(row, fullPath);
        }
        else if (row.syncNode->deletedFS)
        {
            SYNC_verbose << syncname << "Deleting Localnode (deletedFS)" << logTriplet(row, fullPath);
        }
        else if (row.syncNode->deletingCloud)
        {
            SYNC_verbose << syncname << "Deleting Localnode (deletingCloud)" << logTriplet(row, fullPath);
        }
        else if (syncs.mSyncFlags->movesWereComplete)
        {
            SYNC_verbose << syncname << "Deleting Localnode (movesWereComplete)" << logTriplet(row, fullPath);
        }
        else
        {
            SYNC_verbose << syncname << "Deleting Localnode (not due to this sync's actions)" << logTriplet(row, fullPath);
        }

        if (row.syncNode->deletedFS)
        {
            if (row.syncNode->type == FOLDERNODE)
            {
                LOG_debug << "Sync - local folder deletion detected: " << fullPath.localPath.toPath(*syncs.fsaccess);
            }
            else
            {
                LOG_debug << "Sync - local file deletion detected: " << fullPath.localPath.toPath(*syncs.fsaccess);
            }
        }

        // deletes itself and subtree, queues db record removal
        delete row.syncNode;
        row.syncNode = nullptr; // todo: maybe could return true here?
    //}
    //else if (row.syncNode->moveSourceApplyingToCloud)
    //{
    //    // The logic for a move can detect when it's finished, and sets moveAppliedToCloud
    //    LOG_verbose << syncname << "Cloud move still in progress" << logTriplet(row, fullPath);
    //    row.syncNode->setSyncAgain(true, false, false); // visit again
    //    row.suppressRecursion = true;
    //}
    //else
    //{
    //    // this case can occur, eg. when the user deletes some nodes that were an unresolved/stalled move, to resolve it.
    //    LOG_debug << syncname << "resolve_delSyncNode not resolved yet, waiting for movesWereComplete flag: " << logTriplet(row, fullPath);
    //    monitor.noResult();
    //}
    return false;
}

bool Sync::resolve_upsync(syncRow& row, syncRow& parentRow, SyncPath& fullPath)
{
    ProgressingMonitor monitor(syncs);

    if (row.fsNode->type == FILENODE)
    {
        // upload the file if we're not already uploading

        if (row.syncNode->upload.actualUpload &&
          !(row.syncNode->upload.actualUpload->fingerprint() == row.fsNode->fingerprint))
        {
            LOG_debug << syncname << "An older version of this file was already uploading, cancelling." << fullPath.localPath_utf8() << logTriplet(row, fullPath);
            row.syncNode->upload.reset(nullptr);
        }

        if (!row.syncNode->upload.actualUpload /*&& !row.syncNode->newnode*/)
        {
            // Sanity.
            assert(row.syncNode->parent);
            assert(row.syncNode->parent == parentRow.syncNode);

            if (parentRow.cloudNode && parentRow.cloudNode->handle == parentRow.syncNode->syncedCloudNodeHandle)
            {
                LOG_debug << "Sync - local file addition detected: " << fullPath.localPath.toPath(*syncs.fsaccess);

                LOG_debug << syncname << "Uploading file " << fullPath.localPath_utf8() << logTriplet(row, fullPath);
                assert(row.syncNode->syncedFingerprint.isvalid); // LocalNodes for files always have a valid fingerprint

                row.syncNode->upload.reset(std::make_shared<SyncUpload_inClient>(*row.fsNode, parentRow.cloudNode->handle, fullPath.localPath));

                auto uploadPtr = row.syncNode->upload.actualUpload;
                syncs.clientThreadActions.pushBack([uploadPtr](MegaClient& mc, DBTableTransactionCommitter& committer)
                    {
                        mc.nextreqtag();
                        mc.startxfer(PUT, uploadPtr.get(), committer);
                    });


                LOG_debug << "Sync - sending file " << fullPath.localPath_utf8();

//todo: update upload's target handle if this LocalNode moves around
                //h = NodeHandle();

                //if (localNode.parent && !localNode.parent->syncedCloudNodeHandle.isUndef())
                //{
                //    if (Node* p = localNode.sync->client->nodeByHandle(localNode.parent->syncedCloudNodeHandle))
                //    {
                //        h = p->nodeHandle();
                //    }
                //}

                //if (h.isUndef())
                //{
                //    h.set6byte(t->client->rootnodes[RUBBISHNODE - ROOTNODE]);
                //}

// todo: also update Upload's localname in case of moves

            }
            else
            {
                SYNC_verbose << syncname << "Parent cloud folder to upload to doesn't exist yet" << logTriplet(row, fullPath);
                row.syncNode->setSyncAgain(true, false, false);
                monitor.waitingLocal(fullPath.localPath, LocalPath(), fullPath.cloudPath, SyncWaitReason::UpsyncNeedsTargetFolder);
            }
        }
//todo:        else if (row.syncNode->newnode)
        //{
        //    SYNC_verbose << syncname << "Upload complete but putnodes in progress" << logTriplet(row, fullPath);
        //}
        else
        {
            SYNC_verbose << syncname << "Upload already in progress" << logTriplet(row, fullPath);
        }
    }
    else
    {
        if (parentRow.cloudNode)
        {
            // Check for filename anomalies.
            {
                auto type = isFilenameAnomaly(*row.syncNode);

                if (type != FILENAME_ANOMALY_NONE)
                {
                    auto lp = fullPath.localPath_utf8();
                    auto rp = fullPath.cloudPath;

                    syncs.clientThreadActions.pushBack([type, lp, rp](MegaClient& mc, DBTableTransactionCommitter& committer)
                        {
                            mc.filenameAnomalyDetected(type, lp, rp);
                        });
                }
            }

            LOG_verbose << "Creating cloud node for: " << fullPath.localPath_utf8() << logTriplet(row, fullPath);
            // while the operation is in progress sync() will skip over the parent folder
            string foldername = row.syncNode->name;
            NodeHandle targethandle = parentRow.cloudNode->handle;
            syncs.clientThreadActions.pushBack([foldername, targethandle](MegaClient& mc, DBTableTransactionCommitter& committer)
                {
                    vector<NewNode> nn(1);
                    mc.putnodes_prepareOneFolder(&nn[0], foldername);
                    mc.putnodes(targethandle, move(nn), nullptr, 0);
                });
        }
        else
        {
            SYNC_verbose << "Delay creating cloud node until parent cloud node exists: " << fullPath.localPath_utf8() << logTriplet(row, fullPath);
            row.syncNode->setSyncAgain(true, false, false);
            monitor.waitingLocal(fullPath.localPath, LocalPath(), fullPath.cloudPath, SyncWaitReason::UpsyncNeedsTargetFolder);
        }

        // we may not see some moves/renames until the entire folder structure is created.
        row.syncNode->setCheckMovesAgain(true, false, false);     // todo: double check - might not be needed for the wait case? might cause a stall?
    }
    return false;
}

bool Sync::resolve_downsync(syncRow& row, syncRow& parentRow, SyncPath& fullPath, bool alreadyExists)
{
    ProgressingMonitor monitor(syncs);

    if (row.cloudNode->type == FILENODE)
    {
        if (isBackup())
        {
            // Backups must not change the local
            changestate(SYNC_FAILED, BACKUP_MODIFIED, false, true);
            return false;
        }

        // download the file if we're not already downloading
        // if (alreadyExists), we will move the target to the trash when/if download completes //todo: check

        if (row.syncNode->download.actualDownload &&
            !(row.syncNode->download.actualDownload->fingerprint() == row.cloudNode->fingerprint))
        {
            LOG_debug << syncname << "An older version of this file was already downloading, cancelling." << fullPath.cloudPath << logTriplet(row, fullPath);
            row.syncNode->download.reset(nullptr);
        }

        if (parentRow.fsNode)
        {
            if (!row.syncNode->download.actualDownload)
            {
                LOG_debug << "Sync - remote file addition detected: " << fullPath.cloudPath;

                // FIXME: to cover renames that occur during the
                // download, reconstruct localname in complete()
                LOG_debug << syncname << "Start sync download: " << row.syncNode << logTriplet(row, fullPath);
                LOG_debug << "Sync - requesting file " << fullPath.localPath_utf8();

                createDebrisTmpLockOnce();

                // download to tmpfaPath (folder debris/tmp). We will rename/mv it to correct location (updated if necessary) after that completes
                row.syncNode->download.reset(std::make_shared<SyncDownload_inClient>(*row.cloudNode, tmpfaPath, inshare));

                auto downloadPtr = row.syncNode->download.actualDownload;
                syncs.clientThreadActions.pushBack([downloadPtr](MegaClient& mc, DBTableTransactionCommitter& committer)
                    {
                        mc.nextreqtag();
                        mc.startxfer(GET, downloadPtr.get(), committer);
                    });

                if (row.syncNode) row.syncNode->treestate(TREESTATE_SYNCING);
                else if (parentRow.syncNode) parentRow.syncNode->treestate(TREESTATE_SYNCING);
            }
            else if (row.syncNode->download.actualDownload->wasTerminated)
            {
                SYNC_verbose << syncname << "Download was terminated " << logTriplet(row, fullPath);
                row.syncNode->download.reset(nullptr);
            }
            else if (row.syncNode->download.actualDownload->wasCompleted)
            {
// todo: check if there is something at the target path already.  Also, if this folder has moved but we didn't notice yet, should we wait to catch up ?
                if (syncs.fsaccess->renamelocal(row.syncNode->download.actualDownload->localname, fullPath.localPath))
                {
                    SYNC_verbose << syncname << "Download complete, moved file to final destination" << logTriplet(row, fullPath);
                    row.syncNode->download.reset(nullptr);
                }
                else if (syncs.fsaccess->transient_error)
                {
                    SYNC_verbose << syncname << "Download complete, but move transient error" << logTriplet(row, fullPath);
                }
                else
                {
                    SYNC_verbose << syncname << "Download complete, but move failed" << logTriplet(row, fullPath);
                    row.syncNode->download.reset(nullptr);
                }
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
    else
    {
        assert(!alreadyExists); // if it did we would have matched it

        if (parentRow.fsNode)
        {
            if (isBackup())
            {
                // Backups must not change the local
                changestate(SYNC_FAILED, BACKUP_MODIFIED, false, true);
                return false;
            }

            // Check for filename anomalies.
            {
                auto type = isFilenameAnomaly(fullPath.localPath, row.cloudNode->name);

                if (type != FILENAME_ANOMALY_NONE)
                {
                    auto remotePath = fullPath.cloudPath;
                    auto localPath = fullPath.localPath_utf8();
                    syncs.clientThreadActions.pushBack([type, localPath, remotePath](MegaClient& mc, DBTableTransactionCommitter& committer)
                        {
                            mc.filenameAnomalyDetected(type, localPath, remotePath);
                        });
                }
            }

            LOG_verbose << "Creating local folder at: " << fullPath.localPath_utf8() << logTriplet(row, fullPath);

            assert(!isBackup());
            if (syncs.fsaccess->mkdirlocal(fullPath.localPath))
            {
                assert(row.syncNode);
                assert(row.syncNode->localname == fullPath.localPath.leafName());

                // Update our records of what we know is on disk for this (parent) LocalNode.
                // This allows the next level of folders to be created too

                auto fa = syncs.fsaccess->newfileaccess(false);
                if (fa->fopen(fullPath.localPath, true, false))
                {
                    auto fsnode = FSNode::fromFOpened(*fa, fullPath.localPath, *syncs.fsaccess);

                    row.syncNode->localname = fsnode->localname;
                    row.syncNode->slocalname = fsnode->cloneShortname();

					// todo:  synced too, or is just scanned enough?
					// was removed before stash-merge-back
					//row.syncNode->setSyncedFsid(fsnode->fsid, client->localnodeBySyncedFsid, fsnode->localname);   // todo:  synced too, or is just scanned enough?

					row.syncNode->setScannedFsid(fsnode->fsid, syncs.localnodeByScannedFsid, fsnode->localname);
                    statecacheadd(row.syncNode);

                    // Mark other nodes with this FSID as having their FSID reused.
                    syncs.setSyncedFsidReused(fsnode->fsid, row.syncNode);
                    syncs.setScannedFsidReused(fsnode->fsid, row.syncNode);   // todo: could put these lines first then we don't need excluded parameter?

                    // So that we can recurse into the new directory immediately.
                    parentRow.fsPendingSiblings.emplace_back(std::move(*fsnode));
                    row.fsNode = &parentRow.fsPendingSiblings.back();

                    row.syncNode->setScanAgain(false, true, true, 0);
                    row.syncNode->setSyncAgain(false, true, false);
                }
                else
                {
                    LOG_warn << syncname << "Failed to fopen folder straight after creation - revisit in 5s. " << fullPath.localPath_utf8() << logTriplet(row, fullPath);
                    row.syncNode->setScanAgain(true, false, false, 50);
                }
            }
            else if (syncs.fsaccess->transient_error)
            {
                LOG_warn << syncname << "Transient error creating folder, marking as blocked " << fullPath.localPath_utf8() << logTriplet(row, fullPath);
                assert(row.syncNode);
                row.syncNode->setUseBlocked();
            }
            else // !transient_error
            {
                // let's consider this case as blocked too, alert the user
                LOG_warn << syncname << "Non transient error creating folder, marking as blocked " << fullPath.localPath_utf8() << logTriplet(row, fullPath);
                assert(row.syncNode);
                row.syncNode->setUseBlocked();
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
    ProgressingMonitor monitor(syncs);
    LOG_debug << syncname << "write me" << logTriplet(row, fullPath);
    assert(false);
    return false;
}

bool Sync::resolve_pickWinner(syncRow& row, syncRow& parentRow, SyncPath& fullPath)
{
    ProgressingMonitor monitor(syncs);

    const FileFingerprint& cloud = row.cloudNode->fingerprint;
    const FileFingerprint& fs = row.fsNode->fingerprint;

    auto fromFS = fs.mtime > cloud.mtime
                  || (fs.mtime == cloud.mtime
                      && (fs.size > cloud.size
                          || (fs.size == cloud.size && fs.crc > cloud.crc)));

    return fromFS ? resolve_makeSyncNode_fromFS(row, parentRow, fullPath, false)
                  : resolve_makeSyncNode_fromCloud(row, parentRow, fullPath, false);
}

bool Sync::resolve_cloudNodeGone(syncRow& row, syncRow& parentRow, SyncPath& fullPath)
{
    ProgressingMonitor monitor(syncs);

    // todo: detach nodes?
    Node* prevSyncedNode = syncs.mClient.nodeByHandle(row.syncNode->syncedCloudNodeHandle);

    if (prevSyncedNode && syncs.nodeIsInActiveSync(prevSyncedNode, false))   // false because paused syncs do not particpate in move detection
    {
        row.syncNode->setCheckMovesAgain(true, false, false);

        row.syncNode->trimRareFields();

        if (row.syncNode->hasRare() && !row.syncNode->rare().moveToHere.expired())
        {
            SYNC_verbose << syncname << "Node is a cloud move source, move is under way: " << logTriplet(row, fullPath);
        }
        else
        {
            SYNC_verbose << syncname << "Letting move destination node process this first (cloud node is at "
			             << prevSyncedNode->displaypath() << "): " << logTriplet(row, fullPath);

        }
        monitor.waitingCloud(fullPath.cloudPath, prevSyncedNode->displaypath(), fullPath.localPath, SyncWaitReason::MoveNeedsDestinationNodeProcessing);
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
            changestate(SYNC_FAILED, BACKUP_MODIFIED, false, true);
            return false;
        }

        LOG_debug << syncname << "Moving local item to local sync debris: " << fullPath.localPath_utf8() << logTriplet(row, fullPath);
        if (movetolocaldebris(fullPath.localPath))
        {
            row.syncNode->setScanAgain(true, false, false, 0);
            row.syncNode->scanAgain = TREE_RESOLVED;

            // don't let revisits do anything until the tree is cleaned up
            row.syncNode->deletedFS = true;
        }
        else
        {
            LOG_err << syncname << "Failed to move to local debris:  " << fullPath.localPath_utf8();
            // todo: try again on a delay?
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
    }

    row.suppressRecursion = true;
    row.recurseBelowRemovedCloudNode = true;

    return false;
}

LocalNode* Syncs::findLocalNodeBySyncedFsid(mega::handle fsid, nodetype_t type, const FileFingerprint& fingerprint, Sync* filesystemSync, std::function<bool(LocalNode* ln)> extraCheck)
{
    if (fsid == UNDEF) return nullptr;

    auto range = localnodeBySyncedFsid.equal_range(fsid);

    for (auto it = range.first; it != range.second; ++it)
    {
        if (it->second->type != type) continue;
        if (it->second->fsidSyncedReused)   continue;

        //todo: actually we do want to support moving files/folders between syncs
        // in the same filesystem so, commented this

        // make sure we are in the same filesystem (fsid comparison is not valid in other filesystems)
        //if (it->second->sync != &filesystemSync)
        //{
        //    continue;
        //}

        if (filesystemSync)
        {
            auto fp1 = it->second->sync->dirnotify->fsfingerprint();
            auto fp2 = filesystemSync->dirnotify->fsfingerprint();
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
            LOG_verbose << mClient.clientname << "findLocalNodeBySyncedFsid - found " << toHandle(fsid) << " at: " << it->second->getLocalPath().toPath(*mClient.fsaccess);
            return it->second;
        }
    }
    return nullptr;
}

LocalNode* Syncs::findLocalNodeByScannedFsid(mega::handle fsid, nodetype_t type, const FileFingerprint* fingerprint, Sync* filesystemSync, std::function<bool(LocalNode* ln)> extraCheck)
{
    if (fsid == UNDEF) return nullptr;

    auto range = localnodeByScannedFsid.equal_range(fsid);

    for (auto it = range.first; it != range.second; ++it)
    {
        if (it->second->type != type) continue;
        if (it->second->fsidScannedReused)   continue;

        //todo: actually we do want to support moving files/folders between syncs
        // in the same filesystem so, commented this

        // make sure we are in the same filesystem (fsid comparison is not valid in other filesystems)
        //if (it->second->sync != &filesystemSync)
        //{
        //    continue;
        //}

        if (filesystemSync)
        {
            auto fp1 = it->second->sync->dirnotify->fsfingerprint();
            auto fp2 = filesystemSync->dirnotify->fsfingerprint();
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
            LOG_verbose << mClient.clientname << "findLocalNodeByScannedFsid - found at: " << it->second->getLocalPath().toPath(*mClient.fsaccess);
            return it->second;
        }
    }
    return nullptr;
}

void Syncs::setSyncedFsidReused(mega::handle fsid, const LocalNode* exclude)
{
    for (auto range = localnodeBySyncedFsid.equal_range(fsid);
        range.first != range.second;
        ++range.first)
    {
        if (range.first->second == exclude) continue;
        range.first->second->fsidSyncedReused = true;
    }
}

void Syncs::setScannedFsidReused(mega::handle fsid, const LocalNode* exclude)
{
    for (auto range = localnodeByScannedFsid.equal_range(fsid);
        range.first != range.second;
        ++range.first)
    {
        if (range.first->second == exclude) continue;
        range.first->second->fsidScannedReused = true;
    }
}

LocalNode* Syncs::findLocalNodeByNodeHandle(NodeHandle h)
{
    if (h.isUndef()) return nullptr;

    auto range = localnodeByNodeHandle.equal_range(h);

    for (auto it = range.first; it != range.second; ++it)
    {
        // check the file/folder actually exists on disk for this LocalNode
        LocalPath lp = it->second->getLocalPath();

        auto prevfa = mClient.fsaccess->newfileaccess(false);
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
            syncs.clientThreadActions.pushBack([](MegaClient& mc, DBTableTransactionCommitter& committer)
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
    ProgressingMonitor monitor(syncs);

    LocalNode* movedLocalNode = nullptr;

    if (!row.syncNode->fsidSyncedReused)
    {
        auto predicate = [&row](LocalNode* n) {
            return n != row.syncNode;
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
            SYNC_verbose << syncname << "Node was a cloud move source, move is propagating: " << logTriplet(row, fullPath);
        }
        else
        {
            SYNC_verbose << syncname << "This file/folder was moved, letting destination node at "
                         << movedLocalNode->localnodedisplaypath(*syncs.fsaccess) << " process this first: " << logTriplet(row, fullPath);
        }
        // todo: do we need an equivalent to row.recurseToScanforNewLocalNodesOnly = true;  (in resolve_cloudNodeGone)
        monitor.waitingLocal(fullPath.localPath, movedLocalNode->getLocalPath(), string(), SyncWaitReason::MoveNeedsDestinationNodeProcessing);
    }
    else if (syncs.mSyncFlags->movesWereComplete)
    {
        if (!row.syncNode->deletingCloud)
        {
            LOG_debug << syncname << "Moving cloud item to cloud sync debris: " << fullPath.cloudPath << logTriplet(row, fullPath);
            bool fromInshare = inshare;
            auto debrisNodeHandle = row.cloudNode->handle;
            syncs.clientThreadActions.pushBack([debrisNodeHandle, fromInshare](MegaClient& mc, DBTableTransactionCommitter& committer)
                {
                    if (auto n = mc.nodeByHandle(debrisNodeHandle))
                    {
                        mc.movetosyncdebris(n, fromInshare);
                    }
                });
            row.syncNode->deletingCloud = true;   // todo: use lifetime-tracked shared_ptr instead
        }
        else
        {
            SYNC_verbose << syncname << "Already moving cloud item to cloud sync debris: " << fullPath.cloudPath << logTriplet(row, fullPath);
        }
    }
    else
    {
        // in case it's actually a move and we just haven't seen the fsid yet
        SYNC_verbose << syncname << "Wait for scanning/moving to finish before confirming fsid " << toHandle(row.syncNode->fsid_lastSynced) << " deleted: " << logTriplet(row, fullPath);

        monitor.waitingLocal(fullPath.localPath, LocalPath(), string(), SyncWaitReason::DeleteWaitingOnMoves);
    }

    // there's no folder so clear the flag so we don't stall
    row.syncNode->scanAgain = TREE_RESOLVED;
    row.syncNode->checkMovesAgain = TREE_RESOLVED;

    row.suppressRecursion = true;
    row.recurseBelowRemovedFsNode = true;
    row.syncNode->setSyncAgain(true, false, false); // make sure we revisit

    //bool inProgress = row.syncNode->deletingCloud || row.syncNode->moveApplyingToLocal || row.syncNode->moveSourceApplyingToCloud;

    //if (inProgress)
    //{
    //    if (row.syncNode->deletingCloud)        SYNC_verbose << syncname << "Waiting for cloud delete to complete " << logTriplet(row, fullPath);
    //    if (row.syncNode->moveApplyingToLocal)  SYNC_verbose << syncname << "Waiting for corresponding local move to complete " << logTriplet(row, fullPath);
    //    if (row.syncNode->moveSourceApplyingToCloud)  SYNC_verbose << syncname << "Waiting for cloud move to complete " << logTriplet(row, fullPath);
    //}

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
    // Assuming names already match
    // Not comparing nodehandle here.  If they all match we set syncedCloudNodeHandle
    if (n.type != ln.type) return false;
    if (n.type != FILENODE) return true;
    assert(n.fingerprint.isvalid && ln.syncedFingerprint.isvalid);
    return n.fingerprint == ln.syncedFingerprint;  // size, mtime, crc
}

bool Sync::syncEqual(const FSNode& fsn, const LocalNode& ln)
{
    // Assuming names already match
    // Not comparing fsid here. If they all match then we set LocalNode's fsid
    if (fsn.type != ln.type) return false;
    if (fsn.type != FILENODE) return true;
    assert(fsn.fingerprint.isvalid && ln.syncedFingerprint.isvalid);
    return fsn.fingerprint == ln.syncedFingerprint;  // size, mtime, crc
}

void Syncs::triggerSync(NodeHandle h, bool recurse)
{
    if (mClient.fetchingnodes) return;  // on start everything needs scan+sync anyway

    lock_guard<mutex> g(triggerMutex);
    auto& entry = triggerHandles[h];
    if (recurse) entry = true;
}

void Syncs::processTriggerHandles()
{
    map<NodeHandle, bool> triggers;
    {
        lock_guard<mutex> g(triggerMutex);
        triggers.swap(triggerHandles);
    }

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
                bool found = lookupCloudNode(h, cloudNode, &cloudNodePath);
                if (found)
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
                    SYNC_verbose << mClient.clientname << "Triggering sync flag for " << it->second->localnodedisplaypath(*fsaccess) << (recurse ? " recursive" : "");
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
LocalPath::fromPlatformEncoded(PATHSTRING(".megabackup"));

#undef PATHSTRING

const unsigned int NUM_CONFIG_SLOTS = 2;

SyncConfigStore::SyncConfigStore(const LocalPath& dbPath, SyncConfigIOContext& ioContext)
    : mInternalSyncStorePath(dbPath)
    , mIOContext(ioContext)
{
}

SyncConfigStore::~SyncConfigStore()
{
    assert(!dirty());
}

void SyncConfigStore::markDriveDirty(const LocalPath& drivePath)
{
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
    return mKnownDrives.count(drivePath) > 0;
}

vector<LocalPath> SyncConfigStore::knownDrives() const
{
    vector<LocalPath> result;

    for (auto& i : mKnownDrives)
    {
        result.emplace_back(i.first);
    }

    return result;
}

bool SyncConfigStore::removeDrive(const LocalPath& drivePath)
{
    return mKnownDrives.erase(drivePath) > 0;
}

error SyncConfigStore::read(const LocalPath& drivePath, SyncConfigVector& configs)
{
    DriveInfo driveInfo;
    driveInfo.dbPath = dbPath(drivePath);
    driveInfo.drivePath = drivePath;

    vector<unsigned int> confSlots;

    auto result = mIOContext.getSlotsInOrder(driveInfo.dbPath, confSlots);

    if (result == API_OK)
    {
        for (const auto& slot : confSlots)
        {
            result = read(driveInfo, configs, slot);

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
        error e = mIOContext.remove(drive.dbPath);
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

        error e = mIOContext.write(drive.dbPath,
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
        mIOContext.remove(drive.dbPath, drive.slot);

        return API_OK;
    }
}


error SyncConfigStore::read(DriveInfo& driveInfo, SyncConfigVector& configs,
                             unsigned int slot)
{
    const auto& dbPath = driveInfo.dbPath;
    string data;

    if (mIOContext.read(dbPath, data, slot) != API_OK)
    {
        return API_EREAD;
    }

    JSON reader(data);

    if (!mIOContext.deserialize(dbPath, configs, reader, slot))
    {
        return API_EREAD;
    }

    const auto& drivePath = driveInfo.drivePath;

    for (auto& config : configs)
    {
        config.mExternalDrivePath = drivePath;

        if (!drivePath.empty())
        {
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
  , mName(LocalPath::fromPath(NAME_PREFIX + name, mFsAccess))
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
                                      unsigned int slot) const
{
    auto path = dbFilePath(dbPath, slot).toPath(mFsAccess);

    LOG_debug << "Attempting to deserialize config DB: "
              << path;

    if (deserialize(configs, reader))
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
                                      JSON& reader) const
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

                if (deserialize(config, reader))
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
    globPath.append(LocalPath::fromPath(".?", mFsAccess));

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
        const char suffix = filePath.toPath(mFsAccess).back();

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
              << path.toPath(mFsAccess);

    // Try and open the file for reading.
    auto fileAccess = mFsAccess.newfileaccess(false);

    if (!fileAccess->fopen(path, true, false))
    {
        // Couldn't open the file for reading.
        LOG_err << "Unable to open config DB for reading: "
                << path.toPath(mFsAccess);

        return API_EREAD;
    }

    // Try and read the data from the file.
    string d;

    if (!fileAccess->fread(&d, static_cast<unsigned>(fileAccess->size), 0, 0x0))
    {
        // Couldn't read the file.
        LOG_err << "Unable to read config DB: "
                << path.toPath(mFsAccess);

        return API_EREAD;
    }

    // Try and decrypt the data.
    if (!decrypt(d, data))
    {
        // Couldn't decrypt the data.
        LOG_err << "Unable to decrypt config DB: "
                << path.toPath(mFsAccess);

        return API_EREAD;
    }

    LOG_debug << "Config DB successfully read from disk: "
              << path.toPath(mFsAccess)
              << ": "
              << data;

    return API_OK;
}

error SyncConfigIOContext::remove(const LocalPath& dbPath,
                                  unsigned int slot)
{
    LocalPath path = dbFilePath(dbPath, slot);

    if (!mFsAccess.unlinklocal(path))
    {
        LOG_warn << "Unable to remove config DB: "
                 << path.toPath(mFsAccess);

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
              << dbPath.toPath(mFsAccess)
              << " / "
              << slot;

    // Try and create the backup configuration directory.
    if (!(mFsAccess.mkdirlocal(path) || mFsAccess.target_exists))
    {
        LOG_err << "Unable to create config DB directory: "
                << dbPath.toPath(mFsAccess);

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
                << path.toPath(mFsAccess);

        return API_EWRITE;
    }

    // Ensure the file is empty.
    if (!fileAccess->ftruncate())
    {
        // Couldn't truncate the file.
        LOG_err << "Unable to truncate config DB: "
                << path.toPath(mFsAccess);

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
                << path.toPath(mFsAccess);

        return API_EWRITE;
    }

    LOG_debug << "Config DB successfully written to disk: "
              << path.toPath(mFsAccess)
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
    path.append(LocalPath::fromPath("." + to_string(slot), mFsAccess));

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

bool SyncConfigIOContext::deserialize(SyncConfig& config, JSON& reader) const
{
    const auto TYPE_BACKUP_ID       = MAKENAMEID2('i', 'd');
    const auto TYPE_BACKUP_STATE    = MAKENAMEID2('b', 's');
    const auto TYPE_ENABLED         = MAKENAMEID2('e', 'n');
    const auto TYPE_FINGERPRINT     = MAKENAMEID2('f', 'p');
    const auto TYPE_LAST_ERROR      = MAKENAMEID2('l', 'e');
    const auto TYPE_LAST_WARNING    = MAKENAMEID2('l', 'w');
    const auto TYPE_NAME            = MAKENAMEID1('n');
    const auto TYPE_SOURCE_PATH     = MAKENAMEID2('s', 'p');
    const auto TYPE_SYNC_TYPE       = MAKENAMEID2('s', 't');
    const auto TYPE_TARGET_HANDLE   = MAKENAMEID2('t', 'h');
    const auto TYPE_TARGET_PATH     = MAKENAMEID2('t', 'p');
    const auto TYPE_EXCLUSION_RULES = MAKENAMEID2('e', 'r');

    for ( ; ; )
    {
        switch (reader.getnameid())
        {
        case EOO:
            // success if we reached the end of the object
            return *reader.pos == '}';

        case TYPE_ENABLED:
            config.mEnabled = reader.getbool();
            break;

        case TYPE_FINGERPRINT:
            config.mLocalFingerprint = reader.getfp();
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

            config.mLocalPath =
              LocalPath::fromPath(sourcePath, mFsAccess);

            break;
        }

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
    auto sourcePath = config.mLocalPath.toPath(mFsAccess);

    // Strip drive path from source.
    if (config.isExternal())
    {
        auto drivePath = config.mExternalDrivePath.toPath(mFsAccess);
        sourcePath.erase(0, drivePath.size());
    }

    writer.beginobject();
    writer.arg("id", config.getBackupId(), sizeof(handle));
    writer.arg_B64("sp", sourcePath);
    writer.arg_B64("n", config.mName);
    writer.arg_B64("tp", config.mOriginalPathOfRemoteRootNode);
    writer.arg_fsfp("fp", config.mLocalFingerprint);
    writer.arg("th", config.mRemoteNode);
    writer.arg("le", config.mError);
    writer.arg("lw", config.mWarning);
    writer.arg("st", config.mSyncType);
    writer.arg("en", config.mEnabled);
    writer.arg("bs", config.mBackupState);
    writer.endobject();
}

void Syncs::syncLoop()
{
    std::condition_variable cv;
    std::mutex dummy_mutex;
    std::unique_lock<std::mutex> dummy_lock(dummy_mutex);
    for (;;)
    {
        // always wait a little bit, keep CPU down and let MegaClient lock if it needs to
        //cv.wait_for(dummy_lock, std::chrono::milliseconds(50), [this]() { return false; });
        waiter.init(2);
        waiter.wait();

        // execute any requests from the MegaClient
        std::function<void()> f;
        while (syncThreadActions.popFront(f))
        {
            f();
        }

        // now run one recursiveSync loop for all the syncs.
        // mutex avoids syncs appearing/disappearing etc as we loop
        lock_guard<mutex> g(syncMutex);

        if (exitSyncLoop)
        {
            // Flush changes made to internal configs.
            syncConfigStoreFlush();
        }

        // verify filesystem fingerprints, disable deviating syncs
        // (this covers mountovers, some device removals and some failures)
        forEachRunningSync(true, [&](Sync* sync){
            if (sync->state != SYNC_FAILED && sync->fsfp)
            {
                fsfp_t current = sync->dirnotify->fsfingerprint();
                if (sync->fsfp != current)
                {
                    LOG_err << "Local fingerprint mismatch. Previous: " << sync->fsfp
                        << "  Current: " << current;
                    sync->changestate(SYNC_FAILED, current ? LOCAL_FINGERPRINT_MISMATCH : LOCAL_PATH_UNAVAILABLE, false, true);
                }
            }

            if (sync->state != SYNC_FAILED && !sync->cloudRoot())
            {
                LOG_err << "The remote root node doesn't exist";
                sync->changestate(SYNC_FAILED, REMOTE_NODE_NOT_FOUND, false, true);
            }
            });


        // process active syncs
        // sync timer: full rescan in case of filesystem notification failures
        //if (syncscanfailed && syncscanbt.armed())
        //{
        //    syncscanfailed = false;
        //    //syncops = true;
        //}

        //// sync timer: try to transition into monitoring mode.
        //if (mSyncMonitorRetry && mSyncMonitorTimer.armed())
        //{
        //    mSyncMonitorRetry = false;
        //    //            syncdownrequired = true;
        //}

        //// sync timer: file change upload delay timeouts (Nagle algorithm)
        //if (syncnagleretry && syncnaglebt.armed())
        //{
        //    syncnagleretry = false;
        //    //syncops = true;
        //}

        //if (syncextraretry && syncextrabt.armed())
        //{
        //    syncextraretry = false;
        //    syncops = true;
        //}

        // halt all syncing while the local filesystem is pending a lock-blocked operation
        // or while we are fetching nodes


        // now we process notifications AND sync() on each exec()
        //if (!syncdownretry && !syncadding && statecurrent && !anySyncNeedsTargetedSync() && !fetchingnodes)

        //{
        // process active syncs, stop doing so while transient local fs ops are pending
        //if (!syncs.empty() || syncactivity)
        //{
        //bool prevpending = false;
        //for (int q = syncfslockretry ? DirNotify::RETRY : DirNotify::DIREVENTS; q >= DirNotify::DIREVENTS; q--)
        //{
        //    for (Sync* sync : syncs)
        //    {
        //        prevpending = prevpending || sync->dirnotify->notifyq[q].size();
        //        if (prevpending)
        //        {
        //            break;
        //        }
        //    }
        //    if (prevpending)
        //    {
        //        break;
        //    }
        //}

        //dstime nds = NEVER;
        //dstime mindelay = NEVER;
        //for (Sync* sync : syncs)
        //{
        //    if (sync->isnetwork && (sync->state == SYNC_ACTIVE || sync->state == SYNC_INITIALSCAN))
        //    {
        //        Notification notification;
        //        while (sync->dirnotify->notifyq[DirNotify::EXTRA].popFront(notification))
        //        {
        //            dstime dsmin = Waiter::ds - Sync::EXTRA_SCANNING_DELAY_DS;
        //            if (notification.timestamp <= dsmin)
        //            {
        //                LOG_debug << "Processing extra fs notification: " << notification.path.toPath(*fsaccess);
        //                sync->dirnotify->notify(DirNotify::DIREVENTS, notification.localnode, std::move(notification.path));
        //            }
        //            else
        //            {
        //                sync->dirnotify->notifyq[DirNotify::EXTRA].unpopFront(notification);
        //                dstime delay = (notification.timestamp - dsmin) + 1;
        //                if (delay < mindelay)
        //                {
        //                    mindelay = delay;
        //                }
        //                break;
        //            }
        //        }
        //    }
        //}
        //if (EVER(mindelay))
        //{
        //    syncextrabt.backoff(mindelay);
        //    syncextraretry = true;
        //}
        //else
        //{
        //    syncextraretry = false;
        //}

        //for (int q = syncfslockretry ? DirNotify::RETRY : DirNotify::DIREVENTS; q >= DirNotify::DIREVENTS; q--)
        //{
        //    //if (!syncfsopsfailed)
        //    {
        //        syncfslockretry = false;

        //        // not retrying local operations: process pending notifyqs
        //        for (auto it = syncs.begin(); it != syncs.end(); )
        //        {
        //            Sync* sync = *it++;

        //            if (sync->state == SYNC_CANCELED || sync->state == SYNC_FAILED || sync->state == SYNC_DISABLED)
        //            {
        //                delete sync;  // removes itself from the client's list that we are iterating
        //                continue;
        //            }
        //            else if (sync->state == SYNC_ACTIVE || sync->state == SYNC_INITIALSCAN)
        //            {
        //                // process items from the notifyq until depleted
        //                if (sync->dirnotify->notifyq[q].size())
        //                {
        //                    //dstime dsretry;

        //                    syncops = true;

        //                    sync->procscanq(q);
        //                }

        //                if (sync->state == SYNC_INITIALSCAN && q == DirNotify::DIREVENTS && !sync->dirnotify->notifyq[q].size())
        //                {
        //                    sync->changestate(SYNC_ACTIVE);

        //                    // scan for items that were deleted while the sync was stopped
        //                    // FIXME: defer this until RETRY queue is processed
        //                    sync->scanseqno++;
        //                    sync->deletemissing(sync->localroot.get());
        //                }
        //            }
        //        }

        //        if (syncadding)
        //        {
        //            break;
        //        }
        //    }
        //}

        //if (syncadding)
        //{
        //    // do not continue processing syncs while adding nodes
        //    // just go to evaluate the main do-while loop
        //    notifypurge();
        //    continue;
        //}

        // delete files that were overwritten by folders in checkpath()
        //    execsyncdeletions();

        //if (synccreate.size())
        //{
        //    syncupdate();
        //}


        //if (prevpending && !totalpending)
        //{
        //    LOG_debug << "Scan queue processed, triggering a scan";
        //    setAllSyncsNeedFullSyncdown();
        //}

        //     notifypurge();

        //        if (!syncadding && (syncactivity || syncops))
        //        {

        //// perform aggregate ops that require all scanqs to be fully processed
        //bool anyqueued = false;
        //for (Sync* sync : syncs)
        //{
        //    if (sync->dirnotify->fsEventq.size())
        //    {
        //        if (!syncnagleretry)
        //        {
        //            syncactivity = true;
        //        }

        //        anyqueued = true;
        //    }
        //}

        //                    if (!anyqueued)
        //         {
        //// execution of notified deletions - these are held in localsyncnotseen and
        //// kept pending until all creations (that might reference them for the purpose of
        //// copying) have completed and all notification queues have run empty (to ensure
        //// that moves are not executed as deletions+additions.
        //if (localsyncnotseen.size() && !synccreate.size())
        //{
        //    // ... execute all pending deletions
        //    LocalPath path;
        //    auto fa = fsaccess->newfileaccess();
        //    while (localsyncnotseen.size())
        //    {
        //        LocalNode* l = *localsyncnotseen.begin();
        //        unlinkifexists(l, fa.get(), path);
        //        delete l;
        //    }
        //}

        // process filesystem notifications for active syncs unless we
        // are retrying local fs writes

        //         {
        //LOG_verbose << "syncops: " << syncactivity << syncnagleretry;
        //            //<< synccreate.size();
        //syncops = false;

        //// FIXME: only syncup for subtrees that were actually
        //// updated to reduce CPU load
        //bool repeatsyncup = false;
        //bool syncupdone = false;
        //        for (Sync* sync : syncs)
        //{
        //    if ((sync->state == SYNC_ACTIVE || sync->state == SYNC_INITIALSCAN)
        //     && !syncadding && (sync->localroot->syncAgain != TREE_RESOLVED) && !syncnagleretry)
        //    {
        //        LOG_debug << "Running syncup on demand";
        //        size_t numPending = 0;
        //        repeatsyncup |= !(syncup(sync->localroot.get(), &nds, numPending, true) && numPending == 0);
        //        syncupdone = true;
        //        sync->cachenodes();
        //          }
        //}
        //if (!syncupdone || repeatsyncup)
        //{
        //    setAllSyncsNeedFullSync();
        //}

        //if (EVER(nds))
        //{
        //    if (!syncnagleretry || (nds - Waiter::ds) < syncnaglebt.backoffdelta())
        //    {
        //        syncnaglebt.backoff(nds - Waiter::ds);
        //    }

        //    syncnagleretry = true;
        //    setAllSyncsNeedFullSyncup();
        //}

        // delete files that were overwritten by folders in syncup()
        //                            execsyncdeletions();

        //if (synccreate.size())
        //{
        //    syncupdate();
        //}

        //unsigned totalnodes = 0;

        // we have no sync-related operations pending - trigger processing if at least one
        // filesystem item is notified or initiate a full rescan if there has been
        // an event notification failure (or event notification is unavailable)
        //bool scanfailed = false;
        //bool noneSkipped = true;
        //for (Sync* sync : syncs)
        //{
        //    totalnodes += sync->localnodes[FILENODE] + sync->localnodes[FOLDERNODE];

        //    if (sync->state == SYNC_ACTIVE || sync->state == SYNC_INITIALSCAN)
        //    {
        //        if (sync->dirnotify->notifyq[DirNotify::DIREVENTS].size()
        //         || sync->dirnotify->notifyq[DirNotify::RETRY].size())
        //        {
        //            noneSkipped = false;
        //        }
        //        else
        //        {
        //            if (sync->fullscan)
        //            {
        //                // recursively delete all LocalNodes that were deleted (not moved or renamed!)
        //                sync->deletemissing(sync->localroot.get());
        //                sync->cachenodes();
        //            }

        //            // if the directory events notification subsystem is permanently unavailable or
        //            // has signaled a temporary error, initiate a full rescan
        //            if (sync->state == SYNC_ACTIVE)
        //            {
        //                sync->fullscan = false;

        //                string failedReason;
        //                auto failed = sync->dirnotify->getFailed(failedReason);

        //                if (syncscanbt.armed()
        //                        && (failed || fsaccess->notifyfailed
        //                            || sync->dirnotify->mErrorCount.load() || fsaccess->notifyerr))
        //                {
        //                    LOG_warn << "Sync scan failed " << failed
        //                             << " " << fsaccess->notifyfailed
        //                             << " " << sync->dirnotify->mErrorCount.load()
        //                             << " " << fsaccess->notifyerr;
        //                    if (failed)
        //                    {
        //                        LOG_warn << "The cause was: " << failedReason;
        //                    }
        //                    scanfailed = true;

        //                    sync->scan(&sync->localroot->localname, NULL);
        //                    sync->dirnotify->mErrorCount = 0;
        //                    sync->fullscan = true;
        //                    sync->scanseqno++;
        //                }
        //            }
        //        }
        //    }
        //}

        //if (scanfailed)
        //{
        //    fsaccess->notifyerr = false;
        //    dstime backoff = 300 + totalnodes / 128;
        //    syncscanbt.backoff(backoff);
        //    syncscanfailed = true;
        //    LOG_warn << "Next full scan in " << backoff << " ds";
        //}

        // clear pending global notification error flag if all syncs were marked
        // to be rescanned
        //if (fsaccess->notifyerr && noneSkipped)
        //{
        //    fsaccess->notifyerr = false;
        //}

        //execsyncdeletions();
        //            }
        //        }
        //    }
        //}

        //}
        //else
        //{

        //notifypurge();

        // sync timer: retry syncdown() ops in case of local filesystem lock clashes
        //if (syncdownretry && syncdownbt.armed())
        //{
        //    syncdownretry = false;
        //    setAllSyncsNeedFullSyncdown();
        //}

        stopCancelledFailedDisabled();

        forEachRunningSync(true, [&](Sync* sync) {
            sync->procextraq();
            sync->procscanq();
            });

        processTriggerHandles();


        //LOG_debug << clientname << " syncing: " << isAnySyncSyncing() << " apCurrent: " << actionpacketsCurrent;

        // We must have actionpacketsCurrent so that any LocalNode created can straight away indicate if it matched a Node

        bool tooSoon = syncStallState && (mClient.waiter->ds < mSyncFlags->recursiveSyncLastCompletedDs + 10) && (mClient.waiter->ds > mSyncFlags->recursiveSyncLastCompletedDs);

        if (mClient.actionpacketsCurrent && (isAnySyncSyncing(true) || syncStallState) && !tooSoon)
        {
            CodeCounter::ScopeTimer rst(mClient.performanceStats.recursiveSyncTime);

            // we need one pass with recursiveSync() after scanning is complete, to be sure there are no moves left.
            auto scanningCompletePreviously = mSyncFlags->scanningWasComplete && !mSyncFlags->isInitialPass;
            mSyncFlags->scanningWasComplete = !isAnySyncScanning(false);   // paused syncs do not participate in move detection
            mSyncFlags->reachableNodesAllScannedLastPass = mSyncFlags->reachableNodesAllScannedThisPass && !mSyncFlags->isInitialPass;
            mSyncFlags->reachableNodesAllScannedThisPass = true;
            mSyncFlags->movesWereComplete = scanningCompletePreviously && !mightAnySyncsHaveMoves(false); // paused syncs do not participate in move detection
            mSyncFlags->noProgress = true;
            mSyncFlags->stalledNodePaths.clear();
            mSyncFlags->stalledLocalPaths.clear();

            forEachRunningSync(true, [&](Sync* sync) {

                if (sync->state == SYNC_ACTIVE || sync->state == SYNC_INITIALSCAN)
                {

                    if (sync->dirnotify->mErrorCount.load())
                    {
                        LOG_err << "Sync " << toHandle(sync->getConfig().getBackupId()) << " had a filesystem notification buffer overflow.  Triggering full scan.";
                        sync->dirnotify->mErrorCount.store(0);
                        sync->localroot->setScanAgain(false, true, true, 5);
                    }

                    string failReason;
                    if (sync->dirnotify->getFailed(failReason))
                    {
                        if (sync->syncscanbt.armed())
                        {
                            LOG_warn << "Sync " << toHandle(sync->getConfig().getBackupId()) <<  " notifications failed or were not available (reason: " << failReason << " and it's time for another full scan";
                            auto totalnodes = sync->localnodes[FILENODE] + sync->localnodes[FOLDERNODE];
                            dstime backoff = 300 + totalnodes / 128;
                            sync->syncscanbt.backoff(backoff);
                            LOG_warn << "Sync " << toHandle(sync->getConfig().getBackupId()) << " next full scan in " << backoff << " ds";
                        }
                    }

                    if (!sync->syncPaused)
                    {
                        // make sure we don't have a LocalNode for the debris folder (delete if we have added one historically)
                        if (LocalNode* debrisNode = sync->localroot->childbyname(&sync->localdebrisname))
                        {
                            delete debrisNode; // cleans up its own entries in parent maps
                        }

                        // pathBuffer will have leafnames appended as we recurse
                        SyncPath pathBuffer(*this, sync->localroot->localname, sync->cloudRoot()->displaypath());

                        FSNode rootFsNode(sync->localroot->getLastSyncedFSDetails());
                        CloudNode rootCloudNode(*sync->cloudRoot());
                        syncRow row{&rootCloudNode, sync->localroot.get(), &rootFsNode};

                        //bool allNodesSynced =
                        sync->recursiveSync(row, pathBuffer, false, false, 0);

                        //{
                        //    // a local filesystem item was locked - schedule periodic retry
                        //    // and force a full rescan afterwards as the local item may
                        //    // be subject to changes that are notified with obsolete paths
                        //    success = false;
                        //    sync->dirnotify->mErrorCount = true;
                        //}
                        sync->cachenodes();

                        bool doneScanning = sync->localroot->scanAgain == TREE_RESOLVED;
                        if (doneScanning && sync->state == SYNC_INITIALSCAN)
                        {
                            sync->changestate(SYNC_ACTIVE, NO_SYNC_ERROR, true, true);
                        }

                        //if (allNodesSynced && sync->isBackupAndMirroring())
                        //{
                        //    sync->setBackupMonitoring();
                        //}

                        if (sync->isBackupAndMirroring() &&
                            !sync->localroot->scanRequired() &&
                            !sync->localroot->mightHaveMoves() &&
                            !sync->localroot->syncRequired())

                        {
                            sync->setBackupMonitoring();
                        }
                    }
                }
                });

            mSyncFlags->isInitialPass = false;

#ifdef MEGA_MEASURE_CODE
            LOG_verbose << "recursiveSync took ms: " << std::chrono::duration_cast<std::chrono::milliseconds>(rst.timeSpent()).count();
            rst.complete();
#endif
            mSyncFlags->recursiveSyncLastCompletedDs = mClient.waiter->ds;

            if (mSyncFlags->noProgress)
            {
                ++mSyncFlags->noProgressCount;
            }

            bool conflictsNow = conflictsDetected();
            if (conflictsNow != syncConflictState)
            {
                mClient.app->syncupdate_conflicts(conflictsNow);
                syncConflictState = conflictsNow;
                LOG_info << mClient.clientname << "Sync conflicting paths state app notified: " << conflictsNow;
            }

            bool stalled = !mSyncFlags->stalledNodePaths.empty() ||
                !mSyncFlags->stalledLocalPaths.empty();
            if (stalled)
            {
                LOG_warn << mClient.clientname << "Stall detected!";
                for (auto& p : mSyncFlags->stalledNodePaths) LOG_warn << "stalled node path: " << p.first;
                for (auto& p : mSyncFlags->stalledLocalPaths) LOG_warn << "stalled local path: " << p.first.toPath(*mClient.fsaccess);
            }

            if (stalled != syncStallState)
            {
                mClient.app->syncupdate_stalled(stalled);
                syncStallState = stalled;
                LOG_warn << mClient.clientname << "Stall state app notified: " << stalled;
            }

            // best done on main thread
            //execsyncdeletions();
        }

        bool anySyncScanning = isAnySyncScanning(false);
        if (anySyncScanning != syncscanstate)
        {
            clientThreadActions.pushBack([anySyncScanning](MegaClient& mc, DBTableTransactionCommitter& committer)
                {
                    mc.app->syncupdate_scanning(anySyncScanning);
                });
            syncscanstate = anySyncScanning;
        }

        bool anySyncBusy = isAnySyncSyncing(false);
        if (anySyncBusy != syncBusyState)
        {
            clientThreadActions.pushBack([anySyncBusy](MegaClient& mc, DBTableTransactionCommitter& committer)
                {
                    mc.app->syncupdate_syncing(anySyncBusy);
                });
            syncBusyState = anySyncBusy;
        }

// todo: moved from notifypurge()
// #ifdef ENABLE_SYNC
//        // update LocalNode <-> Node associations
//        syncs.forEachRunningSync(true, [&](Sync* sync) {
//            sync->cachenodes();
//            });
//#endif



// todo: moved from notifypurge()
//#ifdef ENABLE_SYNC
//
////update sync root node location and trigger failing cases
//        handle rubbishHandle = rootnodes[RUBBISHNODE - ROOTNODE];
//        // check for renamed/moved sync root folders
//        syncs.forEachUnifiedSync([&](UnifiedSync& us){
//
//            Node* n = nodeByHandle(us.mConfig.getRemoteNode());
//            if (n && (n->changed.attrs || n->changed.parent || n->changed.removed))
//            {
//                bool removed = n->changed.removed;
//
//                // update path in sync configuration
//                bool pathChanged = us.updateSyncRemoteLocation(removed ? nullptr : n, false);
//
//                auto &activeSync = us.mSync;
//                if (!activeSync) // no active sync (already failed)
//                {
//                    return;
//                }
//
//                // fail sync if required
//                if(n->changed.parent) //moved
//                {
//                    assert(pathChanged);
//                    // check if moved to rubbish
//                    auto p = n->parent;
//                    bool alreadyFailed = false;
//                    while (p)
//                    {
//                        if (p->nodehandle == rubbishHandle)
//                        {
//                            failSync(activeSync.get(), REMOTE_NODE_MOVED_TO_RUBBISH);
//                            alreadyFailed = true;
//                            break;
//                        }
//                        p = p->parent;
//                    }
//
//                    if (!alreadyFailed)
//                    {
//                        failSync(activeSync.get(), REMOTE_PATH_HAS_CHANGED);
//                    }
//                }
//                else if (removed)
//                {
//                    failSync(activeSync.get(), REMOTE_NODE_NOT_FOUND);
//                }
//                else if (pathChanged)
//                {
//                    failSync(activeSync.get(), REMOTE_PATH_HAS_CHANGED);
//                }
//            }
//            });
//#endif


        // Flush changes made to internal configs.
        syncConfigStoreFlush();

    }
}

bool Syncs::nodeIsInActiveSync(Node* n, bool includePausedSyncs)
{
    bool found = false;
    if (n)
    {
        forEachRunningSync(includePausedSyncs, [&](Sync* sync) {

            if (sync->active() &&
                n->isbelow(sync->cloudRoot()))
            {
                found = true;
            }
            });
    }
    return found;
}

bool Syncs::isAnySyncSyncing(bool includePausedSyncs)
{
    bool found = false;
    forEachRunningSync(includePausedSyncs, [&](Sync* sync) {

        if (sync->active() &&
            (sync->localroot->scanRequired()
                || sync->localroot->mightHaveMoves()
                || sync->localroot->syncRequired()))
        {
            found = true;
        }
        });
    return found;
}

bool Syncs::isAnySyncScanning(bool includePausedSyncs)
{
    assert(onSyncThread());
    bool found = false;
    forEachRunningSync(includePausedSyncs, [&](Sync* sync) {

        if (sync->active() &&
            sync->localroot->scanRequired())
        {
            found = true;
        }
        });
    return found;
}


bool Syncs::mightAnySyncsHaveMoves(bool includePausedSyncs)
{
    assert(onSyncThread());
    bool found = false;
    forEachRunningSync(includePausedSyncs, [&](Sync* sync) {

        if (sync->active() &&
            (sync->localroot->mightHaveMoves()
                || sync->localroot->scanRequired()))
        {
            found = true;
        }
        });
    return found;
}

bool Syncs::conflictsDetected(list<NameConflict>& conflicts) const
{
    forEachRunningSync(true, [&](Sync* sync) {
        sync->recursiveCollectNameConflicts(conflicts);
        });

    return !conflicts.empty();
}

bool Syncs::conflictsDetected() const
{
    bool found = false;
    forEachRunningSync(true, [&](Sync* sync) {

        if (sync->localroot->conflictsDetected())
        {
            found = true;
        }
        });

    return found;
}

bool Syncs::syncStallDetected(SyncFlags::CloudStallInfoMap& snp, SyncFlags::LocalStallInfoMap& slp) const
{
    bool stalled = !mSyncFlags->stalledNodePaths.empty() ||
        !mSyncFlags->stalledLocalPaths.empty();

    if (stalled)
    {
        snp = mSyncFlags->stalledNodePaths;
        slp = mSyncFlags->stalledLocalPaths;
        return true;
    }
    return false;
}

void Syncs::setAllSyncsNeedFullSync()
{
    assert(onSyncThread());
    forEachRunningSync(true, [&](Sync* sync) {
        sync->localroot->syncAgain = TREE_ACTION_SUBTREE;
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

void Syncs::removeCaches(bool keepSyncsConfigFile)
{
    assert(!onSyncThread());

    std::promise<bool> synchronous;
    syncThreadActions.pushBack([&]()
        {
            assert(onSyncThread());

            // remove the LocalNode cache databases first, otherwise disable would cause this to be skipped
            forEachRunningSync(true, [](Sync* sync)
                {
                    if (sync->statecachetable)
                    {
                        sync->statecachetable->remove();
                        sync->statecachetable.reset();
                    }
                });

            if (keepSyncsConfigFile)
            {
                // Special case backward compatibility for MEGAsync
                // The syncs will be disabled, if the user logs back in they can then manually re-enable.
                disableSelectedSyncs_inThread(
                    [&](SyncConfig& config, Sync*){return config.getEnabled();},
                    LOGGED_OUT, false, nullptr);
            }
            else
            {
                purgeSyncs();
            }
            synchronous.set_value(true);
        });

    synchronous.get_future().get();
}

bool Syncs::lookupCloudNode(NodeHandle h, CloudNode& cn, string* cloudPath)
{
    // we have to avoid doing these lookups when the client thread might be changing the Node tree
    // so we use the mutex to prevent access during that time - which is only actionpacket processing.
    lock_guard<mutex> g(mClient.nodeTreeMutex);
    if (Node* n = mClient.nodeByHandle(h))
    {
        if (cloudPath) *cloudPath = n->displaypath();
        cn = CloudNode(*n);
        return true;
    }
    return false;
}

bool Syncs::lookupCloudChildren(NodeHandle h, vector<CloudNode>& cloudChildren)
{
    // we have to avoid doing these lookups when the client thread might be changing the Node tree
    // so we use the mutex to prevent access during that time - which is only actionpacket processing.
    lock_guard<mutex> g(mClient.nodeTreeMutex);
    if (Node* n = mClient.nodeByHandle(h))
    {
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



} // namespace

#endif
