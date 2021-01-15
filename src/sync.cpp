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
    if (!fa->fopen(localpath, true, false))
    {
        LOG_err << "Unable to open path: " << localpath.toPath(fsaccess);
        return {};
    }
    if (fa->mIsSymLink)
    {
        LOG_debug << "Ignoring symlink: " << localpath.toPath(fsaccess);
        return {};
    }
    assert(fa->type == FOLDERNODE);

    auto da = std::unique_ptr<DirAccess>{fsaccess.newdiraccess()};
    if (!da->dopen(&localpath, fa.get(), false))
    {
        LOG_err << "Unable to open directory: " << localpath.toPath(fsaccess);
        return {};
    }

    set<LocalPath> paths; // has to be a std::set to enforce same sorting as `children` of `LocalNode`

    LocalPath localname;
    while (da->dnext(localpath, localname, false))
    {
        ScopedLengthRestore restoreLength(localpath);
        localpath.appendWithSeparator(localname, false);

        // check if this record is to be ignored
        const auto name = localname.toName(fsaccess, sync.mFilesystemType);
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
        if (!fa->fopen(pathArg, true, false))
        {
            LOG_err << "Unable to open path: " << path.toPath(fsaccess);
            success = false;
            break;
        }
        if (fa->mIsSymLink)
        {
            LOG_debug << "Ignoring symlink: " << path.toPath(fsaccess);
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
    if (!fa->fopen(localpath, true, false))
    {
        LOG_err << "Unable to open path: " << localpath.toPath(fsaccess);
        success = false;
        return;
    }
    if (fa->mIsSymLink)
    {
        LOG_debug << "Ignoring symlink: " << localpath.toPath(fsaccess);
        return;
    }
    if (!fa->fsidvalid)
    {
        LOG_err << "Invalid fs id for: " << localpath.toPath(fsaccess);
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
    LocalPath accumulated;
    while (index <= path1End && index <= path2End)
    {
        const auto value1 = path1.localpath[path1End - index];
        const auto value2 = path2.localpath[path2End - index];
        if (value1 != value2)
        {
            break;
        }
        accumulated.localpath.push_back(value1);

        ++index;

        if (!accumulated.localpath.empty())
        {
            if (accumulated.localpath.back() == LocalPath::localPathSeparator)
            {
                ++separatorBias;
                accumulated.clear();
            }
        }
    }

    if (index > path1End && index > path2End) // we got to the beginning of both paths (full score)
    {
        return static_cast<int>(index - separatorBias);
    }
    else // the paths only partly match
    {
        return static_cast<int>(index - separatorBias - accumulated.localpath.size());
    }
}

bool assignFilesystemIds(Sync& sync, MegaApp& app, FileSystemAccess& fsaccess, handlelocalnode_map& fsidnodes,
                         LocalPath& localdebris)
{
    auto& rootpath = sync.localroot->localname;
    LOG_info << "Assigning fs IDs at rootpath: " << rootpath.toPath(fsaccess);

    auto fa = fsaccess.newfileaccess(false);
    if (!fa->fopen(rootpath, true, false))
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

SyncConfigBag::SyncConfigBag(DbAccess& dbaccess, FileSystemAccess& fsaccess, PrnGen& rng, const std::string& id)
{
    std::string dbname = "syncconfigsv2_" + id;
    mTable.reset(dbaccess.open(rng, fsaccess, dbname));
    if (!mTable)
    {
        LOG_warn << "Unable to open database: " << dbname;
        // no syncs configured --> no database
        return;
    }

    mTable->rewind();

    uint32_t tableId;
    std::string data;
    while (mTable->next(&tableId, &data))
    {
        auto syncConfig = SyncConfig::unserialize(data);
        if (!syncConfig)
        {
            LOG_err << "Unable to unserialize sync config at id: " << tableId;
            LOG_err << "Sync config record was " << data.size() << ": " << Utils::stringToHex(data);

            {
                // remove the reocrd so we can recover in jenkins tests (which fail at the assert())
                DBTableTransactionCommitter committer{ mTable };
                mTable->del(tableId);
            }

            assert(false);
            continue;
        }
        syncConfig->dbid = tableId;

        mSyncConfigs.insert(std::make_pair(syncConfig->getTag(), *syncConfig));
        if (tableId > mTable->nextid)
        {
            mTable->nextid = tableId;
        }
    }
    ++mTable->nextid;
}

error SyncConfigBag::insert(const SyncConfig& syncConfig)
{
    auto insertOrUpdate = [this](const uint32_t id, const SyncConfig& syncConfig)
    {
        std::string data;
        const_cast<SyncConfig&>(syncConfig).serialize(&data);
        DBTableTransactionCommitter committer{mTable};
        if (!mTable->put(id, &data)) // put either inserts or updates
        {
            LOG_err << "Incomplete database put at id: " << mTable->nextid;
            assert(false);
            mTable->abort();
            return false;
        }
        return true;
    };

    map<int, SyncConfig>::iterator syncConfigIt = mSyncConfigs.find(syncConfig.getTag());
    if (syncConfigIt == mSyncConfigs.end()) // syncConfig is new
    {
        if (mTable)
        {
            if (!insertOrUpdate(mTable->nextid, syncConfig))
            {
                return API_EWRITE;
            }
        }
        auto insertPair = mSyncConfigs.insert(std::make_pair(syncConfig.getTag(), syncConfig));
        if (mTable)
        {
            insertPair.first->second.dbid = mTable->nextid;
            ++mTable->nextid;
        }
    }
    else // syncConfig exists already
    {
        const uint32_t tableId = syncConfigIt->second.dbid;
        if (mTable)
        {
            if (!insertOrUpdate(tableId, syncConfig))
            {
                return API_EWRITE;
            }
        }
        syncConfigIt->second = syncConfig;
        syncConfigIt->second.dbid = tableId;
    }

    return API_OK;
}

error SyncConfigBag::removeByTag(const int tag)
{
    auto it = mSyncConfigs.find(tag);

    if (it == mSyncConfigs.end())
    {
        return API_ENOENT;
    }

    if (mTable)
    {
        DBTableTransactionCommitter committer(mTable);

        if (!mTable->del(it->second.dbid))
        {
            LOG_err << "Unable to remove config from database: "
                    << it->second.dbid;

            assert(false);

            mTable->abort();

            return API_EWRITE;
        }
    }

    mSyncConfigs.erase(it);

    return API_OK;
}

const SyncConfig* SyncConfigBag::get(const int tag) const
{
    auto syncConfigPair = mSyncConfigs.find(tag);
    if (syncConfigPair != mSyncConfigs.end())
    {
        return &syncConfigPair->second;
    }
    return nullptr;
}


const SyncConfig* SyncConfigBag::getByNodeHandle(handle nodeHandle) const
{
    for (const auto& syncConfigPair : mSyncConfigs)
    {
        if (syncConfigPair.second.getRemoteNode() == nodeHandle)
            return &syncConfigPair.second;
    }
    return nullptr;
}

void SyncConfigBag::clear()
{
    if (mTable)
    {
        mTable->truncate();
        mTable->nextid = 0;
    }
    mSyncConfigs.clear();
}

std::vector<SyncConfig> SyncConfigBag::all() const
{
    std::vector<SyncConfig> syncConfigs;
    for (const auto& syncConfigPair : mSyncConfigs)
    {
        syncConfigs.push_back(syncConfigPair.second);
    }
    return syncConfigs;
}

// new Syncs are automatically inserted into the session's syncs list
// and a full read of the subtree is initiated
Sync::Sync(UnifiedSync& us, const char* cdebris,
           LocalPath* clocaldebris, Node* remotenode, bool cinshare, int ctag)
: localroot(new LocalNode)
, mUnifiedSync(us)
{
    isnetwork = false;
    client = &mUnifiedSync.mClient;
    tag = ctag;
    inshare = cinshare;
    tmpfa = NULL;
    initializing = true;
    updatedfilesize = ~0;
    updatedfilets = 0;
    updatedfileinitialts = 0;

    localbytes = 0;
    localnodes[FILENODE] = 0;
    localnodes[FOLDERNODE] = 0;

    state = SYNC_INITIALSCAN;
    statecachetable = NULL;

    fullscan = true;
    scanseqno = 0;

    mLocalPath = mUnifiedSync.mConfig.getLocalPath();
    LocalPath crootpath = LocalPath::fromPath(mLocalPath, *client->fsaccess);

    if (cdebris)
    {
        debris = cdebris;
        localdebris = LocalPath::fromPath(debris, *client->fsaccess);

        dirnotify.reset(client->fsaccess->newdirnotify(crootpath, localdebris, client->waiter));

        localdebris.prependWithSeparator(crootpath);
    }
    else
    {
        localdebris = *clocaldebris;

        // FIXME: pass last segment of localdebris
        dirnotify.reset(client->fsaccess->newdirnotify(crootpath, localdebris, client->waiter));
    }
    dirnotify->sync = this;

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

    mFilesystemType = client->fsaccess->getlocalfstype(crootpath);

    localroot->init(this, FOLDERNODE, NULL, crootpath, nullptr);  // the root node must have the absolute path.  We don't store shortname, to avoid accidentally using relative paths.
    localroot->setnode(remotenode);

#ifdef __APPLE__
    if (macOSmajorVersion() >= 19) //macOS catalina+
    {
        LOG_debug << "macOS 10.15+ filesystem detected. Checking fseventspath.";
        string supercrootpath = "/System/Volumes/Data" + crootpath.platformEncoded();

        int fd = open(supercrootpath.c_str(), O_RDONLY);
        if (fd == -1)
        {
            LOG_debug << "Unable to open path using fseventspath.";
            mFsEventsPath = crootpath.platformEncoded();
        }
        else
        {
            char buf[MAXPATHLEN];
            if (fcntl(fd, F_GETPATH, buf) < 0)
            {
                LOG_debug << "Using standard paths to detect filesystem notifications.";
                mFsEventsPath = crootpath.platformEncoded();
            }
            else
            {
                LOG_debug << "Using fsevents paths to detect filesystem notifications.";
                mFsEventsPath = supercrootpath;
            }
            close(fd);
        }
    }
#endif

    if (client->dbaccess)
    {
        // open state cache table
        handle tableid[3];
        string dbname;

        auto fas = client->fsaccess->newfileaccess(false);

        if (fas->fopen(crootpath, true, false))
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
    assert(state == SYNC_CANCELED || state == SYNC_FAILED || state == SYNC_DISABLED);
    mDestructorRunning = true;

    // unlock tmp lock
    tmpfa.reset();

    // stop all active and pending downloads
    if (localroot->node)
    {
        TreeProcDelSyncGet tdsg;
        // Create a committer to ensure we update the transfer database in an efficient single commit,
        // if there are transactions in progress.
        DBTableTransactionCommitter committer(client->tctable);
        client->proctree(localroot->node, &tdsg);
    }

    delete statecachetable;

    client->syncactivity = true;

    {
        // Create a committer and recursively delete all the associated LocalNodes, and their associated transfer and file objects.
        // If any have transactions in progress, the committer will ensure we update the transfer database in an efficient single commit.
        DBTableTransactionCommitter committer(client->tctable);
        localroot.reset();
    }
}

void Sync::addstatecachechildren(uint32_t parent_dbid, idlocalnode_map* tmap, LocalPath& localpath, LocalNode *p, int maxdepth)
{
    auto range = tmap->equal_range(parent_dbid);

    for (auto it = range.first; it != range.second; it++)
    {
        ScopedLengthRestore restoreLen(localpath);

        localpath.appendWithSeparator(it->second->localname, true);

        LocalNode* l = it->second;
        Node* node = l->node.release_unchecked();
        handle fsid = l->fsid;
        m_off_t size = l->size;

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
        auto fa = client->fsaccess->newfileaccess(false);
        if (fa->fopen(localpath))  // exists, is file
        {
            auto sn = client->fsaccess->fsShortname(localpath);
            assert(!l->localname.empty() &&
                ((!l->slocalname && (!sn || l->localname == *sn)) ||
                (l->slocalname && sn && !l->slocalname->empty() && *l->slocalname != l->localname && *l->slocalname == *sn)));
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
                cachenodes();  // periodically output updated nodes with shortname updates, so people who restart megasync still make progress towards a fast startup
            }
        }

        if (maxdepth)
        {
            addstatecachechildren(l->dbid, tmap, localpath, l, maxdepth - 1);
        }
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

        // trigger a single-pass full scan to identify deleted nodes
        fullscan = true;
        scanseqno++;

        return true;
    }

    return false;
}

SyncConfig& Sync::getConfig()
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
    if (statecachetable && (state == SYNC_ACTIVE || (state == SYNC_INITIALSCAN && insertq.size() > 100)) && (deleteq.size() || insertq.size()))
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

void Sync::changestate(syncstate_t newstate, SyncError newSyncError, bool newEnableFlag, bool notifyApp)
{
    if (newstate != state)
    {
        auto oldstate = state;
        state = newstate;
        fullscan = false;

        if (notifyApp)
        {
            bool wasActive = oldstate == SYNC_ACTIVE;
            bool nowActive = newstate == SYNC_ACTIVE;
            if (wasActive != nowActive)
            {
                mUnifiedSync.mClient.app->syncupdate_active(getConfig().getTag(), nowActive);
            }
        }
    }

    getConfig().setError(newSyncError);
    getConfig().setEnabled(newEnableFlag);
    if (newstate != SYNC_CANCELED)
    {
        mUnifiedSync.changedConfigState(notifyApp);
    }
}

// walk path and return corresponding LocalNode and its parent
// path must be relative to l or start with the root prefix if l == NULL
// path must be a full sync path, i.e. start with localroot->localname
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

bool Sync::assignfsids()
{
    return assignFilesystemIds(*this, *client->app, *client->fsaccess, client->fsidnode,
                               localdebris);
}

// scan localpath, add or update child nodes, call recursively for folder nodes
// localpath must be prefixed with Sync
bool Sync::scan(LocalPath* localpath, FileAccess* fa)
{
    if (fa)
    {
        assert(fa->type == FOLDERNODE);
    }
    if (!localdebris.isContainingPathOf(*localpath))
    {
        DirAccess* da;
        LocalPath localname;
        string name;
        bool success;

        if (SimpleLogger::logCurrentLevel >= logDebug)
        {
            LOG_debug << "Scanning folder: " << localpath->toPath(*client->fsaccess);
        }

        da = client->fsaccess->newdiraccess();

        // scan the dir, mark all items with a unique identifier
        if ((success = da->dopen(localpath, fa, false)))
        {
            while (da->dnext(*localpath, localname, client->followsymlinks))
            {
                name = localname.toName(*client->fsaccess, mFilesystemType);

                ScopedLengthRestore restoreLen(*localpath);
                localpath->appendWithSeparator(localname, false);

                // check if this record is to be ignored
                if (client->app->sync_syncable(this, name.c_str(), *localpath))
                {
                    // skip the sync's debris folder
                    if (!localdebris.isContainingPathOf(*localpath))
                    {
                        LocalNode *l = NULL;
                        if (initializing)
                        {
                            // preload all cached LocalNodes
                            l = checkpath(NULL, localpath, nullptr, nullptr, false, da);
                        }

                        if (!l || l == (LocalNode*)~0)
                        {
                            // new record: place in notification queue
                            dirnotify->notify(DirNotify::DIREVENTS, NULL, LocalPath(*localpath));
                        }
                    }
                }
                else
                {
                    LOG_debug << "Excluded: " << name;
                }
            }
        }

        delete da;

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
LocalNode* Sync::checkpath(LocalNode* l, LocalPath* input_localpath, string* const localname, dstime *backoffds, bool wejustcreatedthisfolder, DirAccess* iteratingDir)
{
    LocalNode* ll = l;
    bool newnode = false, changed = false;
    bool isroot;

    LocalNode* parent;
    string path;           // UTF-8 representation of tmppath
    LocalPath tmppath;     // full path represented by l + localpath
    LocalPath newname;     // portion of tmppath not covered by the existing
                           // LocalNode structure (always the last path component
                           // that does not have a corresponding LocalNode yet)

    if (localname)
    {
        // shortcut case (from within syncdown())
        isroot = false;
        parent = l;
        l = NULL;

        path = input_localpath->toPath(*client->fsaccess);
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
            tmppath.appendWithSeparator(*input_localpath, false);
        }

        // look up deepest existing LocalNode by path, store remainder (if any)
        // in newname
        LocalNode *tmp = localnodebypath(l, *input_localpath, &parent, &newname);
        size_t index = 0;

        if (newname.findNextSeparator(index))
        {
            LOG_warn << "Parent not detected yet. Unknown remainder: " << newname.toPath(*client->fsaccess);
            if (parent)
            {
                LocalPath notifyPath = parent->getLocalPath();
                notifyPath.appendWithSeparator(newname.subpathTo(index), true);
                dirnotify->notify(DirNotify::DIREVENTS, l, std::move(notifyPath), true);
            }
            return NULL;
        }

        l = tmp;

        path = tmppath.toPath(*client->fsaccess);

        // path invalid?
        if ( ( !l && newname.empty() ) || !path.size())
        {
            LOG_warn << "Invalid path: " << path;
            return NULL;
        }

        string name = !newname.empty() ? newname.toName(*client->fsaccess, mFilesystemType) : l->name;

        if (!client->app->sync_syncable(this, name.c_str(), tmppath))
        {
            LOG_debug << "Excluded: " << path;
            return NULL;
        }

        isroot = l == localroot.get() && newname.empty();
    }

    LOG_verbose << "Scanning: " << path << " in=" << initializing << " full=" << fullscan << " l=" << l;
    LocalPath* localpathNew = localname ? input_localpath : &tmppath;

    if (parent)
    {
        if (state != SYNC_INITIALSCAN && !parent->node)
        {
            LOG_warn << "Parent doesn't exist yet: " << path;
            return (LocalNode*)~0;
        }
    }

    // attempt to open/type this file
    auto fa = client->fsaccess->newfileaccess(false);

    if (initializing || fullscan)
    {
        // find corresponding LocalNode by file-/foldername
        size_t lastpart = localpathNew->getLeafnameByteIndex(*client->fsaccess);

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
        if (fa->fopen(*localpathNew, false, false, iteratingDir))
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
                        scan(localpathNew, fa.get());
                    }
                    else
                    {
                        localbytes += l->size;
                    }

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

        fa = client->fsaccess->newfileaccess(false);
    }

    if (fa->fopen(*localpathNew, true, false))
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
                                            || ((fp1 = l->sync->dirnotify->fsfingerprint())
                                                && (fp2 = it->second->sync->dirnotify->fsfingerprint())
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

                                        // delete existing LocalNode...
                                        delete l;

                                        // ...move remote node out of the way...
                                        client->execsyncdeletions();

                                        // ...and atomically replace with moved one
                                        client->app->syncupdate_local_move(this, it->second, path.c_str());

                                        // (in case of a move, this synchronously updates l->parent and l->node->parent)
                                        it->second->setnameparent(parent, localpathNew, client->fsaccess->fsShortname(*localpathNew));

                                        // mark as seen / undo possible deletion
                                        it->second->setnotseen(0);

                                        statecacheadd(it->second);

                                        return it->second;
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

                            client->app->syncupdate_local_file_change(this, l, path.c_str());

                            DBTableTransactionCommitter committer(client->tctable);
                            client->stopxfer(l, &committer); // TODO:  can we use one committer for all the files in the folder?  Or for the whole recursion?
                            l->bumpnagleds();
                            l->deleted = false;

                            client->syncactivity = true;

                            statecacheadd(l);

                            fa.reset();

                            if (isnetwork && l->type == FILENODE)
                            {
                                LOG_debug << "Queueing extra fs notification for modified file";
                                dirnotify->notify(DirNotify::EXTRA, NULL, LocalPath(*localpathNew));
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
                        || ((fp1 = it->second->sync->dirnotify->fsfingerprint())
                            && (fp2 = parent->sync->dirnotify->fsfingerprint())
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
                    LOG_debug << client->clientname << "Move detected by fsid " << toHandle(fa->fsid) << " in checkpath. Type: " << it->second->type << " new path: " << path << " old localnode: " << it->second->localnodedisplaypath(*client->fsaccess);

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
                                auto prevfa = client->fsaccess->newfileaccess(false);

                                bool exists = prevfa->fopen(local);
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

                    client->app->syncupdate_local_move(this, it->second, path.c_str());

                    // (in case of a move, this synchronously updates l->parent
                    // and l->node->parent)
                    it->second->setnameparent(parent, localpathNew, client->fsaccess->fsShortname(*localpathNew));

                    // make sure that active PUTs receive their updated filenames
                    client->updateputs();

                    statecacheadd(it->second);

                    // unmark possible deletion
                    it->second->setnotseen(0);

                    // immediately scan folder to detect deviations from cached state
                    if (fullscan && fa->type == FOLDERNODE)
                    {
                        scan(localpathNew, fa.get());
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
                    l = new LocalNode;
                    l->init(this, fa->type, parent, *localpathNew, client->fsaccess->fsShortname(*localpathNew));

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
                if (newnode)
                {
                    scan(localpathNew, fa.get());
                    client->app->syncupdate_local_folder_addition(this, l, path.c_str());

                    if (!isroot)
                    {
                        statecacheadd(l);
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
                    changestate(SYNC_FAILED, INVALID_LOCAL_TYPE, false, true);
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
                        client->app->syncupdate_local_file_addition(this, l, path.c_str());
                    }
                    else if (changed)
                    {
                        client->app->syncupdate_local_file_change(this, l, path.c_str());
                        DBTableTransactionCommitter committer(client->tctable); // TODO:  can we use one committer for all the files in the folder?  Or for the whole recursion?
                        client->stopxfer(l, &committer);
                    }

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
                dirnotify->notify(DirNotify::EXTRA, NULL, LocalPath(*localpathNew));
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
            dirnotify->notify(DirNotify::RETRY, ll, LocalPath(*localpathNew));
            client->syncfslockretry = true;
            client->syncfslockretrybt.backoff(SCANNING_DELAY_DS);
            client->blockedfile = *localpathNew;
        }
        else if (l)
        {
            // immediately stop outgoing transfer, if any
            if (l->transfer)
            {
                DBTableTransactionCommitter committer(client->tctable); // TODO:  can we use one committer for all the files in the folder?  Or for the whole recursion?
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
        if (notification.localnode)
        {
            tmppath = notification.localnode->getLocalPath();
        }

        if (!notification.path.empty())
        {
            tmppath.appendWithSeparator(notification.path, false);
        }

        attr_map::iterator ait;
        auto fa = client->fsaccess->newfileaccess(false);
        bool success = fa->fopen(tmppath, false, false);
        LocalNode *ll = localnodebypath(notification.localnode, notification.path);
        if ((!ll && !success && !fa->retry) // deleted file
            || (ll && success && ll->node && ll->node->localnode == ll
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
            LOG_verbose << "Scanning postponed. Modification too recent";
            dirnotify->notifyq[q].unpopFront(notification);
            return notification.timestamp - dsmin;
        }

        if ((l = notification.localnode) != (LocalNode*)~0)
        {
            dstime backoffds = 0;
            LOG_verbose << "Checkpath: " << notification.path.toPath(*client->fsaccess);

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
            string utf8path = notification.path.toPath(*client->fsaccess);
            LOG_debug << "Notification skipped: " << utf8path;
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
    LocalPath path;
    std::unique_ptr<FileAccess> fa;
    for (localnode_map::iterator it = l->children.begin(); it != l->children.end(); )
    {
        if (scanseqno-it->second->scanseqno > 1)
        {
            if (!fa)
            {
                fa = client->fsaccess->newfileaccess();
            }
            client->unlinkifexists(it->second, fa.get(), path);
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

bool Sync::movetolocaldebris(LocalPath& localpath)
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
            LOG_verbose << "Creating daily local debris folder";
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
                if (sfg->sync == this)
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

    e = startSync(&mClient, DEBRISFOLDER, nullptr, remotenode, inshare, isnetwork, false, rootpath, openedLocalFolder);
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
        if (newpath != mConfig.getRemotePath())
        {
            mConfig.setRemotePath(newpath);
            changed = true;
        }

        if (mConfig.getRemoteNode() != n->nodehandle)
        {
            mConfig.setRemoteNode(n->nodehandle);
            changed = true;
        }
    }
    else //unset remote node: failed!
    {
        if (mConfig.getRemoteNode() != UNDEF)
        {
            mConfig.setRemoteNode(UNDEF);
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
                             bool isNetwork, bool delayInitialScan, LocalPath& rootpath, std::unique_ptr<FileAccess>& openedLocalFolder)
{
    //check we are not in any blocking situation
    using CType = CacheableStatus::Type;
    bool overStorage = mClient.mCachedStatus[CType::STATUS_STORAGE] ? (mClient.mCachedStatus[CType::STATUS_STORAGE]->value() >= STORAGE_RED) : false;
    bool businessExpired = mClient.mCachedStatus[CType::STATUS_BUSINESS] ? (mClient.mCachedStatus[CType::STATUS_BUSINESS]->value() == BIZ_STATUS_EXPIRED) : false;
    bool blocked = mClient.mCachedStatus[CType::STATUS_BLOCKED] ? (mClient.mCachedStatus[CType::STATUS_BLOCKED]->value()) : false;

    mConfig.mError = NO_SYNC_ERROR;
    mConfig.mEnabled = true;

    // the order is important here: a user needs to resolve blocked in order to resolve storage
    if (overStorage)
    {
        mConfig.mError = STORAGE_OVERQUOTA;
    }
    else if (businessExpired)
    {
        mConfig.mError = BUSINESS_EXPIRED;
    }
    else if (blocked)
    {
        mConfig.mError = ACCOUNT_BLOCKED;
    }

    if (mConfig.mError)
    {
        // save configuration but avoid creating active sync, and set as temporary disabled:
        mClient.syncs.saveSyncConfig(mConfig);
        return API_EFAILED;
    }

    auto prevFingerprint = mConfig.getLocalFingerprint();

    assert(!mSync);
    mSync.reset(new Sync(*this, debris, localdebris, remotenode, inshare, mConfig.getTag()));
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

    if (!mSync->fsstableids)
    {
        if (mSync->assignfsids())
        {
            LOG_info << "Successfully assigned fs IDs for filesystem with unstable IDs";
        }
        else
        {
            LOG_warn << "Failed to assign some fs IDs for filesystem with unstable IDs";
        }
    }

    if (delayInitialScan)
    {
        client->syncs.saveSyncConfig(mConfig);
    }
    else
    {
        LOG_debug << "Initial scan sync: " << mConfig.getLocalPath();

        if (mSync->scan(&rootpath, openedLocalFolder.get()))
        {
            client->syncsup = false;
            mSync->initializing = false;
            LOG_debug << "Initial scan finished. New / modified files: " << mSync->dirnotify->notifyq[DirNotify::DIREVENTS].size();

            // Sync constructor now receives the syncConfig as reference, to be able to write -at least- fingerprints for new syncs
            client->syncs.saveSyncConfig(mConfig);
        }
        else
        {
            LOG_err << "Initial scan failed";
            mSync->changestate(SYNC_FAILED, INITIAL_SCAN_FAILED, mConfig.getEnabled(), true);

            mSync.reset();
            return API_EFAILED;
        }
    }
    return API_OK;
}

void UnifiedSync::changedConfigState(bool notifyApp)
{
    if ((mConfig.mError != mConfig.mKnownError) ||
        (mConfig.getEnabled() != mConfig.mKnownEnabled))
    {
        LOG_debug << "Sync enabled/error changing. from " << mConfig.mKnownEnabled << "/" << mConfig.mKnownError << " to "  << mConfig.getEnabled() << "/" << mConfig.mError;

        mClient.syncs.saveSyncConfig(mConfig);
        if (notifyApp)
        {
            mClient.app->syncupdate_stateconfig(mConfig.getTag());
        }
        mClient.abortbackoff(false);

        mConfig.mKnownError = mConfig.mError;
        mConfig.mKnownEnabled = mConfig.getEnabled();
    }
}

Syncs::Syncs(MegaClient& mc)
  : mClient(mc)
{
    mHeartBeatMonitor.reset(new BackupMonitor(&mClient));
}

JSONSyncConfigDB* Syncs::syncConfigDB()
{
    // Have we already created the database?
    if (mSyncConfigDB)
    {
        // Yep, return a reference to the caller.
        return mSyncConfigDB.get();
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
    mSyncConfigDB.reset(new JSONSyncConfigDB(dbPath));

    return mSyncConfigDB.get();
}

bool Syncs::syncConfigDBDirty()
{
    return mSyncConfigDB && mSyncConfigDB->dirty();
}

error Syncs::syncConfigDBFlush()
{
    if (!syncConfigDBDirty())
    {
        return API_OK;
    }

    LOG_verbose << "Attempting to flush internal config database.";

    auto result = mSyncConfigDB->write(*mSyncConfigIOContext);

    if (result != API_OK)
    {
        LOG_err << "Couldn't flush internal config database to disk: "
                << result;
    }
    else
    {
        LOG_verbose << "Internal config database flushed to disk.";
    }

    return result;
}

error Syncs::syncConfigDBLoad()
{
    LOG_verbose << "Attempting to load internal sync configs from disk.";

    auto result = API_EAGAIN;

    // Can we get our hands on the internal sync config database?
    if (auto* db = syncConfigDB())
    {
        // Make sure we have a suitable IO context.
        if (auto* ioContext = syncConfigIOContext())
        {
            // Try and read the database from disk.
            result = db->read(*ioContext);
        }

        if (result == API_OK || result == API_ENOENT)
        {
            LOG_verbose << "Loaded "
                        << db->configs().size()
                        << " internal sync config(s) from disk.";

            return API_OK;
        }
    }

    LOG_err << "Couldn't load internal sync configs from disk: "
            << result;

    return result;
}

JSONSyncConfigIOContext* Syncs::syncConfigIOContext()
{
    // Has a suitable IO context already been created?
    if (mSyncConfigIOContext)
    {
        // Yep, return a reference to it.
        return mSyncConfigIOContext.get();
    }

    using KeyStr  = Base64Str<SymmCipher::KEYLENGTH * 2>;
    using NameStr = Base64Str<SymmCipher::KEYLENGTH>;

    // Verify attributes are correctly sized.
    if (mClient.syncdbkey.size() != KeyStr::STRLEN
        || mClient.syncdbname.size() != NameStr::STRLEN)
    {
        return nullptr;
    }

    // Create the IO context.
    mSyncConfigIOContext.reset(
      new JSONSyncConfigIOContext(mClient.key,
                                 *mClient.fsaccess,
                                 mClient.syncdbkey,
                                 mClient.syncdbname,
                                 mClient.rng));

    // Return a reference to the new IO context.
    return mSyncConfigIOContext.get();
}

void Syncs::clear()
{
    syncConfigDBFlush();

    mSyncConfigDB.reset();
    mSyncConfigIOContext.reset();
    mSyncVec.clear();
    isEmpty = true;
}

error Syncs::truncate()
{
    if (!mSyncConfigDB)
    {
        return API_OK;
    }

    mSyncConfigDB->clear();

    return syncConfigDBFlush();
}

void Syncs::resetSyncConfigDb()
{
    mSyncConfigDB.reset();
    static_cast<void>(syncConfigDB());
}

auto Syncs::appendNewSync(const SyncConfig& c, MegaClient& mc) -> UnifiedSync*
{
    isEmpty = false;
    mSyncVec.push_back(unique_ptr<UnifiedSync>(new UnifiedSync(mc, c)));

    saveSyncConfig(c);

    return mSyncVec.back().get();
}

Sync* Syncs::runningSyncByTag(int tag) const
{
    for (auto& s : mSyncVec)
    {
        if (s->mSync && s->mSync->tag == tag)
        {
            return s->mSync.get();
        }
    }
    return nullptr;
}

SyncConfig* Syncs::syncConfigByTag(const int tag) const
{
    for (auto& s : mSyncVec)
    {
        if (s->mConfig.getTag() == tag)
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

unsigned Syncs::numRunningSyncs()
{
    unsigned n = 0;
    for (auto& s : mSyncVec)
    {
        if (s->mSync) ++n;
    }
    return n;
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
    for (auto& s : mSyncVec)
    {
        if (s->mSync)
        {
            auto tag = s->mConfig.getTag();
            s->mSync->changestate(SYNC_CANCELED, NO_SYNC_ERROR, false, false);
            s->mSync.reset();
            mClient.app->sync_removed(tag);
        }
    }
}

void Syncs::disableSyncs(SyncError syncError, bool newEnabledFlag)
{
    bool anySyncDisabled = false;
    disableSelectedSyncs([&](SyncConfig&, Sync* s){

        if (s)
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
                mSyncVec[i]->mConfig.setError(syncError);
                mSyncVec[i]->mConfig.setEnabled(newEnabledFlag);
                mSyncVec[i]->changedConfigState(true);
            }
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

        // call back before actual removal (intermediate layer may need to make a temp copy to call client app)
        auto tag = mSyncVec[index]->mConfig.getTag();
        mClient.app->sync_removed(tag);

        removeSyncConfig(tag);
        mClient.syncactivity = true;
        mSyncVec.erase(mSyncVec.begin() + index);

        isEmpty = mSyncVec.empty();
    }
}

error Syncs::removeSyncConfig(const int tag)
{
    error result = API_OK;

    if (auto* db = syncConfigDB())
    {
        result = db->remove(tag);
    }

    if (result == API_ENOENT)
    {
        LOG_err << "Found no config for tag: "
                << tag
                << "upon sync removal.";
    }

    return result;
}

error Syncs::enableSyncByTag(int tag, bool resetFingerprint, UnifiedSync*& syncPtrRef)
{
    for (auto& s : mSyncVec)
    {
        if (s->mConfig.getTag() == tag)
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

error Syncs::saveSyncConfig(const SyncConfig& config)
{
    auto c = translate(mClient, config);

    assert(!config.isExternal());

    if (auto* db = syncConfigDB())
    {
        return db->add(c), API_OK;
    }

    return API_ENOENT;
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
                LOG_debug << "Restoring sync: " << unifiedSync->mConfig.getTag() << " " << unifiedSync->mConfig.getLocalPath() << " fsfp= " << unifiedSync->mConfig.getLocalFingerprint() << " old error = " << syncError;

                error e = unifiedSync->enableSync(false, true);
                if (!e)
                {
                    anySyncRestored = true;
                }
            }
            else
            {
                LOG_verbose << "Skipping restoring sync: " << unifiedSync->mConfig.getLocalPath()
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
    if (mClient.loggedinfolderlink()) return;
    if (!mSyncConfigDb) return;

    bool firstSyncResumed = false;

    if (syncConfigDBLoad() != API_OK)
    {
        return;
    }

    for (auto& pair : syncConfigDB()->configs())
    {
        const auto config = translate(mClient, pair.second);
        mSyncVec.push_back(unique_ptr<UnifiedSync>(new UnifiedSync(mClient, config)));
        isEmpty = false;
    }

    for (auto& unifiedSync : mSyncVec)
    {
        if (!unifiedSync->mSync)
        {
            if (!unifiedSync->mConfig.getRemotePath().size()) //should only happen if coming from old cache
            {
                auto node = mClient.nodebyhandle(unifiedSync->mConfig.getRemoteNode());
                unifiedSync->updateSyncRemoteLocation(node, false); //updates cache & notice app of this change
                if (node)
                {
                    auto newpath = node->displaypath();
                    unifiedSync->mConfig.setRemotePath(newpath);//update loaded config
                }
            }

            if (unifiedSync->mConfig.getEnabled())
            {
                if (!firstSyncResumed)
                {
                    mClient.app->syncs_about_to_be_resumed();
                    firstSyncResumed = true;
                }

#ifdef __APPLE__
                unifiedSync->mConfig.setLocalFingerprint(0); //for certain MacOS, fsfp seems to vary when restarting. we set it to 0, so that it gets recalculated
#endif
                LOG_debug << "Resuming cached sync: " << unifiedSync->mConfig.getTag() << " " << unifiedSync->mConfig.getLocalPath() << " fsfp= " << unifiedSync->mConfig.getLocalFingerprint() << " error = " << unifiedSync->mConfig.getError();

                unifiedSync->enableSync(false, true);
                LOG_debug << "Sync autoresumed: " << unifiedSync->mConfig.getTag() << " " << unifiedSync->mConfig.getLocalPath() << " fsfp= " << unifiedSync->mConfig.getLocalFingerprint() << " error = " << unifiedSync->mConfig.getError();

                mClient.app->sync_auto_resume_result(*unifiedSync, true);
            }
            else
            {
                LOG_debug << "Sync loaded (but not resumed): " << unifiedSync->mConfig.getTag() << " " << unifiedSync->mConfig.getLocalPath() << " fsfp= " << unifiedSync->mConfig.getLocalFingerprint() << " error = " << unifiedSync->mConfig.getError();
                mClient.app->sync_auto_resume_result(*unifiedSync, false);
            }

            mClient.mSyncTag = std::max(mClient.mSyncTag, unifiedSync->mConfig.getTag());
        }
    }
}

JSONSyncConfig::JSONSyncConfig()
  : drivePath()
  , sourcePath()
  , targetPath()
  , fingerprint(0)
  , heartbeatID(UNDEF)
  , targetHandle(UNDEF)
  , lastError(NO_SYNC_ERROR)
  , lastWarning(NO_SYNC_WARNING)
  , type()
  , tag(0)
  , enabled(false)
{
}

bool JSONSyncConfig::external() const
{
    return !drivePath.empty();
}

bool JSONSyncConfig::valid() const
{
    return !(sourcePath.empty()
             || targetPath.empty()
             || targetHandle == UNDEF);
}

bool JSONSyncConfig::operator==(const JSONSyncConfig& rhs) const
{
    return drivePath == rhs.drivePath
           && sourcePath == rhs.sourcePath
           && targetPath == rhs.targetPath
           && fingerprint == rhs.fingerprint
           && heartbeatID == rhs.heartbeatID
           && targetHandle == rhs.targetHandle
           && lastError == rhs.lastError
           && lastWarning == rhs.lastWarning
           && type == rhs.type
           && tag == rhs.tag
           && enabled == rhs.enabled;
}

bool JSONSyncConfig::operator!=(const JSONSyncConfig& rhs) const
{
    return !(*this == rhs);
}

JSONSyncConfig translate(const MegaClient& client, const SyncConfig &config)
{
    JSONSyncConfig result;

    result.enabled = config.getEnabled();
    result.fingerprint = config.getLocalFingerprint();
    result.heartbeatID = config.getBackupId();
    result.lastError = config.getError();
    result.lastWarning = config.getWarning();
    result.tag = config.getTag();
    result.targetHandle = config.getRemoteNode();
    result.targetPath = config.getRemotePath();
    result.type = config.getType();

    const auto& drivePath = config.drivePath();
    const auto& name = config.getName();
    const auto sourcePath = config.getLocalPath().substr(drivePath.size());

    const auto& fsAccess = *client.fsaccess;

    result.drivePath = LocalPath::fromPath(drivePath, fsAccess);
    result.sourcePath = LocalPath::fromPath(sourcePath, fsAccess);

    // Ensure paths are normalized.
    if (config.isExternal())
    {
        result.drivePath = NormalizeAbsolute(result.drivePath);
        result.sourcePath = NormalizeRelative(result.sourcePath);
    }
    else
    {
        result.sourcePath = NormalizeAbsolute(result.sourcePath);
    }

    if (name == sourcePath)
    {
        result.name = result.sourcePath.toPath(fsAccess);
    }
    else
    {
        result.name = name;
    }

    return result;
}

SyncConfig translate(const MegaClient& client, const JSONSyncConfig& config)
{
    // Convenience.
    auto& fsAccess = *client.fsaccess;

    LocalPath temp;
    string drivePath = config.drivePath.toPath(fsAccess);
    string sourcePath;
    const string* name = &sourcePath;

    if (config.external())
    {
        temp = NormalizeAbsolute(config.drivePath);
        temp.appendWithSeparator(
            NormalizeRelative(config.sourcePath),
            false);
    }
    else
    {
        temp = NormalizeAbsolute(config.sourcePath);
    }

    sourcePath = temp.toPath(fsAccess);

    if (config.sourcePath.toPath(fsAccess) != config.name)
    {
        name = &config.name;
    }

    // Build config.
    auto result =
      SyncConfig(config.tag,
                 sourcePath,
                 *name,
                 config.targetHandle,
                 config.targetPath,
                 config.fingerprint,
                 string_vector(),
                 config.enabled,
                 config.type,
                 true,
                 false,
                 config.lastError,
                 config.lastWarning,
                 config.heartbeatID);

    result.drivePath(std::move(drivePath));

    return result;
}

const unsigned int JSONSyncConfigDB::NUM_SLOTS = 2;

JSONSyncConfigDB::JSONSyncConfigDB(const LocalPath& dbPath,
                                   const LocalPath& drivePath,
                                   JSONSyncConfigDBObserver& observer)
  : mDBPath(dbPath)
  , mDrivePath(drivePath)
  , mObserver(&observer)
  , mTagToConfig()
  , mTargetToConfig()
  , mSlot(0)
  , mDirty(false)
{
}

JSONSyncConfigDB::JSONSyncConfigDB(const LocalPath& dbPath)
  : mDBPath(dbPath)
  , mDrivePath()
  , mObserver(nullptr)
  , mTagToConfig()
  , mTargetToConfig()
  , mSlot(0)
  , mDirty(false)
{
}

JSONSyncConfigDB::~JSONSyncConfigDB()
{
    // Drop the configs.
    clear(false);
}

const JSONSyncConfig* JSONSyncConfigDB::add(const JSONSyncConfig& config)
{
    // Add (or update) the config and flush.
    return add(config, true);
}

void JSONSyncConfigDB::clear()
{
    // Drop the configs and flush.
    clear(true);
}

const JSONSyncConfigMap& JSONSyncConfigDB::configs() const
{
    return mTagToConfig;
}

bool JSONSyncConfigDB::dirty() const
{
    return mDirty;
}

const LocalPath& JSONSyncConfigDB::dbPath() const
{
    return mDBPath;
}

const LocalPath& JSONSyncConfigDB::drivePath() const
{
    return mDrivePath;
}

const JSONSyncConfig* JSONSyncConfigDB::get(const int tag) const
{
    auto it = mTagToConfig.find(tag);

    if (it != mTagToConfig.end())
    {
        return &it->second;
    }

    return nullptr;
}

const JSONSyncConfig* JSONSyncConfigDB::get(const handle targetHandle) const
{
    auto it = mTargetToConfig.find(targetHandle);

    if (it != mTargetToConfig.end())
    {
        return it->second;
    }

    return nullptr;
}

error JSONSyncConfigDB::read(JSONSyncConfigIOContext& ioContext)
{
    vector<unsigned int> slots;

    // Determine which slots we should load first, if any.
    if (ioContext.get(mDBPath, slots) != API_OK)
    {
        // Couldn't get a list of slots.
        return API_ENOENT;
    }

    // Try and load the database from one of the slots.
    for (const auto& slot : slots)
    {
        // Can we read the database from this slot?
        if (read(ioContext, slot) == API_OK)
        {
            // Update the slot number.
            mSlot = (slot + 1) % NUM_SLOTS;

            // Yep, loaded.
            return API_OK;
        }
    }

    // Up to date with disk.
    mDirty = false;

    // Couldn't load the database.
    return API_EREAD;
}

error JSONSyncConfigDB::remove(const int tag)
{
    // Remove the config, if present and flush.
    return remove(tag, true);
}

error JSONSyncConfigDB::remove(const handle targetHandle)
{
    // Any config present with the given target handle?
    if (const auto* config = get(targetHandle))
    {
        // Yep, remove it.
        return remove(config->tag, true);
    }

    // Nope.
    return API_ENOENT;
}

error JSONSyncConfigDB::write(JSONSyncConfigIOContext& ioContext)
{
    JSONWriter writer;

    // Serialize the database.
    ioContext.serialize(mTagToConfig, writer);

    // Try and write the database out to disk.
    if (ioContext.write(mDBPath, writer.getstring(), mSlot) != API_OK)
    {
        // Couldn't write the database out to disk.
        return API_EWRITE;
    }

    // Rotate the slot.
    mSlot = (mSlot + 1) % NUM_SLOTS;

    // Up to date with disk.
    mDirty = false;

    return API_OK;
}

const JSONSyncConfig* JSONSyncConfigDB::add(const JSONSyncConfig& config,
                                            const bool flush)
{
    auto it = mTagToConfig.find(config.tag);

    // Do we already have a config with this tag?
    if (it != mTagToConfig.end())
    {
        // Has the config changed?
        if (config == it->second)
        {
            // Hasn't changed.
            return &it->second;
        }

        if (mObserver)
        {
            // Tell the observer a config's changed.
            mObserver->onChange(*this, it->second, config);

            // Tell the observer we need to be written.
            if (flush)
            {
                // But only if this change should be flushed.
                mObserver->onDirty(*this);
            }
        }

        // Mark the database as being dirty.
        mDirty |= flush;

        // Remove the existing config from the target index.
        mTargetToConfig.erase(it->second.targetHandle);

        // Update the config.
        it->second = config;

        // Sanity check.
        assert(mTargetToConfig.count(config.targetHandle) == 0);

        // Index the updated config by target, if possible.
        if (config.targetHandle != UNDEF)
        {
            mTargetToConfig.emplace(config.targetHandle, &it->second);
        }

        // We're done.
        return &it->second;
    }

    // Add the config to the database.
    auto result = mTagToConfig.emplace(config.tag, config);

    // Sanity check.
    assert(mTargetToConfig.count(config.targetHandle) == 0);

    // Index the new config by target, if possible.
    if (config.targetHandle != UNDEF)
    {
        mTargetToConfig.emplace(config.targetHandle, &result.first->second);
    }

    if (mObserver)
    {
        // Tell the observer we've added a config.
        mObserver->onAdd(*this, config);

        // Tell the observer we need to be written.
        if (flush)
        {
            // But only if this change should be flushed.
            mObserver->onDirty(*this);
        }
    }

    // Mark the database as being dirty.
    mDirty |= flush;

    // We're done.
    return &result.first->second;
}

void JSONSyncConfigDB::clear(const bool flush)
{
    // Are there any configs to remove?
    if (mTagToConfig.empty())
    {
        // Nope.
        return;
    }

    if (mObserver)
    {
        // Tell the observer we've removed the configs.
        for (auto& it : mTagToConfig)
        {
            mObserver->onRemove(*this, it.second);
        }

        // Tell the observer we need to be written.
        if (flush)
        {
            // But only if these changes should be flushed.
            mObserver->onDirty(*this);
        }
    }

    // Mark the database as being dirty.
    mDirty |= flush;

    // Clear the backup target handle index.
    mTargetToConfig.clear();

    // Clear the config database.
    mTagToConfig.clear();
}

error JSONSyncConfigDB::read(JSONSyncConfigIOContext& ioContext,
                             const unsigned int slot)
{
    // Try and read the database from the specified slot.
    string data;

    if (ioContext.read(mDBPath, data, slot) != API_OK)
    {
        // Couldn't read the database.
        return API_EREAD;
    }

    // Try and deserialize the configs contained in the database.
    JSONSyncConfigMap configs;
    JSON reader(data);

    if (!ioContext.deserialize(configs, reader))
    {
        // Couldn't deserialize the configs.
        return API_EREAD;
    }

    // Remove configs that aren't present on disk.
    auto i = mTagToConfig.begin();
    auto j = mTagToConfig.end();

    while (i != j)
    {
        const auto tag = i++->first;

        if (!configs.count(tag))
        {
            remove(tag, false);
        }
    }

    // Add (or update) configs.
    for (auto& it : configs)
    {
        // Correct config's drive path.
        it.second.drivePath = mDrivePath;

        // Add / update the config.
        add(it.second, false);
    }

    return API_OK;
}

error JSONSyncConfigDB::remove(const int tag, const bool flush)
{
    auto it = mTagToConfig.find(tag);

    // Any config present with the given tag?
    if (it == mTagToConfig.end())
    {
        // Nope.
        return API_ENOENT;
    }

    if (mObserver)
    {
        // Tell the observer we've removed a config.
        mObserver->onRemove(*this, it->second);

        // Tell the observer we need to be written.
        if (flush)
        {
            // But only if this change should be flushed.
            mObserver->onDirty(*this);
        }
    }

    // Mark the database as being dirty.
    mDirty |= flush;

    // Remove the config from the target handle index.
    mTargetToConfig.erase(it->second.targetHandle);

    // Remove the config from the database.
    mTagToConfig.erase(it);

    // We're done.
    return API_OK;
}

JSONSyncConfigIOContext::JSONSyncConfigIOContext(SymmCipher& cipher,
                                                 FileSystemAccess& fsAccess,
                                                 const string& key,
                                                 const string& name,
                                                 PrnGen& rng)
  : mCipher()
  , mFsAccess(fsAccess)
  , mName(LocalPath::fromPath(name, mFsAccess))
  , mRNG(rng)
  , mSigner()
{
    // These attributes *must* be sane.
    assert(!key.empty());
    assert(!name.empty());

    // Deserialize the key.
    string k = Base64::atob(key);
    assert(k.size() == SymmCipher::KEYLENGTH * 2);

    // Decrypt the key.
    cipher.ecb_decrypt(reinterpret_cast<byte*>(&k[0]), k.size());

    // Load the authenticaton key into our internal signer.
    const byte* ka = reinterpret_cast<const byte*>(&k[0]);
    const byte* ke = &ka[SymmCipher::KEYLENGTH];

    mSigner.setkey(ka, SymmCipher::KEYLENGTH);

    // Load the encryption key into our internal cipher.
    mCipher.setkey(ke, SymmCipher::KEYLENGTH);
}

JSONSyncConfigIOContext::~JSONSyncConfigIOContext()
{
}

bool JSONSyncConfigIOContext::deserialize(JSONSyncConfigMap& configs,
                                          JSON& reader) const
{
    if (!reader.enterarray())
    {
        return false;
    }

    // Deserialize the configs.
    while (reader.enterobject())
    {
        JSONSyncConfig config;

        if (!deserialize(config, reader))
        {
            return false;
        }

        // So move is well-defined.
        const auto tag = config.tag;

        configs.emplace(tag, std::move(config));
    }

    return reader.leavearray();
}

error JSONSyncConfigIOContext::get(const LocalPath& dbPath,
                                   vector<unsigned int>& slots)
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
        slotTimes.emplace_back(suffix - 0x30, fileAccess->mtime);
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
        slots.emplace_back(slotTime.first);
    }

    return API_OK;
}

error JSONSyncConfigIOContext::read(const LocalPath& dbPath,
                                    string& data,
                                    const unsigned int slot)
{
    using std::to_string;

    // Generate path to the configuration file.
    LocalPath path = dbPath;

    path.appendWithSeparator(mName, false);
    path.append(LocalPath::fromPath("." + to_string(slot), mFsAccess));

    // Try and open the file for reading.
    auto fileAccess = mFsAccess.newfileaccess(false);

    if (!fileAccess->fopen(path, true, false))
    {
        // Couldn't open the file for reading.
        return API_EREAD;
    }

    // Try and read the data from the file.
    string d;

    if (!fileAccess->fread(&d, fileAccess->size, 0, 0x0))
    {
        // Couldn't read the file.
        return API_EREAD;
    }

    // Try and decrypt the data.
    if (!decrypt(d, data))
    {
        // Couldn't decrypt the data.
        return API_EREAD;
    }

    return API_OK;
}

void JSONSyncConfigIOContext::serialize(const JSONSyncConfigMap& configs,
                                        JSONWriter& writer) const
{
    writer.beginarray();

    for (const auto& it : configs)
    {
        serialize(it.second, writer);
    }

    writer.endarray();
}

error JSONSyncConfigIOContext::write(const LocalPath& dbPath,
                                     const string& data,
                                     const unsigned int slot)
{
    using std::to_string;

    LocalPath path = dbPath;

    // Try and create the backup configuration directory.
    if (!(mFsAccess.mkdirlocal(path) || mFsAccess.target_exists))
    {
        // Couldn't create the directory and it doesn't exist.
        return API_EWRITE;
    }

    // Generate the rest of the path.
    path.appendWithSeparator(mName, false);
    path.append(LocalPath::fromPath("." + to_string(slot), mFsAccess));

    // Open the file for writing.
    auto fileAccess = mFsAccess.newfileaccess(false);

    if (!fileAccess->fopen(path, false, true))
    {
        // Couldn't open the file for writing.
        return API_EWRITE;
    }

    // Ensure the file is empty.
    if (!fileAccess->ftruncate())
    {
        // Couldn't truncate the file.
        return API_EWRITE;
    }

    // Encrypt the configuration data.
    const string d = encrypt(data);

    // Write the encrypted configuration data.
    auto* bytes = reinterpret_cast<const byte*>(&d[0]);

    if (!fileAccess->fwrite(bytes, d.size(), 0x0))
    {
        // Couldn't write out the data.
        return API_EWRITE;
    }

    return API_OK;
}

bool JSONSyncConfigIOContext::decrypt(const string& in, string& out)
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

    // For convenience.
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

bool JSONSyncConfigIOContext::deserialize(JSONSyncConfig& config, JSON& reader) const
{
    const auto TYPE_ENABLED       = MAKENAMEID2('e', 'n');
    const auto TYPE_FINGERPRINT   = MAKENAMEID2('f', 'p');
    const auto TYPE_HEARTBEAT_ID  = MAKENAMEID2('h', 'b');
    const auto TYPE_LAST_ERROR    = MAKENAMEID2('l', 'e');
    const auto TYPE_LAST_WARNING  = MAKENAMEID2('l', 'w');
    const auto TYPE_NAME          = MAKENAMEID1('n');
    const auto TYPE_SOURCE_PATH   = MAKENAMEID2('s', 'p');
    const auto TYPE_SYNC_TYPE     = MAKENAMEID2('s', 't');
    const auto TYPE_TAG           = MAKENAMEID1('t');
    const auto TYPE_TARGET_HANDLE = MAKENAMEID2('t', 'h');
    const auto TYPE_TARGET_PATH   = MAKENAMEID2('t', 'p');

    for ( ; ; )
    {
        switch (reader.getnameid())
        {
        case EOO:
            return true;

        case TYPE_ENABLED:
            config.enabled = reader.getbool();
            break;

        case TYPE_FINGERPRINT:
            config.fingerprint = reader.getfp();
            break;
 
        case TYPE_HEARTBEAT_ID:
            config.heartbeatID =
              reader.gethandle(sizeof(handle));
            break;

        case TYPE_LAST_ERROR:
            config.lastError =
              static_cast<SyncError>(reader.getint32());
            break;

        case TYPE_LAST_WARNING:
            config.lastWarning =
              static_cast<SyncWarning>(reader.getint32());
            break;

        case TYPE_NAME:
            reader.storebinary(&config.name);
            break;

        case TYPE_SOURCE_PATH:
        {
            string sourcePath;

            reader.storebinary(&sourcePath);
            
            config.sourcePath =
              LocalPath::fromPath(sourcePath, mFsAccess);

            break;
        }

        case TYPE_SYNC_TYPE:
            config.type =
              static_cast<SyncType>(reader.getint32());
            break;

        case TYPE_TAG:
            config.tag = reader.getint32();
            break;

        case TYPE_TARGET_HANDLE:
            config.targetHandle =
              reader.gethandle(sizeof(handle));
            break;

        case TYPE_TARGET_PATH:
            reader.storebinary(&config.targetPath);
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

string JSONSyncConfigIOContext::encrypt(const string& data)
{
    byte iv[SymmCipher::KEYLENGTH];

    // Generate initialization vector.
    mRNG.genblock(iv, sizeof(iv));

    string d;

    // Encrypt file using IV.
    mCipher.cbc_encrypt_pkcs_padding(&data, iv, &d);

    // Add IV to file.
    d.insert(d.end(), std::begin(iv), std::end(iv));

    byte mac[32];

    // Compute HMAC on file (including IV).
    mSigner.add(reinterpret_cast<const byte*>(&d[0]), d.size());
    mSigner.get(mac);

    // Add HMAC to file.
    d.insert(d.end(), std::begin(mac), std::end(mac));

    // We're done.
    return d;
}

void JSONSyncConfigIOContext::serialize(const JSONSyncConfig& config,
                                        JSONWriter& writer) const
{
    using std::to_string;

    // Encode path to avoid escaping issues.
    const string sourcePath =
      Base64::btoa(config.sourcePath.toPath(mFsAccess));
    const string name =
      Base64::btoa(config.name);
    const string targetPath =
      Base64::btoa(config.targetPath);

    writer.beginobject();

    writer.arg("sp", sourcePath);
    writer.arg("n",  name);
    writer.arg("tp", targetPath);
    writer.arg("fp", to_string(config.fingerprint), 0);
    writer.arg("hb", config.heartbeatID, sizeof(handle));
    writer.arg("th", config.targetHandle, sizeof(handle));
    writer.arg("le", config.lastError);
    writer.arg("lw", config.lastWarning);
    writer.arg("st", config.type);
    writer.arg("t",  config.tag);
    writer.arg("en", config.enabled);

    writer.endobject();
}

JSONSyncConfigDBObserver::JSONSyncConfigDBObserver()
{
}

JSONSyncConfigDBObserver::~JSONSyncConfigDBObserver()
{
}

} // namespace

#endif

