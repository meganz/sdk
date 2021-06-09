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

// set gLogsync true for very, very detailed sync logging
bool gLogsync = true;
//bool gLogsync = false;
#define SYNC_verbose if (gLogsync) LOG_verbose

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

auto ScanService::queueScan(const LocalNode& target, LocalPath targetPath) -> RequestPtr
{
    // For convenience.
    const auto& debris = target.sync->localdebris;

    // Create a request to represent the scan.
    auto request = std::make_shared<ScanRequest>(mCookie, target, targetPath);

    // Have we been asked to scan the debris?
    request->mComplete = debris.isContainingPathOf(targetPath);

    // Don't bother scanning the debris.
    if (!request->mComplete)
    {
        // Queue request for processing.
        mWorker->queue(request);
    }

    return request;
}

auto ScanService::queueScan(const LocalNode& target) -> RequestPtr
{
    return queueScan(target, target.getLocalPath());
}

ScanService::ScanRequest::ScanRequest(const std::shared_ptr<Cookie>& cookie,
                                      const LocalNode& target,
                                      LocalPath targetPath)
  : mCookie(cookie)
  , mComplete(false)
  , mDebrisPath(target.sync->localdebris)
  , mFollowSymLinks(target.sync->client->followsymlinks)
  , mKnown()
  , mResults()
//  , mTarget(target)
  , mTargetPath(std::move(targetPath))
{
    // Track details about mTarget's current children.
    for (auto& childIt : target.children)
    {
        LocalNode& child = *childIt.second;

        if (child.fsid != UNDEF)
        {
            mKnown.emplace(child.localname, child.getKnownFSDetails());
        }
    }
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
CodeCounter::ScopeStats ScanService::computeSyncTripletsTime = { "folderScan" };

void ScanService::Worker::scan(ScanRequestPtr request)
{
    CodeCounter::ScopeTimer rst(computeSyncTripletsTime);

    // For convenience.
    const auto& debris = request->mDebrisPath;

    // Don't bother processing the debris directory.
    if (debris.isContainingPathOf(request->mTargetPath))
    {
        LOG_debug << "Skipping scan of debris directory.";
        return;
    }

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

        // Except the debris...
        if (debris.isContainingPathOf(path))
        {
            continue;
        }

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
    return localPath.toPath(*client->fsaccess);
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
        localPath.appendWithSeparator(LocalPath::fromName(row.cloudNode->displayname(), *client->fsaccess, filesystemType), true);
    }
    else if (!row.cloudClashingNames.empty() || !row.fsClashingNames.empty())
    {
        // so as not to mislead in logs etc
        localPath.appendWithSeparator(LocalPath::fromName("<<<clashing>>>", *client->fsaccess, filesystemType), true);
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
        cloudPath += row.cloudNode->displayname();
    }
    else if (row.syncNode)
    {
        cloudPath += row.syncNode->name;
    }
    else if (row.fsNode)
    {
        cloudPath += row.fsNode->localname.toName(*client->fsaccess);
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
        syncPath += row.cloudNode->displayname();
    }
    else if (row.syncNode)
    {
        syncPath += row.syncNode->name;
    }
    else if (row.fsNode)
    {
        syncPath += row.fsNode->localname.toName(*client->fsaccess);
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
Sync::Sync(UnifiedSync& us, const char* cdebris,
           LocalPath* clocaldebris, Node* remotenode, bool cinshare)
: localroot(new LocalNode)
, mUnifiedSync(us)
, syncscanbt(us.mClient.rng)
{
    isnetwork = false;
    client = &mUnifiedSync.mClient;
    inshare = cinshare;
    tmpfa = NULL;
    syncname = client->clientname; // can be updated to be more specific in logs
    //initializing = true;

    localnodes[FILENODE] = 0;
    localnodes[FOLDERNODE] = 0;

    state = SYNC_INITIALSCAN;
    statecachetable = NULL;

    //fullscan = true;
    //scanseqno = 0;

    mLocalPath = mUnifiedSync.mConfig.getLocalPath();

    if (mUnifiedSync.mConfig.isBackup())
    {
        mUnifiedSync.mConfig.setBackupState(SYNC_BACKUP_MIRROR);
    }

    mFilesystemType = client->fsaccess->getlocalfstype(mLocalPath);

    localroot->init(this, FOLDERNODE, NULL, mLocalPath, nullptr);  // the root node must have the absolute path.  We don't store shortname, to avoid accidentally using relative paths.
    localroot->setSyncedNodeHandle(remotenode->nodeHandle(), remotenode->displayname());
    localroot->setScanAgain(false, true, true, 0);
    localroot->setCheckMovesAgain(false, true, true);
    localroot->setSyncAgain(false, true, true);

    if (cdebris)
    {
        debris = cdebris;
        localdebrisname = LocalPath::fromPath(debris, *client->fsaccess);

        dirnotify.reset(client->fsaccess->newdirnotify(*localroot, mLocalPath, client->waiter));

        localdebris = localdebrisname;
        localdebris.prependWithSeparator(mLocalPath);
    }
    else
    {
        localdebrisname = clocaldebris->leafName();
        localdebris = *clocaldebris;

        // FIXME: pass last segment of localdebris
        dirnotify.reset(client->fsaccess->newdirnotify(*localroot, mLocalPath, client->waiter));
    }

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
    if (client->dbaccess && !us.mConfig.isExternal())
    {
        // open state cache table
        handle tableid[3];
        string dbname;

        auto fas = client->fsaccess->newfileaccess(false);

        if (fas->fopen(mLocalPath, true, false))
        {
            tableid[0] = fas->fsid;
            tableid[1] = remotenode->nodehandle;
            tableid[2] = client->me;

            dbname.resize(sizeof tableid * 4 / 3 + 3);
            dbname.resize(Base64::btoa((byte*)tableid, sizeof tableid, (char*)dbname.c_str()));

            statecachetable = client->dbaccess->open(client->rng, *client->fsaccess, dbname);

            readstatecache();
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
    delete statecachetable;

    client->syncactivity = true;

    {
        // Create a committer and recursively delete all the associated LocalNodes, and their associated transfer and file objects.
        // If any have transactions in progress, the committer will ensure we update the transfer database in an efficient single commit.
        DBTableTransactionCommitter committer(client->tctable);
        localroot.reset();
    }
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

    assert(client);

    client->syncs.saveSyncConfig(config);
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

bool Sync::paused() const
{
    return state == SYNC_DISABLED && getConfig().getError() == NO_SYNC_ERROR && !getConfig().getEnabled();
}

bool Sync::purgeable() const
{
    switch (state)
    {
    case SYNC_CANCELED:
    case SYNC_FAILED:
        return true;
    case SYNC_DISABLED:
        return !paused();
    default:
        break;
    }

    return false;
}

Node* Sync::cloudRoot()
{
    return client->nodeByHandle(localroot->syncedCloudNodeHandle);
}

void Sync::addstatecachechildren(uint32_t parent_dbid, idlocalnode_map* tmap, LocalPath& localpath, LocalNode *p, int maxdepth)
{
    auto range = tmap->equal_range(parent_dbid);

    for (auto it = range.first; it != range.second; it++)
    {
        ScopedLengthRestore restoreLen(localpath);

        localpath.appendWithSeparator(it->second->localname, true);

        LocalNode* l = it->second;
        handle fsid = l->fsid;
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
            shortname = client->fsaccess->fsShortname(localpath);
        }

        l->init(this, l->type, p, localpath, std::move(shortname));

#ifdef DEBUG
        if (fsid != UNDEF)
        {
            auto fa = client->fsaccess->newfileaccess(false);
            if (fa->fopen(localpath))  // exists, is file
            {
                auto sn = client->fsaccess->fsShortname(localpath);
                if (!(!l->localname.empty() &&
                    ((!l->slocalname && (!sn || l->localname == *sn)) ||
                    (l->slocalname && sn && !l->slocalname->empty() && *l->slocalname != l->localname && *l->slocalname == *sn))))
                {
                    // This can happen if a file was moved elsewhere and moved back before the sync restarts.
                    // We'll refresh slocalname while scanning.
                    LOG_warn << "Shortname mismatch on LocalNode load!" <<
                        " Was: " << (l->slocalname ? l->slocalname->toPath(*client->fsaccess) : "(null") <<
                        " Now: " << (sn ? sn->toPath(*client->fsaccess) : "(null") <<
                        " at " << localpath.toPath(*client->fsaccess);
                }
            }
        }
#endif

        l->parent_dbid = parent_dbid;
        l->syncedFingerprint.size = size;
        l->setfsid(fsid, client->localnodeByFsid, l->localname);
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
        while (statecachetable->next(&cid, &cachedata, &client->key))
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
                    statecachetable->put(MegaClient::CACHEDLOCALNODE, *it, &client->key);
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
                mUnifiedSync.mClient.app->syncupdate_active(config.getBackupId(), nowActive);
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
    MegaClient* client;
    SyncFlags& sf;
    ProgressingMonitor(MegaClient* c) : client(c), sf(*client->mSyncFlags) {}

    bool isContainingNodePath(const string& a, const string& b)
    {
        return a.size() <= b.size() &&
            !memcmp(a.c_str(), b.c_str(), a.size()) &&
            (a.size() == b.size() || b[a.size()] == '/');
    }

    void waitingCloud(const string& cloudPath, SyncWaitReason r)
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
            sf.stalledNodePaths[cloudPath] = r;
        }
    }

    void waitingLocal(const LocalPath& p, SyncWaitReason r)
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
                if (i->first.isContainingPathOf(p))
                {
                    // we already have a parent or ancestor listed
                    return;
                }
                else if (p.isContainingPathOf(i->first))
                {
                    // this new path is the parent or ancestor
                    i = sf.stalledLocalPaths.erase(i);
                }
                else ++i;
            }
            sf.stalledLocalPaths[p] = r;
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


bool Sync::checkLocalPathForMovesRenames(syncRow& row, syncRow& parentRow, SyncPath& fullPath, bool& rowResult)
{
    // rename or move of existing node?
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

        // was the file overwritten by moving an existing file over it?
        if (LocalNode* sourceLocalNode = client->findLocalNodeByFsid(row.fsNode->fsid, row.fsNode->type, row.fsNode->fingerprint, *this, nullptr))
        {
            assert(parentRow.syncNode);
            ProgressingMonitor monitor(client);

            if (!row.syncNode)
            {
                resolve_makeSyncNode_fromFS(row, parentRow, fullPath, false);
                assert(row.syncNode);
            }

            row.syncNode->setCheckMovesAgain(true, false, false);

            // logic to detect files being updated in the local computer moving the original file
            // to another location as a temporary backup
            if (sourceLocalNode->type == FILENODE &&
                client->checkIfFileIsChanging(*row.fsNode, sourceLocalNode->getLocalPath()))
            {
                // if we revist here and the file is still the same after enough time, we'll move it
                rowResult = false;
                return true;
            }

            if (!sourceLocalNode->moveSourceApplyingToCloud)
            {
                assert(!sourceLocalNode->moveTargetApplyingToCloud);

                LOG_debug << syncname << "Move detected by fsid. Type: " << sourceLocalNode->type
                    << " new path: " << fullPath.localPath_utf8()
                    << " old localnode: " << sourceLocalNode->localnodedisplaypath(*client->fsaccess)
                    << logTriplet(row, fullPath);
                sourceLocalNode->moveSourceApplyingToCloud = true;
                row.syncNode->moveTargetApplyingToCloud = true;
                //sourceLocalNode->setScanAgain(true, false, false);
            }
            row.suppressRecursion = true;   // wait until we have moved the other LocalNodes below this one

            // is it a move within the same folder?  (ie, purely a rename?)
            syncRow* sourceRow = nullptr;
            if (sourceLocalNode->parent == row.syncNode->parent
                && row.rowSiblings
                && !sourceLocalNode->moveSourceAppliedToCloud)
            {
                // then prevent any other action on this node for now
                // if there is a new matching fs name, we'll need this node to be renamed, then a new one will be created
                for (syncRow& r : *row.rowSiblings)
                {
                    // skip the syncItem() call for this one this time (so no upload until move is resolved)
                    if (r.syncNode == sourceLocalNode) sourceRow = &r;
                }
            }

            // we don't want the source LocalNode to be visited until the move completes
            // because it might see a new file with the same name, and start an
            // upload attached to that LocalNode (which would create a wrong version chain in the account)
            // TODO: consider alternative of preventing version on upload completion - probably resulting in much more complicated name matching though
            if (sourceRow) sourceRow->itemProcessed = true;

            // Although we have detected a move locally, there's no guarantee the cloud side
            // is complete, the corresponding parent folders in the cloud may not match yet.
            // ie, either of sourceCloudNode or targetCloudNode could be null here.
            // So, we have a heirarchy of statuses:
            //    If any scan flags anywhere are set then we can't be sure a missing fs node isn't a move
            //    After all scanning is done, we need one clean tree traversal with no moves detected
            //    before we can be sure we can remove nodes or upload/download.
            Node* sourceCloudNode = client->nodeByHandle(sourceLocalNode->syncedCloudNodeHandle);
            Node* targetCloudNode = client->nodeByHandle(parentRow.syncNode->syncedCloudNodeHandle);

            if (sourceCloudNode && !sourceCloudNode->mPendingChanges.empty())
            {
                // come back again later when there isn't already a command in progress
                SYNC_verbose << syncname << "Actions are already in progress for " << sourceCloudNode->displaypath() << logTriplet(row, fullPath);
                row.syncNode->setSyncAgain(true, false, false);
                rowResult = false;
                return true;
            }

            if (sourceCloudNode && targetCloudNode)
            {
                string newName = row.fsNode->localname.toName(*client->fsaccess);
                if (newName == sourceCloudNode->displayname() ||
                    sourceLocalNode->localname == row.fsNode->localname)
                {
                    // if it wasn't renamed locally, or matches the target anyway
                    // then don't change the name
                    newName.clear();
                }

                if (sourceCloudNode->parent == targetCloudNode && newName.empty())
                {
                    LOG_debug << syncname << "Move/rename has completed: " << sourceCloudNode->displaypath() << logTriplet(row, fullPath);

                    // Now that the move has completed, it's ok (and necessary) to resume visiting the source node
                    if (sourceRow) sourceRow->itemProcessed = true;

                    // remove fsid (and handle) from source node, so we don't detect
                    // that as a move source anymore
                    sourceLocalNode->setfsid(UNDEF, client->localnodeByFsid, sourceLocalNode->localname);
                    sourceLocalNode->setSyncedNodeHandle(NodeHandle(), sourceLocalNode->name);

                    // Move all the LocalNodes under the source node to the new location
                    // We can't move the source node itself as the recursive callers may be using it

                    sourceLocalNode->moveContentTo(row.syncNode, fullPath.localPath, true);

                    // Mark the source node as moved from, it can be removed when visited
                    sourceLocalNode->moveSourceAppliedToCloud = true;
                    sourceLocalNode->moveSourceApplyingToCloud = false;
                    row.syncNode->moveTargetApplyingToCloud = false;

                    row.syncNode->setScanAgain(true, true, true, 0);
                    sourceLocalNode->setScanAgain(true, false, false, 0);

                    // Check for filename anomalies.
                    if (row.syncNode->fsid == UNDEF)
                    {
                        auto type = isFilenameAnomaly(fullPath.localPath, sourceCloudNode);

                        if (type != FILENAME_ANOMALY_NONE)
                        {
                            auto local  = fullPath.localPath_utf8();
                            auto remote = sourceCloudNode->displaypath();

                            client->filenameAnomalyDetected(type, local, remote);
                        }
                    }

                    rowResult = false; // one more future visit to double check everything
                    return true; // job done for this node for now
                }

                if (row.cloudNode && row.cloudNode != sourceCloudNode)
                {
                    LOG_debug << syncname << "Moving node to debris for replacement: " << row.cloudNode->displaypath() << logTriplet(row, fullPath);;
                    client->movetosyncdebris(row.cloudNode, false);
                    client->execsyncdeletions();
                }

                if (sourceCloudNode->parent == targetCloudNode && !newName.empty())
                {
                    LOG_debug << syncname
                              << "Renaming node: " << sourceCloudNode->displaypath()
                              << " to " << newName  << logTriplet(row, fullPath);
                    client->setattr(sourceCloudNode, attr_map('n', newName), 0);
                    client->app->syncupdate_local_move(this, sourceLocalNode->getLocalPath(), fullPath.localPath);
                    rowResult = false;
                    return true;
                }
                else
                {
                    LOG_debug << syncname << "Moving node: " << sourceCloudNode->displaypath()
                              << " to " << targetCloudNode->displaypath()
                              << (newName.empty() ? "" : (" as " + newName).c_str()) << logTriplet(row, fullPath);
                    auto err = client->rename(sourceCloudNode, targetCloudNode,
                                            SYNCDEL_NONE,
                                            sourceCloudNode->parent ? sourceCloudNode->parent->nodehandle : UNDEF,
                                            newName.empty() ? nullptr : newName.c_str());

                    if (err == API_EACCESS)
                    {
                        LOG_warn << syncname << "Rename not permitted: " << sourceCloudNode->displaypath()
                            << " to " << targetCloudNode->displaypath()
                            << (newName.empty() ? "" : (" as " + newName).c_str()) << logTriplet(row, fullPath);

                        // todo: figure out if the problem could be overcome by copying and later deleting the source
                        // but for now, mark the sync as disabled
                        // todo: work out the right sync error code
                        changestate(SYNC_FAILED, COULD_NOT_MOVE_CLOUD_NODES, false, true);
                    }
                    else
                    {
                        // command sent, now we wait for the actinpacket updates, later we will recognise
                        // the row as synced from fsNode, cloudNode and update the syncNode from those
                        client->app->syncupdate_local_move(this, sourceLocalNode->getLocalPath(), fullPath.localPath);

                        assert(sourceLocalNode->moveSourceApplyingToCloud);
                        assert(row.syncNode->moveTargetApplyingToCloud);

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
                if (!sourceCloudNode) SYNC_verbose << syncname << "Source parent cloud node doesn't exist yet" << logTriplet(row, fullPath);
                if (!targetCloudNode) SYNC_verbose << syncname << "Target parent cloud node doesn't exist yet" << logTriplet(row, fullPath);

                monitor.waitingLocal(sourceLocalNode->getLocalPath(), SyncWaitReason::MoveNeedsTargetFolder);
                monitor.waitingLocal(fullPath.localPath, SyncWaitReason::MoveNeedsTargetFolder);

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
 handle debug_getfsid(const LocalPath& p, FileSystemAccess* fsa)
 {
    auto fa = fsa->newfileaccess();
    LocalPath lp = p;
    if (fa->fopen(lp, true, false, nullptr, false)) return fa->fsid;
    else return UNDEF;
 }
 #endif

bool Sync::checkCloudPathForMovesRenames(syncRow& row, syncRow& parentRow, SyncPath& fullPath, bool& rowResult)
{
    if (row.syncNode && row.syncNode->type != row.cloudNode->type)
    {
        LOG_debug << syncname << "checked node does not have the same type, blocked: " << fullPath.cloudPath;
        row.syncNode->setUseBlocked();
        row.suppressRecursion = true;
        rowResult = false;
        return true;
    }
    else if (LocalNode* sourceLocalNode = client->findLocalNodeByNodeHandle(row.cloudNode->nodeHandle()))
    {
        if (sourceLocalNode == row.syncNode) return false;

        // It's a move or rename

        if (isBackup())
        {
            // Backups must not change the local
            changestate(SYNC_FAILED, BACKUP_MODIFIED, false, true);
            rowResult = false;
            return true;
        }

        ProgressingMonitor monitor(client);

        assert(parentRow.syncNode);
        if (parentRow.syncNode) parentRow.syncNode->setCheckMovesAgain(false, true, false);
        if (row.syncNode) row.syncNode->setCheckMovesAgain(true, false, false);

        sourceLocalNode->treestate(TREESTATE_SYNCING);
        if (row.syncNode) row.syncNode->treestate(TREESTATE_SYNCING);

        LocalPath sourcePath = sourceLocalNode->getLocalPath();
        Node* oldCloudParent = sourceLocalNode->parent ?
                               client->nodeByHandle(sourceLocalNode->parent->syncedCloudNodeHandle) :
                               nullptr;

        if (!sourceLocalNode->moveApplyingToLocal)
        {
            LOG_debug << syncname << "Move detected by nodehandle. Type: " << sourceLocalNode->type
                << " moved node: " << row.cloudNode->displaypath()
                << " old parent: " << oldCloudParent->displaypath()
                << logTriplet(row, fullPath);

            client->app->syncupdate_remote_move(this, row.cloudNode, oldCloudParent);
            sourceLocalNode->moveApplyingToLocal = true;
        }

        assert(!isBackup());

        // Check for filename anomalies.
        {
            auto type = isFilenameAnomaly(fullPath.localPath, row.cloudNode);

            if (type != FILENAME_ANOMALY_NONE)
            {
                auto remotePath = row.cloudNode->displaypath();
                client->filenameAnomalyDetected(type, fullPath.localPath_utf8(), remotePath);
            }
        }

        // is it a move within the same folder?  (ie, purely a rename?)
        syncRow* sourceRow = nullptr;
        if (sourceLocalNode->parent == parentRow.syncNode
            && row.rowSiblings
            && !sourceLocalNode->moveAppliedToLocal)
        {
            // then prevent any other action on this node for now
            // if there is a new matching fs name, we'll need this node to be renamed, then a new one will be created
            for (syncRow& r : *row.rowSiblings)
            {
                // skip the syncItem() call for this one this time (so no upload until move is resolved)
                if (r.syncNode == sourceLocalNode) sourceRow = &r;
            }
        }

        // we don't want the source LocalNode to be visited until after the move completes, and we revisit with rescanned folder data
        // because it might see a new file with the same name, and start a download, keeping the row instead of removing it
        if (sourceRow)
        {
            sourceRow->itemProcessed = true;
            sourceRow->syncNode->setScanAgain(true, false, false, 0);
        }

        // check filesystem is not changing fsids as a result of rename
        assert(sourceLocalNode->fsid == debug_getfsid(sourcePath, client->fsaccess));

        if (client->fsaccess->renamelocal(sourcePath, fullPath.localPath))
        {
            // todo: move anything at this path to sync debris first?  Old algo didn't though

            // check filesystem is not changing fsids as a result of rename
            assert(sourceLocalNode->fsid == debug_getfsid(fullPath.localPath, client->fsaccess));

            client->app->syncupdate_local_move(this, sourceLocalNode->getLocalPath(), fullPath.localPath);

            if (!row.syncNode)
            {
                resolve_makeSyncNode_fromCloud(row, parentRow, fullPath, false);
                assert(row.syncNode);
            }

            // remove fsid (and handle) from source node, so we don't detect
            // that as a move source anymore
            sourceLocalNode->setfsid(UNDEF, client->localnodeByFsid, sourceLocalNode->localname);
            sourceLocalNode->setSyncedNodeHandle(NodeHandle(), sourceLocalNode->name);

            sourceLocalNode->moveContentTo(row.syncNode, fullPath.localPath, true);
            sourceLocalNode->moveAppliedToLocal = true;

            sourceLocalNode->setScanAgain(true, false, false, 0);
            row.syncNode->setScanAgain(true, false, false, 0);

            rowResult = false;
            return true;
        }
        else if (client->fsaccess->transient_error)
        {
            LOG_warn << "transient error moving folder: " << sourcePath.toPath(*client->fsaccess) << logTriplet(row, fullPath);
            row.syncNode->setUseBlocked();   // todo: any interaction with checkMovesAgain ? might we stall if the transient error never resovles?
            row.suppressRecursion = true;
            sourceLocalNode->moveApplyingToLocal = false;
            rowResult = false;
            return true;
        }
        else
        {
            SYNC_verbose << "Move to here delayed since local parent doesn't exist yet: " << sourcePath.toPath(*client->fsaccess) << logTriplet(row, fullPath);
            monitor.waitingCloud(row.cloudNode->displaypath(), SyncWaitReason::MoveNeedsTargetFolder);
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
                      << notification.path.toPath(*client->fsaccess);
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
                    << notification.path.toPath(*client->fsaccess);

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
            SYNC_verbose << "Trigger scan flag by delayed notification on " << nearest->localnodedisplaypath(*client->fsaccess);
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
                      << notification.path.toPath(*client->fsaccess);
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
            SYNC_verbose << "Trigger scan flag by fs notification on " << nearest->localnodedisplaypath(*client->fsaccess);
        }
#endif

        nearest->setScanAgain(false, true, !remainder.empty(), SCANNING_DELAY_DS);

        // Queue an extra notification if we're a network sync.
        if (isnetwork)
        {
            LOG_verbose << "Queuing extra notification for: "
                        << notification.path.toPath(*client->fsaccess);

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

bool Sync::updateSyncRemoteLocation(Node* n, bool forceCallback)
{
    return mUnifiedSync.updateSyncRemoteLocation(n, forceCallback);
}

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
            client->fsaccess->mkdirlocal(localdebris, true);
        }

        sprintf(buf, "%04d-%02d-%02d", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday);

        if (i >= 0)
        {
            sprintf(strchr(buf, 0), " %02d.%02d.%02d.%02d", ptm->tm_hour,  ptm->tm_min, ptm->tm_sec, i);
        }

        day = buf;
        localdebris.appendWithSeparator(LocalPath::fromPath(day, *client->fsaccess), true);

        if (i > -3)
        {
            LOG_verbose << syncname << "Creating daily local debris folder";
            havedir = client->fsaccess->mkdirlocal(localdebris, false) || client->fsaccess->target_exists;
        }

        localdebris.appendWithSeparator(localpath.subpathFrom(localpath.getLeafnameByteIndex(*client->fsaccess)), true);

        client->fsaccess->skip_errorreport = i == -3;  // we expect a problem on the first one when the debris folders or debris day folders don't exist yet
        if (client->fsaccess->renamelocal(localpath, localdebris, false))
        {
            client->fsaccess->skip_errorreport = false;
            return true;
        }
        client->fsaccess->skip_errorreport = false;

        if (client->fsaccess->transient_error)
        {
            return false;
        }

        if (havedir && !client->fsaccess->target_exists)
        {
            return false;
        }
    }

    return false;
}

m_off_t Sync::getInflightProgress()
{
    m_off_t progressSum = 0;

    for (auto tslot : client->tslots)
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
            else if (auto sfg = dynamic_cast<SyncFileGet*>(file))
            {
                if (sfg->localNode.sync == this)
                {
                    progressSum += tslot->progressreported;
                }
            }
        }
    }

    return progressSum;
}


UnifiedSync::UnifiedSync(MegaClient& mc, const SyncConfig& c)
    : mClient(mc), mConfig(c)
{
    mNextHeartbeat.reset(new HeartBeatSyncInfo());
}


error UnifiedSync::enableSync(bool resetFingerprint, bool notifyApp)
{
    assert(!mSync);
    mConfig.mError = NO_SYNC_ERROR;

    if (resetFingerprint)
    {
        mConfig.setLocalFingerprint(0); //This will cause the local filesystem fingerprint to be recalculated
    }

    LocalPath rootpath;
    std::unique_ptr<FileAccess> openedLocalFolder;
    Node* remotenode;
    bool inshare, isnetwork;
    error e = mClient.checkSyncConfig(mConfig, rootpath, openedLocalFolder, remotenode, inshare, isnetwork);

    if (e)
    {
        // error and enable flag were already changed
        changedConfigState(notifyApp);
        return e;
    }

    e = startSync(&mClient, DEBRISFOLDER, nullptr, remotenode, inshare, isnetwork, rootpath, openedLocalFolder);
    mClient.syncactivity = true;
    changedConfigState(notifyApp);

    mClient.syncs.mHeartBeatMonitor->updateOrRegisterSync(*this);

    return e;
}

bool UnifiedSync::updateSyncRemoteLocation(Node* n, bool forceCallback)
{
    bool changed = false;
    if (n)
    {
        auto newpath = n->displaypath();
        if (newpath != mConfig.mOriginalPathOfRemoteRootNode)
        {
            mConfig.mOriginalPathOfRemoteRootNode = newpath;
            changed = true;
        }

        if (mConfig.getRemoteNode() != n->nodehandle)
        {
            mConfig.setRemoteNode(NodeHandle().set6byte(n->nodehandle));
            changed = true;
        }
    }
    else //unset remote node: failed!
    {
        if (!mConfig.getRemoteNode().isUndef())
        {
            mConfig.setRemoteNode(NodeHandle());
            changed = true;
        }
    }

    if (changed || forceCallback)
    {
        mClient.app->syncupdate_remote_root_changed(mConfig);
    }

    //persist
    mClient.syncs.saveSyncConfig(mConfig);

    return changed;
}



error UnifiedSync::startSync(MegaClient* client, const char* debris, LocalPath* localdebris, Node* remotenode, bool inshare,
                             bool isNetwork, LocalPath& rootpath, std::unique_ptr<FileAccess>& openedLocalFolder)
{
    //check we are not in any blocking situation
    using CType = CacheableStatus::Type;
    bool overStorage = client->mCachedStatus.lookup(CType::STATUS_STORAGE, STORAGE_UNKNOWN) >= STORAGE_RED;
    bool businessExpired = client->mCachedStatus.lookup(CType::STATUS_BUSINESS, BIZ_STATUS_UNKNOWN) == BIZ_STATUS_EXPIRED;
    bool blocked = client->mCachedStatus.lookup(CType::STATUS_BLOCKED, 0) == 1;

    mConfig.mError = NO_SYNC_ERROR;
    mConfig.mEnabled = true;

    // the order is important here: a user needs to resolve blocked in order to resolve storage
    if (overStorage)
    {
        mConfig.mError = STORAGE_OVERQUOTA;
        mConfig.mEnabled = false;
    }
    else if (businessExpired)
    {
        mConfig.mError = BUSINESS_EXPIRED;
        mConfig.mEnabled = false;
    }
    else if (blocked)
    {
        mConfig.mError = ACCOUNT_BLOCKED;
        mConfig.mEnabled = false;
    }

    if (mConfig.mError)
    {
        // save configuration but avoid creating active sync:
        mClient.syncs.saveSyncConfig(mConfig);
        return API_EFAILED;
    }

    auto prevFingerprint = mConfig.getLocalFingerprint();

    assert(!mSync);
    mSync.reset(new Sync(*this, debris, localdebris, remotenode, inshare));
    mConfig.setLocalFingerprint(mSync->fsfp);

    if (prevFingerprint && prevFingerprint != mConfig.getLocalFingerprint())
    {
        LOG_err << "New sync local fingerprint mismatch. Previous: " << prevFingerprint
            << "  Current: " << mConfig.getLocalFingerprint();
        mSync->changestate(SYNC_FAILED, LOCAL_FINGERPRINT_MISMATCH, false, true);
        mConfig.mError = LOCAL_FINGERPRINT_MISMATCH;
        mConfig.mEnabled = false;
        mSync.reset();
        return API_EFAILED;
    }

    mSync->isnetwork = isNetwork;

    client->syncs.saveSyncConfig(mConfig);
    return API_OK;
}

void UnifiedSync::changedConfigState(bool notifyApp)
{
    if (mConfig.errorOrEnabledChanged())
    {
        LOG_debug << "Sync " << toHandle(mConfig.mBackupId) << " enabled/error changed to " << mConfig.mEnabled << "/" << mConfig.mError;

        mClient.syncs.saveSyncConfig(mConfig);
        if (notifyApp)
        {
            mClient.app->syncupdate_stateconfig(mConfig.getBackupId());
        }
        mClient.abortbackoff(false);
    }
}

Syncs::Syncs(MegaClient& mc)
  : mClient(mc)
{
    mHeartBeatMonitor.reset(new BackupMonitor(&mClient));
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
            mSyncVec.emplace_back(new UnifiedSync(mClient, config));

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
    size_t disabled = 0;

    disableSelectedSyncs(
      [&](SyncConfig& config, Sync*)
      {
          // But only if they're not already disabled.
          if (!config.getEnabled()) return false;

          auto matched = failed.count(config.mExternalDrivePath);

          disabled += matched;

          return matched > 0;
      },
      SYNC_CONFIG_WRITE_FAILURE,
      false);

    LOG_warn << "Disabled "
             << disabled
             << " sync(s) on "
             << failed.size()
             << " drive(s).";

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
            auto state = BackupInfoSync::getSyncState(config, &client);
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
    auto authKey = store->get("ak");
    auto cipherKey = store->get("ck");
    auto name = store->get("fn");

    if (authKey.size() != KEYLENGTH
        || cipherKey.size() != KEYLENGTH
        || name.size() != KEYLENGTH)
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
}

void Syncs::resetSyncConfigStore()
{
    mSyncConfigStore.reset();
    static_cast<void>(syncConfigStore());
}

auto Syncs::appendNewSync(const SyncConfig& c, MegaClient& mc) -> UnifiedSync*
{
    isEmpty = false;
    mSyncVec.push_back(unique_ptr<UnifiedSync>(new UnifiedSync(mc, c)));

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

void Syncs::forEachRunningSync(std::function<void(Sync* s)> f) const
{
    for (auto& s : mSyncVec)
    {
        if (s->mSync)
        {
            f(s->mSync.get());
        }
    }
}

void Syncs::forEachRunningSyncContainingNode(Node* node, std::function<void(Sync* s)> f)
{
    for (auto& s : mSyncVec)
    {
        if (s->mSync)
        {
            if (s->mSync->cloudRoot() &&
                node->isbelow(s->mSync->cloudRoot()))
            {
                f(s->mSync.get());
            }
        }
    }
}

bool Syncs::forEachRunningSync_shortcircuit(std::function<bool(Sync* s)> f)
{
    for (auto& s : mSyncVec)
    {
        if (s->mSync)
        {
            if (!f(s->mSync.get()))
            {
                return false;
            }
        }
    }
    return true;
}

void Syncs::forEachSyncConfig(std::function<void(const SyncConfig&)> f)
{
    for (auto& s : mSyncVec)
    {
        f(s->mConfig);
    }
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
    bool anySyncDisabled = false;
    disableSelectedSyncs([&](SyncConfig& config, Sync*){

        if (config.getEnabled())
        {
            anySyncDisabled = true;
            return true;
        }
        return false;
    }, syncError, newEnabledFlag);

    if (anySyncDisabled)
    {
        LOG_info << "Disabled syncs. error = " << syncError;
        mClient.app->syncs_disabled(syncError);
    }
}

void Syncs::disableSelectedSyncs(std::function<bool(SyncConfig&, Sync*)> selector, SyncError syncError, bool newEnabledFlag)
{
    for (auto i = mSyncVec.size(); i--; )
    {
        if (selector(mSyncVec[i]->mConfig, mSyncVec[i]->mSync.get()))
        {
            if (auto sync = mSyncVec[i]->mSync.get())
            {
                sync->changestate(SYNC_DISABLED, syncError, newEnabledFlag, true); //This will cause the later deletion of Sync (not MegaSyncPrivate) object
                mClient.syncactivity = true;
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

            mHeartBeatMonitor->updateOrRegisterSync(*mSyncVec[i]);
        }
    }
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
                delete syncPtr->statecachetable;
                syncPtr->statecachetable = NULL;
            }
            syncPtr.reset(); // deletes sync
        }

        mSyncConfigStore->markDriveDirty(mSyncVec[index]->mConfig.mExternalDrivePath);

        // call back before actual removal (intermediate layer may need to make a temp copy to call client app)
        auto backupId = mSyncVec[index]->mConfig.getBackupId();
        mClient.app->sync_removed(backupId);

        // unregister this sync/backup from API (backup center)
        mClient.reqs.add(new CommandBackupRemove(&mClient, backupId));

        mClient.syncactivity = true;
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
                delete syncPtr->statecachetable;
                syncPtr->statecachetable = NULL;
            }
            syncPtr.reset(); // deletes sync
        }

        // the sync config is not affected by this operation; it should already be up to date on disk (or be pending)
        // we don't call sync_removed back since the sync is not deleted
        // we don't unregister from the backup/sync heartbeats as the sync can be resumed later

        mClient.syncactivity = true;
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
                return s->enableSync(resetFingerprint, true);
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

// restore all configured syncs that were in a temporary error state (not manually disabled)
void Syncs::enableResumeableSyncs()
{
    bool anySyncRestored = false;

    for (auto& unifiedSync : mSyncVec)
    {
        if (!unifiedSync->mSync)
        {
            if (unifiedSync->mConfig.getEnabled())
            {
                SyncError syncError = unifiedSync->mConfig.getError();
                LOG_debug << "Restoring sync: " << toHandle(unifiedSync->mConfig.getBackupId()) << " " << unifiedSync->mConfig.getLocalPath().toPath(*mClient.fsaccess) << " fsfp= " << unifiedSync->mConfig.getLocalFingerprint() << " old error = " << syncError;

                // We should never try to resume a backup sync.
                assert(!unifiedSync->mConfig.isBackup());

                anySyncRestored |= unifiedSync->enableSync(false, true) == API_OK;
            }
            else
            {
                LOG_verbose << "Skipping restoring sync: " << unifiedSync->mConfig.getLocalPath().toPath(*mClient.fsaccess)
                    << " enabled=" << unifiedSync->mConfig.getEnabled() << " error=" << unifiedSync->mConfig.getError();
            }
        }
    }

    if (anySyncRestored)
    {
        mClient.app->syncs_restored();
    }
}

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
        mSyncVec.push_back(unique_ptr<UnifiedSync>(new UnifiedSync(mClient, config)));
        isEmpty = false;
    }

    for (auto& unifiedSync : mSyncVec)
    {
        if (!unifiedSync->mSync)
        {
            if (unifiedSync->mConfig.mOriginalPathOfRemoteRootNode.empty()) //should only happen if coming from old cache
            {
                auto node = mClient.nodeByHandle(unifiedSync->mConfig.getRemoteNode());
                unifiedSync->updateSyncRemoteLocation(node, false); //updates cache & notice app of this change
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

                unifiedSync->enableSync(false, false);
                LOG_debug << "Sync autoresumed: " << toHandle(unifiedSync->mConfig.getBackupId()) << " " << unifiedSync->mConfig.getLocalPath().toPath(*mClient.fsaccess) << " fsfp= " << unifiedSync->mConfig.getLocalFingerprint() << " error = " << unifiedSync->mConfig.getError();

                mClient.app->sync_auto_resume_result(*unifiedSync, true, hadAnError);
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
    FSNode rootFsNode(localroot->getKnownFSDetails());
    syncRow row{ cloudRoot(), localroot.get(), &rootFsNode };
    recursiveCollectNameConflicts(row, conflicts);
    return !conflicts.empty();
}

void Sync::recursiveCollectNameConflicts(syncRow& row, list<NameConflict>& ncs)
{
    assert(row.syncNode);
    if (!row.syncNode->conflictsDetected())
    {
        return;
    }

    vector<FSNode>* effectiveFsChildren;
    vector<FSNode> fsChildren;
    {
        // Effective children are from the last scan, if present.
        effectiveFsChildren = row.syncNode->lastFolderScan.get();

        // Otherwise, we can reconstruct the filesystem entries from the LocalNodes
        if (!effectiveFsChildren)
        {
            fsChildren.reserve(row.syncNode->children.size());

            for (auto &childIt : row.syncNode->children)
            {
                if (childIt.second->fsid != UNDEF)
                {
                    fsChildren.emplace_back(childIt.second->getKnownFSDetails());
                }
            }

            effectiveFsChildren = &fsChildren;
        }
    }

    // Get sync triplets.
    auto childRows = computeSyncTriplets(row.cloudNode, *row.syncNode, *effectiveFsChildren);


    for (auto& childRow : childRows)
    {
        if (!childRow.cloudClashingNames.empty() ||
            !childRow.fsClashingNames.empty())
        {
            NameConflict nc;
            if (!childRow.cloudClashingNames.empty())
            {
                nc.cloudPath = row.cloudNode ? row.cloudNode->displaypath() : "";
                for (Node* n : childRow.cloudClashingNames)
                {
                    nc.clashingCloudNames.push_back(n->displayname());
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
            recursiveCollectNameConflicts(childRow, ncs);
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


auto Sync::computeSyncTriplets(Node* cloudParent, const LocalNode& syncParent, vector<FSNode>& fsNodes) const -> vector<syncRow>
{
    CodeCounter::ScopeTimer rst(client->performanceStats.computeSyncTripletsTime);
    //Comparator comparator(*this);
    vector<LocalNode*> localNodes;
    vector<Node*> remoteNodes;
    vector<syncRow> triplets;

    localNodes.reserve(syncParent.children.size());
    remoteNodes.reserve(cloudParent ? cloudParent->children.size() : 0);

    for (auto& child : syncParent.children)
    {
        localNodes.emplace_back(child.second);
    }

    if (cloudParent)
    {
        for (auto* child : cloudParent->children)
        {
            remoteNodes.emplace_back(child);
        }
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
                lNext = std::upper_bound(lCurr, lEnd, *lCurr, lnCompareLess);
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
                        << i->localname.toPath(*client->fsaccess);

                    triplets.back().fsClashingNames.push_back(&*i);

                    if (localNode && i->fsid != UNDEF && i->fsid == localNode->fsid)
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

    auto cloudCompareLess = [this, caseInsensitive](const Node* lhs, const Node* rhs) -> bool {
        return compareUtf(
            lhs->displayname(), false,
            rhs->displayname(), false, caseInsensitive) < 0;
    };

    auto rowCompareLess = [=](const syncRow& lhs, const syncRow& rhs) -> bool {
        return compareUtf(
            lhs.comparisonLocalname(), true,
            rhs.comparisonLocalname(), true, caseInsensitive) < 0;
    };

    auto cloudrowCompareSpaceship = [=](const Node* lhs, const syncRow& rhs) -> int {
        // todo:  add an escape-as-we-compare option?
        // consider: local d%  upload to cloud as d%, now we need to match those up for a single syncrow.
        // consider: alternamtely d% in the cloud and local d%25 should also be considered a match.
        //auto a = LocalPath::fromName(lhs->displayname(), *client->fsaccess, mFilesystemType);

        return compareUtf(
            lhs->displayname(), false,
            rhs.comparisonLocalname(), true, caseInsensitive);
    };

    std::sort(remoteNodes.begin(), remoteNodes.end(), cloudCompareLess);
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
        auto rCurr = remoteNodes.begin();
        auto rEnd = remoteNodes.end();
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

            auto* remoteNode = rCurr != rEnd ? *rCurr : nullptr;
            auto* triplet = tCurr != tEnd ? &triplets[tCurr] : nullptr;

//#ifdef DEBUG
//            auto rn = remoteNode ? remoteNode->displayname() : "";
//#endif

            // Bail as there's nothing to pair.
            if (!(remoteNode || triplet)) break;

            if (remoteNode && triplet)
            {
                const auto relationship =
                    cloudrowCompareSpaceship(remoteNode, *triplet);

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
                        << (*i)->displaypath();

                    if (triplet)
                    {
                        triplet->cloudClashingNames.push_back(*i);

                        if ((*i)->nodehandle != UNDEF &&
                            triplet->syncNode->syncedCloudNodeHandle == (*i)->nodehandle)
                        {
                            // In case of a name clash, it might be new.
                            // Do sync the subtree we were already syncing.
                            // But also complain about the clash
                            triplet->cloudNode = *i;
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

bool Sync::inferAlreadySyncedTriplets(Node* cloudParent, const LocalNode& syncParent, vector<FSNode>& inferredFsNodes, vector<syncRow>& inferredRows) const
{
    CodeCounter::ScopeTimer rst(client->performanceStats.inferSyncTripletsTime);

    inferredFsNodes.reserve(syncParent.children.size());

    for (auto& child : syncParent.children)
    {
        Node* node = client->nodeByHandle(child.second->syncedCloudNodeHandle, true);
        if (!node ||
            node->nodehandle != child.second->syncedCloudNodeHandle.as8byte() ||
            node->parent != cloudParent)
        {
            // the node tree has actually changed so we need to run the full algorithm
            return false;
        }

        if (node->parent && node->parent->type == FILENODE)
        {
            LOG_err << "Looked up a file version node during infer: " << node->displaypath();
			assert(false);
            return false;
        }

        inferredFsNodes.push_back(child.second->getKnownFSDetails());
        inferredRows.emplace_back(node, child.second, &inferredFsNodes.back());
    }
    return true;
}

bool Sync::recursiveSync(syncRow& row, SyncPath& fullPath, DBTableTransactionCommitter& committer)
{
    // in case of sync failing while we recurse
    if (state < 0) return false;

    assert(row.syncNode);
    assert(row.syncNode->type != FILENODE);
    assert(row.syncNode->getLocalPath() == fullPath.localPath);

    // nothing to do for this subtree? Skip traversal
    if (!(row.syncNode->scanRequired() || row.syncNode->mightHaveMoves() || row.syncNode->syncRequired()))
    {
        SYNC_verbose << syncname << "No scanning/moving/syncing needed at " << logTriplet(row, fullPath);
        return true;
    }

    SYNC_verbose << syncname << "Entering folder with "
        << row.syncNode->scanAgain  << "-"
        << row.syncNode->checkMovesAgain << "-"
        << row.syncNode->syncAgain << " ("
        << row.syncNode->conflicts << ") at "
        << fullPath.syncPath;

    // make sure any subtree flags are passed to child nodes, so we can clear the flag at this level
    for (auto& child : row.syncNode->children)
    {
        if (child.second->type != FILENODE)
        {
            if (row.syncNode->scanAgain == TREE_ACTION_SUBTREE)
            {
                child.second->scanDelayUntil = std::max<dstime>(child.second->scanDelayUntil,  row.syncNode->scanDelayUntil);
            }

            child.second->scanAgain = propagateSubtreeFlag(row.syncNode->scanAgain, child.second->scanAgain);
            child.second->checkMovesAgain = propagateSubtreeFlag(row.syncNode->checkMovesAgain, child.second->checkMovesAgain);
            child.second->syncAgain = propagateSubtreeFlag(row.syncNode->syncAgain, child.second->syncAgain);
        }
    }
    if (row.syncNode->scanAgain == TREE_ACTION_SUBTREE) row.syncNode->scanAgain = TREE_ACTION_HERE;
    if (row.syncNode->checkMovesAgain == TREE_ACTION_SUBTREE) row.syncNode->checkMovesAgain = TREE_ACTION_HERE;
    if (row.syncNode->syncAgain == TREE_ACTION_SUBTREE) row.syncNode->syncAgain = TREE_ACTION_HERE;

    // Whether we should perform sync actions at this level.
    bool wasSynced = row.syncNode->scanAgain < TREE_ACTION_HERE
                  && row.syncNode->syncAgain < TREE_ACTION_HERE
                  && row.syncNode->checkMovesAgain < TREE_ACTION_HERE;
    bool syncHere = !wasSynced;
    bool recurseHere = true;

    SYNC_verbose << syncname << "sync/recurse here: " << syncHere << recurseHere;

    // reset this node's sync flag. It will be set again if anything remains to be done.
    row.syncNode->syncAgain = TREE_RESOLVED;

    vector<FSNode>* effectiveFsChildren;
    vector<FSNode> fsChildren;

    {
        // For convenience.
        LocalNode& node = *row.syncNode;

        // Do we need to scan this node?
        if (node.scanAgain >= TREE_ACTION_HERE)
        {
            client->mSyncFlags->reachableNodesAllScannedThisPass = false;

            if (row.fsNode)
            {
                std::shared_ptr<ScanService::Request> ourScanRequest = node.scanInProgress ? node.rare().scanRequest  : nullptr;

                if (!ourScanRequest && (!mActiveScanRequest || mActiveScanRequest->completed()))
                {
                    // we can start a single new request if we are still recursing and the last request from this sync completed already
                    if (node.scanDelayUntil != 0 && Waiter::ds < node.scanDelayUntil)
                    {
                        SYNC_verbose << "Too soon to scan this folder, needs more ds: " << node.scanDelayUntil - Waiter::ds;
                        syncHere = false;
                    }
                    else
                    {
                        // queueScan() already logs: LOG_verbose << "Requesting scan for: " << fullPath.toPath(*client->fsaccess);
                        node.scanObsolete = false;
                        node.scanInProgress = true;
                        ourScanRequest = client->mScanService->queueScan(node, fullPath.localPath);
                        node.rare().scanRequest = ourScanRequest;
                        mActiveScanRequest = ourScanRequest;
                        syncHere = false;
                    }
                }
                else if (ourScanRequest &&
                         ourScanRequest->completed())
                {
                    if (ourScanRequest == mActiveScanRequest) mActiveScanRequest.reset();

                    node.scanInProgress = false;
                    if (node.scanObsolete)   // TODO: also consider obsolete if the results are more than 10 seconds old - eg a folder scanned but stuck (unvisitable) behind something unresolvable for hours.  Or if fsid of the folder was not a match after the scan
                    {
                        LOG_verbose << "Directory scan outdated for : " << fullPath.localPath_utf8();
                        node.scanObsolete = false;
                        node.scanInProgress = false;
                        syncHere = false;
                        node.scanDelayUntil = Waiter::ds + 10; // don't scan too frequently
                    }
                    else
                    {
                        node.lastFolderScan.reset(
                            new vector<FSNode>(ourScanRequest->results()));

                        LOG_verbose << "Received " << node.lastFolderScan->size() << " directory scan results for: " << fullPath.localPath_utf8();

                        node.scanDelayUntil = Waiter::ds + 20; // don't scan too frequently
                        row.syncNode->scanAgain = TREE_RESOLVED;
                        row.syncNode->setCheckMovesAgain(false, true, false);
                        row.syncNode->setSyncAgain(false, true, false);
                        syncHere = true;
                    }
                }
                else
                {
                    syncHere = false;
                }
            }
            else
            {
                syncHere = false;
                recurseHere = false;  // If we need to scan, we need the folder to exist first - revisit later
            }
            node.trimRareFields();
        }
        else
        {
            // this will be restored at the end of the function if any nodes below in the tree need it
            row.syncNode->scanAgain = TREE_RESOLVED;
        }

        // Effective children are from the last scan, if present.
        effectiveFsChildren = node.lastFolderScan.get();

        // Otherwise, we can reconstruct the filesystem entries from the LocalNodes
        if (!effectiveFsChildren)
        {
            fsChildren.reserve(node.children.size() + 50);  // leave some room for others to be added in syncItem()

            for (auto &childIt : node.children)
            {
                if (childIt.second->fsid != UNDEF)
                {
                    fsChildren.emplace_back(childIt.second->getKnownFSDetails());
                }
            }

            effectiveFsChildren = &fsChildren;
        }
    }
    SYNC_verbose << syncname << "sync/recurse here after scan logic: " << syncHere << recurseHere;

    // Have we encountered the scan target?
    //client->mSyncFlags->scanTargetReachable |= mScanRequest && mScanRequest->matches(*row.syncNode);

    syncHere &= !row.cloudNode || row.cloudNode->mPendingChanges.empty();
    SYNC_verbose << syncname << "sync/recurse here after pending changes logic: " << syncHere << recurseHere << !!row.cloudNode << (row.cloudNode ? (row.cloudNode->mPendingChanges.empty()?"1":"0") : "-");

#ifdef DEBUG
    static char clientOfInterest[100] = "clientA2";
    static char folderOfInterest[100] = "f_2";
    if (string(clientOfInterest) == client->clientname)
    if (string(folderOfInterest) == fullPath.localPath.leafName().toPath(*client->fsaccess))
    {
        clientOfInterest[99] = 0;  // breakpoint opporunity here
    }
#endif

    // Reset these flags before we evaluate each subnode.
    // They could be set again during that processing,
    // And additionally we double check with each child node after, in case we had reason to skip it.
    row.syncNode->checkMovesAgain = TREE_RESOLVED;
    row.syncNode->conflicts = TREE_RESOLVED;
    bool folderSynced = syncHere;
    bool subfoldersSynced = true;

    // Get sync triplets.
    vector<syncRow> childRows;
    vector<FSNode> fsInferredChildren;

    if (!wasSynced || !inferAlreadySyncedTriplets(row.cloudNode, *row.syncNode, fsInferredChildren, childRows))
    {
        childRows = computeSyncTriplets(row.cloudNode, *row.syncNode, *effectiveFsChildren);
    }

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
            childRow.fsSiblings = effectiveFsChildren;
            childRow.rowSiblings = &childRows;

            assert(!row.syncNode || row.syncNode->localname.empty() || row.syncNode->name.empty() || !row.syncNode->parent ||
                   0 == compareUtf(row.syncNode->localname, true, row.syncNode->name, false, true));

            ScopedSyncPathRestore syncPathRestore(fullPath);

            if (!fullPath.appendRowNames(childRow, mFilesystemType))
            {
                // this is a legitimate case; eg. we only had a syncNode and it is removed in resolve_delSyncNode
                continue;
            }

            if (!(!childRow.syncNode || childRow.syncNode->getLocalPath() == fullPath.localPath))
            {
                auto s = childRow.syncNode->getLocalPath();
                assert(!childRow.syncNode || 0 == compareUtf(childRow.syncNode->getLocalPath(), true, fullPath.localPath, true, false));
            }

            if (!fsstableids && !row.syncNode->unstableFsidAssigned)
            {
                // for FAT and other filesystems where we can't rely on fsid
                // being the same after remount, so update our previously synced nodes
                // with the actual fsids now attached to them (usually generated by FUSE driver)
                FSNode* fsnode = childRow.fsNode;
                LocalNode* localnode = childRow.syncNode;

                if (localnode && localnode->fsid != UNDEF)
                {
                    if (fsnode && syncEqual(*fsnode, *localnode))
                    {
                        localnode->setfsid(fsnode->fsid, client->localnodeByFsid, fsnode->localname);
                        statecacheadd(localnode);
                    }
                }
                row.syncNode->unstableFsidAssigned = true;
            }

            switch (step)
            {
            case 0:
                // first pass: check for any renames within the folder (or other move activity)
                // these must be processed first, otherwise if another file
                // was added with a now-renamed name, an upload would be
                // attached to the wrong node, resulting in node versions
                if (syncHere)
                {
                    if (!syncItem_checkMoves(childRow, row, fullPath, committer))
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
                if (syncHere && !childRow.itemProcessed)
                {
                    if (!syncItem(childRow, row, fullPath, committer))
                    {
                        folderSynced = false;
                        row.syncNode->setSyncAgain(false, true, false);
                    }
                }
                break;

            case 2:
                // third and final pass: recurse into the folders
                if (recurseHere &&
                    childRow.syncNode &&
                    childRow.syncNode->type == FOLDERNODE &&
                    !childRow.suppressRecursion &&
                    !childRow.syncNode->moveSourceApplyingToCloud &&  // we may not have visited syncItem if !syncHere, but skip these if we determine they are being moved from or deleted already
                    !childRow.syncNode->moveTargetApplyingToCloud &&
                    //!childRow.syncNode->deletedFS &&   we should not check this one, or we won't remove the LocalNode
                    !childRow.syncNode->deletingCloud)
                {
                    // Add watches as necessary.
                    if (childRow.fsNode)
                    {
                        childRow.syncNode->watch(fullPath.localPath, childRow.fsNode->fsid);
                    }

                    if (!recursiveSync(childRow, fullPath, committer))
                    {
                        subfoldersSynced = false;
                    }
                }
                break;

            }
        }
    }

    if (syncHere && folderSynced &&
        !anyNameConflicts &&
        row.syncNode->lastFolderScan &&
        row.syncNode->lastFolderScan->size() == row.syncNode->children.size())
    {
#ifdef DEBUG
        // Double check we really can recreate the filesystem entries correctly
        vector<FSNode> generated;
        for (auto &childIt : row.syncNode->children)
        {
            if (childIt.second->fsid != UNDEF)
            {
                generated.emplace_back(childIt.second->getKnownFSDetails());
            }
        }
        assert(generated.size() == row.syncNode->lastFolderScan->size());
        sort(generated.begin(), generated.end(), [](FSNode& a, FSNode& b){ return a.localname < b.localname; });
        sort(row.syncNode->lastFolderScan->begin(), row.syncNode->lastFolderScan->end(), [](FSNode& a, FSNode& b){ return a.localname < b.localname; });
        for (size_t i = generated.size(); i--; )
        {
            assert(generated[i].type == (*row.syncNode->lastFolderScan)[i].type);
            if (generated[i].type == FILENODE)
            {
                if (!(generated[i].equivalentTo((*row.syncNode->lastFolderScan)[i])))
                {
                    assert(generated[i].equivalentTo((*row.syncNode->lastFolderScan)[i]));
                }
            }
        }
#endif


        // LocalNodes are now consistent with the last scan.
        LOG_debug << syncname << "Clearing folder scan records at " << fullPath.localPath_utf8();
        row.syncNode->lastFolderScan.reset();
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

    SYNC_verbose << syncname
                << "Exiting folder with "
                << row.syncNode->scanAgain  << "-"
                << row.syncNode->checkMovesAgain << "-"
                << row.syncNode->syncAgain << " ("
                << row.syncNode->conflicts << ") at "
                << fullPath.syncPath;

    return folderSynced && subfoldersSynced;
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

bool Sync::syncItem_checkMoves(syncRow& row, syncRow& parentRow, SyncPath& fullPath, DBTableTransactionCommitter& committer)
{
    CodeCounter::ScopeTimer rst(client->performanceStats.syncItemTime1);

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
                     << "Updating slocalname: " << row.fsNode->shortname->toPath(*client->fsaccess)
                     << " at " << fullPath.localPath_utf8()
                     << " was " << (row.syncNode->slocalname ? row.syncNode->slocalname->toPath(*client->fsaccess) : "(null)")
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

    if (row.fsNode && (!row.syncNode || row.syncNode->fsid == UNDEF ||
                                        row.syncNode->fsid != row.fsNode->fsid))
    {
        bool rowResult;
        if (checkLocalPathForMovesRenames(row, parentRow, fullPath, rowResult))
        {
            row.itemProcessed = true;
            return rowResult;
        }
    }

    if (row.cloudNode && (!row.syncNode || row.syncNode->syncedCloudNodeHandle.isUndef() ||
        row.syncNode->syncedCloudNodeHandle.as8byte() != row.cloudNode->nodehandle))
    {
        LOG_verbose << syncname << "checking localnodes for synced handle " << row.cloudNode->nodeHandle();

        bool rowResult;
        if (checkCloudPathForMovesRenames(row, parentRow, fullPath, rowResult))
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
            row.cloudNode->nodeHandle() == row.syncNode->syncedCloudNodeHandle &&
            row.fsNode->fsid != UNDEF && row.fsNode->fsid == row.syncNode->fsid)
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


bool Sync::syncItem(syncRow& row, syncRow& parentRow, SyncPath& fullPath, DBTableTransactionCommitter& committer)
{
    CodeCounter::ScopeTimer rst(client->performanceStats.syncItemTime2);
    bool rowSynced = false;

    // each of the 8 possible cases of present/absent for this row
    if (row.syncNode)
    {
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
                        assert(row.syncNode->syncedFingerprint == row.cloudNode->fingerprint());
                    }

                    // these comparisons may need to be adjusted for UTF, escapes
                    assert(row.syncNode->fsid != row.fsNode->fsid || row.syncNode->localname == row.fsNode->localname);
                    assert(row.syncNode->fsid == row.fsNode->fsid || 0 == compareUtf(row.syncNode->localname, true, row.fsNode->localname, true, isCaseInsensitive(mFilesystemType)));
                    assert(row.syncNode->syncedCloudNodeHandle != row.cloudNode->nodeHandle() || row.syncNode->name == row.cloudNode->displayname());
                    assert(row.syncNode->syncedCloudNodeHandle == row.cloudNode->nodeHandle() || 0 == compareUtf(row.syncNode->name, true, row.cloudNode->displayname(), true, isCaseInsensitive(mFilesystemType)));

                    assert((!!row.syncNode->slocalname == !!row.fsNode->shortname) &&
                           (!row.syncNode->slocalname ||
                           (*row.syncNode->slocalname == *row.fsNode->shortname)));

                    if (row.syncNode->fsid != row.fsNode->fsid ||
                        row.syncNode->syncedCloudNodeHandle != row.cloudNode->nodehandle ||
                        row.syncNode->localname != row.fsNode->localname ||
                        row.syncNode->name != row.cloudNode->displayname())
                    {
                        LOG_verbose << syncname << "Row is synced, setting fsid and nodehandle" << logTriplet(row, fullPath);

                        if (row.syncNode->type == FOLDERNODE && row.syncNode->fsid != row.fsNode->fsid)
                        {
                            // a folder disappeared and was replaced by a different one - scan it all
                            row.syncNode->setScanAgain(false, true, true, 0);
                        }

                        row.syncNode->setfsid(row.fsNode->fsid, client->localnodeByFsid, row.fsNode->localname);
                        row.syncNode->setSyncedNodeHandle(row.cloudNode->nodeHandle(), row.cloudNode->displayname());

                        row.syncNode->treestate(TREESTATE_SYNCED);

                        statecacheadd(row.syncNode);
                        ProgressingMonitor monitor(client); // not stalling
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
                    rowSynced = resolve_upsync(row, parentRow, fullPath, committer);
                }
                else if (fsEqual)
                {
                    // cloud has changed, get the change
                    rowSynced = resolve_downsync(row, parentRow, fullPath, committer, true);
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
                    rowSynced = resolve_upsync(row, parentRow, fullPath, committer);
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
                if (row.syncNode->fsid != UNDEF)
                {
                    // used to be synced - remove in the cloud (or detect move)
                    rowSynced = resolve_fsNodeGone(row, parentRow, fullPath);
                }
                else
                {
                    // fs item did not exist before; downsync
                    rowSynced = resolve_downsync(row, parentRow, fullPath, committer, false);
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
                         row.fsNode->fingerprint == *static_cast<FileFingerprint*>(row.cloudNode))
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
    ProgressingMonitor monitor(client);

    // this really is a new node: add
    LOG_debug << syncname << "Creating LocalNode from FS at: " << fullPath.localPath_utf8() << logTriplet(row, fullPath);

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
        row.syncNode->setfsid(row.fsNode->fsid, client->localnodeByFsid, row.fsNode->localname);
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
    ProgressingMonitor monitor(client);
    LOG_debug << syncname << "Creating LocalNode from Cloud at: " << fullPath.cloudPath << logTriplet(row, fullPath);

    assert(row.syncNode == nullptr);
    row.syncNode = new LocalNode;

    if (row.cloudNode->type == FILENODE)
    {
        assert(row.cloudNode->fingerprint().isvalid);
        row.syncNode->syncedFingerprint = row.cloudNode->fingerprint();
    }
    row.syncNode->init(this, row.cloudNode->type, parentRow.syncNode, fullPath.localPath, nullptr);
    if (considerSynced)
    {
        row.syncNode->setSyncedNodeHandle(row.cloudNode->nodeHandle(), row.cloudNode->displayname());
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
    ProgressingMonitor monitor(client);

    if (client->mSyncFlags->movesWereComplete ||
        row.syncNode->moveSourceAppliedToCloud ||
        row.syncNode->moveAppliedToLocal ||
        row.syncNode->deletedFS ||
        row.syncNode->deletingCloud) // once the cloud delete finishes, the fs node is gone and so we visit here on the next run
    {
        // local and cloud disappeared; remove sync item also
        LOG_verbose << syncname << "Marking Localnode for deletion" << logTriplet(row, fullPath);

        if (row.syncNode->deletedFS)
        {
            if (row.syncNode->type == FOLDERNODE)
            {
                client->app->syncupdate_local_folder_deletion(this, fullPath.localPath);
            }
            else
            {
                client->app->syncupdate_local_file_deletion(this, fullPath.localPath);
            }
        }

        // deletes itself and subtree, queues db record removal
        delete row.syncNode;
        row.syncNode = nullptr; // todo: maybe could return true here?
    }
    else if (row.syncNode->moveSourceApplyingToCloud)
    {
        // The logic for a move can detect when it's finished, and sets moveAppliedToCloud
        LOG_verbose << syncname << "Cloud move still in progress" << logTriplet(row, fullPath);
        row.syncNode->setSyncAgain(true, false, false); // visit again
        row.suppressRecursion = true;
    }
    else
    {
        LOG_debug << syncname << "resolve_delSyncNode not resolved at: " << logTriplet(row, fullPath);
        assert(false);
        row.syncNode->setScanAgain(true, true, true, 2);
        monitor.noResult();
    }
    return false;
}

bool Sync::resolve_upsync(syncRow& row, syncRow& parentRow, SyncPath& fullPath, DBTableTransactionCommitter& committer)
{
    ProgressingMonitor monitor(client);

    if (row.fsNode->type == FILENODE)
    {
        // upload the file if we're not already uploading

        if (row.syncNode->upload &&
          !(row.syncNode->upload->fingerprint() == row.fsNode->fingerprint))
        {
            LOG_debug << syncname << "An older version of this file was already uploading, cancelling." << fullPath.localPath_utf8() << logTriplet(row, fullPath);
            row.syncNode->upload.reset();
        }

        if (!row.syncNode->upload && !row.syncNode->newnode)
        {
            // Sanity.
            assert(row.syncNode->parent);
            assert(row.syncNode->parent == parentRow.syncNode);

            if (parentRow.cloudNode && parentRow.cloudNode->nodeHandle() == parentRow.syncNode->syncedCloudNodeHandle)
            {
                client->app->syncupdate_local_file_addition(this, fullPath.localPath);

                LOG_debug << syncname << "Uploading file " << fullPath.localPath_utf8() << logTriplet(row, fullPath);
                assert(row.syncNode->syncedFingerprint.isvalid); // LocalNodes for files always have a valid fingerprint
                client->nextreqtag();
                row.syncNode->upload.reset(new LocalNode::Upload(*row.syncNode, *row.fsNode, parentRow.cloudNode->nodeHandle(), fullPath.localPath));
                client->startxfer(PUT, row.syncNode->upload.get(), committer);  // full path will be calculated in the prepare() callback
                client->app->syncupdate_put(this, fullPath.localPath_utf8().c_str());
            }
            else
            {
                SYNC_verbose << syncname << "Parent cloud folder to upload to doesn't exist yet" << logTriplet(row, fullPath);
                row.syncNode->setSyncAgain(true, false, false);
                monitor.waitingLocal(fullPath.localPath, SyncWaitReason::UpsyncNeedsTargetFolder);
            }
        }
        else if (row.syncNode->newnode)
        {
            SYNC_verbose << syncname << "Upload complete but putnodes in progress" << logTriplet(row, fullPath);
        }
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
                    ostringstream remotePath;

                    // Generate remote path for reporting.
                    remotePath << parentRow.cloudNode->displaypath()
                               << "/"
                               << row.syncNode->name;

                    client->filenameAnomalyDetected(type, fullPath.localPath_utf8(), remotePath.str());
                }
            }

            LOG_verbose << "Creating cloud node for: " << fullPath.localPath_utf8() << logTriplet(row, fullPath);
            // while the operation is in progress sync() will skip over the parent folder
            vector<NewNode> nn(1);
            client->putnodes_prepareOneFolder(&nn[0], row.syncNode->name);
            client->putnodes(parentRow.cloudNode->nodehandle, move(nn), nullptr, 0);
        }
        else
        {
            SYNC_verbose << "Delay creating cloud node until parent cloud node exists: " << fullPath.localPath_utf8() << logTriplet(row, fullPath);
            row.syncNode->setSyncAgain(true, false, false);
            monitor.waitingLocal(fullPath.localPath, SyncWaitReason::UpsyncNeedsTargetFolder);
        }

        // we may not see some moves/renames until the entire folder structure is created.
        row.syncNode->setCheckMovesAgain(true, false, false);     // todo: double check - might not be needed for the wait case? might cause a stall?
    }
    return false;
}

bool Sync::resolve_downsync(syncRow& row, syncRow& parentRow, SyncPath& fullPath, DBTableTransactionCommitter& committer, bool alreadyExists)
{
    ProgressingMonitor monitor(client);

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

        if (row.syncNode->download &&
            !(row.syncNode->download->fingerprint() == row.cloudNode->fingerprint()))
        {
            LOG_debug << syncname << "An older version of this file was already downloading, cancelling." << fullPath.cloudPath << logTriplet(row, fullPath);
            row.syncNode->download.reset();
        }

        if (parentRow.fsNode)
        {
            if (!row.syncNode->download)
            {
                client->app->syncupdate_remote_file_addition(this, row.cloudNode);

                // FIXME: to cover renames that occur during the
                // download, reconstruct localname in complete()
                LOG_debug << syncname << "Start sync download: " << row.syncNode << logTriplet(row, fullPath);
                client->app->syncupdate_get(this, row.cloudNode, fullPath.cloudPath.c_str());

                row.syncNode->download.reset(new SyncFileGet(*row.syncNode, *row.cloudNode, fullPath.localPath));
                client->nextreqtag();
                client->startxfer(GET, row.syncNode->download.get(), committer);

                if (row.syncNode) row.syncNode->treestate(TREESTATE_SYNCING);
                else if (parentRow.syncNode) parentRow.syncNode->treestate(TREESTATE_SYNCING);
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
            monitor.waitingCloud(row.cloudNode->displaypath(), SyncWaitReason::DownsyncNeedsTargetFolder);
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
                auto type = isFilenameAnomaly(fullPath.localPath, row.cloudNode);

                if (type != FILENAME_ANOMALY_NONE)
                {
                    auto remotePath = row.cloudNode->displaypath();
                    client->filenameAnomalyDetected(type, fullPath.localPath_utf8(), remotePath);
                }
            }

            LOG_verbose << "Creating local folder at: " << fullPath.localPath_utf8() << logTriplet(row, fullPath);

            assert(!isBackup());
            if (client->fsaccess->mkdirlocal(fullPath.localPath))
            {
                assert(row.syncNode);
                assert(row.syncNode->localname == fullPath.localPath.leafName());

                // Update our records of what we know is on disk for this (parent) LocalNode.
                // This allows the next level of folders to be created too

                auto fa = client->fsaccess->newfileaccess(false);
                if (fa->fopen(fullPath.localPath, true, false))
                {
                    auto fsnode = FSNode::fromFOpened(*fa, fullPath.localPath, *client->fsaccess);

                    row.syncNode->localname = fsnode->localname;
                    row.syncNode->slocalname = fsnode->cloneShortname();
                    row.syncNode->setfsid(fsnode->fsid, client->localnodeByFsid, fsnode->localname);
                    statecacheadd(row.syncNode);

                    // Mark other nodes with this FSID as having their FSID reused.
                    client->setFsidReused(fsnode->fsid, row.syncNode);

                    if (row.fsSiblings->empty() && row.fsSiblings->capacity() < 50)
                    {
                        row.fsSiblings->reserve(50);
                    }
                    if (row.fsSiblings->capacity() > row.fsSiblings->size())
                    {
                        // However much room we left in the vector, is how many directories we can
                        // drill into, recursively creating as we go.
                        // Since we can't invalidate the pointers taken to these elements already
                        row.fsSiblings->emplace_back(std::move(*fsnode));
                        row.fsNode = &row.fsSiblings->back();
                    }
                    else
                    {
                        row.syncNode->setScanAgain(true, true, false, 0);
                    }
                }
                else
                {
                    LOG_warn << syncname << "Failed to fopen folder straight after creation - revisit in 5s. " << fullPath.localPath_utf8() << logTriplet(row, fullPath);
                    row.syncNode->setScanAgain(true, false, false, 50);
                }
            }
            else if (client->fsaccess->transient_error)
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
            monitor.waitingCloud(row.cloudNode->displaypath(), SyncWaitReason::DownsyncNeedsTargetFolder);
        }

        // we may not see some moves/renames until the entire folder structure is created.
        row.syncNode->setCheckMovesAgain(true, false, false);  // todo: is this still right for the watiing case
    }
    return false;
}


bool Sync::resolve_userIntervention(syncRow& row, syncRow& parentRow, SyncPath& fullPath)
{
    ProgressingMonitor monitor(client);
    LOG_debug << syncname << "write me" << logTriplet(row, fullPath);
    assert(false);
    return false;
}

bool Sync::resolve_pickWinner(syncRow& row, syncRow& parentRow, SyncPath& fullPath)
{
    ProgressingMonitor monitor(client);

    const FileFingerprint& cloud = *row.cloudNode;
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
    ProgressingMonitor monitor(client);

    if (!row.syncNode->syncedCloudNodeHandle.isUndef() &&
        client->nodeIsInActiveSync(client->nodeByHandle(row.syncNode->syncedCloudNodeHandle)))
    {
        row.syncNode->setCheckMovesAgain(true, false, false);

        if (row.syncNode->moveSourceAppliedToCloud)
        {
            SYNC_verbose << syncname << "Node was a cloud move source, it will be removed next pass: " << logTriplet(row, fullPath);
        }
        else if (row.syncNode->moveSourceApplyingToCloud)
        {
            SYNC_verbose << syncname << "Node was a cloud move source, move is propagating: " << logTriplet(row, fullPath);
        }
        else
        {
            SYNC_verbose << syncname << "Letting move destination node process this first (cloud node is at "
			             << client->nodeByHandle(row.syncNode->syncedCloudNodeHandle)->displaypath() << "): " << logTriplet(row, fullPath);
        }
        row.suppressRecursion = true;
        monitor.waitingCloud(fullPath.cloudPath, SyncWaitReason::MoveNeedsDestinationNodeProcessing);
    }
    else if (row.syncNode->deletedFS)
    {
        SYNC_verbose << syncname << "FS item already removed: " << logTriplet(row, fullPath);
        row.suppressRecursion = true;
        monitor.noResult();
    }
    else if (client->mSyncFlags->movesWereComplete)
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
            row.suppressRecursion = true;
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
            monitor.waitingCloud(parentRow.cloudNode->displaypath() + "/" + row.syncNode->name, SyncWaitReason::DeleteWaitingOnMoves);
        }
        else
        {
            monitor.noResult();
        }
    }
    return false;
}

LocalNode* MegaClient::findLocalNodeByFsid(mega::handle fsid, nodetype_t type, const FileFingerprint& fingerprint, Sync& filesystemSync, std::function<bool(LocalNode* ln)> extraCheck)
{
    if (fsid == UNDEF) return nullptr;

    auto range = localnodeByFsid.equal_range(fsid);

    for (auto it = range.first; it != range.second; ++it)
    {
        if (it->second->type != type) continue;
        if (it->second->fsidReused)   continue;

        //todo: actually we do want to support moving files/folders between syncs
        // in the same filesystem so, commented this

        // make sure we are in the same filesystem (fsid comparison is not valid in other filesystems)
        //if (it->second->sync != &filesystemSync)
        //{
        //    continue;
        //}

        auto fp1 = it->second->sync->dirnotify->fsfingerprint();
        auto fp2 = filesystemSync.dirnotify->fsfingerprint();
        if (!fp1 || !fp2 || fp1 != fp2)
        {
            continue;
        }

#ifdef _WIN32
        // (from original sync code) Additionally for windows, check drive letter
        // only consider fsid matches between different syncs for local drives with the
        // same drive letter, to prevent problems with cloned Volume IDs
        if (it->second->sync->localroot->localname.driveLetter() !=
            filesystemSync.localroot->localname.driveLetter())
        {
            continue;
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
            LOG_verbose << clientname << "findLocalNodeByFsid - found at: " << it->second->getLocalPath().toPath(*fsaccess);
            return it->second;
        }
    }
    return nullptr;
}

void MegaClient::setFsidReused(mega::handle fsid, const LocalNode* exclude)
{
    for (auto range = localnodeByFsid.equal_range(fsid);
         range.first != range.second;
         ++range.first)
    {
        if (range.first->second == exclude) continue;
        range.first->second->fsidReused = true;
    }
}

LocalNode* MegaClient::findLocalNodeByNodeHandle(NodeHandle h)
{
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



bool MegaClient::checkIfFileIsChanging(FSNode& fsNode, const LocalPath& fullPath)
{
    // logic to prevent moving files that may still be being updated

    // (original sync code comment:)
    // detect files being updated in the local computer moving the original file
    // to another location as a temporary backup

    assert(fsNode.type == FILENODE);

    bool waitforupdate = false;
    FileChangingState& state = mFileChangingCheckState[fullPath];

    m_time_t currentsecs = m_time();
    if (!state.updatedfileinitialts)
    {
        state.updatedfileinitialts = currentsecs;
    }

    if (currentsecs >= state.updatedfileinitialts)
    {
        if (currentsecs - state.updatedfileinitialts <= Sync::FILE_UPDATE_MAX_DELAY_SECS)
        {
            auto prevfa = fsaccess->newfileaccess(false);

            bool exists = prevfa->fopen(fullPath);
            if (exists)
            {
                LOG_debug << clientname << "File detected in the origin of a move";

                if (currentsecs >= state.updatedfilets)
                {
                    if ((currentsecs - state.updatedfilets) < (Sync::FILE_UPDATE_DELAY_DS / 10))
                    {
                        LOG_verbose << clientname << "currentsecs = " << currentsecs << "  lastcheck = " << state.updatedfilets
                            << "  currentsize = " << prevfa->size << "  lastsize = " << state.updatedfilesize;
                        LOG_debug << "The file size changed too recently. Waiting " << currentsecs - state.updatedfilets << " ds for " << fsNode.localname.toPath();
                        waitforupdate = true;
                    }
                    else if (state.updatedfilesize != prevfa->size)
                    {
                        LOG_verbose << clientname << "currentsecs = " << currentsecs << "  lastcheck = " << state.updatedfilets
                            << "  currentsize = " << prevfa->size << "  lastsize = " << state.updatedfilesize;
                        LOG_debug << "The file size has changed since the last check. Waiting...";
                        state.updatedfilesize = prevfa->size;
                        state.updatedfilets = currentsecs;
                        waitforupdate = true;
                    }
                    else
                    {
                        LOG_debug << clientname << "The file size seems stable";
                    }
                }
                else
                {
                    LOG_warn << clientname << "File checked in the future";
                }

                if (!waitforupdate)
                {
                    if (currentsecs >= prevfa->mtime)
                    {
                        if (currentsecs - prevfa->mtime < (Sync::FILE_UPDATE_DELAY_DS / 10))
                        {
                            LOG_verbose << clientname << "currentsecs = " << currentsecs << "  mtime = " << prevfa->mtime;
                            LOG_debug << clientname << "File modified too recently. Waiting...";
                            waitforupdate = true;
                        }
                        else
                        {
                            LOG_debug << clientname << "The modification time seems stable.";
                        }
                    }
                    else
                    {
                        LOG_warn << clientname << "File modified in the future";
                    }
                }
            }
            else
            {
                if (prevfa->retry)
                {
                    LOG_debug << clientname << "The file in the origin is temporarily blocked. Waiting...";
                    waitforupdate = true;
                }
                else
                {
                    LOG_debug << clientname << "There isn't anything in the origin path";
                }
            }

            if (waitforupdate)
            {
                LOG_debug << clientname << "Possible file update detected.";
                return NULL;
            }
        }
        else
        {
            sendevent(99438, "Timeout waiting for file update", 0);
        }
    }
    else
    {
        LOG_warn << clientname << "File check started in the future";
    }

    if (!waitforupdate)
    {
        mFileChangingCheckState.erase(fullPath);
    }
    return waitforupdate;
}

bool Sync::resolve_fsNodeGone(syncRow& row, syncRow& parentRow, SyncPath& fullPath)
{
    ProgressingMonitor monitor(client);
    bool inProgress = row.syncNode->deletingCloud || row.syncNode->moveApplyingToLocal || row.syncNode->moveSourceApplyingToCloud;

    if (inProgress)
    {
        if (row.syncNode->deletingCloud)        SYNC_verbose << syncname << "Waiting for cloud delete to complete " << logTriplet(row, fullPath);
        if (row.syncNode->moveApplyingToLocal)  SYNC_verbose << syncname << "Waiting for corresponding local move to complete " << logTriplet(row, fullPath);
        if (row.syncNode->moveSourceApplyingToCloud)  SYNC_verbose << syncname << "Waiting for cloud move to complete " << logTriplet(row, fullPath);
    }

    if (!inProgress)
    {
        LocalNode* movedLocalNode = nullptr;

        if (!row.syncNode->fsidReused)
        {
            auto predicate = [&row](LocalNode* n) {
                return n != row.syncNode;
            };

            movedLocalNode =
              client->findLocalNodeByFsid(row.syncNode->fsid,
                                          row.syncNode->type,
                                          row.syncNode->syncedFingerprint,
                                          *this,
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
                SYNC_verbose << syncname << "This file/folder was moved, letting destination node process this first: " << logTriplet(row, fullPath);
            }
            inProgress = true;
        }
        else if (client->mSyncFlags->movesWereComplete)
        {
            if (!row.syncNode->deletingCloud)
            {
                LOG_debug << syncname << "Moving cloud item to cloud sync debris: " << row.cloudNode->displaypath() << logTriplet(row, fullPath);
                client->movetosyncdebris(row.cloudNode, inshare);
                row.syncNode->deletingCloud = true;
            }
            else
            {
                SYNC_verbose << syncname << "Already moving cloud item to cloud sync debris: " << row.cloudNode->displaypath() << logTriplet(row, fullPath);
            }
            inProgress = true;
        }
        else
        {
            // in case it's actually a move and we just haven't seen the fsid yet
            SYNC_verbose << syncname << "Wait for scanning/moving to finish before moving to local debris: " << logTriplet(row, fullPath);
            row.syncNode->setSyncAgain(true, false, false); // make sure we revisit

            monitor.waitingLocal(fullPath.localPath, SyncWaitReason::DeleteWaitingOnMoves);
        }
    }

    // there's no folder so clear the flag so we don't stall
    row.syncNode->scanAgain = TREE_RESOLVED;
    row.syncNode->checkMovesAgain = TREE_RESOLVED;

    if (inProgress)
    {
        row.suppressRecursion = true;
        row.syncNode->setSyncAgain(true, false, false); // make sure we revisit
    }

    return false;
}

bool Sync::syncEqual(const Node& n, const FSNode& fs)
{
    // Assuming names already match
    if (n.type != fs.type) return false;
    if (n.type != FILENODE) return true;
    assert(n.fingerprint().isvalid && fs.fingerprint.isvalid);
    return n.fingerprint() == fs.fingerprint;  // size, mtime, crc
}

bool Sync::syncEqual(const Node& n, const LocalNode& ln)
{
    // Assuming names already match
    // Not comparing nodehandle here.  If they all match we set syncedCloudNodeHandle
    if (n.type != ln.type) return false;
    if (n.type != FILENODE) return true;
    assert(n.fingerprint().isvalid && ln.syncedFingerprint.isvalid);
    return n.fingerprint() == ln.syncedFingerprint;  // size, mtime, crc
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

void MegaClient::triggerSync(NodeHandle h, bool recurse)
{
    if (fetchingnodes) return;  // on start everything needs scan+sync anyway

#ifdef ENABLE_SYNC
#ifdef DEBUG
    // this was only needed for the fetchingnodes case anyway?
    //for (auto* n = nodeByHandle(h); n; n = n->parent)
    //{
    //    n->applykey();
    //}

    //if (Node* n = nodeByHandle(h))
    //{
    //    SYNC_verbose << clientname << "Received sync trigger notification for sync trigger of " << n->displaypath();
    //}
#endif

    auto range = localnodeByNodeHandle.equal_range(h);

    if (range.first == range.second)
    {
        // corresponding sync node not found.
        // this could be a move target though, to a syncNode we have not created yet
        // go back up the (cloud) node tree to find an ancestor we can mark as needing sync checks
        if (Node* n = nodeByHandle(h, true))
        {
            // if the parent is a file, then it's just old versions being mentioned in the actionpackets, ignore
            if (n->type != FILENODE)
            {
                SYNC_verbose << clientname << "Trigger syncNode not fournd for " << n->displaypath() << ", will trigger parent";
                if (n->parent) triggerSync(n->parent->nodeHandle(), true);
            }
        }
    }
    else
    {
        // we are already being called with the handle of the parent of the thing that changed
        for (auto it = range.first; it != range.second; ++it)
        {
            SYNC_verbose << clientname << "Triggering sync flag for " << it->second->localnodedisplaypath(*fsaccess) << (recurse ? " recursive" : "");
            it->second->setSyncAgain(false, true, recurse);
        }
    }
#endif
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


} // namespace

#endif
