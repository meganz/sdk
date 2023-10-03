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
#include <memory>
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

namespace {

// Need this to store `LightFileFingerprint` by-value in `FingerprintSet`
struct LightFileFingerprintComparator
{
    bool operator()(const LightFileFingerprint& lhs, const LightFileFingerprint& rhs) const
    {
        return LightFileFingerprintCmp{}(&lhs, &rhs);
    }
};

// Represents a file/folder for use in assigning fs IDs
struct FsFile
{
    handle fsid;
    LocalPath path;
};

// Caches fingerprints
class FingerprintCache
{
public:
    using FingerprintSet = std::set<LightFileFingerprint, LightFileFingerprintComparator>;

    // Adds a new fingerprint
    template<typename T, typename = typename std::enable_if<std::is_same<LightFileFingerprint, typename std::decay<T>::type>::value>::type>
    const LightFileFingerprint* add(T&& ffp)
    {
         const auto insertPair = mFingerprints.insert(std::forward<T>(ffp));
         return &*insertPair.first;
    }

    // Returns the set of all fingerprints
    const FingerprintSet& all() const
    {
        return mFingerprints;
    }

private:
    FingerprintSet mFingerprints;
};

using FingerprintLocalNodeMap = std::multimap<const LightFileFingerprint*, LocalNode*, LightFileFingerprintCmp>;
using FingerprintFileMap = std::multimap<const LightFileFingerprint*, FsFile, LightFileFingerprintCmp>;

// Collects all syncable filesystem paths in the given folder under `localpath`
set<LocalPath> collectAllPathsInFolder(Sync& sync, MegaApp& app, FileSystemAccess& fsaccess, LocalPath& localpath,
                                    LocalPath& localdebris)
{
    auto fa = fsaccess.newfileaccess(false);
    if (!fa->fopen(localpath, true, false, FSLogging::logOnError))
    {
        LOG_err << "Unable to open path: " << localpath;
        return {};
    }
    if (fa->mIsSymLink)
    {
        LOG_debug << "Ignoring symlink: " << localpath;
        return {};
    }
    assert(fa->type == FOLDERNODE);

    auto da = std::unique_ptr<DirAccess>{fsaccess.newdiraccess()};
    if (!da->dopen(&localpath, fa.get(), false))
    {
        LOG_err << "Unable to open directory: " << localpath;
        return {};
    }

    set<LocalPath> paths; // has to be a std::set to enforce same sorting as `children` of `LocalNode`

    LocalPath localname;
    while (da->dnext(localpath, localname, false))
    {
        ScopedLengthRestore restoreLength(localpath);
        localpath.appendWithSeparator(localname, false);

        // check if this record is to be ignored
        const auto name = localname.toName(fsaccess);
        if (app.sync_syncable(&sync, name.c_str(), localpath))
        {
            // skip the sync's debris folder
            if (!localdebris.isContainingPathOf(localpath))
            {
                paths.insert(localpath);
            }
        }
    }

    return paths;
}

// Combines another fingerprint into `ffp`
void hashCombineFingerprint(LightFileFingerprint& ffp, const LightFileFingerprint& other)
{
    hashCombine(ffp.size, other.size);
    hashCombine(ffp.mtime, other.mtime);
}

// Combines the fingerprints of all file nodes in the given map
bool combinedFingerprint(LightFileFingerprint& ffp, const localnode_map& nodeMap)
{
    bool success = false;
    for (const auto& nodePair : nodeMap)
    {
        const LocalNode& l = *nodePair.second;
        if (l.type == FILENODE)
        {
            LightFileFingerprint lFfp;
            lFfp.genfingerprint(l.size, l.mtime);
            hashCombineFingerprint(ffp, lFfp);
            success = true;
        }
    }
    return success;
}

// Combines the fingerprints of all files in the given paths
bool combinedFingerprint(LightFileFingerprint& ffp, FileSystemAccess& fsaccess, const set<LocalPath>& paths)
{
    bool success = false;
    for (auto& path : paths)
    {
        auto fa = fsaccess.newfileaccess(false);
        auto pathArg = path; // todo: sort out const
        if (!fa->fopen(pathArg, true, false, FSLogging::logOnError))
        {
            LOG_err << "Unable to open path: " << path;
            success = false;
            break;
        }
        if (fa->mIsSymLink)
        {
            LOG_debug << "Ignoring symlink: " << path;
            continue;
        }
        if (fa->type == FILENODE)
        {
            LightFileFingerprint faFfp;
            faFfp.genfingerprint(fa->size, fa->mtime);
            hashCombineFingerprint(ffp, faFfp);
            success = true;
        }
    }
    return success;
}

// Computes the fingerprint of the given `l` (file or folder) and stores it in `ffp`
bool computeFingerprint(LightFileFingerprint& ffp, const LocalNode& l)
{
    if (l.type == FILENODE)
    {
        ffp.genfingerprint(l.size, l.mtime);
        return true;
    }
    else if (l.type == FOLDERNODE)
    {
        return combinedFingerprint(ffp, l.children);
    }
    else
    {
        assert(false && "Invalid node type");
        return false;
    }
}

// Computes the fingerprint of the given `fa` (file or folder) and stores it in `ffp`
bool computeFingerprint(LightFileFingerprint& ffp, FileSystemAccess& fsaccess,
                        FileAccess& fa, LocalPath& path, const set<LocalPath>& paths)
{
    if (fa.type == FILENODE)
    {
        assert(paths.empty());
        ffp.genfingerprint(fa.size, fa.mtime);
        return true;
    }
    else if (fa.type == FOLDERNODE)
    {
        return combinedFingerprint(ffp, fsaccess, paths);
    }
    else
    {
        assert(false && "Invalid node type");
        return false;
    }
}

// Collects all `LocalNode`s by storing them in `localnodes`, keyed by LightFileFingerprint.
// Invalidates the fs IDs of all local nodes.
// Stores all fingerprints in `fingerprints` for later reference.
void collectAllLocalNodes(FingerprintCache& fingerprints, FingerprintLocalNodeMap& localnodes,
                          LocalNode& l, handlelocalnode_map& fsidnodes)
{
    // invalidate fsid of `l`
    l.fsid = mega::UNDEF;
    if (l.fsid_it != fsidnodes.end())
    {
        fsidnodes.erase(l.fsid_it);
        l.fsid_it = fsidnodes.end();
    }
    // collect fingerprint
    LightFileFingerprint ffp;
    if (computeFingerprint(ffp, l))
    {
        const auto ffpPtr = fingerprints.add(std::move(ffp));
        localnodes.insert(std::make_pair(ffpPtr, &l));
    }
    if (l.type == FILENODE)
    {
        return;
    }
    for (auto& childPair : l.children)
    {
        collectAllLocalNodes(fingerprints, localnodes, *childPair.second, fsidnodes);
    }
}

// Collects all `File`s by storing them in `files`, keyed by FileFingerprint.
// Stores all fingerprints in `fingerprints` for later reference.
void collectAllFiles(bool& success, FingerprintCache& fingerprints, FingerprintFileMap& files,
                     Sync& sync, MegaApp& app, FileSystemAccess& fsaccess, LocalPath& localpath,
                     LocalPath& localdebris)
{
    auto insertFingerprint = [&files, &fingerprints](FileSystemAccess& fsaccess, FileAccess& fa,
                                                     LocalPath& path, const set<LocalPath>& paths)
    {
        LightFileFingerprint ffp;
        if (computeFingerprint(ffp, fsaccess, fa, path, paths))
        {
            const auto ffpPtr = fingerprints.add(std::move(ffp));
            files.insert(std::make_pair(ffpPtr, FsFile{fa.fsid, path}));
        }
    };

    auto fa = fsaccess.newfileaccess(false);
    if (!fa->fopen(localpath, true, false, FSLogging::logOnError))
    {
        LOG_err << "Unable to open path: " << localpath;
        success = false;
        return;
    }
    if (fa->mIsSymLink)
    {
        LOG_debug << "Ignoring symlink: " << localpath;
        return;
    }
    if (!fa->fsidvalid)
    {
        LOG_err << "Invalid fs id for: " << localpath;
        success = false;
        return;
    }

    if (fa->type == FILENODE)
    {
        insertFingerprint(fsaccess, *fa, localpath, {});
    }
    else if (fa->type == FOLDERNODE)
    {
        const auto paths = collectAllPathsInFolder(sync, app, fsaccess, localpath, localdebris);
        insertFingerprint(fsaccess, *fa, localpath, paths);
        fa.reset();
        for (const auto& path : paths)
        {
            LocalPath tmpPath = path;
            collectAllFiles(success, fingerprints, files, sync, app, fsaccess, tmpPath, localdebris);
        }
    }
    else
    {
        assert(false && "Invalid file type");
        success = false;
        return;
    }
}

// Assigns fs IDs from `files` to those `localnodes` that match the fingerprints found in `files`.
// If there are multiple matches we apply a best-path heuristic.
size_t assignFilesystemIdsImpl(const FingerprintCache& fingerprints, FingerprintLocalNodeMap& localnodes,
                               FingerprintFileMap& files, handlelocalnode_map& fsidnodes, FileSystemAccess& fsaccess)
{
    LocalPath nodePath;
    size_t assignmentCount = 0;
    for (const auto& fp : fingerprints.all())
    {
        const auto nodeRange = localnodes.equal_range(&fp);
        const auto nodeCount = std::distance(nodeRange.first, nodeRange.second);
        if (nodeCount <= 0)
        {
            continue;
        }

        const auto fileRange = files.equal_range(&fp);
        const auto fileCount = std::distance(fileRange.first, fileRange.second);
        if (fileCount <= 0)
        {
            // without files we cannot assign fs IDs to these localnodes, so no need to keep them
            localnodes.erase(nodeRange.first, nodeRange.second);
            continue;
        }

        struct Element
        {
            int score;
            handle fsid;
            LocalNode* l;
        };
        std::vector<Element> elements;
        elements.reserve(nodeCount * fileCount);

        for (auto nodeIt = nodeRange.first; nodeIt != nodeRange.second; ++nodeIt)
        {
            auto l = nodeIt->second;
            if (l != l->sync->localroot.get()) // never assign fs ID to the root localnode
            {
                nodePath = l->getLocalPath();
                for (auto fileIt = fileRange.first; fileIt != fileRange.second; ++fileIt)
                {
                    auto& filePath = fileIt->second.path;
                    const auto score = computeReversePathMatchScore(nodePath, filePath, fsaccess);
                    if (score > 0) // leaf name must match
                    {
                        elements.push_back({score, fileIt->second.fsid, l});
                    }
                }
            }
        }

        // Sort in descending order by score. Elements with highest score come first
        std::sort(elements.begin(), elements.end(), [](const Element& e1, const Element& e2)
                                                    {
                                                        return e1.score > e2.score;
                                                    });

        std::unordered_set<handle> usedFsIds;
        for (const auto& e : elements)
        {
            if (e.l->fsid == mega::UNDEF // node not assigned
                && usedFsIds.find(e.fsid) == usedFsIds.end()) // fsid not used
            {
                e.l->setfsid(e.fsid, fsidnodes);
                usedFsIds.insert(e.fsid);
                ++assignmentCount;
            }
        }

        // the fingerprint that these files and localnodes correspond to has now finished processing
        files.erase(fileRange.first, fileRange.second);
        localnodes.erase(nodeRange.first, nodeRange.second);
    }
    return assignmentCount;
}

} // anonymous

int computeReversePathMatchScore(const LocalPath& path1, const LocalPath& path2, const FileSystemAccess& fsaccess)
{
    if (path1.empty() || path2.empty())
    {
        return 0;
    }

    const auto path1End = path1.localpath.size() - 1;
    const auto path2End = path2.localpath.size() - 1;

    size_t index = 0;
    size_t separatorBias = 0;
    LocalPath::string_type accumulated;
    while (index <= path1End && index <= path2End)
    {
        const auto value1 = path1.localpath[path1End - index];
        const auto value2 = path2.localpath[path2End - index];
        if (value1 != value2)
        {
            break;
        }
        accumulated.push_back(value1); // accumulated will clearly have content after this

        ++index;

        if (accumulated.back() == LocalPath::localPathSeparator)
        {
            ++separatorBias;
            accumulated.clear();
        }
    }

    if (index > path1End && index > path2End) // we got to the beginning of both paths (full score)
    {
        return static_cast<int>(index - separatorBias);
    }
    else // the paths only partly match
    {
        return static_cast<int>(index - separatorBias - accumulated.size());
    }
}

bool assignFilesystemIds(Sync& sync, MegaApp& app, FileSystemAccess& fsaccess, handlelocalnode_map& fsidnodes,
                         LocalPath& localdebris)
{
    auto rootpath = sync.localroot->getLocalname();
    LOG_info << "Assigning fs IDs at rootpath: " << rootpath;

    auto fa = fsaccess.newfileaccess(false);
    if (!fa->fopen(rootpath, true, false, FSLogging::logOnError))
    {
        LOG_err << "Unable to open rootpath";
        return false;
    }
    if (fa->type != FOLDERNODE)
    {
        LOG_err << "rootpath not a folder";
        assert(false);
        return false;
    }
    if (fa->mIsSymLink)
    {
        LOG_err << "rootpath is a symlink";
        assert(false);
        return false;
    }
    fa.reset();

    bool success = true;

    FingerprintCache fingerprints;

    FingerprintLocalNodeMap localnodes;
    collectAllLocalNodes(fingerprints, localnodes, *sync.localroot, fsidnodes);
    LOG_info << "Number of localnodes: " << localnodes.size();

    if (localnodes.empty())
    {
        return success;
    }

    FingerprintFileMap files;
    collectAllFiles(success, fingerprints, files, sync, app, fsaccess, rootpath, localdebris);
    LOG_info << "Number of files: " << files.size();

    LOG_info << "Number of fingerprints: " << fingerprints.all().size();
    const auto assignmentCount = assignFilesystemIdsImpl(fingerprints, localnodes, files, fsidnodes, fsaccess);
    LOG_info << "Number of fsid assignments: " << assignmentCount;

    return success;
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
           const LocalPath& clocaldebris, Node* remotenode, bool cinshare, const string& logname)
: syncs(us.syncs)
, localroot(nullptr)
, mUnifiedSync(us)
, threadSafeState(new SyncThreadsafeState(us.mConfig.mBackupId, &syncs.mClient, us.mConfig.isBackup())) // assuming backups are only in Vault
{
    assert(cdebris.empty() || clocaldebris.empty());
    assert(!cdebris.empty() || !clocaldebris.empty());

    localroot.reset(new LocalNode(this));

    const SyncConfig& config = us.mConfig;

    isnetwork = false;
    client = &syncs.mClient;
    inshare = cinshare;
    tmpfa = NULL;
    syncname = logname; // can be updated to be more specific in logs
    initializing = true;
    updatedfilesize = ~0;
    updatedfilets = 0;
    updatedfileinitialts = 0;

    localbytes = 0;
    localnodes[FILENODE] = 0;
    localnodes[FOLDERNODE] = 0;

    state() = SYNC_INITIALSCAN;
    assert(mUnifiedSync.mConfig.mRunState == SyncRunState::Loading);

    fullscan = true;
    scanseqno = 0;

    mLocalPath = mUnifiedSync.mConfig.getLocalPath();


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

    mFilesystemType = syncs.fsaccess->getlocalfstype(mLocalPath);

    localroot->init(FOLDERNODE, NULL, mLocalPath, nullptr);  // the root node must have the absolute path.  We don't store shortname, to avoid accidentally using relative paths.
    localroot->setnode(remotenode);

    // notifications may be queueing from this moment
    dirnotify.reset(syncs.fsaccess->newdirnotify(mLocalPath, localdebris.leafName(), client->waiter.get(), localroot.get()));
    assert(dirnotify->sync == this);

    // order issue - localroot->init() couldn't do this until dirnotify is created but that needs
    dirnotify->addnotify(localroot.get(), mLocalPath);

    // set specified fsfp or get from fs if none
    const auto cfsfp = mUnifiedSync.mConfig.mFilesystemFingerprint;
    if (cfsfp.id)
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

    if (fas->fopen(mLocalPath, true, false, FSLogging::logOnError))
    {
        // get the fsid of the synced folder
        localroot->fsid = fas->fsid;

        // load LocalNodes from cache (only for internal syncs)
        // We are using SQLite in the no-mutex mode, so only access a database from a single thread.
        if (shouldHaveDatabase())
        {
            string dbname = config.getSyncDbStateCacheName(fas->fsid, config.mRemoteNode, syncs.mClient.me);

            // Check if the database exists on disk.
            us.mConfig.mDatabaseExists = syncs.mClient.dbaccess->probe(*syncs.fsaccess, dbname);

            // Note, we opened dbaccess in thread-safe mode
            statecachetable.reset(syncs.mClient.dbaccess->open(syncs.rng, *syncs.fsaccess, dbname, DB_OPEN_FLAG_RECYCLE, [this](DBError error)
            {
                client->handleDbError(error);
            }));

            // Did the call above create the database?
            us.mConfig.mDatabaseExists |= !!statecachetable;

            // Don't bother trying to read the cache if we couldn't open the database.
            if (us.mConfig.mDatabaseExists)
            {
                readstatecache();
            }
        }
    }
    us.mConfig.mRunState = SyncRunState::Run;
}

Sync::~Sync()
{
    // must be set to prevent remote mass deletion while rootlocal destructor runs
    mDestructorRunning = true;

    // unlock tmp lock
    tmpfa.reset();

    // stop all active and pending downloads
    if (localroot->node)
    {
        TreeProcDelSyncGet tdsg;
        // Create a committer to ensure we update the transfer database in an efficient single commit,
        // if there are transactions in progress.
        TransferDbCommitter committer(client->tctable);
        client->proctree(localroot->node, &tdsg);
    }

    // Close the database so that deleting localnodes will not remove them
    statecachetable.reset();

    client->syncactivity = true;

    {
        // Create a committer and recursively delete all the associated LocalNodes, and their associated transfer and file objects.
        // If any have transactions in progress, the committer will ensure we update the transfer database in an efficient single commit.
        TransferDbCommitter committer(client->tctable);
        localroot.reset();
    }
}

void Sync::backupModified()
{
    changestate(SYNC_DISABLED, BACKUP_MODIFIED, false, true, false);
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

    LOG_verbose << "Sync "
                << toHandle(config.mBackupId)
                << " transitioning to monitoring mode.";

    config.setBackupState(SYNC_BACKUP_MONITOR);

    assert(client);

    client->syncs.saveSyncConfig(config);
}

bool Sync::shouldHaveDatabase() const
{
    return syncs.mClient.dbaccess && !mUnifiedSync.mConfig.isExternal();
}

bool Sync::active() const
{
    return getConfig().mRunningState >= SYNC_INITIALSCAN;
}

void Sync::addstatecachechildren(uint32_t parent_dbid, idlocalnode_map* tmap, LocalPath& localpath, LocalNode *p, int maxdepth)
{
    assert(syncs.onSyncThread());

    auto range = tmap->equal_range(parent_dbid);

    // remove processed elements as we go, so we can then clean the database at the end.
    for (auto it = range.first; it != tmap->end() && it->first == parent_dbid; it = tmap->erase(it))
    {
        LocalNode* const l = it->second;

        auto preExisting = p->children.find(l->getLocalname());
        if (preExisting != p->children.end())
        {
            // tidying up from prior versions of the SDK which might have duplicate LocalNodes
            LOG_debug << "Removing duplicate LocalNode: " << preExisting->second->debugGetParentList();
            delete preExisting->second;   // also detaches and preps removal from db
            assert(p->children.find(l->getLocalname()) == p->children.end());
            // l will be added in its place.  Later entries were the ones used by the old algorithm
        }

        ScopedLengthRestore restoreLen(localpath);

        localpath.appendWithSeparator(l->getLocalname(), true);

        Node* node = l->node.release_unchecked();
        handle fsid = l->fsid;
        m_off_t size = l->size;

        // clear localname to force newnode = true in setnameparent
        l->setLocalname(LocalPath());

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

        l->init(l->type, p, localpath, std::move(shortname));

#ifdef DEBUG
        auto fa = syncs.fsaccess->newfileaccess(false);
        if (fa->fopen(localpath, FSLogging::logOnError))  // exists, is file
        {
            auto sn = syncs.fsaccess->fsShortname(localpath);
            assert(!l->getLocalname().empty() &&
                ((!l->slocalname && (!sn || l->getLocalname() == *sn)) ||
                (l->slocalname && sn && !l->slocalname->empty() && *l->slocalname != l->getLocalname() && *l->slocalname == *sn)));
        }
#endif

        l->parent_dbid = parent_dbid;
        l->size = size;
        l->setfsid(fsid, client->fsidnode);
        l->setnode(node);

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
    if (statecachetable && state() == SYNC_INITIALSCAN)
    {
        string cachedata;
        idlocalnode_map tmap;
        uint32_t cid;
        LocalNode* l;

        statecachetable->rewind();
        unsigned numLocalNodes = 0;

        // bulk-load cached nodes into tmap
        while (statecachetable->next(&cid, &cachedata, &client->key))
        {
            if ((l = LocalNode::unserialize(this, &cachedata)))
            {
                l->dbid = cid;
                tmap.insert(pair<int32_t,LocalNode*>(l->parent_dbid,l));
                numLocalNodes += 1;
            }
        }

        // recursively build LocalNode tree, set scanseqnos to sync's current scanseqno
        {
            DBTableTransactionCommitter committer(statecachetable);
            LocalPath pathBuffer = localroot->getLocalname(); // don't let localname be appended during recurse
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

        // trigger a single-pass full scan to identify deleted nodes
        fullscan = true;
        scanseqno++;
    }
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

    if (state() == SYNC_CANCELED)
    {
        return;
    }

    // Always queue the update even if we don't have a state cache.
    //
    // The reasoning here is that our integration tests regularly check the
    // size of these queues to determine whether a sync is or is not idle.
    //
    // The same reasoning applies to statecacheadd(...) below.
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

    if (state() == SYNC_CANCELED)
    {
        return;
    }

    if (l->dbid)
    {
        deleteq.erase(l->dbid);
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
    // Purge the queues if we have no state cache.
    if (!statecachetable)
    {
        deleteq.clear();
        insertq.clear();
        return;
    }

    if ((state() == SYNC_ACTIVE ||
        (state() == SYNC_INITIALSCAN && insertq.size() > 100)) && (deleteq.size() || insertq.size()))
    {
        LOG_debug << "Saving LocalNode database with " << insertq.size() << " additions and " << deleteq.size() << " deletions";
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
                if ((*it)->parent->dbid || (*it)->parent == localroot.get())
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

void Sync::changestate(syncstate_t newstate, SyncError newSyncError, bool newEnableFlag, bool notifyApp, bool keepSyncDb)
{
    mUnifiedSync.changeState(newstate, newSyncError, newEnableFlag, notifyApp, keepSyncDb);
}

void UnifiedSync::changeState(syncstate_t newstate, SyncError newSyncError, bool newEnableFlag, bool notifyApp, bool keepSyncDb)
{
    assert(syncs.onSyncThread());

    if (newSyncError != NO_SYNC_ERROR &&
        newSyncError != UNLOADING_SYNC)
    {
        // until sync rework, we only have active and disbled states.
        // keepSyncDb on error would be Suspended
        keepSyncDb = false;
    }

    // Transitioning to a 'stopped' state...
    if (newstate < SYNC_INITIALSCAN)
    {
        // Should "user-disable" external backups...
        newEnableFlag &= mConfig.isInternal();
    }

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

    if (newstate != mConfig.mRunningState)
    {
        mConfig.mRunningState = newstate;
    }

    mConfig.mError = newSyncError;
    mConfig.setEnabled(newEnableFlag);

    if (newSyncError || !newEnableFlag)
    {
        mConfig.mRunState = SyncRunState::Disable;
    }

    changedConfigState(!!syncs.mSyncConfigStore, notifyApp);
    mNextHeartbeat->updateSPHBStatus(*this);
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
        if (!localroot->getLocalname().isContainingPathOf(localpath, &subpathIndex))
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

bool Sync::assignfsids()
{
    return assignFilesystemIds(*this, *client->app, *syncs.fsaccess, client->fsidnode,
                               localdebris);
}

// scan localpath, add or update child nodes, call recursively for folder nodes
// localpath must be prefixed with Sync
bool Sync::scan(LocalPath localpath, FileAccess* fa)
{
    if (fa)
    {
        assert(fa->type == FOLDERNODE);
    }
    if (!localdebris.isContainingPathOf(localpath))
    {
        LocalPath localname;
        string name;
        bool success;

        if (SimpleLogger::logCurrentLevel >= logDebug)
        {
            LOG_debug << "Scanning folder: " << localpath;
        }

       auto da = syncs.fsaccess->newdiraccess();

        // scan the dir, mark all items with a unique identifier
        if ((success = da->dopen(&localpath, fa, false)))
        {
            while (da->dnext(localpath, localname, false))
            {
                name = localname.toName(*syncs.fsaccess);

                ScopedLengthRestore restoreLen(localpath);
                localpath.appendWithSeparator(localname, false);

                // check if this record is to be ignored
                if (client->app->sync_syncable(this, name.c_str(), localpath))
                {
                    // skip the sync's debris folder
                    if (!localdebris.isContainingPathOf(localpath))
                    {
                        LocalNode *l = NULL;
                        if (initializing)
                        {
                            // preload all cached LocalNodes
                            l = checkpath(NULL, &localpath, nullptr, nullptr, false, da.get());
                        }

                        if (!l || l == (LocalNode*)~0)
                        {
                            // new record: place in notification queue
                            dirnotify->notify(DirNotify::DIREVENTS, NULL, LocalPath(localpath), false, false);
                        }
                    }
                }
                else
                {
                    LOG_debug << "Excluded: " << name;
                }
            }
        }

        return success;
    }
    else return false;
}

// check local path - if !localname, localpath is relative to l, with l == NULL
// being the root of the sync
// if localname is set, localpath is absolute and localname its last component
// path references a new FOLDERNODE: returns created node
// path references a existing FILENODE: returns node
// otherwise, returns NULL
// empty input_localpath means to process l rather than a named subitem of l (for scan propagation purposes with folderNeedsRescan flag)
LocalNode* Sync::checkpath(LocalNode* l, LocalPath* input_localpath, string* const localname, dstime *backoffds, bool wejustcreatedthisfolder, DirAccess* iteratingDir)
{
    LocalNode* ll = l;
    bool newnode = false, changed = false;
    bool isroot;

    LocalNode* parent = nullptr;
    string path;           // UTF-8 representation of tmppath
    LocalPath tmppath;     // full path represented by l + localpath

    if (localname)
    {
        // shortcut case (from within syncdown())
        isroot = false;
        parent = l;
        l = NULL;

        path = input_localpath->toPath(true);
        assert(path.size());
    }
    else
    {
        // construct full filesystem path in tmppath
        if (l)
        {
            tmppath = l->getLocalPath();
        }

        if (!input_localpath->empty())
        {
            if (tmppath.empty())
            {
                tmppath = *input_localpath;
            }
            else
            {
                tmppath.appendWithSeparator(*input_localpath, false);
            }
        }

        string name = tmppath.leafName().toPath(true);
        path = tmppath.toPath(true);

        if (!client->app->sync_syncable(this, name.c_str(), tmppath))
        {
            LOG_debug << "Excluded: " << path;
            return NULL;
        }

        // look up deepest existing LocalNode by path, store remainder (if any) in newname

        LocalPath newname;     // portion of tmppath not covered by the existing
                               // LocalNode structure (always the last path component
                               // that does not have a corresponding LocalNode yet)

        LocalNode *tmp = localnodebypath(l, *input_localpath, &parent, &newname);
        size_t index = 0;

        if (newname.findNextSeparator(index))
        {
            LOG_warn << "Parent not detected yet. Remainder: " << newname;
            // when (if) the parent is created, we'll rescan the folder
            return NULL;
        }

        l = tmp;

        // path invalid?
        if ((!l && newname.empty()) || !path.size())
        {
            LOG_warn << "Invalid path: " << path;
            return NULL;
        }

        isroot = l == localroot.get() && newname.empty();
    }

    LOG_verbose << "Scanning: " << path << " in=" << initializing << " full=" << fullscan << " l=" << l;
    LocalPath* localpathNew = localname ? input_localpath : &tmppath;

    if (parent)
    {
        if (state() != SYNC_INITIALSCAN && !parent->node)
        {
            LOG_warn << "Parent doesn't exist yet: " << path;
            return (LocalNode*)~0;
        }
    }

    // attempt to open/type this file
    auto fa = syncs.fsaccess->newfileaccess(false);

    if (initializing || fullscan)
    {
        // find corresponding LocalNode by file-/foldername
        size_t lastpart = localpathNew->getLeafnameByteIndex();

        LocalPath fname(localpathNew->subpathFrom(lastpart));

        LocalNode* cl = (parent ? parent : localroot.get())->childbyname(&fname);
        if (initializing && cl)
        {
            // the file seems to be still in the folder
            // mark as present to prevent deletions if the file is not accesible
            // in that case, the file would be checked again after the initialization
            cl->deleted = false;
            cl->setnotseen(0);
            l->scanseqno = scanseqno;
        }

        // match cached LocalNode state during initial/rescan to prevent costly re-fingerprinting
        // (just compare the fsids, sizes and mtimes to detect changes)
        if (fa->fopen(*localpathNew, false, false, FSLogging::logOnError, iteratingDir))
        {
            if (cl && fa->fsidvalid && fa->fsid == cl->fsid)
            {
                // node found and same file
                l = cl;
                l->deleted = false;
                l->setnotseen(0);

                // if it's a file, size and mtime must match to qualify
                if (l->type != FILENODE || (l->size == fa->size && l->mtime == fa->mtime))
                {
                    LOG_verbose << "Cached localnode is still valid. Type: " << l->type << "  Size: " << l->size << "  Mtime: " << l->mtime << " fsid " << (fa->fsidvalid ? toHandle(fa->fsid) : "NO");
                    l->scanseqno = scanseqno;

                    if (l->type == FOLDERNODE)
                    {
                        scan(*localpathNew, fa.get());
                    }
                    else
                    {
                        localbytes += l->size;
                    }

                    l->needsRescan = false;

                    return l;
                }
            }
        }
        else
        {
            LOG_warn << "Error opening file during the initialization: " << path;
        }

        if (initializing)
        {
            if (cl)
            {
                LOG_verbose << "Outdated localnode. Type: " << cl->type << "  Size: " << cl->size << "  Mtime: " << cl->mtime
                            << "    FaType: " << fa->type << "  FaSize: " << fa->size << "  FaMtime: " << fa->mtime;
            }
            else
            {
                LOG_verbose << "New file. FaType: " << fa->type << "  FaSize: " << fa->size << "  FaMtime: " << fa->mtime;
            }
            return NULL;
        }

        fa = syncs.fsaccess->newfileaccess(false);
    }

    if (fa->fopen(*localpathNew, true, false, FSLogging::logOnError))
    {
        if (!isroot)
        {
            if (l)
            {
                if (l->type == fa->type)
                {
                    // mark as present
                    l->setnotseen(0);

                    if (fa->type == FILENODE)
                    {
                        // has the file been overwritten or changed since the last scan?
                        // or did the size or mtime change?
                        if (fa->fsidvalid)
                        {
                            // if fsid has changed, the file was overwritten
                            // (FIXME: handle type changes)
                            if (l->fsid != fa->fsid)
                            {
                                handlelocalnode_map::iterator it;
#ifdef _WIN32
                                const char *colon;
#endif
                                fsfp_t fp1, fp2;

                                // was the file overwritten by moving an existing file over it?
                                if ((it = client->fsidnode.find(fa->fsid)) != client->fsidnode.end()
                                        && (l->sync == it->second->sync
                                            || ((fp1 = syncs.fsaccess->fsFingerprint(l->sync->getConfig().mLocalPath))
                                                && (fp2 = syncs.fsaccess->fsFingerprint(it->second->sync->getConfig().mLocalPath))
                                                && (fp1 == fp2)
                                            #ifdef _WIN32
                                                // only consider fsid matches between different syncs for local drives with the
                                                // same drive letter, to prevent problems with cloned Volume IDs
                                                && (colon = strstr(parent->sync->localroot->name.c_str(), ":"))
                                                && !memcmp(parent->sync->localroot->name.c_str(),
                                                       it->second->sync->localroot->name.c_str(),
                                                       colon - parent->sync->localroot->name.c_str())
                                            #endif
                                                )
                                            )
                                    )
                                {
                                    // catch the not so unlikely case of a false fsid match due to
                                    // e.g. a file deletion/creation cycle that reuses the same inode
                                    if (it->second->mtime != fa->mtime || it->second->size != fa->size)
                                    {
                                        l->mtime = -1;  // trigger change detection
                                        delete it->second;   // delete old LocalNode
                                    }
                                    else
                                    {
                                        LOG_debug << "File move/overwrite detected";

                                        if (parent && !parent->node)
                                        {
                                            // we can't handle such a move yet, the target cloud node doesn't exist.
                                            // when it does, we'll rescan that node's local node (ie, this folder)
                                            LOG_debug << "File move/overwrite detected BUT can't be processed yet - waiting on parent's cloud node creation:" << parent->getLocalPath();
                                            return NULL;
                                        }
                                        else
                                        {
                                            // delete existing LocalNode...
                                            delete l;

                                            // ...move remote node out of the way...
                                            client->execsyncdeletions();

                                            // ...and atomically replace with moved one
                                            LOG_debug << "Sync - local rename/move " << it->second->getLocalPath() << " -> " << path;

                                            // (in case of a move, this synchronously updates l->parent and l->node->parent)
                                            it->second->setnameparent(parent, localpathNew, syncs.fsaccess->fsShortname(*localpathNew));

                                            // mark as seen / undo possible deletion
                                            it->second->setnotseen(0);

                                            statecacheadd(it->second);

                                            return it->second;
                                        }
                                    }
                                }
                                else
                                {
                                    l->mtime = -1;  // trigger change detection
                                }
                            }
                        }

                        // no fsid change detected or overwrite with unknown file:
                        if (fa->mtime != l->mtime || fa->size != l->size)
                        {
                            if (fa->fsidvalid && l->fsid != fa->fsid)
                            {
                                l->setfsid(fa->fsid, client->fsidnode);
                            }

                            m_off_t dsize = l->size > 0 ? l->size : 0;

                            if (l->genfingerprint(fa.get()) && l->size >= 0)
                            {
                                localbytes -= dsize - l->size;
                            }

                            LOG_debug << "Sync - local file change detected: " << path;

                            TransferDbCommitter committer(client->tctable);
                            client->stopxfer(l, &committer); // TODO:  can we use one committer for all the files in the folder?  Or for the whole recursion?
                            l->bumpnagleds();
                            l->deleted = false;

                            client->syncactivity = true;

                            statecacheadd(l);

                            fa.reset();

                            if (isnetwork && l->type == FILENODE)
                            {
                                LOG_debug << "Queueing extra fs notification for modified file";
                                dirnotify->notify(DirNotify::EXTRA, NULL, LocalPath(*localpathNew), false, false);
                            }
                            return l;
                        }
                    }
                    else
                    {
                        // (we tolerate overwritten folders, because we do a
                        // content scan anyway)
                        if (fa->fsidvalid && fa->fsid != l->fsid)
                        {
                            l->setfsid(fa->fsid, client->fsidnode);
                            newnode = true;
                        }
                    }
                }
                else
                {
                    LOG_debug << "node type changed: recreate";
                    delete l;
                    l = NULL;
                }
            }

            // new node
            if (!l)
            {
                // rename or move of existing node?
                handlelocalnode_map::iterator it;
#ifdef _WIN32
                const char *colon;
#endif
                fsfp_t fp1, fp2;
                if (fa->fsidvalid && (it = client->fsidnode.find(fa->fsid)) != client->fsidnode.end()
                    // additional checks to prevent wrong fsid matches
                    && it->second->type == fa->type
                    && (!parent
                        || (it->second->sync == parent->sync)
                        || ((fp1 = syncs.fsaccess->fsFingerprint(it->second->sync->getConfig().mLocalPath))
                            && (fp2 = syncs.fsaccess->fsFingerprint(parent->sync->getConfig().mLocalPath))
                            && (fp1 == fp2)
                        #ifdef _WIN32
                            // allow moves between different syncs only for local drives with the
                            // same drive letter, to prevent problems with cloned Volume IDs
                            && (colon = strstr(parent->sync->localroot->name.c_str(), ":"))
                            && !memcmp(parent->sync->localroot->name.c_str(),
                                   it->second->sync->localroot->name.c_str(),
                                   colon - parent->sync->localroot->name.c_str())
                        #endif
                            )
                       )
                    && ((it->second->type != FILENODE && !wejustcreatedthisfolder)
                        || (it->second->mtime == fa->mtime && it->second->size == fa->size)))
                {
                    LOG_debug << client->clientname << "Move detected by fsid " << toHandle(fa->fsid) << " in checkpath. Type: " << it->second->type << " new path: " << path << " old localnode: " << it->second->getLocalPath();

                    if (fa->type == FILENODE && backoffds)
                    {
                        // logic to detect files being updated in the local computer moving the original file
                        // to another location as a temporary backup

                        m_time_t currentsecs = m_time();
                        if (!updatedfileinitialts)
                        {
                            updatedfileinitialts = currentsecs;
                        }

                        if (currentsecs >= updatedfileinitialts)
                        {
                            if (currentsecs - updatedfileinitialts <= FILE_UPDATE_MAX_DELAY_SECS)
                            {
                                bool waitforupdate = false;
                                auto local = it->second->getLocalPath();
                                auto prevfa = syncs.fsaccess->newfileaccess(false);

                                bool exists = prevfa->fopen(local, FSLogging::logOnError);
                                if (exists)
                                {
                                    LOG_debug << "File detected in the origin of a move";

                                    if (currentsecs >= updatedfilets)
                                    {
                                        if ((currentsecs - updatedfilets) < (FILE_UPDATE_DELAY_DS / 10))
                                        {
                                            LOG_verbose << "currentsecs = " << currentsecs << "  lastcheck = " << updatedfilets
                                                      << "  currentsize = " << prevfa->size << "  lastsize = " << updatedfilesize;
                                            LOG_debug << "The file was checked too recently. Waiting...";
                                            waitforupdate = true;
                                        }
                                        else if (updatedfilesize != prevfa->size)
                                        {
                                            LOG_verbose << "currentsecs = " << currentsecs << "  lastcheck = " << updatedfilets
                                                      << "  currentsize = " << prevfa->size << "  lastsize = " << updatedfilesize;
                                            LOG_debug << "The file size has changed since the last check. Waiting...";
                                            updatedfilesize = prevfa->size;
                                            updatedfilets = currentsecs;
                                            waitforupdate = true;
                                        }
                                        else
                                        {
                                            LOG_debug << "The file size seems stable";
                                        }
                                    }
                                    else
                                    {
                                        LOG_warn << "File checked in the future";
                                    }

                                    if (!waitforupdate)
                                    {
                                        if (currentsecs >= prevfa->mtime)
                                        {
                                            if (currentsecs - prevfa->mtime < (FILE_UPDATE_DELAY_DS / 10))
                                            {
                                                LOG_verbose << "currentsecs = " << currentsecs << "  mtime = " << prevfa->mtime;
                                                LOG_debug << "File modified too recently. Waiting...";
                                                waitforupdate = true;
                                            }
                                            else
                                            {
                                                LOG_debug << "The modification time seems stable.";
                                            }
                                        }
                                        else
                                        {
                                            LOG_warn << "File modified in the future";
                                        }
                                    }
                                }
                                else
                                {
                                    if (prevfa->retry)
                                    {
                                        LOG_debug << "The file in the origin is temporarily blocked. Waiting...";
                                        waitforupdate = true;
                                    }
                                    else
                                    {
                                        LOG_debug << "There isn't anything in the origin path";
                                    }
                                }

                                if (waitforupdate)
                                {
                                    LOG_debug << "Possible file update detected.";
                                    *backoffds = FILE_UPDATE_DELAY_DS;
                                    return NULL;
                                }
                            }
                            else
                            {
                                int creqtag = client->reqtag;
                                client->reqtag = 0;
                                client->sendevent(99438, "Timeout waiting for file update");
                                client->reqtag = creqtag;
                            }
                        }
                        else
                        {
                            LOG_warn << "File check started in the future";
                        }
                    }

                    LOG_debug << "Sync - local rename/move " << it->second->getLocalPath() << " -> " << path.c_str();

                    if (parent && !parent->node)
                    {
                        // we can't handle such a move yet, the target cloud node doesn't exist.
                        // when it does, we'll rescan that node's local node (ie, this folder)
                        LOG_debug << "Move or rename of existing node detected BUT can't be processed yet - waiting on parent's cloud node creation: " << parent->getLocalPath();
                        return NULL;
                    }
                    else
                    {
                        // (in case of a move, this synchronously updates l->parent
                        // and l->node->parent)
                        it->second->setnameparent(parent, localpathNew, syncs.fsaccess->fsShortname(*localpathNew));
                    }

                    // make sure that active PUTs receive their updated filenames
                    client->updateputs();

                    statecacheadd(it->second);

                    // unmark possible deletion
                    it->second->setnotseen(0);

                    if (fa->type == FOLDERNODE)
                    {
                        // mark this and folders below to be rescanned
                        it->second->setSubtreeNeedsRescan(fullscan);

                        if (fullscan)
                        {
                            // immediately scan folder to detect deviations from cached state
                            scan(*localpathNew, fa.get());

                            // consider this folder scanned.
                            it->second->needsRescan = false;
                        }
                        else
                        {
                            // queue this one to be scanned, recursion is by notify of subdirs
                            dirnotify->notify(DirNotify::DIREVENTS, it->second, LocalPath(), true, false);
                        }
                    }
                }
                else if (fa->mIsSymLink)
                {
                    LOG_debug << "checked path is a symlink.  Parent: " << (parent ? parent->name : "NO");
                    //doing nothing for the moment
                }
                else
                {
                    // this is a new node: add
                    LOG_debug << "New localnode.  Parent: " << (parent ? parent->name : "NO") << " fsid " << (fa->fsidvalid ? toHandle(fa->fsid) : "NO");
                    l = new LocalNode(this);
                    l->init(fa->type, parent, *localpathNew, syncs.fsaccess->fsShortname(*localpathNew));

                    if (fa->fsidvalid)
                    {
                        l->setfsid(fa->fsid, client->fsidnode);
                    }

                    newnode = true;
                }
            }
        }

        if (l)
        {
            // detect file changes or recurse into new subfolders
            if (l->type == FOLDERNODE)
            {
                if (newnode || l->needsRescan)
                {
                    scan(*localpathNew, fa.get());
                    l->needsRescan = false;

                    if (newnode)
                    {
                        LOG_debug << "Sync - local folder addition detected: " << path;

                        if (!isroot)
                        {
                            statecacheadd(l);
                        }
                    }
                }
                else
                {
                    l = NULL;
                }
            }
            else
            {
                if (isroot)
                {
                    // root node cannot be a file
                    LOG_err << "The local root node is a file";
                    changestate(SYNC_FAILED, INVALID_LOCAL_TYPE, false, true, false);
                }
                else
                {
                    if (fa->fsidvalid && l->fsid != fa->fsid)
                    {
                        l->setfsid(fa->fsid, client->fsidnode);
                    }

                    if (l->size > 0)
                    {
                        localbytes -= l->size;
                    }

                    if (l->genfingerprint(fa.get()))
                    {
                        changed = true;
                        l->bumpnagleds();
                        l->deleted = false;
                    }

                    if (l->size > 0)
                    {
                        localbytes += l->size;
                    }

                    if (newnode)
                    {
                        LOG_debug << "Sync - local file addition detected: " << path;
                    }
                    else if (changed)
                    {
                        LOG_debug << "Sync - local file change detected: " << path;
                        TransferDbCommitter committer(client->tctable); // TODO:  can we use one committer for all the files in the folder?  Or for the whole recursion?
                        client->stopxfer(l, &committer);
                    }

                    l->needsRescan = false;

                    if (newnode || changed)
                    {
                        statecacheadd(l);
                    }
                }
            }
        }

        if (changed || newnode)
        {
            if (isnetwork && l->type == FILENODE)
            {
                LOG_debug << "Queueing extra fs notification for new file";
                dirnotify->notify(DirNotify::EXTRA, NULL, LocalPath(*localpathNew), false, false);
            }

            client->syncactivity = true;
        }
    }
    else
    {
        LOG_warn << "Error opening file";
        if (fa->retry)
        {
            // fopen() signals that the failure is potentially transient - do
            // nothing and request a recheck
            LOG_warn << "File blocked. Adding notification to the retry queue: " << path;
            dirnotify->notify(DirNotify::RETRY, ll, LocalPath(*input_localpath), false, false);
            client->syncfslockretry = true;
            client->syncfslockretrybt.backoff(SCANNING_DELAY_DS);
            client->blockedfile = *localpathNew;
        }
        else if (l)
        {
            // immediately stop outgoing transfer, if any
            if (l->transfer)
            {
                TransferDbCommitter committer(client->tctable); // TODO:  can we use one committer for all the files in the folder?  Or for the whole recursion?
                client->stopxfer(l, &committer);
            }

            client->syncactivity = true;

            // in fullscan mode, missing files are handled in bulk in deletemissing()
            // rather than through setnotseen()
            if (!fullscan)
            {
                l->setnotseen(1);
            }
        }

        l = NULL;
    }

    return l;
}

bool Sync::checkValidNotification(int q, Notification& notification)
{
    // This code moved from filtering before going on notifyq, to filtering after when it's thread-safe to do so

    if (q == DirNotify::DIREVENTS || q == DirNotify::EXTRA)
    {
        Notification next;
        while (dirnotify->notifyq[q].peekFront(next)
            && next.localnode == notification.localnode && next.path == notification.path)
        {
            dirnotify->notifyq[q].popFront(next);  // this is the only thread removing from the queue so it will be the same item
            if (!notification.timestamp || !next.timestamp)
            {
                notification.timestamp = 0;  // immediate
            }
            else
            {
                notification.timestamp = std::max(notification.timestamp, next.timestamp);
            }
            LOG_debug << "Next notification repeats, skipping duplicate";
        }
    }

    if (notification.timestamp && !initializing && q == DirNotify::DIREVENTS)
    {
        LocalPath tmppath;

        if (auto* node = notification.localnode)
        {
            if (node == (LocalNode*)~0) return false;

            tmppath = node->getLocalPath();
            tmppath.appendWithSeparator(notification.path, false);
        }
        else
        {
            tmppath = notification.path;
        }

        attr_map::iterator ait;
        auto fa = syncs.fsaccess->newfileaccess(false);
        bool success = fa->fopen(tmppath, false, false, FSLogging::logOnError);
        LocalNode *ll = localnodebypath(notification.localnode, notification.path);
        auto deleted = !ll && !success && !fa->retry;

        if (deleted
            || (ll && success && ll->node && ll->node->localnode == ll
                && !(notification.recursive | ll->needsRescan)
                && (ll->type != FILENODE || (*(FileFingerprint *)ll) == (*(FileFingerprint *)ll->node))
                && (ait = ll->node->attrs.map.find('n')) != ll->node->attrs.map.end()
                && ait->second == ll->name
                && fa->fsidvalid && fa->fsid == ll->fsid && fa->type == ll->type
                && (ll->type != FILENODE || (ll->mtime == fa->mtime && ll->size == fa->size))))
        {
            LOG_debug << "Self filesystem notification skipped";
            return false;
        }
    }
    return true;
}

// add or refresh local filesystem item from scan stack, add items to scan stack
// returns 0 if a parent node is missing, ~0 if control should be yielded, or the time
// until a retry should be made (500 ms minimum latency).
dstime Sync::procscanq(int q)
{
    dstime dsmin = Waiter::ds - SCANNING_DELAY_DS;
    LocalNode* l;

    Notification notification;
    while (dirnotify->notifyq[q].popFront(notification))
    {
        if (!checkValidNotification(q, notification))
        {
            continue;
        }

        LOG_verbose << "Scanning... Remaining files: " << dirnotify->notifyq[q].size();

        if (notification.timestamp > dsmin)
        {
            LOG_verbose << syncname << "Scanning postponed. Modification too recent: " << notification.timestamp << " (ds) vs now " << Waiter::ds << " at " << notification.path;
            dirnotify->notifyq[q].unpopFront(notification);
            return notification.timestamp - dsmin;
        }

#ifdef DEBUG
        client->app->syncdebug_notification(getConfig(), q, notification);
#endif // DEBUG

        if ((l = notification.localnode) != (LocalNode*)~0)
        {
            dstime backoffds = 0;
            LOG_verbose << "Checkpath: " << notification.path;

            // Are we dealing with a recursive notification?
            if (notification.recursive)
            {
                auto remainder = LocalPath();

                // For purposes of debugging.
                {
                    auto path = notification.localnode->getLocalPath();

                    path.appendWithSeparator(notification.path, false);

                    LOG_debug << "Recursive notification queued for: "
                              << path;
                }

                // Then make sure we scan everyone below the notified node.
                auto node = localnodebypath(notification.localnode,
                                            notification.path,
                                            nullptr,
                                            &remainder);

                // Only mark the precise subtree.
                if (node && remainder.empty())
                {
                    LOG_debug << "Recursive scan queued for nodes below: "
                              << node->getLocalPath();

                    node->setSubtreeNeedsRescan(true);
                }
            }

            l = checkpath(l, &notification.path, NULL, &backoffds, false, nullptr);
            if (backoffds)
            {
                LOG_verbose << "Scanning deferred during " << backoffds << " ds";
                notification.timestamp = Waiter::ds + backoffds - SCANNING_DELAY_DS;
                dirnotify->notifyq[q].unpopFront(notification);
                return backoffds;
            }
            updatedfilesize = ~0;
            updatedfilets = 0;
            updatedfileinitialts = 0;

            // defer processing because of a missing parent node?
            if (l == (LocalNode*)~0)
            {
                LOG_verbose << "Scanning deferred";
                dirnotify->notifyq[q].unpopFront(notification);
                return 0;
            }
        }
        else
        {
            LOG_debug << "Notification skipped: " << notification.path;
        }

        // we return control to the application in case a filenode was added
        // (in order to avoid lengthy blocking episodes due to multiple
        // consecutive fingerprint calculations)
        // or if new nodes are being added due to a copy/delete operation
        if ((l && l != (LocalNode*)~0 && l->type == FILENODE) || client->syncadding)
        {
            break;
        }
    }

    if (dirnotify->notifyq[q].empty())
    {
        if (q == DirNotify::DIREVENTS)
        {
            client->syncactivity = true;
        }
    }
    else if (dirnotify->notifyq[!q].empty())
    {
        cachenodes();
    }

    return dstime(~0);
}

// delete all child LocalNodes that have been missing for two consecutive scans (*l must still exist)
void Sync::deletemissing(LocalNode* l)
{
    std::unique_ptr<FileAccess> fa;
    for (localnode_map::iterator it = l->children.begin(); it != l->children.end(); )
    {
        if (scanseqno-it->second->scanseqno > 1)
        {
            if (!fa)
            {
                fa = syncs.fsaccess->newfileaccess();
            }
            client->unlinkifexists(it->second, fa.get());
            delete it++->second;
        }
        else
        {
            deletemissing(it->second);
            it++;
        }
    }
}

bool Sync::updateSyncRemoteLocation(Node* n, bool forceCallback)
{
    return mUnifiedSync.updateSyncRemoteLocation(n, forceCallback);
}

bool Sync::movetolocaldebris(const LocalPath& localpath)
{
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
            LOG_verbose << "Creating local debris folder";
            syncs.fsaccess->mkdirlocal(localdebris, true, false);
        }

        snprintf(buf, sizeof(buf), "%04d-%02d-%02d", ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday);

        if (i >= 0)
        {
            char* ptr = strchr(buf, 0);
            snprintf(ptr, sizeof(buf) - (ptr - buf), " %02d.%02d.%02d.%02d", ptm->tm_hour,  ptm->tm_min, ptm->tm_sec, i);
        }

        day = buf;
        localdebris.appendWithSeparator(LocalPath::fromRelativePath(day), true);

        if (i > -3)
        {
            LOG_verbose << "Creating daily local debris folder";
            havedir = syncs.fsaccess->mkdirlocal(localdebris, false, false) || syncs.fsaccess->target_exists;
        }

        localdebris.appendWithSeparator(localpath.subpathFrom(localpath.getLeafnameByteIndex()), true);

        syncs.fsaccess->skip_targetexists_errorreport = i == -3;  // we expect a problem on the first one when the debris folders or debris day folders don't exist yet
        if (syncs.fsaccess->renamelocal(localpath, localdebris, false))
        {
            syncs.fsaccess->skip_targetexists_errorreport = false;
            return true;
        }
        syncs.fsaccess->skip_targetexists_errorreport = false;

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
                if (sfg->sync == this)
                {
                    progressSum += tslot->progressreported;
                }
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

void Syncs::enableSyncByBackupId(handle backupId, bool paused, bool resetFingerprint, bool notifyApp, bool setOriginalPath, std::function<void(error, SyncError, handle)> completion, bool completionInClient, const string& logname)
{
    //assert(!onSyncThread());

    auto clientCompletion = [=](error e, SyncError se, handle)
        {
            queueClient([completion, e, se, backupId](MegaClient&, TransferDbCommitter&)
                {
                    if (completion) completion(e, se, backupId);
                });
        };

    queueSync([=]()
        {
            enableSyncByBackupId_inThread(backupId, paused, resetFingerprint, notifyApp, setOriginalPath, completionInClient ? clientCompletion : completion, logname);
        });
}

void Syncs::enableSyncByBackupId_inThread(handle backupId, bool paused, bool resetFingerprint, bool notifyApp, bool setOriginalPath, std::function<void(error, SyncError, handle)> completion, const string& logname, const string& excludedPath)
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
        // this is where sync rework will pause/unpause the sync (according to the supplied paused flag)

        if (completion) completion(API_OK, NO_SYNC_ERROR, backupId);
        return;
    }
    us.mConfig.mError = NO_SYNC_ERROR;

    if (resetFingerprint)
    {
        us.mConfig.mFilesystemFingerprint = 0; //This will cause the local filesystem fingerprint to be recalculated
    }

    LocalPath rootpath;
    std::unique_ptr<FileAccess> openedLocalFolder;
    bool inshare, isnetwork;
    error e = mClient.checkSyncConfig(us.mConfig, rootpath, openedLocalFolder, inshare, isnetwork);

    if (e)
    {
        // error and enable flag were already changed
        LOG_debug << "Enablesync checks resulted in error: " << e;

        us.mConfig.mRunState = SyncRunState::Disable;

        us.changedConfigState(true, notifyApp);
        if (completion) completion(e, us.mConfig.mError, backupId);
        return;
    }

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
            us.mConfig.mBackupState = SYNC_BACKUP_MIRROR;
        }
    }

    string debris = DEBRISFOLDER;
    auto localdebris = LocalPath();

    startSync_inThread(us, debris, localdebris, inshare, isnetwork, rootpath, completion, openedLocalFolder, logname, notifyApp);
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

        if (mConfig.mRemoteNode != n->nodehandle)
        {
            mConfig.mRemoteNode = NodeHandle().set6byte(n->nodehandle);
            changed = true;
        }
    }
    else //unset remote node: failed!
    {
        if (!mConfig.mRemoteNode.isUndef())
        {
            mConfig.mRemoteNode = NodeHandle();
            changed = true;
        }
    }

    if (changed || forceCallback)
    {
        syncs.mClient.app->syncupdate_remote_root_changed(mConfig);
    }

    //persist
    syncs.saveSyncConfig(mConfig);

    return changed;
}



void Syncs::startSync_inThread(UnifiedSync& us, const string& debris, const LocalPath& localdebris,
    bool inshare, bool isNetwork, const LocalPath& rootpath,
    std::function<void(error, SyncError, handle)> completion, std::unique_ptr<FileAccess>& openedLocalFolder, const string& logname, bool notifyApp)
{
    assert(!us.mSync);

    auto prevFingerprint = us.mConfig.mFilesystemFingerprint;

    auto fail = [&us, &completion](Error e, SyncError se, bool newEnableFlag) -> void {
        us.changeState(SYNC_FAILED, se, newEnableFlag, true, true);
        us.mSync.reset();
        LOG_debug << "Final error for sync start: " << e;
        if (completion) completion(e, us.mConfig.mError, us.mConfig.mBackupId);
    };

    Node* remotenode = mClient.nodeByHandle(us.mConfig.mRemoteNode);
    if (!remotenode)
    {
        return fail(API_EEXIST, REMOTE_NODE_NOT_FOUND, false);
    }

    us.mConfig.mRunState = SyncRunState::Loading;
    us.changedConfigState(false, true);

    us.mSync.reset(new Sync(us, debris, localdebris, remotenode, inshare, logname));
    us.mConfig.mFilesystemFingerprint = us.mSync->fsfp;
    debugLogHeapUsage();


    // this was already set in the Sync constructor
    assert(us.mConfig.mRunState == SyncRunState::Run);

    us.changedConfigState(false, true);

    if (prevFingerprint && prevFingerprint != us.mConfig.mFilesystemFingerprint)
    {
        LOG_err << "New sync local fingerprint mismatch. Previous: " << prevFingerprint.id
            << "  Current: " << us.mConfig.mFilesystemFingerprint.id;

        return fail(API_EEXIST, LOCAL_FILESYSTEM_MISMATCH, false);
    }

    if (!us.mSync->fsstableids)
    {
        if (us.mSync->assignfsids())
        {
            LOG_info << "Successfully assigned fs IDs for filesystem with unstable IDs";
        }
        else
        {
            LOG_warn << "Failed to assign some fs IDs for filesystem with unstable IDs";
        }
    }

    LOG_debug << "Initial scan sync: " << us.mConfig.getLocalPath();

    if (us.mSync->scan(rootpath, openedLocalFolder.get()))
    {
        mClient.syncsup = false;
        us.mSync->initializing = false;
        LOG_debug << "Initial scan finished. New / modified files: " << us.mSync->dirnotify->notifyq[DirNotify::DIREVENTS].size();

        // Sync constructor now receives the syncConfig as reference, to be able to write -at least- fingerprints for new syncs
        saveSyncConfig(us.mConfig);
    }
    else
    {
        LOG_err << "Initial scan failed";
        return fail(API_EFAILED, INITIAL_SCAN_FAILED, us.mConfig.getEnabled());
    }

    us.mSync->isnetwork = isNetwork;

    mClient.syncactivity = true;
    mHeartBeatMonitor->updateOrRegisterSync(us);

    if (completion) completion(API_OK, us.mConfig.mError, us.mConfig.mBackupId);
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

        if (notifyApp && !mConfig.mRemovingSyncBySds)
        {
            assert(syncs.onSyncThread());
            syncs.mClient.app->syncupdate_stateconfig(mConfig);
        }
        syncs.mClient.abortbackoff(false);
    }
}

Syncs::Syncs(MegaClient& mc, unique_ptr<FileSystemAccess>& fsa)
  : mClient(mc)
  , fsaccess(fsa)  // reference to MegaClient's for now, for linux we need it that way ofr notifications.  In sync rework, this will be a separate instance
{
    fsaccess->initFilesystemNotificationSystem();

    mHeartBeatMonitor.reset(new BackupMonitor(*this));
}

Syncs::~Syncs()
{
}

void Syncs::syncRun(std::function<void()> f)
{
    // todo: enable this assert when we merge sync rework
    // assert(!onSyncThread());

    // when sync rework gets merged, this function will be run off a queue on the sync thread
    f();

    // and we will wait for it to finish, and return synchronously
}

void Syncs::queueSync(std::function<void()>&& f)
{
    //assert(!onSyncThread());

    // when sync rework gets merged, this function will be run (asynchronously) off a queue on the sync thread
    f();
}

void Syncs::queueClient(std::function<void(MegaClient&, TransferDbCommitter&)>&& f, bool fromAnyThread)
{
    assert(onSyncThread() || fromAnyThread);

    // when sync rework gets merged, this function will be run (asynchronously) off a queue in MegaClient::exec()
    TransferDbCommitter committer(mClient.tctable);
    f(mClient, committer);
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

SyncConfigVector Syncs::getConfigs(bool onlyActive) const
{
    assert(onSyncThread() || !onSyncThread());

    lock_guard<mutex> g(mSyncVecMutex);

    SyncConfigVector v;
    for (auto& s : mSyncVec)
    {
        if (s->mSync || !onlyActive)
        {
            v.push_back(s->mConfig);
        }
    }
    return v;
}

bool Syncs::configById(handle backupId, SyncConfig& configResult) const
{
    //assert(!onSyncThread());

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

void Syncs::backupCloseDrive(const LocalPath& drivePath, std::function<void(Error)> clientCallback)
{
    //assert(!onSyncThread());
    assert(clientCallback);

    queueSync([this, drivePath, clientCallback]()
        {
            Error e = backupCloseDrive_inThread(drivePath);
            queueClient([clientCallback, e](MegaClient& mc, TransferDbCommitter& committer)
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
    assert(clientCallback);

    queueSync([this, drivePath, clientCallback]()
        {
            Error e = backupOpenDrive_inThread(drivePath);
            queueClient([clientCallback, e](MegaClient& mc, TransferDbCommitter& committer)
                {
                    clientCallback(e);
                });
        });
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

error Syncs::syncConfigStoreAdd(const SyncConfig& config)
{
    //assert(!onSyncThread());

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
                                  true,
                                  SYNC_CONFIG_WRITE_FAILURE,
                                  false,
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
                if (fas->fopen(c.mLocalPath, true, false, FSLogging::logOnError))
                {
                    string dbname = c.getSyncDbStateCacheName(fas->fsid, c.mRemoteNode, mClient.me);

                    // Note, we opened dbaccess in thread-safe mode
                    LocalPath dbPath;
                    c.mDatabaseExists = mClient.dbaccess->checkDbFileAndAdjustLegacy(*fsaccess, dbname, DB_OPEN_FLAG_TRANSACTED, dbPath);
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
    if (context->mDeviceHash.empty())
    {
        LOG_err << "Failed to get Device ID while importing sync configs";
        completion(API_EARGS);
        return;
    }
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
                 << config.mLocalPath;
        return;
    }

    string localPath = config.mLocalPath.toPath(false);
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

    mSyncsLoaded = false;
    mSyncsResumed = false;
}

void Syncs::appendNewSync(const SyncConfig& c, bool startSync, bool notifyApp, std::function<void(error, SyncError, handle)> completion, bool completionInClient, const string& logname, const string& excludedPath)
{
    //assert(!onSyncThread());
    assert(c.mBackupId != UNDEF);

    auto clientCompletion = [this, completion](error e, SyncError se, handle backupId)
    {
        queueClient([e, se, backupId, completion](MegaClient& mc, TransferDbCommitter& committer)
            {
                if (completion) completion(e, se, backupId);
            });
    };

    queueSync([=]()
    {
        appendNewSync_inThread(c, startSync, notifyApp, completionInClient ? clientCompletion : completion, logname, excludedPath);
    });
}

void Syncs::appendNewSync_inThread(const SyncConfig& c, bool startSync, bool notifyApp, std::function<void(error, SyncError, handle)> completion, const string& logname, const string& excludedPath)
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

    enableSyncByBackupId_inThread(c.mBackupId, false, false, notifyApp, true, completion, logname, excludedPath);
}

Sync* Syncs::runningSyncByBackupIdForTests(handle backupId) const
{
    // returning a Sync* is not really thread safe but the tests are using these directly currently.  So long as they only browse the Sync while nothing changes, it should be ok

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

void Syncs::forEachUnifiedSync(std::function<void(UnifiedSync&)> f)
{
    for (auto& s : mSyncVec)
    {
        f(*s);
    }
}

void Syncs::transferPauseFlagsUpdated(bool downloadsPaused, bool uploadsPaused)
{
    lock_guard<mutex> g(mSyncVecMutex);

    mDownloadsPaused = downloadsPaused;
    mUploadsPaused = uploadsPaused;

    for (auto& us : mSyncVec)
    {
        mHeartBeatMonitor->updateOrRegisterSync(*us);
    }
}

void Syncs::forEachRunningSync(std::function<void(Sync* s)> f)
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
            if (s->mSync->localroot->node &&
                node->isbelow(s->mSync->localroot->node))
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
            unifiedSync->mSync->state() == SYNC_CANCELED ||
            unifiedSync->mSync->state() == SYNC_FAILED ||
            unifiedSync->mSync->state() == SYNC_DISABLED))
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

void Syncs::renameSync(handle backupId, const string& newname, std::function<void(Error e)> completion)
{
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

void Syncs::disableSyncs(bool disableIsFail, SyncError syncError, bool newEnabledFlag, std::function<void(size_t)> callerCompletion)
{
    SyncConfigVector v = getConfigs(false);

    int nEnabled = 0;
    for (auto& c : v)
    {
        if (c.getEnabled()) ++nEnabled;
    }

    auto countdown = nEnabled;
    for (auto& c : v)
    {
        if (c.getEnabled())
        {

            std::function<void()> completion = nullptr;
            if (!--countdown)
            {
                completion = [=](){
                    LOG_info << "Disabled syncs. error = " << syncError;
                    mClient.app->syncs_disabled(syncError);
                };
            }

            disableSyncByBackupId(c.mBackupId, disableIsFail, syncError, newEnabledFlag, completion);
        }
    }

    if (callerCompletion) callerCompletion(nEnabled);
}

void Syncs::disableSyncByBackupId(handle backupId, bool disableIsFail, SyncError syncError, bool newEnabledFlag, std::function<void()> completion)
{
    //assert(!onSyncThread());
    queueSync([this, backupId, syncError, newEnabledFlag, disableIsFail, completion]()
    {
            disableSyncByBackupId_inThread(backupId, disableIsFail, syncError, newEnabledFlag, completion);
    });
}

void Syncs::disableSyncByBackupId_inThread(handle backupId, bool disableIsFail, SyncError syncError, bool newEnabledFlag, std::function<void()> completion)
{
    assert(onSyncThread());

    for (auto i = mSyncVec.size(); i--; )
    {
        auto& us = *mSyncVec[i];
        auto& config = us.mConfig;

        if (config.mBackupId == backupId)
        {
            us.changeState(disableIsFail ? SYNC_FAILED : SYNC_DISABLED, syncError, newEnabledFlag, true, newEnabledFlag); //This will cause the later deletion of Sync (not MegaSyncPrivate) object

            mHeartBeatMonitor->updateOrRegisterSync(us);
        }
    }
    if (completion) completion();
}

SyncConfigVector Syncs::selectedSyncConfigs(std::function<bool(SyncConfig&, Sync*)> selector) const
{
    SyncConfigVector selected;

    lock_guard<mutex> g(mSyncVecMutex);

    for (size_t i = 0; i < mSyncVec.size(); ++i)
    {
        if (selector(mSyncVec[i]->mConfig, mSyncVec[i]->mSync.get()))
        {
            selected.emplace_back(mSyncVec[i]->mConfig);
        }
    }

    return selected;
}

void Syncs::deregisterThenRemoveSync(handle backupId, std::function<void(Error)> completion, bool removingSyncBySds)
{
    //assert(!onSyncThread());

    // Try and deregister this sync's backup ID first.
    // If later removal operations fail, the heartbeat record will be resurrected

    LOG_debug << "Deregistering backup ID: " << toHandle(backupId);

    {
        // since we are only setting flags, we can actually do this off-thread
        // (but using mSyncVecMutex and hidden inside Syncs class)
        lock_guard<mutex> g(mSyncVecMutex);
        for (size_t i = 0; i < mSyncVec.size(); ++i)
        {
            auto& config = mSyncVec[i]->mConfig;
            if (config.mBackupId == backupId)
            {
                // prevent any sp or sphb messages being queued after
                config.mSyncDeregisterSent = true;

                // Prevent notifying the client app for this sync's state changes
                config.mRemovingSyncBySds = removingSyncBySds;
            }
        }
    }

    // use queueClient since we are not certain to be locked on client thread
    queueClient([backupId, completion, this](MegaClient& mc, TransferDbCommitter&){

        mc.reqs.add(new CommandBackupRemove(&mc, backupId,
                [backupId, completion, this](Error e){
                    if (e)
                    {
                        // de-registering is not critical - we continue anyway
                        LOG_warn << "API error deregisterig sync " << toHandle(backupId) << ":" << e;
                    }

                    queueSync([=](){ removeSyncAfterDeregistration_inThread(backupId, std::move(completion)); });
                }));
    }, true);

}

void Syncs::removeSyncAfterDeregistration_inThread(handle backupId, std::function<void(Error)> clientCompletion)
{
    assert(onSyncThread());

    Error e = API_OK;
    SyncConfig configCopy;
    if (unloadSyncByBackupID(backupId, false, configCopy))
    {
        mClient.app->sync_removed(configCopy);
        mSyncConfigStore->markDriveDirty(configCopy.mExternalDrivePath);
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
            configCopy = mSyncVec[i]->mConfig;

            if (auto& syncPtr = mSyncVec[i]->mSync)
            {
                // if it was running, the app gets a callback saying it's no longer active
                // SYNC_CANCELED is a special value that means we are shutting it down without changing config
                syncPtr->changestate(SYNC_CANCELED, UNLOADING_SYNC, newEnabledFlag, false, true);
                assert(!syncPtr->statecachetable);
                syncPtr.reset(); // deletes sync
            }

            // the sync config is not affected by this operation; it should already be up to date on disk (or be pending)
            // we don't call sync_removed back since the sync is not deleted
            // we don't unregister from the backup/sync heartbeats as the sync can be resumed later

            lock_guard<mutex> g(mSyncVecMutex);
            mSyncVec.erase(mSyncVec.begin() + i);
            mSyncVecIsEmpty = mSyncVec.empty();
            return true;
        }
    }

    return false;
}

void Syncs::prepareForLogout(bool keepSyncsConfigFile, std::function<void()> clientCompletion)
{
    queueSync([=](){ prepareForLogout_inThread(keepSyncsConfigFile, clientCompletion); });
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
                disableSyncByBackupId_inThread(us->mConfig.mBackupId, true, LOGGED_OUT, false, nullptr);
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
            queueClient([backupId, onFinalDeregister](MegaClient& mc, TransferDbCommitter& tc){
                mc.reqs.add(new CommandBackupRemove(&mc, backupId, [onFinalDeregister](Error){
                    if (onFinalDeregister) onFinalDeregister();
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
    //assert(!onSyncThread());
    syncRun([=](){ locallogout_inThread(removecaches, keepSyncsConfigFile, reopenStoreAfter); });
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

    clear_inThread();
    mExecutingLocallogout = false;

    if (reopenStoreAfter)
    {
        SyncConfigVector configs;
        syncConfigStoreLoad(configs);
    }
}

void Syncs::saveSyncConfig(const SyncConfig& config)
{
    assert(onSyncThread());

    if (auto* store = syncConfigStore())
    {

        // If the app hasn't opened this drive itself, then we open it now (loads any syncs that already exist there)
        if (!config.mExternalDrivePath.empty() && !store->driveKnown(config.mExternalDrivePath))
        {
            backupOpenDrive_inThread(config.mExternalDrivePath);
        }

        store->markDriveDirty(config.mExternalDrivePath);
    }
}

void Syncs::resumeResumableSyncsOnStartup(bool resetSyncConfigStore)
{
    if (mClient.loggedin() != FULLACCOUNT) return;
//    assert(!onSyncThread());

    // Double check the client only calls us once (per session) for this
    assert(!mSyncsLoaded);
    if (mSyncsLoaded) return;
    mSyncsLoaded = true;

    queueSync([this, resetSyncConfigStore]()
        {
            loadSyncConfigsOnFetchnodesComplete_inThread(resetSyncConfigStore);
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

    // before sync rework, the syncs have to be started before we start processing actionpackets.
    resumeSyncsOnStateCurrent_inThread();
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
                auto node = mClient.nodeByHandle(unifiedSync->mConfig.mRemoteNode);
                unifiedSync->updateSyncRemoteLocation(node, false); //updates cache & notice app of this change
                if (node)
                {
                    auto newpath = node->displaypath();
                    unifiedSync->mConfig.mOriginalPathOfRemoteRootNode = newpath;//update loaded config
                }
            }

#ifndef NDEBUG
            bool hadAnError = unifiedSync->mConfig.mError != NO_SYNC_ERROR;
#endif

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
                unifiedSync->mConfig.mFilesystemFingerprint = 0; //for certain MacOS, fsfp seems to vary when restarting. we set it to 0, so that it gets recalculated
#endif
                LOG_debug << "Resuming cached sync: " << toHandle(unifiedSync->mConfig.mBackupId) << " " << unifiedSync->mConfig.getLocalPath() << " fsfp= " << unifiedSync->mConfig.mFilesystemFingerprint.id << " error = " << unifiedSync->mConfig.mError;

                enableSyncByBackupId_inThread(unifiedSync->mConfig.mBackupId, false, false, true, false, [&unifiedSync](error e, SyncError se, handle backupId)
                    {
                        LOG_debug << "Sync autoresumed: " << toHandle(backupId) << " " << unifiedSync->mConfig.getLocalPath() << " fsfp= " << unifiedSync->mConfig.mFilesystemFingerprint.id << " error = " << se;
                    }, "");
            }
            else
            {
                unifiedSync->mConfig.mRunState = SyncRunState::Disable;
                LOG_debug << "Sync loaded (but not resumed): " << toHandle(unifiedSync->mConfig.mBackupId) << " " << unifiedSync->mConfig.getLocalPath() << " fsfp= " << unifiedSync->mConfig.mFilesystemFingerprint.id << " error = " << unifiedSync->mConfig.mError;
            }
        }
    }

    mClient.app->syncs_restored(NO_SYNC_ERROR);
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

handle SyncConfigStore::driveID(const LocalPath& drivePath) const
{
    auto i = mKnownDrives.find(drivePath);

    if (i != mKnownDrives.end())
        return i->second.driveID;

    assert(!"Drive should be known!");

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
        const char suffix = filePath.toPath(false).back();

        // Skip invalid suffixes.
        if (!isdigit(suffix))
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
    const auto TYPE_ENABLED         = MAKENAMEID2('e', 'n');
    const auto TYPE_FINGERPRINT     = MAKENAMEID2('f', 'p');
    const auto TYPE_LAST_ERROR      = MAKENAMEID2('l', 'e');
    const auto TYPE_LAST_WARNING    = MAKENAMEID2('l', 'w');
    const auto TYPE_NAME            = MAKENAMEID1('n');
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

        case TYPE_ENABLED:
            config.mEnabled = reader.getbool();
            break;

        case TYPE_FINGERPRINT:
            config.mFilesystemFingerprint = reader.getfsfp();
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
    writer.arg_fsfp("fp", config.mFilesystemFingerprint);
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
