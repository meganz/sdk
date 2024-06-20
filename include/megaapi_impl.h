/**
 * @file megaapi_impl.h
 * @brief Private header file of the intermediate layer for the MEGA C++ SDK.
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

#ifndef MEGAAPI_IMPL_H
#define MEGAAPI_IMPL_H

#include <atomic>
#include <memory>

#include "mega.h"
#include "mega/gfx/external.h"
#include "megaapi.h"

#include "mega/heartbeats.h"

#define CRON_USE_LOCAL_TIME 1
#include "mega/mega_ccronexpr.h"

#ifdef HAVE_LIBUV
#include "uv.h"
#include "mega/mega_http_parser.h"
#include "mega/mega_evt_tls.h"

#endif

#ifndef _WIN32
#include <curl/curl.h>
#include <fcntl.h>
#endif

#if TARGET_OS_IPHONE
#include "mega/gfx/GfxProcCG.h"
#endif

// FUSE
#include <mega/fuse/common/mount_flags.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/service_flags.h>

////////////////////////////// SETTINGS //////////////////////////////
////////// Support for threads and mutexes
//Choose one of these options.
//Otherwise, C++11 threads and mutexes will be used
//#define USE_PTHREAD

////////// Support for thumbnails and previews.
//If you selected QT for threads and mutexes, it will be also used for thumbnails and previews
//You can create a subclass of MegaGfxProcessor and pass it to the constructor of MegaApi
//#define USE_FREEIMAGE
/////////////////////////// END OF SETTINGS ///////////////////////////

namespace mega
{

#if USE_PTHREAD
class MegaThread : public PosixThread {};
class MegaSemaphore : public PosixSemaphore {};
#else
class MegaThread : public CppThread {};
class MegaSemaphore : public CppSemaphore {};
#endif

#ifdef WIN32
    class MegaHttpIO : public CurlHttpIO {};
    class MegaFileSystemAccess : public WinFileSystemAccess {};
    class MegaWaiter : public WinWaiter {};
#else
    class MegaHttpIO : public CurlHttpIO {};
    class MegaWaiter : public PosixWaiter {};
    #ifdef __APPLE__
    class MegaFileSystemAccess : public MacFileSystemAccess {};
    #else
    class MegaFileSystemAccess : public LinuxFileSystemAccess {};
    #endif
#endif

#ifdef HAVE_LIBUV
class MegaTCPServer;
class MegaHTTPServer;
class MegaFTPServer;
#endif

typedef std::vector<int8_t> MegaSmallIntVector;
typedef std::multimap<int8_t, int8_t> MegaSmallIntMap;

class MegaDbAccess
  : public SqliteDbAccess
{
public:
    MegaDbAccess(const LocalPath& rootPath)
      : SqliteDbAccess(rootPath)
    {
    }
}; // MegaDbAccess

class MegaErrorPrivate : public MegaError
{
public:
    /**
     * @param errorCode: API MegaError API_* value or internal ErrorCodes enum
     */
    MegaErrorPrivate(int errorCode = MegaError::API_OK);

    /**
     * @param errorCode: API MegaError API_* value or internal ErrorCodes enum
     */
    MegaErrorPrivate(int errorCode, SyncError syncError);

#ifdef ENABLE_SYNC
    /**
     * @param errorCode: API MegaError API_* value or internal ErrorCodes enum
     */
    MegaErrorPrivate(int errorCode, MegaSync::Error syncError);
#endif
    /**
     * @param errorCode: API MegaError API_* value or internal ErrorCodes enum
     */
    MegaErrorPrivate(int errorCode, long long value);

    MegaErrorPrivate(const Error &err);
    MegaErrorPrivate(fuse::MountResult result);
    MegaErrorPrivate(const MegaError &megaError);
    ~MegaErrorPrivate() override;
    MegaError* copy() const override;
    int getErrorCode() const override;
    int getMountResult() const override;
    long long getValue() const override;
    bool hasExtraInfo() const override;
    long long getUserStatus() const override;
    long long getLinkStatus() const override;
    const char* getErrorString() const override;
    const char* toString() const override;
    const char* __str__() const override;
    const char* __toString() const override;

private:
    long long mValue = 0;
    long long mUserStatus = MegaError::UserErrorCode::USER_ETD_UNKNOWN;
    long long mLinkStatus = MegaError::LinkErrorCode::LINK_UNKNOWN;
    fuse::MountResult mMountResult = fuse::MOUNT_SUCCESS;
};

class MegaTransferPrivate;
class MegaTreeProcCopy : public MegaTreeProcessor
{
public:
    vector<NewNode> nn;
    unsigned nc = 0;
    bool allocated = false;

    MegaTreeProcCopy(MegaClient *client);
    bool processMegaNode(MegaNode* node) override;
    void allocnodes(void);

protected:
    MegaClient *client;
};


class MegaSizeProcessor : public MegaTreeProcessor
{
    protected:
        long long totalBytes;

    public:
        MegaSizeProcessor();
        bool processMegaNode(MegaNode* node) override;
        long long getTotalBytes();
};

class ExecuteOnce
{
    // An object to go on the requestQueue.
    // It could be completed early (eg on cancel()), in which case nothing happens when it's dequeued.
    // If not completed early, it executes on dequeue.
    // In either case the flag is set when executed, so it won't be executed in the other case.
    // An atomic type is used to make sure the flag is set and checked along with actual execution.
    // The objects referred to in the completion function must live until the first execution completes.
    // After that it doesn't matter if it contains dangling pointers etc as it won't be called anymore.
    std::function<void()> f;
    std::atomic_uint executed;

public:
    ExecuteOnce(std::function<void()> fn) : f(fn), executed(0) {}
    bool exec()
    {
        if (++executed > 1)
        {
            return false;
        }
        f();
        return true;  // indicates that this call is the time it ran
    }
};

class MegaRecursiveOperation : public MegaTransferListener
{
public:
    MegaRecursiveOperation(MegaClient* c) : mMegaapiThreadClient(c) {}
    virtual ~MegaRecursiveOperation() = default;
    virtual void start(MegaNode* node) = 0;

    void notifyStage(uint8_t stage);
    void ensureThreadStopped();

    // check if user has cancelled recursive operation by using cancelToken of associated transfer
    bool isCancelledByFolderTransferToken() const;

    // check if we have received onTransferFinishCallback for every transfersTotalCount
    bool allSubtransfersResolved()              { return  transfersFinishedCount >= transfersTotalCount; }

    // setter/getter for transfersTotalCount
    void setTransfersTotalCount (size_t count)  { transfersTotalCount = count; }
    size_t getTransfersTotalCount ()            { return transfersTotalCount; }

    // ---- MegaTransferListener methods ---
    void onTransferStart(MegaApi *, MegaTransfer *t) override;
    void onTransferUpdate(MegaApi *, MegaTransfer *t) override;
    void onTransferFinish(MegaApi*, MegaTransfer *t, MegaError *e) override;

protected:
    MegaApiImpl *megaApi;
    MegaTransferPrivate *transfer;
    MegaTransferListener *listener;
    int recursive;
    int tag;

    // number of sub-transfers finished with an error
    uint64_t mIncompleteTransfers = 0;

    // number of sub-transfers expected to be transferred (size of TransferQueue provided to sendPendingTransfers)
    // in case we detect that user cancelled recursive operation (via cancel token) at sendPendingTransfers,
    // those sub-transfers not processed yet (startxfer not called) will be discounted from transfersTotalCount
    size_t transfersTotalCount = 0;

    // number of sub-transfers started, onTransferStart received (startxfer called, and file injected into SDK transfer subsystem)
    size_t transfersStartedCount = 0;

    // number of sub-transfers finished, onTransferFinish received
    size_t transfersFinishedCount = 0;

    // flag to notify STAGE_TRANSFERRING_FILES to apps, when all sub-transfers have been queued in SDK core already
    bool startedTransferring = false;

    // If the thread was started, it queues a completion before exiting
    // That will be executed when the queued request is procesed
    // We also keep a pointer to it here, so cancel() can execute it early.
    shared_ptr<ExecuteOnce> mCompletionForMegaApiThread;

    // worker thread
    std::atomic_bool mWorkerThreadStopFlag { false };
    std::thread mWorkerThread;

    // thread id of MegaApiImpl thread
    std::thread::id mMainThreadId;

    // it's only safe to use this client ptr when on the MegaApiImpl's thread
    MegaClient* megaapiThreadClient();

    // set node handle for root folder in transfer
    void setRootNodeHandleInTransfer();

    // called from onTransferFinish for the last sub-transfer
    void complete(Error e, bool cancelledByUser = false);

    // return true if thread is stopped or canceled by transfer token
    bool isStoppedOrCancelled(const std::string& name) const;

private:
    // client ptr to only be used from the MegaApiImpl's thread
    MegaClient* mMegaapiThreadClient;
};

class TransferQueue;
class MegaFolderUploadController : public MegaRecursiveOperation, public std::enable_shared_from_this<MegaFolderUploadController>
{
public:
    MegaFolderUploadController(MegaApiImpl *megaApi, MegaTransferPrivate *transfer);
    ~MegaFolderUploadController() override;

    // ---- MegaRecursiveOperation methods ---
    void start(MegaNode* node) override;

protected:
    unique_ptr<FileSystemAccess> fsaccess;

    // Random number generator and cipher to avoid using client's which would cause threading corruption
    PrnGen rng;
    SymmCipher tmpnodecipher;

    // temporal nodeHandle for uploads from App
    handle mCurrUploadId = 1;

    // generates a temporal nodeHandle for uploads from App
    handle nextUploadId();

    struct Tree
    {
        // represents the node name in case of folder type
        string folderName;

        // Only figure out the fs type per folder (and on the worker thread), as it is expensive
		FileSystemType fsType;

        // If there is already a cloud node with this name for this parent, this is set
        // It also becomes set after we have created a cloud node for this folder
        unique_ptr<MegaNode> megaNode;

        // true when children nodes of 'megaNode' are pre-loaded already
        bool childrenLoaded = false;

        // Otherwise this is the record we will send to create this folder
        NewNode newnode;

        // files to upload to this folder
        struct FileRecord {
            LocalPath lp;
            FileFingerprint fp;
            FileRecord(const LocalPath& a, const FileFingerprint& b) : lp(a), fp(b) {}
        };
        vector<FileRecord> files;

        // subfolders
        vector<unique_ptr<Tree>> subtrees;

        void recursiveCountFolders(unsigned& existing, unsigned& total)
        {
            total += 1;
            existing += megaNode ? 1 : 0;
            for (auto& n : subtrees) { n->recursiveCountFolders(existing, total); }
        }
    };
    Tree mUploadTree;

    /* Scan entire tree recursively, and retrieve folder structure and files to be uploaded.
     * A putnodes command can only add subtrees under same target, so in case we need to add
     * subtrees under different targets, this method will generate a subtree for each one.
     * This happens on the worker thread.
     */
    enum scanFolder_result { scanFolder_succeeded, scanFolder_cancelled, scanFolder_failed };
    scanFolder_result scanFolder(Tree& tree, LocalPath& localPath, uint32_t& foldercount, uint32_t& filecount);

    // Gathers up enough (but not too many) newnode records that are all descendants of a single folder
    // and can be created in a single operation.
    // Called from the main thread just before we send the next set of folder creation commands.
    enum batchResult { batchResult_cancelled, batchResult_requestSent, batchResult_batchesComplete, batchResult_stillRecursing };
    batchResult createNextFolderBatch(Tree& tree, vector<NewNode>& newnodes, uint32_t filecount, bool isBatchRootLevel);

    // Iterate through all pending files of each uploaded folder, and start all upload transfers
    bool genUploadTransfersForFiles(Tree& tree, TransferQueue& transferQueue);
};


class MegaScheduledCopyController : public MegaScheduledCopy, public MegaRequestListener, public MegaTransferListener
{
public:
    MegaScheduledCopyController(MegaApiImpl *megaApi, int tag, int folderTransferTag, handle parenthandle, const char *filename, bool attendPastBackups, const char *speriod, int64_t period=-1, int maxBackups = 10);
    MegaScheduledCopyController(MegaScheduledCopyController *backup);
    ~MegaScheduledCopyController() override;

    void update();
    void start(bool skip = false);
    void removeexceeding(bool currentoneOK);
    void abortCurrent();

    // MegaScheduledCopy interface
    MegaScheduledCopy *copy() override;
    const char *getLocalFolder() const override;
    MegaHandle getMegaHandle() const override;
    int getTag() const override;
    int64_t getPeriod() const override;
    const char *getPeriodString() const override;
    int getMaxBackups() const override;
    int getState() const override;
    long long getNextStartTime(long long oldStartTimeAbsolute = -1) const override;
    bool getAttendPastBackups() const override;
    MegaTransferList *getFailedTransfers() override;


    // MegaScheduledCopy setters
    void setLocalFolder(const std::string &value);
    void setMegaHandle(const MegaHandle &value);
    void setTag(int value);
    void setPeriod(const int64_t &value);
    void setPeriodstring(const std::string &value);
    void setMaxBackups(int value);
    void setState(int value);
    void setAttendPastBackups(bool value);

    //getters&setters
    int64_t getStartTime() const;
    void setStartTime(const int64_t &value);
    std::string getBackupName() const;
    void setBackupName(const std::string &value);
    int64_t getOffsetds() const;
    void setOffsetds(const int64_t &value);
    int64_t getLastbackuptime() const;
    void setLastbackuptime(const int64_t &value);
    int getFolderTransferTag() const;
    void setFolderTransferTag(int value);

    //convenience methods
    bool isBackup(std::string localname, std::string backupname) const;
    int64_t getTimeOfBackup(std::string localname) const;

protected:

    // common variables
    MegaApiImpl *megaApi;
    MegaClient *client;
    MegaScheduledCopyListener *backupListener;

    int state;
    int tag;
    int64_t lastwakeuptime;
    int64_t lastbackuptime; //ds absolute
    int pendingremovals;
    int folderTransferTag; //reused between backup instances
    std::string basepath;
    std::string backupName;
    handle parenthandle;
    int maxBackups;
    int64_t period;
    std::string periodstring;
    cron_expr ccronexpr;
    bool valid;
    int64_t offsetds; //times offset with epoch time?
    int64_t startTime; // when shall the next backup begin
    bool attendPastBackups;

    // backup instance related
    handle currentHandle;
    std::string currentName;
    std::list<LocalPath> pendingFolders;
    std::vector<MegaTransfer *> failedTransfers;
    int recursive;
    int pendingTransfers;
    int pendingTags;
    // backup instance stats
    int64_t currentBKStartTime;
    int64_t updateTime;
    long long transferredBytes;
    long long totalBytes;
    long long speed;
    long long meanSpeed;
    long long numberFiles; //number of files successfully uploaded
    long long totalFiles;
    long long numberFolders;


    // internal methods
    void onFolderAvailable(MegaHandle handle);
    bool checkCompletion();
    bool isBusy() const;
    int64_t getLastBackupTime();
    long long getNextStartTimeDs(long long oldStartTimeds = -1) const;

    std::string epochdsToString(int64_t rawtimeds) const;

    void clearCurrentBackupData();

public:
    void onRequestFinish(MegaApi* api, MegaRequest *request, MegaError *e) override;
    void onTransferStart(MegaApi *api, MegaTransfer *transfer) override;
    void onTransferUpdate(MegaApi *api, MegaTransfer *transfer) override;
    void onTransferTemporaryError(MegaApi *, MegaTransfer *t, MegaError* e) override;
    void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError *e) override;

    long long getNumberFolders() const override;
    void setNumberFolders(long long value);
    long long getNumberFiles() const override;
    void setNumberFiles(long long value);
    long long getMeanSpeed() const override;
    void setMeanSpeed(long long value);
    long long getSpeed() const override;
    void setSpeed(long long value);
    long long getTotalBytes() const override;
    void setTotalBytes(long long value);
    long long getTransferredBytes() const override;
    void setTransferredBytes(long long value);
    int64_t getUpdateTime() const override;
    void setUpdateTime(const int64_t &value);
    int64_t getCurrentBKStartTime() const override;
    void setCurrentBKStartTime(const int64_t &value);
    long long getTotalFiles() const override;
    void setTotalFiles(long long value);
    MegaScheduledCopyListener *getBackupListener() const;
    void setBackupListener(MegaScheduledCopyListener *value);
    cron_expr getCcronexpr() const;
    void setCcronexpr(const cron_expr &value);
    bool isValid() const;
    void setValid(bool value);
};

class MegaFolderDownloadController : public MegaRecursiveOperation, public std::enable_shared_from_this<MegaFolderDownloadController>
{
public:
    MegaFolderDownloadController(MegaApiImpl *megaApi, MegaTransferPrivate *transfer);
    ~MegaFolderDownloadController() override;

    // ---- MegaRecursiveOperation methods ---
    void start(MegaNode *node) override;

protected:
    unique_ptr<FileSystemAccess> fsaccess;

    struct LocalTree
    {
        LocalTree(LocalPath lp)
        {
            localPath = lp;
        }

        LocalPath localPath;
        vector<unique_ptr<MegaNode>> childrenNodes;
    };
    vector<LocalTree> mLocalTree;

    // Scan entire tree recursively, and retrieve folder structure and files to be downloaded.
    enum scanFolder_result { scanFolder_succeeded, scanFolder_cancelled, scanFolder_failed };
    scanFolder_result scanFolder(MegaNode *node, LocalPath& path, FileSystemType fsType, unsigned& fileAddedCount);

    // Create all local directories in one shot. This happens on the worker thread.
    std::unique_ptr<TransferQueue> createFolderGenDownloadTransfersForFiles(FileSystemType fsType, uint32_t fileCount, Error& e);

    // Iterate through all pending files, and adds all download transfers
    bool genDownloadTransfersForFiles(TransferQueue* transferQueue,
                                      LocalTree& folder,
                                      FileSystemType fsType,
                                      bool folderExists);
};

class MegaNodePrivate : public MegaNode, public Cacheable
{
    public:
    class PNDataPrivate : public MegaNode::PasswordNodeData
    {
    public:
        PNDataPrivate(const char* p, const char* n, const char* url, const char* un)
            : mPwd {p ? std::make_unique<std::string>(p) : nullptr},
              mNotes {n ? std::make_unique<std::string>(n) : nullptr},
              mURL {url ? std::make_unique<std::string>(url) : nullptr},
              mUserName {un ? std::make_unique<std::string>(un) : nullptr}
            {}

        virtual void setPassword(const char* pwd) override { mPwd.reset(); if (pwd) mPwd = std::make_unique<std::string>(pwd); }
        virtual void setNotes(const char* n)      override { mNotes.reset(); if (n) mNotes = std::make_unique<std::string>(n); }
        virtual void setUrl(const char* u)        override { mURL.reset(); if (u) mURL = std::make_unique<std::string>(u); }
        virtual void setUserName(const char* un)  override { mUserName.reset(); if (un) mUserName = std::make_unique<std::string>(un); }

        virtual const char* password() const override { return mPwd ? mPwd->c_str() : nullptr; }
        virtual const char* notes() const    override { return mNotes ? mNotes->c_str(): nullptr; }
        virtual const char* url() const      override { return mURL ? mURL->c_str() : nullptr; }
        virtual const char* userName() const override { return mUserName ? mUserName->c_str() : nullptr; }
    private:
        std::unique_ptr<std::string> mPwd, mNotes, mURL, mUserName;
    };

        MegaNodePrivate(const char *name, int type, int64_t size, int64_t ctime, int64_t mtime,
                        MegaHandle nodeMegaHandle, const std::string *nodekey, const std::string *fileattrstring,
                        const char *fingerprint, const char *originalFingerprint, MegaHandle owner, MegaHandle parentHandle = INVALID_HANDLE,
                        const char *privateauth = NULL, const char *publicauth = NULL, bool isPublic = true,
                        bool isForeign = false, const char *chatauth = NULL, bool isNodeDecrypted = true);

        MegaNodePrivate(MegaNode *node);
        ~MegaNodePrivate() override;
        int getType() const override;
        const char* getName() override;
        const char* getFingerprint() override;
        const char* getOriginalFingerprint() override;
        bool hasCustomAttrs() override;
        MegaStringList *getCustomAttrNames() override;
        const char *getCustomAttr(const char* attrName) override;
        int getDuration() override;
        int getWidth() override;
        bool isFavourite() override;
        bool isMarkedSensitive() override;
        int getLabel() override;
        int getHeight() override;
        int getShortformat() override;
        int getVideocodecid() override;
        double getLatitude() override;
        double getLongitude() override;
        const char* getDescription() override;
        MegaStringList* getTags() override;
        char *getBase64Handle() override;
        int64_t getSize() override;
        int64_t getCreationTime() override;
        int64_t getModificationTime() override;
        MegaHandle getHandle() const override;
        MegaHandle getRestoreHandle() override;
        MegaHandle getParentHandle() override;
        std::string* getNodeKey() override;
        bool isNodeKeyDecrypted() override;
        char *getBase64Key() override;
        char* getFileAttrString() override;
        int64_t getExpirationTime() override;
        MegaHandle getPublicHandle() override;
        MegaNode* getPublicNode() override;
        char *getPublicLink(bool includeKey = true) override;
        int64_t getPublicLinkCreationTime() override;
        const char * getWritableLinkAuthKey() override;

        bool isNewLinkFormat();
        bool isFile() override;
        bool isFolder() override;
        bool isRemoved() override;
        bool hasChanged(uint64_t changeType) override;
        uint64_t getChanges() override;
        bool hasThumbnail() override;
        bool hasPreview() override;
        bool isPublic() override;
        bool isExported() override;
        bool isExpired() override;
        bool isTakenDown() override;
        bool isForeign() override;
        bool isPasswordNode() const override;
        PasswordNodeData* getPasswordData() const override;
        std::string* getPrivateAuth() override;
        MegaNodeList *getChildren() override;
        void setPrivateAuth(const char *privateAuth) override;
        void setPublicAuth(const char *publicAuth);
        void setChatAuth(const char *chatAuth);
        void setForeign(bool foreign);
        void setChildren(MegaNodeList *children);
        void setName(const char *newName);
        std::string* getPublicAuth() override;
        const char *getChatAuth() override;
        bool isShared() override;
        bool isOutShare() override;
        bool isInShare() override;
        std::string* getSharekey();
        MegaHandle getOwner() const override;
        const char* getDeviceId() const override;
        const char* getS4() const override;

        static MegaNode *fromNode(Node *node);
        MegaNode *copy() override;

        char *serialize() override;
        bool serialize(string*) const override;  // only FILENODEs
        static MegaNodePrivate* unserialize(string*);  // only FILENODEs

        static string removeAppPrefixFromFingerprint(const char* appFingerprint, m_off_t* nodeSize = nullptr);
        static string addAppPrefixToFingerprint(const string& fingerprint, const m_off_t nodeSize);

    protected:
        MegaNodePrivate(Node *node);
        const char* getAttrFrom(const char *attrName, const attr_map* attrMap) const;
        const char *getOfficialAttr(const char* attrName) const;

        int type;
        const char *name;
        const char *fingerprint;
        const char *originalfingerprint;
        attr_map *customAttrs;
        std::unique_ptr<attr_map> mOfficialAttrs;
        int64_t size;
        int64_t ctime;
        int64_t mtime;
        MegaHandle nodehandle;
        MegaHandle parenthandle;
        MegaHandle restorehandle = UNDEF;
        std::string nodekey;
        std::string fileattrstring;
        std::string privateAuth;
        std::string publicAuth;
        std::string mDeviceId;
        std::string mS4;
        const char *chatAuth;
        uint64_t changed;
        struct {
            bool thumbnailAvailable : 1;
            bool previewAvailable : 1;
            bool isPublicNode : 1;
            bool outShares : 1;
            bool inShare : 1;
            bool foreign : 1;
        };
        PublicLink *plink;
        bool mNewLinkFormat;
        std::string *sharekey;   // for plinks of folders
        int duration;
        int width;
        int height;
        int shortformat;
        int videocodecid;
        double latitude;
        double longitude;
        MegaNodeList *children;
        MegaHandle owner;
        bool mFavourite;
        bool mMarkedSensitive = false; // sensitive attribute set on this node
        nodelabel_t mLabel;
        bool mIsNodeKeyDecrypted = false;
};


class MegaBackupInfoPrivate : public MegaBackupInfo
{
public:
    MegaBackupInfoPrivate(const CommandBackupSyncFetch::Data& d) : mData(d) {}

    MegaHandle id() const override { return mData.backupId; }
    int type() const override { return mData.backupType; }
    MegaHandle root() const override { return mData.rootNode; }
    const char* localFolder() const override { return mData.localFolder.c_str(); }
    const char* deviceId() const override { return mData.deviceId.c_str(); }
    const char* deviceUserAgent() const override { return mData.deviceUserAgent.c_str(); }
    int state() const override { return mData.syncState; }
    int substate() const override { return mData.syncSubstate; }
    const char* extra() const override { return mData.extra.c_str(); }
    const char* name() const override { return mData.backupName.c_str(); }
    uint64_t ts() const override { return mData.hbTimestamp; }
    int status() const override { return mData.hbStatus; }
    int progress() const override { return mData.hbProgress; }
    int uploads() const override { return mData.uploads; }
    int downloads() const override { return mData.downloads; }
    uint64_t activityTs() const override { return mData.lastActivityTs; }
    MegaHandle lastSync() const override { return mData.lastSyncedNodeHandle; }

    MegaBackupInfoPrivate* copy() const override { return new MegaBackupInfoPrivate(*this); }

private:
    const CommandBackupSyncFetch::Data mData;
};


class MegaBackupInfoListPrivate : public MegaBackupInfoList
{
public:
    MegaBackupInfoListPrivate(const std::vector<CommandBackupSyncFetch::Data>& d)
    {
        mBackups.reserve(d.size());
        for (const auto& bd : d)
        {
            mBackups.emplace_back(bd);
        }
    }

    MegaBackupInfoListPrivate* copy() const override { return new MegaBackupInfoListPrivate(*this); }

    const MegaBackupInfo* get(unsigned i) const override { return i < size() ? &mBackups[i] : nullptr; }
    unsigned size() const override { return (unsigned)mBackups.size(); }

private:
    vector<MegaBackupInfoPrivate> mBackups;
};


class MegaSetPrivate : public MegaSet
{
public:

    MegaSetPrivate(const Set& s)
        : mId(s.id()), mPublicId(s.publicId()), mUser(s.user()), mTs(s.ts()), mCTs(s.cts()),
          mName(s.name()), mCover(s.cover()), mChanges(s.changes()), mType(s.type()) {}

    MegaHandle id() const override { return mId; }
    MegaHandle publicId() const override { return mPublicId; }
    MegaHandle user() const override { return mUser; }
    int64_t ts() const override { return mTs; }
    int64_t cts() const override { return mCTs; }
    int type() const override { return static_cast<int>(mType); }
    const char* name() const override { return mName.c_str(); }
    MegaHandle cover() const override { return mCover; }

    bool hasChanged(uint64_t changeType) const override;
    uint64_t getChanges() const override { return mChanges.to_ullong(); }
    bool isExported() const override { return mPublicId != UNDEF; }

    MegaSet* copy() const override { return new MegaSetPrivate(*this); }

private:
    MegaHandle mId;
    MegaHandle mPublicId;
    MegaHandle mUser;
    m_time_t mTs;
    m_time_t mCTs;
    string mName;
    MegaHandle mCover;
    std::bitset<Set::CH_SIZE> mChanges;
    Set::SetType mType;
};


class MegaSetListPrivate : public MegaSetList
{
public:
    MegaSetListPrivate(const Set *const* sets, int count); // ptr --> const ptr --> const Set
    MegaSetListPrivate(const map<handle, Set>& sets);

    void add(MegaSetPrivate&& s);
    MegaSetList* copy() const override { return new MegaSetListPrivate(*this); }

    const MegaSet* get(unsigned i) const override { return i < size() ? &mSets[i] : nullptr; }
    unsigned size() const override { return (unsigned)mSets.size(); }

private:
    vector<MegaSetPrivate> mSets;
};


class MegaSetElementPrivate : public MegaSetElement
{
public:
    MegaSetElementPrivate(const SetElement& el)
        : mId(el.id()), mNode(el.node()), mSetId(el.set()), mOrder(el.order()), mTs(el.ts()),
          mName(el.name()), mChanges(el.changes())
        {}

    MegaHandle id() const override { return mId; }
    MegaHandle node() const override { return mNode; }
    MegaHandle setId() const override { return mSetId; }
    int64_t order() const override { return mOrder; }
    int64_t ts() const override { return mTs; }
    const char* name() const override { return mName.c_str(); }

    bool hasChanged(uint64_t changeType) const override;
    uint64_t getChanges() const override { return mChanges.to_ullong(); }

    virtual MegaSetElement* copy() const override { return new MegaSetElementPrivate(*this); }

private:
    MegaHandle mId;
    MegaHandle mNode;
    MegaHandle mSetId;
    int64_t mOrder;
    m_time_t mTs;
    string mName;
    std::bitset<SetElement::CH_EL_SIZE> mChanges;
};


class MegaSetElementListPrivate : public MegaSetElementList
{
public:
    MegaSetElementListPrivate(const SetElement *const* elements, int count); // ptr --> const ptr --> const SetElement
    MegaSetElementListPrivate(const elementsmap_t* elements, const std::function<bool(handle)>& filterOut = nullptr);

    void add(MegaSetElementPrivate&& el);
    MegaSetElementList* copy() const override { return new MegaSetElementListPrivate(*this); }

    const MegaSetElement* get(unsigned i) const override { return i < size() ? &mElements[i] : nullptr; }
    unsigned size() const override { return (unsigned)mElements.size(); }

private:
    vector<MegaSetElementPrivate> mElements;
};


class MegaUserPrivate : public MegaUser
{
	public:
		MegaUserPrivate(User *user);
		MegaUserPrivate(MegaUser *user);
		static MegaUser *fromUser(User *user);
        MegaUser *copy() override;

        ~MegaUserPrivate() override;
        const char* getEmail() override;
        MegaHandle getHandle() override;
        int getVisibility() override;
        int64_t getTimestamp() override;
        bool hasChanged(uint64_t changeType) override;
        uint64_t getChanges() override;
        int isOwnChange() override;

	protected:
		const char *email;
        MegaHandle handle;
        int visibility;
        int64_t ctime;
        uint64_t changed;
        int tag;
};

class MegaUserAlertPrivate : public MegaUserAlert
{
public:
    MegaUserAlertPrivate(UserAlert::Base* user, MegaClient* mc);
    //MegaUserAlertPrivate(const MegaUserAlertPrivate&); // default copy works for this type
    MegaUserAlert* copy() const override;

    unsigned getId() const override;
    bool getSeen() const override;
    bool getRelevant() const override;
    int getType() const override;
    const char *getTypeString() const override;
    MegaHandle getUserHandle() const override;
    MegaHandle getNodeHandle() const override;
    const char* getEmail() const override;
    const char* getPath() const override;
    const char* getName() const override;
    const char* getHeading() const override;
    const char* getTitle() const override;
    int64_t getNumber(unsigned index) const override;
    int64_t getTimestamp(unsigned index) const override;
    const char* getString(unsigned index) const override;
    MegaHandle getHandle(unsigned index) const override;
#ifdef ENABLE_CHAT
    MegaHandle getSchedId() const override;
    bool hasSchedMeetingChanged(uint64_t changeType) const override;
    MegaStringList* getUpdatedTitle() const override;
    MegaStringList* getUpdatedTimeZone() const override;
    MegaIntegerList* getUpdatedStartDate() const override;
    MegaIntegerList* getUpdatedEndDate() const override;
#endif
    bool isOwnChange() const override;
    bool isRemoved() const override;
    MegaHandle getPcrHandle() const override;

protected:
    unsigned id;
    bool seen;
    bool relevant;
    int type;
    int tag;
    string heading;
    string title;
    handle userHandle;
    string email;
    handle nodeHandle;
    handle mPcrHandle = UNDEF;
    string nodePath;
    string nodeName;
    vector<int64_t> numbers;
    vector<int64_t> timestamps;
    vector<string> extraStrings;
    vector<MegaHandle> handles;
    bool removed = false;
    handle schedMeetingId = UNDEF;
#ifdef ENABLE_CHAT
    UserAlert::UpdatedScheduledMeeting::Changeset schedMeetingChangeset;
#endif
};

class MegaHandleListPrivate : public MegaHandleList
{
public:
    MegaHandleListPrivate();
    MegaHandleListPrivate(const MegaHandleListPrivate *hList);
    ~MegaHandleListPrivate() override;
    MegaHandleListPrivate(const vector<handle> &handles);

    MegaHandleList *copy() const override;
    MegaHandle get(unsigned int i) const override;
    unsigned int size() const override;
    void addMegaHandle(MegaHandle megaHandle) override;

private:
    std::vector<MegaHandle> mList;
};

class MegaIntegerListPrivate : public MegaIntegerList
{
public:
    MegaIntegerListPrivate();
    MegaIntegerListPrivate(const vector<int8_t>& bytesList);
    MegaIntegerListPrivate(const vector<int64_t>& integerList);
    MegaIntegerListPrivate(const vector<uint32_t>& integerList);
    ~MegaIntegerListPrivate() override;
    MegaSmallIntVector* toByteList() const;
    MegaIntegerList *copy() const override;
    void add(long long i) override;
    int64_t get(int i) const override;
    int size() const override;
    const vector<int64_t>* getList() const;

private:
    vector<int64_t> mIntegers;
};

class MegaSharePrivate : public MegaShare
{
	public:
        static MegaShare *fromShare(MegaHandle nodeMegaHandle, Share *share, bool verified);
        MegaShare *copy() override;
        ~MegaSharePrivate() override;
        const char *getUser() override;
        MegaHandle getNodeHandle() override;
        int getAccess() override;
        int64_t getTimestamp() override;
        bool isPending() override;
        bool isVerified() override;

	protected:
        MegaSharePrivate(MegaHandle nodehandle, Share *share, bool verified);
		MegaSharePrivate(MegaShare *share);

		MegaHandle nodehandle;
		const char *user;
		int access;
		int64_t ts;
        bool pending;
        bool mVerified;
};

class MegaCancelTokenPrivate : public MegaCancelToken
{
public:

    // The default constructor leaves the token empty, so we don't waste space when it may not be needed (eg. a request object not related to transfers)
    MegaCancelTokenPrivate();

    // Use this one to actually embed a token
    MegaCancelTokenPrivate(CancelToken);

    void cancel() override;
    bool isCancelled() const override;

    MegaCancelTokenPrivate* existencePtr() { return cancelFlag.exists() ? this : nullptr; }

    CancelToken cancelFlag;
};

inline CancelToken convertToCancelToken(MegaCancelToken* mct)
{
    if (!mct) return CancelToken();
    return static_cast<MegaCancelTokenPrivate*>(mct)->cancelFlag;
}

class CollisionChecker
{
public:
    enum class Option
    {
        Begin   = 1,
        AssumeSame      = 1,
        AlwaysError     = 2,
        Fingerprint     = 3,
        Metamac         = 4,
        AssumeDifferent = 5,
        End     = 6,
    };

    enum class Result
    {
        NotYet      = 1,                // Not checked yet
        Skip        = 2,                // Skip it
        ReportError = 3,                // Report Error
        Download    = 4,                // Download it
    };

    // Use faGetter instead of a FileAcccess instance which delays the access to the file system and only does it based
    // on demand by check. This helps in a network folder.
    static Result check(FileSystemAccess* fsaccess, const LocalPath &fileLocalPath, MegaNode* fileNode, Option option);
    static Result check(std::function<FileAccess* ()> faGetter, MegaNode* fileNode, Option option);
    static Result check(std::function<FileAccess* ()> faGetter, Node* node, Option option);

private:
    static Result check(std::function<bool()> fingerprintEqualF, std::function<bool()> metamacEqualF, Option option);
    static bool CompareLocalFileMetaMac(FileAccess* fa, MegaNode* fileNode);
};

class MegaTransferPrivate : public MegaTransfer, public Cacheable
{
	public:
        MegaTransferPrivate(int type, MegaTransferListener *listener = NULL);
        MegaTransferPrivate(const MegaTransferPrivate *transfer);
        ~MegaTransferPrivate() override;

        MegaTransfer *copy() override;
	    Transfer *getTransfer() const;
        void setTransfer(Transfer *transfer);
        void setStartTime(int64_t startTime);
		void setTransferredBytes(long long transferredBytes);
		void setTotalBytes(long long totalBytes);
		void setPath(const char* path);
		void setParentPath(const char* path);
        void setNodeHandle(MegaHandle nodeHandle);
        void setParentHandle(MegaHandle parentHandle);
		void setStartPos(long long startPos);
		void setEndPos(long long endPos);
		void setNumRetry(int retry);
        void setStage(unsigned mStage);
		void setMaxRetries(int retry);
        void setTime(int64_t time);
		void setFileName(const char* fileName);
		void setTag(int tag);
		void setSpeed(long long speed);
        void setMeanSpeed(long long meanSpeed);
		void setDeltaSize(long long deltaSize);
        void setUpdateTime(int64_t updateTime);
        void setPublicNode(MegaNode *publicNode, bool copyChildren = false);
        void setNodeToUndelete(MegaNode* toUndelete);
        void setSyncTransfer(bool syncTransfer);
        void setSourceFileTemporary(bool temporary);
        void setStartFirst(bool startFirst);
        void setBackupTransfer(bool backupTransfer);
        void setForeignOverquota(bool backupTransfer);
        void setForceNewUpload(bool forceNewUpload);
        void setStreamingTransfer(bool streamingTransfer);
        void setLastBytes(char *lastBytes);
        void setLastError(const MegaError *e);
        void setFolderTransferTag(int tag);
        void setNotificationNumber(long long notificationNumber);
        void setListener(MegaTransferListener *listener);
        void setTargetOverride(bool targetOverride);
        void setCancelToken(CancelToken);
        void setCollisionCheck(CollisionChecker::Option);
        void setCollisionCheck(int);
        void setCollisionCheckResult(CollisionChecker::Result);
        void setCollisionResolution(CollisionResolution);
        void setCollisionResolution(int);
        void setFileSystemType(FileSystemType fsType) { mFsType = fsType; }

        int getType() const override;
        const char * getTransferString() const override;
        const char* toString() const override;
        const char* __str__() const override;
        const char* __toString() const override;
        virtual int64_t getStartTime() const override;
        long long getTransferredBytes() const override;
        long long getTotalBytes() const override;
        const char* getPath() const override;
        const char* getParentPath() const override;
        MegaHandle getNodeHandle() const override;
        MegaHandle getParentHandle() const override;
        long long getStartPos() const override;
        long long getEndPos() const override;
        const char* getFileName() const override;
        MegaTransferListener* getListener() const override;
        int getNumRetry() const override;
        int getMaxRetries() const override;
        unsigned getStage() const override;
        virtual int64_t getTime() const;
        int getTag() const override;
        long long getSpeed() const override;
        long long getMeanSpeed() const override;
        long long getDeltaSize() const override;
        int64_t getUpdateTime() const override;
        virtual MegaNode *getPublicNode() const;
        MegaNode *getPublicMegaNode() const override;
        bool isSyncTransfer() const override;
        bool isStreamingTransfer() const override;
        bool isFinished() const override;
        virtual bool isSourceFileTemporary() const;
        virtual bool shouldStartFirst() const;
        bool isBackupTransfer() const override;
        bool isForeignOverquota() const override;
        bool isForceNewUpload() const override;
        char *getLastBytes() const override;
        MegaError getLastError() const override;
        const MegaError *getLastErrorExtended() const override;
        bool isFolderTransfer() const override;
        int getFolderTransferTag() const override;
        virtual void setAppData(const char *data);
        const char* getAppData() const override;
        virtual void setState(int state);
        int getState() const override;
        virtual void setPriority(unsigned long long p);
        unsigned long long getPriority() const override;
        long long getNotificationNumber() const override;
        bool getTargetOverride() const override;

        bool serialize(string*) const override;
        static MegaTransferPrivate* unserialize(string*);

        void startRecursiveOperation(shared_ptr<MegaRecursiveOperation>, MegaNode* node); // takes ownership of both
        void stopRecursiveOperationThread();

        long long getPlaceInQueue() const;
        void setPlaceInQueue(long long value);

        MegaCancelToken* getCancelToken() override;
        bool isRecursive() const { return recursiveOperation.get() != nullptr; }
        size_t getTotalRecursiveOperation() const;

        CancelToken& accessCancelToken() { return mCancelToken.cancelFlag; }

        CollisionChecker::Option    getCollisionCheck() const;
        CollisionChecker::Result    getCollisionCheckResult() const;
        CollisionResolution         getCollisionResolution() const;
        FileSystemType              getFileSystemType() const { return mFsType; };

        MegaNode* getNodeToUndelete() const;

        // for uploads, we fingerprint the file before queueing
        // as that way, it can be done without the main mutex locked
        error fingerprint_error = API_OK;

        nodetype_t fingerprint_filetype = TYPE_UNKNOWN;
        FileFingerprint fingerprint_onDisk;

protected:
        int type;
        int tag;
        int state;
        uint64_t priority;
        CollisionChecker::Option    mCollisionCheck;
        CollisionResolution         mCollisionResolution;
        CollisionChecker::Result    mCollisionCheckResult;
        FileSystemType              mFsType;

        struct
        {
            bool syncTransfer : 1;
            bool streamingTransfer : 1;
            bool temporarySourceFile : 1;
            bool startFirst : 1;
            bool backupTransfer : 1;
            bool foreignOverquota : 1;
            bool forceNewUpload : 1;
        };

        int64_t startTime;
        int64_t updateTime;
        int64_t time;
        long long transferredBytes;
        long long totalBytes;
        long long speed;
        long long meanSpeed;
        long long deltaSize;
        long long notificationNumber;
        MegaHandle nodeHandle;
        MegaHandle parentHandle;
        const char* path;
        const char* parentPath; //used as targetUser for uploads
        const char* fileName;
        char *lastBytes;
        MegaNode *publicNode;
        std::unique_ptr<MegaNode> nodeToUndelete;
        long long startPos;
        long long endPos;
        int retry;
        int maxRetries;

        long long placeInQueue = 0;

        MegaTransferListener *listener;
        Transfer *transfer = nullptr;
        std::unique_ptr<MegaError> lastError;
        MegaCancelTokenPrivate mCancelToken;  // default-constructed with no actual token inside
        int folderTransferTag;
        const char* appData;
        uint8_t mStage;

        bool mTargetOverride;

    public:
        // use shared_ptr here so callbacks can use a weak_ptr
        // to protect against the operation being cancelled in the meantime
        shared_ptr<MegaRecursiveOperation> recursiveOperation;

};

class MegaTransferDataPrivate : public MegaTransferData
{
public:
    MegaTransferDataPrivate(TransferList *transferList, long long notificationNumber);
    MegaTransferDataPrivate(const MegaTransferDataPrivate *transferData);

    ~MegaTransferDataPrivate() override;
    MegaTransferData *copy() const override;
    int getNumDownloads() const override;
    int getNumUploads() const override;
    int getDownloadTag(int i) const override;
    int getUploadTag(int i) const override;
    unsigned long long getDownloadPriority(int i) const override;
    unsigned long long getUploadPriority(int i) const override;
    long long getNotificationNumber() const override;

protected:
    int numDownloads;
    int numUploads;
    long long notificationNumber;
    vector<int> downloadTags;
    vector<int> uploadTags;
    vector<uint64_t> downloadPriorities;
    vector<uint64_t> uploadPriorities;
};

class MegaFolderInfoPrivate : public MegaFolderInfo
{
public:
    MegaFolderInfoPrivate(int numFiles, int numFolders, int numVersions, long long currentSize, long long versionsSize);
    MegaFolderInfoPrivate(const MegaFolderInfoPrivate *folderData);

    ~MegaFolderInfoPrivate() override;

    MegaFolderInfo *copy() const override;

    int getNumVersions() const override;
    int getNumFiles() const override;
    int getNumFolders() const override;
    long long getCurrentSize() const override;
    long long getVersionsSize() const override;

protected:
    int numFiles;
    int numFolders;
    int numVersions;
    long long currentSize;
    long long versionsSize;
};

class MegaTimeZoneDetailsPrivate : public MegaTimeZoneDetails
{
public:
    MegaTimeZoneDetailsPrivate(vector<string>* timeZones, vector<int> *timeZoneOffsets, int defaultTimeZone);
    MegaTimeZoneDetailsPrivate(const MegaTimeZoneDetailsPrivate *timeZoneDetails);

    ~MegaTimeZoneDetailsPrivate() override;
    MegaTimeZoneDetails *copy() const override;

    int getNumTimeZones() const override;
    const char *getTimeZone(int index) const override;
    int getTimeOffset(int index) const override;
    int getDefault() const override;

protected:
    int defaultTimeZone;
    vector<string> timeZones;
    vector<int> timeZoneOffsets;
};

class MegaPushNotificationSettingsPrivate : public MegaPushNotificationSettings
{
public:
    MegaPushNotificationSettingsPrivate(const std::string &settingsJSON);
    MegaPushNotificationSettingsPrivate();
    MegaPushNotificationSettingsPrivate(const MegaPushNotificationSettingsPrivate *settings);

    std::string generateJson() const;
    bool isValid() const;

    ~MegaPushNotificationSettingsPrivate() override;
    MegaPushNotificationSettings *copy() const override;

private:
    m_time_t mGlobalDND = -1;        // defaults to -1 if not defined
    int mGlobalScheduleStart = -1;   // defaults to -1 if not defined
    int mGlobalScheduleEnd = -1;     // defaults to -1 if not defined
    std::string mGlobalScheduleTimezone;

    std::map<MegaHandle, m_time_t> mChatDND;
    std::map<MegaHandle, bool> mChatAlwaysNotify;

    m_time_t mContactsDND = -1;      // defaults to -1 if not defined
    m_time_t mSharesDND = -1;        // defaults to -1 if not defined
    m_time_t mGlobalChatsDND = -1;        // defaults to -1 if not defined

    bool mJsonInvalid = false;  // true if ctor from JSON find issues

public:

    // getters

    bool isGlobalEnabled() const override;
    bool isGlobalDndEnabled() const override;
    bool isGlobalChatsDndEnabled() const override;
    int64_t getGlobalDnd() const override;
    int64_t getGlobalChatsDnd() const override;
    bool isGlobalScheduleEnabled() const override;
    int getGlobalScheduleStart() const override;
    int getGlobalScheduleEnd() const override;
    const char *getGlobalScheduleTimezone() const override;

    bool isChatEnabled(MegaHandle chatid) const override;
    bool isChatDndEnabled(MegaHandle chatid) const override;
    int64_t getChatDnd(MegaHandle chatid) const override;
    bool isChatAlwaysNotifyEnabled(MegaHandle chatid) const override;

    bool isContactsEnabled() const override;
    bool isSharesEnabled() const override;
    bool isChatsEnabled() const override;

    // setters

    void enableGlobal(bool enable) override;
    void setGlobalDnd(int64_t timestamp) override;
    void disableGlobalDnd() override;
    void setGlobalSchedule(int start, int end, const char *timezone) override;
    void disableGlobalSchedule() override;

    void enableChat(MegaHandle chatid, bool enable) override;
    void setChatDnd(MegaHandle chatid, int64_t timestamp) override;
    void setGlobalChatsDnd(int64_t timestamp) override;
    void enableChatAlwaysNotify(MegaHandle chatid, bool enable) override;

    void enableContacts(bool enable) override;
    void enableShares(bool enable) override;
    void enableChats(bool enable) override;
};

class MegaContactRequestPrivate : public MegaContactRequest
{
public:
    MegaContactRequestPrivate(PendingContactRequest *request);
    MegaContactRequestPrivate(const MegaContactRequest *request);
    ~MegaContactRequestPrivate() override;

    static MegaContactRequest *fromContactRequest(PendingContactRequest *request);
    MegaContactRequest *copy() const override;

    MegaHandle getHandle() const override;
    char* getSourceEmail() const override;
    char* getSourceMessage() const override;
    char* getTargetEmail() const override;
    int64_t getCreationTime() const override;
    int64_t getModificationTime() const override;
    int getStatus() const override;
    bool isOutgoing() const override;
    bool isAutoAccepted() const override;

protected:
    MegaHandle handle;
    char* sourceEmail;
    char* sourceMessage;
    char* targetEmail;
    int64_t creationTime;
    int64_t modificationTime;
    int status;
    bool outgoing;
    bool autoaccepted;
};

#ifdef ENABLE_SYNC

class MegaSyncPrivate : public MegaSync
{
public:
    MegaSyncPrivate(const SyncConfig& config, MegaClient* client);
    MegaSyncPrivate(MegaSyncPrivate *sync);

    ~MegaSyncPrivate() override;

    MegaSync *copy() override;

    MegaHandle getMegaHandle() const override;
    void setMegaHandle(MegaHandle handle);
    const char* getLocalFolder() const override;
    void setLocalFolder(const char*path);
    const char* getName() const override;
    void setName(const char*name);
    const char* getLastKnownMegaFolder() const override;
    void setLastKnownMegaFolder(const char *path);
    MegaHandle getBackupId() const override;
    void setBackupId(MegaHandle backupId);

    int getError() const override;
    void setError(int error);
    int getWarning() const override;
    void setWarning(int warning);

    int getType() const override;
    void setType(SyncType type);

    int getRunState() const override;

    MegaSync::SyncRunningState mRunState = SyncRunningState::RUNSTATE_DISABLED;

protected:
    MegaHandle megaHandle;
    char *localFolder;
    char *mName;
    char *lastKnownMegaFolder;

    SyncType mType = TYPE_UNKNOWN;

    //holds error cause
    int mError = NO_SYNC_ERROR;
    int mWarning = NO_SYNC_WARNING;

    handle mBackupId = UNDEF;
};

class MegaSyncStatsPrivate : public MegaSyncStats
{
    handle backupId;
    PerSyncStats stats;
public:
    MegaSyncStatsPrivate(handle bid, const PerSyncStats& s) : backupId(bid), stats(s) {}
    MegaHandle getBackupId() const override { return backupId; }
    bool isScanning() const override { return stats.scanning; }
    bool isSyncing() const override { return stats.syncing; }
    int getFolderCount() const override { return stats.numFolders; }
    int getFileCount() const override { return stats.numFiles; }
    int getUploadCount() const override { return stats.numUploads; }
    int getDownloadCount() const override { return stats.numDownloads; }
    MegaSyncStatsPrivate *copy() const override { return new MegaSyncStatsPrivate(*this); }
};


class MegaSyncListPrivate : public MegaSyncList
{
    public:
        MegaSyncListPrivate();
        MegaSyncListPrivate(MegaSyncPrivate **newlist, int size);
        MegaSyncListPrivate(const MegaSyncListPrivate *syncList);
        ~MegaSyncListPrivate() override;
        MegaSyncList *copy() const override;
        MegaSync* get(int i) const override;
        int size() const override;

        void addSync(MegaSync* sync) override;

    protected:
        MegaSync** list;
        int s;
};

#endif // ENABLE_SYNC


class MegaPricingPrivate;
class MegaCurrencyPrivate;
class MegaBannerListPrivate;
class MegaRequestPrivate : public MegaRequest
{
	public:
        MegaRequestPrivate(int type, MegaRequestListener *listener = NULL);
        MegaRequestPrivate(MegaRequestPrivate *request);

        // Set the function to be executed in sendPendingRequests()
        // instead of adding more code to the huge switch there
        std::function<error()> performRequest;
        std::function<error(TransferDbCommitter&)> performTransferRequest;

        // perform fireOnRequestFinish in sendPendingReqeusts()
        // See fireOnRequestFinish
        std::function<void()> performFireOnRequestFinish;

        ~MegaRequestPrivate() override;
        MegaRequest *copy() override;
        void setNodeHandle(MegaHandle nodeHandle);
        void setLink(const char* link);
        void setParentHandle(MegaHandle parentHandle);
        void setSessionKey(const char* sessionKey);
        void setName(const char* name);
        void setEmail(const char* email);
        void setPassword(const char* email);
        void setNewPassword(const char* email);
        void setPrivateKey(const char* privateKey);
        void setAccess(int access);
        void setNumRetry(int ds);
        void setNextRetryDelay(int delay);
        void setPublicNode(MegaNode* publicNode, bool copyChildren = false);
        void setNumDetails(int numDetails);
        void setFile(const char* file);
        void setParamType(int type);
        void setText(const char* text);
        void setNumber(long long number);
        void setFlag(bool flag);
        void setTransferTag(int transfer);
        void setListener(MegaRequestListener *listener);
        void setTotalBytes(long long totalBytes);
        void setTransferredBytes(long long transferredBytes);
        void setTag(int tag);
        void addProduct(unsigned int type, handle product, int proLevel, int gbStorage, int gbTransfer,
                        int months, int amount, int amountMonth, int localPrice,
                        const char *description, std::map<std::string, uint32_t>&& features, const char *iosid, const char *androidid,
                        unsigned int testCategory, std::unique_ptr<BusinessPlan>);
        void setCurrency(std::unique_ptr<CurrencyData> currencyData);
        void setProxy(Proxy *proxy);
        Proxy *getProxy();
        void setTimeZoneDetails(MegaTimeZoneDetails *timeZoneDetails);

        int getType() const override;
        const char *getRequestString() const override;
        const char* toString() const override;
        const char* __str__() const override;
        const char* __toString() const override;
        MegaHandle getNodeHandle() const override;
        const char* getLink() const override;
        MegaHandle getParentHandle() const override;
        const char* getSessionKey() const override;
        const char* getName() const override;
        const char* getEmail() const override;
        const char* getPassword() const override;
        const char* getNewPassword() const override;
        const char* getPrivateKey() const override;
        int getAccess() const override;
        const char* getFile() const override;
        int getNumRetry() const override;
        MegaNode *getPublicNode() const override;
        MegaNode *getPublicMegaNode() const override;
        int getParamType() const override;
        const char *getText() const override;
        long long getNumber() const override;
        bool getFlag() const override;
        long long getTransferredBytes() const override;
        long long getTotalBytes() const override;
        MegaRequestListener *getListener() const override;
        MegaAccountDetails *getMegaAccountDetails() const override;
        int getTransferTag() const override;
        int getNumDetails() const override;
        int getTag() const override;
        MegaPricing *getPricing() const override;
        MegaCurrency *getCurrency() const override;
        std::shared_ptr<AccountDetails> getAccountDetails() const;
        MegaAchievementsDetails *getMegaAchievementsDetails() const override;
        AchievementsDetails *getAchievementsDetails() const;
        MegaTimeZoneDetails *getMegaTimeZoneDetails () const override;
        MegaStringList *getMegaStringList() const override;
        MegaHandleList* getMegaHandleList() const override;

#ifdef ENABLE_SYNC
        MegaSyncStallList* getMegaSyncStallList() const override;
        void setMegaSyncStallList(unique_ptr<MegaSyncStallList>&& stalls);
#endif // ENABLE_SYNC

#ifdef ENABLE_CHAT
        MegaTextChatPeerList *getMegaTextChatPeerList() const override;
        void setMegaTextChatPeerList(MegaTextChatPeerList *chatPeers);
        MegaTextChatList *getMegaTextChatList() const override;
        void setMegaTextChatList(MegaTextChatList *chatList);
        MegaScheduledMeetingList* getMegaScheduledMeetingList() const override;
#endif
        MegaStringMap *getMegaStringMap() const override;
        void setMegaStringMap(const MegaStringMap *);
        MegaStringListMap *getMegaStringListMap() const override;
        void setMegaStringListMap(const MegaStringListMap *stringListMap);
        MegaStringTable *getMegaStringTable() const override;
        void setMegaStringTable(const MegaStringTable *stringTable);
        MegaFolderInfo *getMegaFolderInfo() const override;
        void setMegaFolderInfo(const MegaFolderInfo *);
        const MegaPushNotificationSettings *getMegaPushNotificationSettings() const override;
        void setMegaPushNotificationSettings(const MegaPushNotificationSettings *settings);
        MegaBackgroundMediaUpload *getMegaBackgroundMediaUploadPtr() const override;
        void setMegaBackgroundMediaUploadPtr(MegaBackgroundMediaUpload *);  // non-owned pointer
        void setMegaStringList(const MegaStringList* stringList);
        void setMegaHandleList(const MegaHandleList* handles);
        void setMegaHandleList(const vector<handle> &handles);
        void setMegaScheduledMeetingList(const MegaScheduledMeetingList *schedMeetingList);

        MegaScheduledCopyListener *getBackupListener() const;
        void setBackupListener(MegaScheduledCopyListener *value);

        MegaBannerList* getMegaBannerList() const override;
        void setBanners(vector< tuple<int, string, string, string, string, string, string> >&& banners);

        MegaRecentActionBucketList *getRecentActions() const override;
        void setRecentActions(std::unique_ptr<MegaRecentActionBucketList> recentActionBucketList);


        MegaSet* getMegaSet() const override;
        void setMegaSet(std::unique_ptr<MegaSet> s);

        MegaSetElementList* getMegaSetElementList() const override;
        void setMegaSetElementList(std::unique_ptr<MegaSetElementList> els);

        const MegaIntegerList* getMegaIntegerList() const override;
        void setMegaIntegerList(std::unique_ptr<MegaIntegerList> ints);

        MegaBackupInfoList* getMegaBackupInfoList() const override;
        void setMegaBackupInfoList(std::unique_ptr<MegaBackupInfoList> bkps);

        MegaVpnCredentials* getMegaVpnCredentials() const override;
        void setMegaVpnCredentials(MegaVpnCredentials* megaVpnCredentials);

        const MegaNotificationList* getMegaNotifications() const override;
        void setMegaNotifications(MegaNotificationList* megaNotifications);

        const MegaNodeTree* getMegaNodeTree() const override;
        void setMegaNodeTree(MegaNodeTree* megaNodeTree);

protected:
        std::shared_ptr<AccountDetails> accountDetails;
        MegaPricingPrivate *megaPricing;
        MegaCurrencyPrivate *megaCurrency;
        AchievementsDetails *achievementsDetails;
        MegaTimeZoneDetails *timeZoneDetails;
        int type;
        MegaHandle nodeHandle;
        const char* link;
        const char* name;
        MegaHandle parentHandle;
        const char* sessionKey;
        const char* email;
        const char* password;
        const char* newPassword;
        const char* privateKey;
        const char* text;
        long long number;
        int access;
        const char* file;
        int attrType;
        bool flag;
        long long totalBytes;
        long long transferredBytes;
        MegaRequestListener *listener;
        MegaScheduledCopyListener *backupListener;

        int transfer;
        int numDetails;
        MegaNode* publicNode;
        int numRetry;
        int tag;
        Proxy *proxy;

#ifdef ENABLE_CHAT
        MegaTextChatPeerList *chatPeerList;
        MegaTextChatList *chatList;
        unique_ptr<MegaScheduledMeetingList> mScheduledMeetingList;
#endif
        MegaStringMap *stringMap;
        MegaStringListMap *mStringListMap;
        MegaStringTable *mStringTable;
        MegaFolderInfo *folderInfo;
        MegaPushNotificationSettings *settings;
        MegaBackgroundMediaUpload* backgroundMediaUpload;  // non-owned pointer
        unique_ptr<MegaStringList> mStringList;
        unique_ptr<MegaHandleList> mHandleList;
        unique_ptr<MegaRecentActionBucketList> mRecentActions;

    private:
        unique_ptr<MegaBannerListPrivate> mBannerList;
        unique_ptr<MegaSet> mMegaSet;
        unique_ptr<MegaSetElementList> mMegaSetElementList;
        unique_ptr<MegaIntegerList> mMegaIntegerList;
        unique_ptr<MegaBackupInfoList> mMegaBackupInfoList;
        unique_ptr<MegaVpnCredentials> mMegaVpnCredentials;

#ifdef ENABLE_SYNC
        unique_ptr<MegaSyncStallList> mSyncStallList;
#endif // ENABLE_SYNC

        unique_ptr<MegaNotificationList> mMegaNotifications;
        unique_ptr<MegaNodeTree> mMegaNodeTree;

    public:
        shared_ptr<ExecuteOnce> functionToExecute;
};

class MegaEventPrivate : public MegaEvent
{
public:
    MegaEventPrivate(int atype);
    MegaEventPrivate(MegaEventPrivate *event);
    virtual ~MegaEventPrivate();
    MegaEvent *copy() override;

    int getType() const override;
    const char *getText() const override;
    int64_t getNumber() const override;
    MegaHandle getHandle() const override;
    const char *getEventString() const override;

    std::string getValidDataToString() const;
    static const char* getEventString(int type);

    void setText(const char* text);
    void setNumber(int64_t number);
    void setHandle(const MegaHandle &handle);

protected:
    int type;
    const char* text = nullptr;
    int64_t number = -1;
    MegaHandle mHandle = INVALID_HANDLE;
};

class MegaAccountBalancePrivate : public MegaAccountBalance
{
public:
    static MegaAccountBalance *fromAccountBalance(const AccountBalance *balance);
    virtual ~MegaAccountBalancePrivate() ;
    virtual MegaAccountBalance* copy();

    virtual double getAmount() const;
    virtual char* getCurrency() const;

protected:
    MegaAccountBalancePrivate(const AccountBalance *balance);
    AccountBalance balance;
};

class MegaAccountSessionPrivate : public MegaAccountSession
{
public:
    static MegaAccountSession *fromAccountSession(const AccountSession *session);
    ~MegaAccountSessionPrivate() override;
    MegaAccountSession* copy();

    int64_t getCreationTimestamp() const override;
    int64_t getMostRecentUsage() const override;
    char *getUserAgent() const override;
    char *getIP() const override;
    char *getCountry() const override;
    bool isCurrent() const override;
    bool isAlive() const override;
    MegaHandle getHandle() const override;
    char *getDeviceId() const override;

private:
    MegaAccountSessionPrivate(const AccountSession *session);
    AccountSession session;
};

class MegaAccountPurchasePrivate : public MegaAccountPurchase
{
public:
    static MegaAccountPurchase *fromAccountPurchase(const AccountPurchase *purchase);
    ~MegaAccountPurchasePrivate() override;
    MegaAccountPurchase* copy();

    int64_t getTimestamp() const override;
    char *getHandle() const override;
    char *getCurrency() const override;
    double getAmount() const override;
    int getMethod() const override;

private:
    MegaAccountPurchasePrivate(const AccountPurchase *purchase);
    AccountPurchase purchase;
};

class MegaAccountTransactionPrivate : public MegaAccountTransaction
{
public:
    static MegaAccountTransaction *fromAccountTransaction(const AccountTransaction *transaction);
    ~MegaAccountTransactionPrivate() override;
    MegaAccountTransaction* copy();

    int64_t getTimestamp() const override;
    char *getHandle() const override;
    char *getCurrency() const override;
    double getAmount() const override;

private:
    MegaAccountTransactionPrivate(const AccountTransaction *transaction);
    AccountTransaction transaction;
};

class MegaAccountFeaturePrivate : public MegaAccountFeature
{
public:
    static MegaAccountFeaturePrivate* fromAccountFeature(const AccountFeature* feature);

    int64_t getExpiry() const override;
    char* getId() const override;

private:
    MegaAccountFeaturePrivate(const AccountFeature* feature);
    AccountFeature mFeature;
};

class MegaAccountDetailsPrivate : public MegaAccountDetails
{
    public:
        static MegaAccountDetails *fromAccountDetails(AccountDetails *details);
        ~MegaAccountDetailsPrivate() override;

        int getProLevel() override;
        int64_t getProExpiration() override;
        int getSubscriptionStatus() override;
        int64_t getSubscriptionRenewTime() override;
        char* getSubscriptionMethod() override;
        int getSubscriptionMethodId() override;
        char* getSubscriptionCycle() override;

        long long getStorageMax() override;
        long long getStorageUsed() override;
        long long getVersionStorageUsed() override;
        long long getTransferMax() override;
        long long getTransferOwnUsed() override;
        long long getTransferSrvUsed() override;
        long long getTransferUsed() override;

        int getNumUsageItems() override;
        long long getStorageUsed(MegaHandle handle) override;
        long long getNumFiles(MegaHandle handle) override;
        long long getNumFolders(MegaHandle handle) override;
        long long getVersionStorageUsed(MegaHandle handle) override;
        long long getNumVersionFiles(MegaHandle handle) override;

        MegaAccountDetails* copy() override;

        int getNumBalances() const override;
        MegaAccountBalance* getBalance(int i) const override;

        int getNumSessions() const override;
        MegaAccountSession* getSession(int i) const override;

        int getNumPurchases() const override;
        MegaAccountPurchase* getPurchase(int i) const override;

        int getNumTransactions() const override;
        MegaAccountTransaction* getTransaction(int i) const override;

        int getTemporalBandwidthInterval() override;
        long long getTemporalBandwidth() override;
        bool isTemporalBandwidthValid() override;

        int getNumActiveFeatures() const override;
        MegaAccountFeature* getActiveFeature(int featureIndex) const override;
        int64_t getSubscriptionLevel() const override;
        MegaStringIntegerMap* getSubscriptionFeatures() const override;

    private:
        MegaAccountDetailsPrivate(AccountDetails *details);
        AccountDetails details;
};

class MegaCurrencyPrivate : public MegaCurrency
{
public:
    ~MegaCurrencyPrivate() override;
    MegaCurrency *copy() override;

    const char *getCurrencySymbol() override;
    const char *getCurrencyName() override;
    const char *getLocalCurrencySymbol() override;
    const char *getLocalCurrencyName() override;

    void setCurrency(std::unique_ptr<CurrencyData>);    // common for all products

private:
    CurrencyData mCurrencyData;   // reused for all plans
};

class MegaPricingPrivate : public MegaPricing
{
public:
    ~MegaPricingPrivate() override;
    int getNumProducts() override;
    MegaHandle getHandle(int productIndex) override;
    int getProLevel(int productIndex) override;
    int getGBStorage(int productIndex) override;
    int getGBTransfer(int productIndex) override;
    int getMonths(int productIndex) override;
    int getAmount(int productIndex) override;
    int getLocalPrice(int productIndex) override;
    const char* getDescription(int productIndex) override;
    const char* getIosID(int productIndex) override;
    const char* getAndroidID(int productIndex) override;
    bool isBusinessType(int productIndex) override;
    bool isFeaturePlan(int productIndex) const override;
    int getAmountMonth(int productIndex) override;
    MegaPricing *copy() override;
    int getGBStoragePerUser(int productIndex) override;
    int getGBTransferPerUser(int productIndex) override;
    unsigned int getMinUsers(int productIndex) override;
    unsigned int getPricePerUser(int productIndex) override;
    unsigned int getLocalPricePerUser(int productIndex) override;
    unsigned int getPricePerStorage(int productIndex) override;
    unsigned int getLocalPricePerStorage(int productIndex) override;
    int getGBPerStorage(int productIndex) override;
    unsigned int getPricePerTransfer(int productIndex) override;
    unsigned int getLocalPricePerTransfer(int productIndex) override;
    int getGBPerTransfer(int productIndex) override;
    MegaStringIntegerMap* getFeatures(int productIndex) const override;
    unsigned int getTestCategory(int productIndex) const override;

    void addProduct(unsigned int type, handle product, int proLevel, int gbStorage, int gbTransfer,
                    int months, int amount, int amountMonth, unsigned localPrice,
                    const char *description, std::map<std::string, uint32_t>&& features, const char *iosid, const char *androidid,
                    unsigned int testCategory, std::unique_ptr<BusinessPlan>);

private:
    enum PlanType : unsigned
    {
        PRO_LEVEL,
        BUSINESS,
        FEATURE,
    };

    bool isType(int productIndex, unsigned t) const;

    vector<unsigned int> type;
    vector<handle> handles;
    vector<int> proLevel;
    vector<int> gbStorage;
    vector<int> gbTransfer;
    vector<int> months;
    vector<int> amount;
    vector<int> amountMonth;
    vector<int> mLocalPrice;
    vector<const char *> description;
    vector<map<string, uint32_t>> features;
    vector<const char *> iosId;
    vector<const char *> androidId;
    vector<unsigned int> mTestCategory;

    std::vector<std::unique_ptr<BusinessPlan>> mBizPlan;
};

class MegaAchievementsDetailsPrivate : public MegaAchievementsDetails
{
public:
    static MegaAchievementsDetails *fromAchievementsDetails(AchievementsDetails *details);
    ~MegaAchievementsDetailsPrivate() override;

    MegaAchievementsDetails* copy() override;

    long long getBaseStorage() override;
    long long getClassStorage(int class_id) override;
    long long getClassTransfer(int class_id) override;
    int getClassExpire(int class_id) override;
    unsigned int getAwardsCount() override;
    int getAwardClass(unsigned int index) override;
    int getAwardId(unsigned int index) override;
    int64_t getAwardTimestamp(unsigned int index) override;
    int64_t getAwardExpirationTs(unsigned int index) override;
    MegaStringList* getAwardEmails(unsigned int index) override;
    int getRewardsCount() override;
    int getRewardAwardId(unsigned int index) override;
    long long getRewardStorage(unsigned int index) override;
    long long getRewardTransfer(unsigned int index) override;
    long long getRewardStorageByAwardId(int award_id) override;
    long long getRewardTransferByAwardId(int award_id) override;
    int getRewardExpire(unsigned int index) override;

    long long currentStorage() override;
    long long currentTransfer() override;
    long long currentStorageReferrals() override;
    long long currentTransferReferrals() override;

private:
    MegaAchievementsDetailsPrivate(AchievementsDetails *details);
    AchievementsDetails details;
};

#ifdef ENABLE_CHAT
class MegaTextChatPeerListPrivate : public MegaTextChatPeerList
{
public:
    MegaTextChatPeerListPrivate();
    MegaTextChatPeerListPrivate(const userpriv_vector *);

    ~MegaTextChatPeerListPrivate() override;
    MegaTextChatPeerList *copy() const override;
    void addPeer(MegaHandle h, int priv) override;
    MegaHandle getPeerHandle(int i) const override;
    int getPeerPrivilege(int i) const override;
    int size() const override;

    // returns the list of user-privilege (this object keeps the ownership)
    const userpriv_vector * getList() const;

    void setPeerPrivilege(handle uh, privilege_t priv);

private:
    userpriv_vector list;
};

class MegaTextChatPrivate : public MegaTextChat
{
public:
    MegaTextChatPrivate(const MegaTextChat *);
    MegaTextChatPrivate(const TextChat *);

    ~MegaTextChatPrivate() override;
    MegaTextChat *copy() const override;

    MegaHandle getHandle() const override;
    int getOwnPrivilege() const override;
    int getShard() const override;
    const MegaTextChatPeerList *getPeerList() const override;
    void setPeerList(const MegaTextChatPeerList *peers) override;
    bool isGroup() const override;
    MegaHandle getOriginatingUser() const override;
    const char *getTitle() const override;
    const char *getUnifiedKey() const override;
    unsigned char getChatOptions() const override;
    int64_t getCreationTime() const override;
    bool isArchived() const override;
    bool isPublicChat() const override;
    bool isMeeting() const override;

    bool hasChanged(uint64_t changeType) const override;
    uint64_t getChanges() const override;
    int isOwnChange() const override;
    const MegaScheduledMeetingList* getScheduledMeetingList() const override;
    const MegaScheduledMeetingList* getUpdatedOccurrencesList() const override;
    const MegaHandleList* getSchedMeetingsChanged() const override;

private:
    handle id;
    int priv;
    string url;
    int shard;
    MegaTextChatPeerList *peers;
    bool group;
    handle ou;
    string title;
    string unifiedKey;
    uint64_t changed;
    int tag;
    bool archived;
    bool publicchat;
    int64_t ts;
    bool meeting;
    ChatOptions_t chatOptions;

    // list of scheduled meetings
    std::unique_ptr<MegaScheduledMeetingList> mScheduledMeetings;

    // list of scheduled meetings Id's that have changed
    std::unique_ptr<MegaHandleList> mSchedMeetingsChanged;

    // list of updated scheduled meetings occurrences (just in case app requested manually for more occurrences)
    std::unique_ptr<MegaScheduledMeetingList> mUpdatedOcurrences;
};

class MegaTextChatListPrivate : public MegaTextChatList
{
public:
    MegaTextChatListPrivate();
    MegaTextChatListPrivate(textchat_map *list);

    ~MegaTextChatListPrivate() override;
    MegaTextChatList *copy() const override;
    const MegaTextChat *get(unsigned int i) const override;
    int size() const override;

    void addChat(MegaTextChatPrivate*);

private:
    MegaTextChatListPrivate(const MegaTextChatListPrivate*);
    vector<MegaTextChat*> list;
};

#endif

class MegaBannerPrivate : public MegaBanner
{
public:
    MegaBannerPrivate(std::tuple<int, std::string, std::string, std::string, std::string, std::string, std::string>&& details);
    MegaBanner* copy() const override;

    int getId() const override;
    const char* getTitle() const override;
    const char* getDescription() const override;
    const char* getImage() const override;
    const char* getUrl() const override;
    const char* getBackgroundImage() const override;
    const char* getImageLocation() const override;

private:
    std::tuple<int, std::string, std::string, std::string, std::string, std::string, std::string> mDetails;
};

class MegaBannerListPrivate : public MegaBannerList
{
public:
    MegaBannerListPrivate* copy() const override; // "different" return type is Covariant
    const MegaBanner* get(int i) const override;
    int size() const override;
    void add(MegaBannerPrivate&&);

private:
    std::vector<MegaBannerPrivate> mVector;
};

class MegaStringMapPrivate : public MegaStringMap
{
public:
    MegaStringMapPrivate();
    MegaStringMapPrivate(const string_map *map, bool toBase64 = false);
    ~MegaStringMapPrivate() override;
    MegaStringMap *copy() const override;
    const char *get(const char* key) const override;
    MegaStringList *getKeys() const override;
    void set(const char *key, const char *value) override;
    int size() const override;
    const string_map *getMap() const;

protected:
    MegaStringMapPrivate(const MegaStringMapPrivate *megaStringMap);
    string_map strMap;
};

class MegaIntegerMapPrivate : public MegaIntegerMap
{
public:
    MegaIntegerMapPrivate();
    MegaIntegerMapPrivate(const std::multimap<int8_t, int8_t>& bytesMap);
    MegaIntegerMapPrivate(const std::multimap<int64_t, int64_t>& integerMap);
    ~MegaIntegerMapPrivate() override;
    MegaSmallIntMap* toByteMap() const;
    MegaIntegerMap* copy() const override;
    MegaIntegerList* getKeys() const override;
    MegaIntegerList* get(int64_t key) const override;
    int64_t size() const override;
    void set(int64_t key, int64_t value) override;
    const integer_map* getMap() const;
private:
    MegaIntegerMapPrivate(const MegaIntegerMapPrivate &megaIntegerMap);
    integer_map mIntegerMap;
};

class MegaStringListPrivate : public MegaStringList
{
public:
    MegaStringListPrivate() = default;
    MegaStringListPrivate(string_vector&&); // takes ownership
    ~MegaStringListPrivate() override = default;
    MegaStringList *copy() const override;
    const char* get(int i) const override;
    int size() const override;
    void add(const char* value) override;
    const string_vector& getVector() const;
protected:
    MegaStringListPrivate(const MegaStringListPrivate& stringList) = default;
    string_vector mList;
};

bool operator==(const MegaStringList& lhs, const MegaStringList& rhs);

class MegaStringIntegerMapPrivate : public MegaStringIntegerMap
{
public:
    MegaStringIntegerMapPrivate() = default;
    ~MegaStringIntegerMapPrivate() override = default;
    MegaStringIntegerMapPrivate* copy() const override { return new MegaStringIntegerMapPrivate(*this); }
    MegaStringListPrivate* getKeys() const override;
    MegaIntegerListPrivate* get(const char* key) const override;
    void set(const char* key, int64_t value) override;
    void set(const std::string& key, int64_t value);
    int64_t size() const override { return static_cast<decltype(size())>(mStorage.size()); }

protected:
    MegaStringIntegerMapPrivate(const MegaStringIntegerMapPrivate& stringIntMap) = default;
    std::map<std::string, int64_t> mStorage;
};

class MegaStringListMapPrivate : public MegaStringListMap
{
public:
    MegaStringListMapPrivate() = default;
    MEGA_DISABLE_COPY_MOVE(MegaStringListMapPrivate)
    MegaStringListMap* copy() const override;
    const MegaStringList* get(const char* key) const override;
    MegaStringList *getKeys() const override;
    void set(const char* key, const MegaStringList* value) override; // takes ownership of value
    int size() const override;
protected:
    struct Compare
    {
        bool operator()(const std::unique_ptr<const char[]>& rhs,
                        const std::unique_ptr<const char[]>& lhs) const;
    };

    map<std::unique_ptr<const char[]>, std::unique_ptr<const MegaStringList>, Compare> mMap;
};

class MegaStringTablePrivate : public MegaStringTable
{
public:
    MegaStringTablePrivate() = default;
    MEGA_DISABLE_COPY_MOVE(MegaStringTablePrivate)
    MegaStringTable* copy() const override;
    void append(const MegaStringList* value) override; // takes ownership of value
    const MegaStringList* get(int i) const override;
    int size() const override;
protected:
    vector<std::unique_ptr<const MegaStringList>> mTable;
};

class MegaNodeListPrivate : public MegaNodeList
{
	public:
        MegaNodeListPrivate();
        MegaNodeListPrivate(Node** newlist, int size);
        MegaNodeListPrivate(const MegaNodeListPrivate *nodeList, bool copyChildren = false);
        MegaNodeListPrivate(sharedNode_vector& v);
        MegaNodeListPrivate(sharedNode_list& l);
        ~MegaNodeListPrivate() override;
        MegaNodeList *copy() const override;
        MegaNode* get(int i) const override;
        int size() const override;

        void addNode(MegaNode* node) override;

        //This ones takes the ownership of the given node
        void addNode(std::unique_ptr<MegaNode> node);

	protected:
		MegaNode** list;
		int s;
};

class MegaChildrenListsPrivate : public MegaChildrenLists
{
    public:
        MegaChildrenListsPrivate();
        MegaChildrenListsPrivate(MegaChildrenLists*);
        MegaChildrenListsPrivate(unique_ptr<MegaNodeListPrivate> folderList, unique_ptr<MegaNodeListPrivate> fileList);
        MegaChildrenLists *copy() override;
        MegaNodeList* getFolderList() override;
        MegaNodeList* getFileList() override;

    protected:
        unique_ptr<MegaNodeList> folders;
        unique_ptr<MegaNodeList> files;
};

class MegaUserListPrivate : public MegaUserList
{
	public:
        MegaUserListPrivate();
        MegaUserListPrivate(User** newlist, int size);
        ~MegaUserListPrivate() override;
        MegaUserList *copy() override;
        MegaUser* get(int i) override;
        int size() override;

	protected:
        MegaUserListPrivate(MegaUserListPrivate *userList);
		MegaUser** list;
		int s;
};

class MegaShareListPrivate : public MegaShareList
{
	public:
        MegaShareListPrivate();
        MegaShareListPrivate(Share** newlist, MegaHandle *MegaHandlelist, byte *verified, int size);
        ~MegaShareListPrivate() override;
        MegaShare* get(int i) override;
        int size() override;

	protected:
		MegaShare** list;
		int s;
};

class MegaTransferListPrivate : public MegaTransferList
{
	public:
        MegaTransferListPrivate();
        MegaTransferListPrivate(MegaTransfer** newlist, int size);
        ~MegaTransferListPrivate() override;
        MegaTransfer* get(int i) override;
        int size() override;

	protected:
		MegaTransfer** list;
		int s;
};

class MegaContactRequestListPrivate : public MegaContactRequestList
{
    public:
        MegaContactRequestListPrivate();
        MegaContactRequestListPrivate(PendingContactRequest ** newlist, int size);
        ~MegaContactRequestListPrivate() override;
        MegaContactRequestList *copy() override;
        MegaContactRequest* get(int i) override;
        int size() override;

    protected:
        MegaContactRequestListPrivate(MegaContactRequestListPrivate *requestList);
        MegaContactRequest** list;
        int s;
};

class MegaUserAlertListPrivate : public MegaUserAlertList
{
public:
    MegaUserAlertListPrivate();
    MegaUserAlertListPrivate(UserAlert::Base** newlist, int size, MegaClient* mc);
    MegaUserAlertListPrivate(const MegaUserAlertListPrivate &userList);
    ~MegaUserAlertListPrivate() override;
    MegaUserAlertList *copy() const override;
    MegaUserAlert* get(int i) const override;
    int size() const override;
    void clear() override;

protected:
    MegaUserAlertListPrivate(MegaUserAlertListPrivate *userList);
    MegaUserAlert** list;
    int s;
};

class MegaRecentActionBucketPrivate : public MegaRecentActionBucket
{
public:
    MegaRecentActionBucketPrivate(recentaction& ra, MegaClient* mc);
    MegaRecentActionBucketPrivate(int64_t timestamp, const string& user, handle parent, bool update, bool media, MegaNodeList*);
    ~MegaRecentActionBucketPrivate() override;
    MegaRecentActionBucket *copy() const override;
    int64_t getTimestamp() const override;
    const char* getUserEmail() const override;
    MegaHandle getParentHandle() const override;
    bool isUpdate() const override;
    bool isMedia() const override;
    const MegaNodeList* getNodes() const override;

private:
    int64_t timestamp;
    string user;
    handle parent;
    bool update, media;
    MegaNodeList* nodes;
};

class MegaRecentActionBucketListPrivate : public MegaRecentActionBucketList
{
public:
    MegaRecentActionBucketListPrivate();
    MegaRecentActionBucketListPrivate(recentactions_vector& v, MegaClient* mc);
    MegaRecentActionBucketListPrivate(const MegaRecentActionBucketListPrivate &userList);
    ~MegaRecentActionBucketListPrivate() override;
    MegaRecentActionBucketList *copy() const override;
    MegaRecentActionBucket* get(int i) const override;
    int size() const override;

protected:
    MegaRecentActionBucketPrivate** list;
    int s;
};

class EncryptFilePieceByChunks : public EncryptByChunks
{
    // specialisation for encrypting a piece of a file without using too much RAM
    FileAccess* fain;
    FileAccess* faout;
    m_off_t inpos, outpos;
    string buffer;
    unsigned lastsize;

public:

    EncryptFilePieceByChunks(FileAccess* cFain, m_off_t cInPos, FileAccess* cFaout, m_off_t cOutPos,
                             SymmCipher* cipher, chunkmac_map* chunkmacs, uint64_t ctriv);

    byte* nextbuffer(unsigned bufsize) override;
};

class MegaBackgroundMediaUploadPrivate : public MegaBackgroundMediaUpload
{
public:
    MegaBackgroundMediaUploadPrivate(MegaApi* api);
    MegaBackgroundMediaUploadPrivate(const string& serialised, MegaApi* api);
    ~MegaBackgroundMediaUploadPrivate();

    bool analyseMediaInfo(const char* inputFilepath) override;
    char *encryptFile(const char* inputFilepath, int64_t startPos, m_off_t* length, const char *outputFilepath,
                     bool adjustsizeonly) override;
    char *getUploadURL() override;

    bool serialize(string* s);
    char *serialize() override;

    void setThumbnail(MegaHandle h) override;
    void setPreview(MegaHandle h) override;
    void setCoordinates(double lat, double lon, bool unshareable) override;

    SymmCipher* nodecipher(MegaClient*);

    MegaApiImpl* api;
    string url;
    chunkmac_map chunkmacs;
    byte filekey[FILENODEKEYLENGTH];
    MediaProperties mediaproperties;

    double latitude = MegaNode::INVALID_COORDINATE;
    double longitude = MegaNode::INVALID_COORDINATE;
    bool unshareableGPS = false;
    handle thumbnailFA = INVALID_HANDLE;
    handle previewFA = INVALID_HANDLE;
};

struct MegaFile : public File
{
    MegaFile();

    void setTransfer(MegaTransferPrivate *transfer);
    MegaTransferPrivate *getTransfer();
    bool serialize(string*) const override;

    static MegaFile* unserialize(string*);

protected:
    MegaTransferPrivate *megaTransfer;
};

struct MegaFileGet : public MegaFile
{
    void prepare(FileSystemAccess&) override;
    void updatelocalname() override;
    void progress() override;
    void completed(Transfer*, putsource_t source) override;
    void terminated(error e) override;
    bool undelete() const override { return mUndelete; }
    void setUndelete(bool u = true) { mUndelete = u; }
    MegaFileGet(MegaClient *client, Node* n, const LocalPath& dstPath, FileSystemType fsType, CollisionResolution collisionResolution);
    MegaFileGet(MegaClient *client, MegaNode* n, const LocalPath& dstPath, CollisionResolution collisionResolution);
    ~MegaFileGet() {}

    bool serialize(string*) const override;
    static MegaFileGet* unserialize(string*);

private:
    MegaFileGet() {}

    bool mUndelete = false;
};

struct MegaFilePut : public MegaFile
{
    void completed(Transfer* t, putsource_t source) override;
    void terminated(error e) override;
    MegaFilePut(MegaClient *client, LocalPath clocalname, string *filename, NodeHandle ch, const char* ctargetuser, int64_t mtime = -1, bool isSourceTemporary = false, std::shared_ptr<Node> pvNode = nullptr);
    ~MegaFilePut() {}

    bool serialize(string*) const override;
    static MegaFilePut* unserialize(string*);

protected:
    int64_t customMtime;

private:
    MegaFilePut() {}
};

//Thread safe request queue
class RequestQueue
{
    protected:
        std::deque<MegaRequestPrivate *> requests;
        std::mutex mutex;

    public:
        RequestQueue();
        void push(MegaRequestPrivate *request);
        void push(std::unique_ptr<MegaRequestPrivate> request);
        void push_front(MegaRequestPrivate *request);
        MegaRequestPrivate * pop();
        MegaRequestPrivate * front();
        void removeListener(MegaRequestListener *listener);
        void removeListener(MegaScheduledCopyListener *listener);
};


//Thread safe transfer queue
class TransferQueue
{
    protected:
        std::deque<MegaTransferPrivate *> transfers;
        std::mutex mutex;
        int lastPushedTransferTag = 0;

    public:
        TransferQueue();
        void push(MegaTransferPrivate *transfer);
        void push_front(MegaTransferPrivate *transfer);
        MegaTransferPrivate * pop();
        bool empty();
        size_t size();
        void clear();

        /**
         * @brief pops and returns transfer up to the designated one
         * @param lastQueuedTransfer position of the last transfer to pop
         * @param direction directio of transfers to pop
         * @return
         */
        std::vector<MegaTransferPrivate *> popUpTo(int lastQueuedTransfer, int direction);

        void removeWithFolderTag(int folderTag, std::function<void(MegaTransferPrivate *)> callback);
        void removeListener(MegaTransferListener *listener);
        int getLastPushedTag() const;
        void setAllCancelled(CancelToken t, int direction);
};

#ifdef ENABLE_SYNC

/**
 * Implementation for a Sync stall conflict (immutable)
 * It Could wrap a single synchronization conflict or a reference to it
 * if we know the MegaSyncStallList container is kept around.
 */
class MegaSyncStallPrivate : public MegaSyncStall
{
    public:
        MegaSyncStallPrivate(const SyncStallEntry& e);

        MegaSyncStallPrivate* copy() const override;

        SyncStallReason reason() const override
        {
            return SyncStallReason(info.reason);
        }

        MegaHandle cloudNodeHandle(int index) const override
        {
            if (index == 0) return info.cloudPath1.cloudHandle.as8byte();
            if (index == 1) return info.cloudPath2.cloudHandle.as8byte();
            return UNDEF;
        }

        const char* path(bool cloudSide, int index)  const override
        {
            if (cloudSide)
            {
                if (index == 0) return info.cloudPath1.cloudPath.c_str();
                if (index == 1) return info.cloudPath2.cloudPath.c_str();
            }
            else
            {
                if (lpConverted[0].empty() && lpConverted[1].empty())
                {
                    lpConverted[0] = info.localPath1.localPath.toPath(false);
                    lpConverted[1] = info.localPath2.localPath.toPath(false);
                }
                if (index == 0) return lpConverted[0].c_str();
                if (index == 1) return lpConverted[1].c_str();
            }
            return nullptr;
        }

        unsigned int pathCount(bool cloudSide) const override
        {
            unsigned int count(0);

            if (cloudSide)
            {
                if(!info.cloudPath1.cloudPath.empty())
                {
                    count++;
                }
                if(!info.cloudPath2.cloudPath.empty())
                {
                    count++;
                }
            }
            else
            {
                if(!info.localPath1.localPath.empty())
                {
                    count++;
                }
                if(!info.localPath2.localPath.empty())
                {
                    count++;
                }
            }

            return count;
        }

        int pathProblem(bool cloudSide, int index) const override
        {
            if (cloudSide)
            {
                if (index == 0) return int(info.cloudPath1.problem);
                if (index == 1) return int(info.cloudPath2.problem);
            }
            else
            {
                if (index == 0) return int(info.localPath1.problem);
                if (index == 1) return int(info.localPath2.problem);
            }
            return -1;
        }

        bool couldSuggestIgnoreThisPath(bool cloudSide, int index) const override
        {
            if (info.reason != SyncWaitReason::FileIssue) return false;

            int problem = pathProblem(cloudSide, index);

            return problem == DetectedHardLink ||
                   problem == DetectedNestedMount ||
                   problem == DetectedSymlink ||
                   problem == DetectedSpecialFile ||
                   problem == FilesystemErrorListingFolder ||
                   problem == FilesystemErrorIdentifyingFolderContent; // Deprecated after SDK-3206
        }

        const char* reasonDebugString() const override
        {
            return reasonDebugString(reason());
        }


        bool detectedCloudSide() const override
        {
            return info.detectionSideIsMEGA;
        }

        static const char*
        reasonDebugString(MegaSyncStall::SyncStallReason reason);

        static const char*
        pathProblemDebugString(MegaSyncStall::SyncPathProblem reason);

        const SyncStallEntry info;
    protected:
        mutable string lpConverted[2];
};

class MegaSyncNameConflictStallPrivate : public MegaSyncStall
{
public:
    MegaSyncNameConflictStallPrivate(const NameConflict& nc) : mConflict(nc) {}

    MegaSyncNameConflictStallPrivate* copy() const override
    {
        return new MegaSyncNameConflictStallPrivate(*this);
    }

    SyncStallReason reason() const override
    {
        return SyncStallReason(NamesWouldClashWhenSynced);
    }

    MegaHandle cloudNodeHandle(int index)  const override
    {
        if (index >= 0 && index < int(mConflict.clashingCloud.size()))
        {
            return mConflict.clashingCloud[index].handle.as8byte();
        }
        return UNDEF;
    }


    const char* path(bool cloudSide, int index)  const override
    {
        if (cloudSide)
        {
            auto i = mCache1.find(index);
            if (i != mCache1.end()) return i->second.c_str();

            if (index >= 0 && index < int(mConflict.clashingCloud.size()))
            {
                mCache1[index] = mConflict.cloudPath + "/" + mConflict.clashingCloud[index].name;
                return mCache1[index].c_str();
            }
        }
        else
        {
            auto i = mCache2.find(index);
            if (i != mCache2.end()) return i->second.c_str();

            if (index >= 0 && index < int(mConflict.clashingLocalNames.size()))
            {
                LocalPath lp = mConflict.localPath;
                lp.appendWithSeparator(mConflict.clashingLocalNames[index], true);
                mCache2[index] = lp.toPath(false);
                return mCache2[index].c_str();
            }
        }
        return nullptr;
    }

    unsigned int pathCount(bool cloudSide) const override
    {
        if (cloudSide)
        {
            return static_cast<unsigned int>(mConflict.clashingCloud.size());
        }
        else
        {
            return static_cast<unsigned int>(mConflict.clashingLocalNames.size());
        }
    }

    int pathProblem(bool, int) const override
    {
        return -1;
    }

    bool couldSuggestIgnoreThisPath(bool cloudSide, int index) const override
    {
        return false;
    }

    const char* reasonDebugString() const override
    {
        return reasonDebugString(reason());
    }

    bool detectedCloudSide() const override
    {
        return mCache1.size() > 1;
    }

    static const char*
        reasonDebugString(MegaSyncStall::SyncStallReason reason);

    static const char*
        pathProblemDebugString(MegaSyncStall::SyncPathProblem reason);

    const NameConflict mConflict;
protected:
    mutable map<int, string> mCache1, mCache2;
};

class AddressedStallFilter
{
    // Keeps track of which stalls the user addressed already
    // So we don't re-show them if the user presses Refresh
    // before the sync actually re-evaluates those nodes
    // in a complete new pass over the sync nodes

    mutex m;

    std::map<string, int> addressedSyncCloudStalls;
    std::map<LocalPath, int> addressedSyncLocalStalls;
    std::map<string, int> addressedNameConflictCloudStalls;
    std::map<LocalPath, int> addressedNameConflictLocalStalls;

public:
    bool addressedNameConfict(const string& cloudPath, const LocalPath& localPath);
    bool addressedCloudStall(const string& cloudPath);
    bool addressedLocalStall(const LocalPath& localPath);
    void filterStallCloud(const string& cloudPath, int completedPassCount);
    void filterStallLocal(const LocalPath& localPath, int completedPassCount);
    void filterNameConfict(const string& cloudPath, const LocalPath& localPath, int completedPassCount);
    void removeOldFilters(int completedPassCount);
    void clear();
};


class MegaSyncStallListPrivate : public MegaSyncStallList
{
    public:
        MegaSyncStallListPrivate(SyncProblems&&, AddressedStallFilter& filter);

        MegaSyncStallListPrivate* copy() const override;

        const MegaSyncStall* get(size_t i) const override;

        size_t size() const override
        {
            return mStalls.size();
        }

    protected:
        std::vector<std::shared_ptr<MegaSyncStall>> mStalls;
};

#endif // ENABLE_SYNC

class MegaSearchFilterPrivate : public MegaSearchFilter
{
public:
    MegaSearchFilterPrivate* copy() const override;

    void byName(const char* searchString) override;
    void byNodeType(int nodeType) override;
    void byCategory(int mimeType) override;
    void byFavourite(int boolFilterOption) override;
    void bySensitivity(bool excludeSensitive) override;
    void bySensitivity(int boolFilterOption) override;
    void byLocationHandle(MegaHandle ancestorHandle) override;
    void byLocation(int locationType) override;
    void byCreationTime(int64_t lowerLimit, int64_t upperLimit) override;
    void byModificationTime(int64_t lowerLimit, int64_t upperLimit) override;
    void byDescription(const char* searchString) override;
    void byTag(const char* searchString) override;

    const char* byName() const override { return mNameFilter.c_str(); }
    int byNodeType() const override { return mNodeType; }
    int byCategory() const override { return mMimeCategory; }
    int byFavourite() const override { return mFavouriteFilterOption; }
    int bySensitivity() const override { return mExcludeSensitive; }
    MegaHandle byLocationHandle() const override { return mLocationHandle; }
    int byLocation() const override { return mLocationType; }
    int64_t byCreationTimeLowerLimit() const override { return mCreationLowerLimit; }
    int64_t byCreationTimeUpperLimit() const override { return mCreationUpperLimit; }
    int64_t byModificationTimeLowerLimit() const override { return mModificationLowerLimit; }
    int64_t byModificationTimeUpperLimit() const override { return mModificationUpperLimit; }
    const char* byDescription() const override { return mDescriptionFilter.c_str(); }
    const char* byTag() const override { return mTag.c_str(); }

private:
    std::string mNameFilter;
    int mNodeType = MegaNode::TYPE_UNKNOWN;
    int mMimeCategory = MegaApi::FILE_TYPE_DEFAULT;
    int mFavouriteFilterOption = MegaSearchFilter::BOOL_FILTER_DISABLED;
    int mExcludeSensitive = MegaSearchFilter::BOOL_FILTER_DISABLED;
    MegaHandle mLocationHandle = INVALID_HANDLE;
    int mLocationType = MegaApi::SEARCH_TARGET_ALL;
    int64_t mCreationLowerLimit = 0;
    int64_t mCreationUpperLimit = 0;
    int64_t mModificationLowerLimit = 0;
    int64_t mModificationUpperLimit = 0;
    std::string mDescriptionFilter;
    std::string mTag;

    /**
     * @brief Checks if the input value is:
     *  0 -> MegaSearchFilter::BOOL_FILTER_DISABLED
     *  1 -> MegaSearchFilter::BOOL_FILTER_ONLY_TRUE
     *  2 -> MegaSearchFilter::BOOL_FILTER_ONLY_FALSE
     *
     * If it is out of range, 0 is returned and a warning message is logged
     */
    static int validateBoolFilterOption(const int value);
};

class MegaSearchPagePrivate : public MegaSearchPage
{
public:
    MegaSearchPagePrivate(size_t startingOffset, size_t size) : mOffset(startingOffset), mSize(size) {}
    MegaSearchPagePrivate* copy() const override { return new MegaSearchPagePrivate(*this); }
    size_t startingOffset() const override { return mOffset; }
    size_t size() const override { return mSize; }

private:
    size_t mOffset;
    size_t mSize;
};


class MegaGfxProviderPrivate : public MegaGfxProvider
{
public:
    explicit MegaGfxProviderPrivate(std::unique_ptr<::mega::IGfxProvider> provider) : mProvider(std::move(provider)) {}

    explicit MegaGfxProviderPrivate(MegaGfxProviderPrivate&& other) : mProvider(std::move(other.mProvider)) {}

    std::unique_ptr<::mega::IGfxProvider> releaseProvider() { return std::move(mProvider); }

    static std::unique_ptr<MegaGfxProviderPrivate> createIsolatedInstance(const std::string& endpointName,
                                                                          const std::string& executable);

    static std::unique_ptr<MegaGfxProviderPrivate> createExternalInstance(MegaGfxProcessor* processor);

    static std::unique_ptr<MegaGfxProviderPrivate> createInternalInstance();

private:
    std::unique_ptr<::mega::IGfxProvider> mProvider;
};

class MegaFlagPrivate : public MegaFlag
{
public:
    MegaFlagPrivate(uint32_t type, uint32_t group) : mType(type), mGroup(group) {}
    uint32_t getType() const override { return mType; }
    uint32_t getGroup() const override { return mGroup; }

private:
    uint32_t mType;
    uint32_t mGroup;
};

class MegaApiImpl : public MegaApp
{
    public:
        MegaApiImpl(MegaApi *api, const char *appKey, MegaGfxProcessor* processor, const char *basePath, const char *userAgent, unsigned workerThreadCount, int clientType);
        MegaApiImpl(MegaApi *api, const char *appKey, MegaGfxProvider* provider, const char *basePath, const char *userAgent, unsigned workerThreadCount, int clientType);
        virtual ~MegaApiImpl();

        static MegaApiImpl* ImplOf(MegaApi*);

        //Multiple listener management.
        void addListener(MegaListener* listener);
        void addRequestListener(MegaRequestListener* listener);
        void addTransferListener(MegaTransferListener* listener);
        void addScheduledCopyListener(MegaScheduledCopyListener* listener);
        void addGlobalListener(MegaGlobalListener* listener);
        bool removeListener(MegaListener* listener);
        bool removeRequestListener(MegaRequestListener* listener);
        bool removeTransferListener(MegaTransferListener* listener);
        bool removeScheduledCopyListener(MegaScheduledCopyListener* listener);
        bool removeGlobalListener(MegaGlobalListener* listener);

        //Utils
        long long getSDKtime();
        void getSessionTransferURL(const char *path, MegaRequestListener *listener);
        static MegaHandle base32ToHandle(const char* base32Handle);
        static handle base64ToHandle(const char* base64Handle);
        static handle base64ToUserHandle(const char* base64Handle);
        static handle base64ToBackupId(const char* backupId);
        static char *handleToBase64(MegaHandle handle);
        static char *userHandleToBase64(MegaHandle handle);
        static const char* backupIdToBase64(MegaHandle handle);
        static char *binaryToBase64(const char* binaryData, size_t length);
        static void base64ToBinary(const char *base64string, unsigned char **binary, size_t* binarysize);
        static const char* ebcEncryptKey(const char* encryptionKey, const char* plainKey);
        void retryPendingConnections(bool disconnect = false, bool includexfers = false, MegaRequestListener* listener = NULL);
        void setDnsServers(const char *dnsServers, MegaRequestListener* listener = NULL);
        void addEntropy(char* data, unsigned int size);
        static string userAttributeToString(int);
        static string userAttributeToLongName(int);
        static int userAttributeFromString(const char *name);
        static char userAttributeToScope(int);
        bool serverSideRubbishBinAutopurgeEnabled();
        bool appleVoipPushEnabled();
        bool newLinkFormatEnabled();
        bool accountIsNew() const;
        unsigned int getABTestValue(const char* flag);
        void sendABTestActive(const char* flag, MegaRequestListener* listener);
        int smsAllowedState();
        char* smsVerifiedPhoneNumber();
        void resetSmsVerifiedPhoneNumber(MegaRequestListener *listener);

        bool multiFactorAuthAvailable();
        void multiFactorAuthCheck(const char *email, MegaRequestListener *listener = NULL);
        void multiFactorAuthGetCode(MegaRequestListener *listener = NULL);
        void multiFactorAuthEnable(const char *pin, MegaRequestListener *listener = NULL);
        void multiFactorAuthDisable(const char *pin, MegaRequestListener *listener = NULL);
        void multiFactorAuthLogin(const char* email, const char* password, const char* pin, MegaRequestListener *listener = NULL);
        void multiFactorAuthChangePassword(const char *oldPassword, const char *newPassword, const char* pin, MegaRequestListener *listener = NULL);
        void multiFactorAuthChangeEmail(const char *email, const char* pin, MegaRequestListener *listener = NULL);
        void multiFactorAuthCancelAccount(const char* pin, MegaRequestListener *listener = NULL);

        void fetchTimeZone(bool forceApiFetch = true, MegaRequestListener *listener = NULL);

        //API requests
        void login(const char* email, const char* password, MegaRequestListener *listener = NULL);
        char *dumpSession();
        char *getSequenceNumber();
        char *getSequenceTag();
        char *getAccountAuth();
        void setAccountAuth(const char* auth);

        void fastLogin(const char* session, MegaRequestListener *listener = NULL);
        void killSession(MegaHandle sessionHandle, MegaRequestListener *listener = NULL);
        void getUserData(MegaRequestListener *listener = NULL);
        void getUserData(MegaUser *user, MegaRequestListener *listener = NULL);
        void getUserData(const char *user, MegaRequestListener *listener = NULL);
        void getMiscFlags(MegaRequestListener *listener = NULL);
        void sendDevCommand(const char *command, const char *email, long long quota, int businessStatus, int userStatus, MegaRequestListener *listener);
        void getCloudStorageUsed(MegaRequestListener *listener = NULL);
        void getAccountDetails(bool storage, bool transfer, bool pro, bool sessions, bool purchases, bool transactions, int source = -1, MegaRequestListener *listener = NULL);
        void queryTransferQuota(long long size, MegaRequestListener *listener = NULL);
        void createAccount(const char* email, const char* password, const char* firstname, const char* lastname, MegaHandle lastPublicHandle, int lastPublicHandleType, int64_t lastAccessTimestamp, MegaRequestListener *listener = NULL);
        void createEphemeralAccountPlusPlus(const char* firstname, const char* lastname, MegaRequestListener *listener = NULL);
        void resumeCreateAccount(const char* sid, MegaRequestListener *listener = NULL);
        void resumeCreateAccountEphemeralPlusPlus(const char* sid, MegaRequestListener *listener = NULL);
        void cancelCreateAccount(MegaRequestListener *listener = NULL);
        void sendSignupLink(const char* email, const char *name, const char *password, MegaRequestListener *listener = NULL);
        void resendSignupLink(const char* email, const char *name, MegaRequestListener *listener = NULL);
        void querySignupLink(const char* link, MegaRequestListener *listener = NULL);
        void confirmAccount(const char* link, const char *password, MegaRequestListener *listener = NULL);
        void resetPassword(const char *email, bool hasMasterKey, MegaRequestListener *listener = NULL);
        void queryRecoveryLink(const char *link, MegaRequestListener *listener = NULL);
        void confirmResetPasswordLink(const char *link, const char *newPwd, const char *masterKey = NULL, MegaRequestListener *listener = NULL);
        void checkRecoveryKey(const char* link, const char* masterKey, MegaRequestListener* listener = NULL);
        void cancelAccount(MegaRequestListener *listener = NULL);
        void confirmCancelAccount(const char *link, const char *pwd, MegaRequestListener *listener = NULL);
        void resendVerificationEmail(MegaRequestListener *listener = NULL);
        void changeEmail(const char *email, MegaRequestListener *listener = NULL);
        void confirmChangeEmail(const char *link, const char *pwd, MegaRequestListener *listener = NULL);
        void setProxySettings(MegaProxy *proxySettings, MegaRequestListener *listener = NULL);
        MegaProxy *getAutoProxySettings();
        int isLoggedIn();
        void loggedInStateChanged(sessiontype_t, handle me, const string &email) override;
        bool isEphemeralPlusPlus();
        void whyAmIBlocked(bool logout, MegaRequestListener *listener = NULL);
        char* getMyEmail();
        int64_t getAccountCreationTs();
        char* getMyUserHandle();
        MegaHandle getMyUserHandleBinary();
        MegaUser *getMyUser();
        bool isAchievementsEnabled();
        bool isProFlexiAccount();
        bool isBusinessAccount();
        bool isMasterBusinessAccount();
        bool isBusinessAccountActive();
        int getBusinessStatus();
        int64_t getOverquotaDeadlineTs();
        MegaIntegerList *getOverquotaWarningsTs();
        bool checkPassword(const char *password);
        char* getMyCredentials();
        void getUserCredentials(MegaUser *user, MegaRequestListener *listener = NULL);
        bool areCredentialsVerified(MegaUser *user);
        void verifyCredentials(MegaUser *user, MegaRequestListener *listener = NULL);
        void resetCredentials(MegaUser *user, MegaRequestListener *listener = NULL);
        char* getMyRSAPrivateKey();
        void setLogExtraForModules(bool networking, bool syncs);
        static void setLogLevel(int logLevel);
        static void setMaxPayloadLogSize(long long maxSize);
        static void addLoggerClass(MegaLogger *megaLogger, bool singleExclusiveLogger);
        static void removeLoggerClass(MegaLogger *megaLogger, bool singleExclusiveLogger);
        static void setLogToConsole(bool enable);
        static void log(int logLevel, const char* message, const char *filename = NULL, int line = -1);
        void setLoggingName(const char* loggingName);

        void createFolder(const char* name, MegaNode *parent, MegaRequestListener *listener = NULL);
        bool createLocalFolder(const char *path);
        static Error createLocalFolder_unlocked(LocalPath & localPath, FileSystemAccess& fsaccess);
        void moveNode(MegaNode* node, MegaNode* newParent, MegaRequestListener *listener = NULL);
        void moveNode(MegaNode* node, MegaNode* newParent, const char *newName, MegaRequestListener *listener = NULL);
        void copyNode(MegaNode* node, MegaNode *newParent, MegaRequestListener *listener = NULL);
        void copyNode(MegaNode* node, MegaNode *newParent, const char* newName, MegaRequestListener *listener = NULL);
        void renameNode(MegaNode* node, const char* newName, MegaRequestListener *listener = NULL);
        void remove(MegaNode* node, bool keepversions = false, MegaRequestListener *listener = NULL);
        void removeVersions(MegaRequestListener *listener = NULL);
        void restoreVersion(MegaNode *version, MegaRequestListener *listener = NULL);
        void cleanRubbishBin(MegaRequestListener *listener = NULL);
        void sendFileToUser(MegaNode *node, MegaUser *user, MegaRequestListener *listener = NULL);
        void sendFileToUser(MegaNode *node, const char* email, MegaRequestListener *listener = NULL);
        void upgradeSecurity(MegaRequestListener* listener = NULL);
        bool contactVerificationWarningEnabled();
        void setManualVerificationFlag(bool enable);
        void openShareDialog(MegaNode *node, MegaRequestListener *listener = NULL);
        void share(MegaNode *node, MegaUser* user, int level, MegaRequestListener *listener = NULL);
        void share(MegaNode* node, const char* email, int level, MegaRequestListener *listener = NULL);
        void loginToFolder(const char* megaFolderLink, const char *authKey = nullptr, MegaRequestListener *listener = NULL);
        void importFileLink(const char* megaFileLink, MegaNode* parent, MegaRequestListener *listener = NULL);
        void decryptPasswordProtectedLink(const char* link, const char* password, MegaRequestListener *listener = NULL);
        void encryptLinkWithPassword(const char* link, const char* password, MegaRequestListener *listener = NULL);
        void getDownloadUrl(MegaNode* node, bool singleUrl, MegaRequestListener *listener);
        void getPublicNode(const char* megaFileLink, MegaRequestListener *listener = NULL);
        const char *buildPublicLink(const char *publicHandle, const char *key, bool isFolder);
        void getThumbnail(MegaNode* node, const char *dstFilePath, MegaRequestListener *listener = NULL);
		void cancelGetThumbnail(MegaNode* node, MegaRequestListener *listener = NULL);
        void setThumbnail(MegaNode* node, const char *srcFilePath, MegaRequestListener *listener = NULL);
        void putThumbnail(MegaBackgroundMediaUpload* node, const char *srcFilePath, MegaRequestListener *listener = NULL);
        void setThumbnailByHandle(MegaNode* node, MegaHandle attributehandle, MegaRequestListener *listener = NULL);
        void getPreview(MegaNode* node, const char *dstFilePath, MegaRequestListener *listener = NULL);
		void cancelGetPreview(MegaNode* node, MegaRequestListener *listener = NULL);
        void setPreview(MegaNode* node, const char *srcFilePath, MegaRequestListener *listener = NULL);
        void putPreview(MegaBackgroundMediaUpload* node, const char *srcFilePath, MegaRequestListener *listener = NULL);
        void setPreviewByHandle(MegaNode* node, MegaHandle attributehandle, MegaRequestListener *listener = NULL);
        void getUserAvatar(MegaUser* user, const char *dstFilePath, MegaRequestListener *listener = NULL);
        void setAvatar(const char *dstFilePath, MegaRequestListener *listener = NULL);
        void getUserAvatar(const char *email_or_handle, const char *dstFilePath, MegaRequestListener *listener = NULL);
        static char* getUserAvatarColor(MegaUser *user);
        static char *getUserAvatarColor(const char *userhandle);
        static char* getUserAvatarSecondaryColor(MegaUser *user);
        static char *getUserAvatarSecondaryColor(const char *userhandle);
        char* getPrivateKey(int type);
        bool testAllocation(unsigned allocCount, size_t allocSize);
        void getUserAttribute(MegaUser* user, int type, MegaRequestListener *listener = NULL);
        void getUserAttribute(const char* email_or_handle, int type, MegaRequestListener *listener = NULL);
        void getChatUserAttribute(const char* email_or_handle, int type, const char* ph, MegaRequestListener *listener = NULL);
        void getUserAttr(const char* email_or_handle, int type, const char *dstFilePath, int number = 0, MegaRequestListener *listener = NULL);
        void getChatUserAttr(const char* email_or_handle, int type, const char *dstFilePath, const char *ph = NULL, int number = 0, MegaRequestListener *listener = NULL);
        void setUserAttribute(int type, const char* value, MegaRequestListener *listener = NULL);
        void setUserAttribute(int type, const MegaStringMap* value, MegaRequestListener *listener = NULL);
        void getRubbishBinAutopurgePeriod(MegaRequestListener *listener = NULL);
        void setRubbishBinAutopurgePeriod(int days, MegaRequestListener *listener = NULL);
        const char* getDeviceId() const;
        void getDeviceName(const char* deviceId, MegaRequestListener *listener = NULL);
        void setDeviceName(const char* deviceId, const char* deviceName, MegaRequestListener *listener = NULL);
        void getDriveName(const char *pathToDrive, MegaRequestListener *listener = NULL);
        void setDriveName(const char* pathToDrive, const char *driveName, MegaRequestListener *listener = NULL);
        void getUserEmail(MegaHandle handle, MegaRequestListener *listener = NULL);
        void setCustomNodeAttribute(MegaNode *node, const char *attrName, const char *value, MegaRequestListener *listener = NULL);
        void setNodeS4(MegaNode *node, const char *value, MegaRequestListener *listener = NULL);
        void setNodeDuration(MegaNode *node, int secs, MegaRequestListener *listener = NULL);
        void setNodeLabel(MegaNode *node, int label, MegaRequestListener *listener = NULL);
        void setNodeFavourite(MegaNode *node, bool fav, MegaRequestListener *listener = NULL);
        void getFavourites(MegaNode* node, int count, MegaRequestListener* listener = nullptr);
        void setNodeSensitive(MegaNode* node, bool sensitive, MegaRequestListener* listener);
        void setNodeCoordinates(MegaNode *node, bool unshareable, double latitude, double longitude, MegaRequestListener *listener = NULL);
        void setNodeDescription(MegaNode* node, const char* description, MegaRequestListener* listener = NULL);
        void addNodeTag(MegaNode* node, const char* tag, MegaRequestListener* listener = NULL);
        void removeNodeTag(MegaNode* node, const char* tag, MegaRequestListener* listener = NULL);
        void updateNodeTag(MegaNode* node,
                       const char* newTag,
                       const char* oldTag,
                       MegaRequestListener* listener = NULL);

        void exportNode(MegaNode *node, int64_t expireTime, bool writable, bool megaHosted, MegaRequestListener *listener = NULL);
        void disableExport(MegaNode *node, MegaRequestListener *listener = NULL);
        void fetchNodes(MegaRequestListener *listener = NULL);
        void getPricing(MegaRequestListener *listener = NULL);
        void getRecommendedProLevel(MegaRequestListener* listener = NULL);
        void getPaymentId(handle productHandle, handle lastPublicHandle, int lastPublicHandleType, int64_t lastAccessTimestamp, MegaRequestListener *listener = NULL);
        void upgradeAccount(MegaHandle productHandle, int paymentMethod, MegaRequestListener *listener = NULL);
        void submitPurchaseReceipt(int gateway, const char *receipt, MegaHandle lastPublicHandle, int lastPublicHandleType, int64_t lastAccessTimestamp, MegaRequestListener *listener = NULL);
        void creditCardStore(const char* address1, const char* address2, const char* city,
                             const char* province, const char* country, const char *postalcode,
                             const char* firstname, const char* lastname, const char* creditcard,
                             const char* expire_month, const char* expire_year, const char* cv2,
                             MegaRequestListener *listener = NULL);

        void creditCardQuerySubscriptions(MegaRequestListener *listener = NULL);
        void creditCardCancelSubscriptions(const char* reason, MegaRequestListener *listener = NULL);
        void getPaymentMethods(MegaRequestListener *listener = NULL);

        char *exportMasterKey();
        void updatePwdReminderData(bool lastSuccess, bool lastSkipped, bool mkExported, bool dontShowAgain, bool lastLogin, MegaRequestListener *listener = NULL);

        void changePassword(const char *oldPassword, const char *newPassword, MegaRequestListener *listener = NULL);
        void inviteContact(const char* email, const char* message, int action, MegaHandle contactLink, MegaRequestListener* listener = NULL);
        void replyContactRequest(MegaContactRequest *request, int action, MegaRequestListener* listener = NULL);
        void respondContactRequest();

        void removeContact(MegaUser *user, MegaRequestListener* listener=NULL);
        void logout(bool keepSyncConfigsFile, MegaRequestListener *listener);
        void localLogout(MegaRequestListener *listener = NULL);
        void invalidateCache();
        int getPasswordStrength(const char *password);
        static char* generateRandomCharsPassword(bool useUpper, bool useDigit, bool useSymbol, unsigned int length);
        void submitFeedback(int rating, const char *comment, MegaRequestListener *listener = NULL);
        void reportEvent(const char *details = NULL, MegaRequestListener *listener = NULL);
        void sendEvent(int eventType, const char* message, bool addJourneyId, const char* viewId, MegaRequestListener *listener = NULL);
        void createSupportTicket(const char* message, int type = 1, MegaRequestListener *listener = NULL);

        void useHttpsOnly(bool httpsOnly, MegaRequestListener *listener = NULL);
        bool usingHttpsOnly();

        //Backups
        MegaStringList *getBackupFolders(int backuptag);
        void setScheduledCopy(const char* localPath, MegaNode *parent, bool attendPastBackups, int64_t period, string periodstring, int numBackups, MegaRequestListener *listener=NULL);
        void removeScheduledCopy(int tag, MegaRequestListener *listener=NULL);
        void abortCurrentScheduledCopy(int tag, MegaRequestListener *listener=NULL);

        //Timer
        void startTimer( int64_t period, MegaRequestListener *listener=NULL);

        //Transfers
        void startUploadForSupport(const char* localPath, bool isSourceFileTemporary, FileSystemType fsType, MegaTransferListener* listener);
        void startUpload(bool startFirst, const char* localPath, MegaNode* parent, const char* fileName, const char* targetUser, int64_t mtime, int folderTransferTag, bool isBackup, const char* appData, bool isSourceFileTemporary, bool forceNewUpload, FileSystemType fsType, CancelToken cancelToken, MegaTransferListener* listener);
        MegaTransferPrivate* createUploadTransfer(bool startFirst, const char *localPath, MegaNode *parent, const char *fileName, const char *targetUser, int64_t mtime, int folderTransferTag, bool isBackup, const char *appData, bool isSourceFileTemporary, bool forceNewUpload, FileSystemType fsType, CancelToken cancelToken, MegaTransferListener *listener, const FileFingerprint* preFingerprintedFile = nullptr);
        void startDownload (bool startFirst, MegaNode *node, const char* localPath, const char *customName, int folderTransferTag, const char *appData, CancelToken cancelToken, int collisionCheck, int collisionResolution, bool undelete, MegaTransferListener *listener);
        MegaTransferPrivate* createDownloadTransfer(bool startFirst, MegaNode *node, const char* localPath, const char *customName, int folderTransferTag, const char *appData, CancelToken cancelToken, int collisionCheck, int collisionResolution, bool undelete, MegaTransferListener *listener, FileSystemType fsType);
        void startStreaming(MegaNode* node, m_off_t startPos, m_off_t size, MegaTransferListener *listener);
        void setStreamingMinimumRate(int bytesPerSecond);
        void retryTransfer(MegaTransfer *transfer, MegaTransferListener *listener = NULL);
        void cancelTransfer(MegaTransfer *transfer, MegaRequestListener *listener=NULL);
        void cancelTransferByTag(int transferTag, MegaRequestListener *listener = NULL);
        void cancelTransfers(int direction, MegaRequestListener *listener=NULL);
        void pauseTransfers(bool pause, int direction, MegaRequestListener* listener=NULL);
        void pauseTransfer(int transferTag, bool pause, MegaRequestListener* listener = NULL);
        void moveTransferUp(int transferTag, MegaRequestListener *listener = NULL);
        void moveTransferDown(int transferTag, MegaRequestListener *listener = NULL);
        void moveTransferToFirst(int transferTag, MegaRequestListener *listener = NULL);
        void moveTransferToLast(int transferTag, MegaRequestListener *listener = NULL);
        void moveTransferBefore(int transferTag, int prevTransferTag, MegaRequestListener *listener = NULL);
        bool areTransfersPaused(int direction);
        void setUploadLimit(int bpslimit);
        void setMaxConnections(int direction, int connections, MegaRequestListener* listener = NULL);
        void setDownloadMethod(int method);
        void setUploadMethod(int method);
        bool setMaxDownloadSpeed(m_off_t bpslimit);
        bool setMaxUploadSpeed(m_off_t bpslimit);
        int getMaxDownloadSpeed();
        int getMaxUploadSpeed();
        int getCurrentDownloadSpeed();
        int getCurrentUploadSpeed();
        int getCurrentSpeed(int type);
        int getDownloadMethod();
        int getUploadMethod();
        MegaTransferData *getTransferData(MegaTransferListener *listener = NULL);
        MegaTransfer *getFirstTransfer(int type);
        void notifyTransfer(int transferTag, MegaTransferListener *listener = NULL);
        MegaTransferList *getTransfers();
        MegaTransferList *getStreamingTransfers();
        MegaTransfer* getTransferByTag(int transferTag);
        MegaTransferList *getTransfers(int type);
        MegaTransferList *getChildTransfers(int transferTag);
        MegaTransferList *getTansfersByFolderTag(int folderTransferTag);

        // FUSE
        using FuseEventHandler =
          void (MegaListener::*)(MegaApi*, const char*, int);

        // Add a new mount.
        void addMount(const MegaMount* mount, MegaRequestListener* listener);

        // Disable an enabled mount.
        void disableMount(const char* path,
                          MegaRequestListener* listener,
                          bool remember);

        // Enable a disabled mount.
        void enableMount(const char* path,
                         MegaRequestListener* listener,
                         bool remember);

        // Retrieve FUSE flags.
        MegaFuseFlags* getFUSEFlags();

        // Broadcast a mount event.
        void fireOnFuseEvent(FuseEventHandler handler,
                             const fuse::MountEvent& event);

        // Retrieve a mount's flags.
        MegaMountFlags* getMountFlags(const char* path);

        // Retrieve a mount's description.
        MegaMount* getMountInfo(const char* path);

        // Retrieve the path of all mounts associated with a given name.
        MegaStringList* getMountPaths(const char* name);

        // Retrieve a list of all (enabled) mounts.
        MegaMountList* listMounts(bool enabled);

        // Called when FUSE wants to broadcast a mount event.
        void onFuseEvent(const fuse::MountEvent& event) override;

        // Query whether a file is in FUSE's file cache.
        bool isCached(const char* path);

        // Query whether FUSE is supported on this platform.
        bool isFUSESupported();

        // Query whether a mount is enabled.
        bool isMountEnabled(const char* path);

        // Remove a disabled mount.
        void removeMount(const char* path, MegaRequestListener* listener);

        // Update FUSE flags.
        void setFUSEFlags(const MegaFuseFlags& flags);

        // Update a mount's flags.
        void setMountFlags(const MegaMountFlags* flags,
                           const char* path,
                           MegaRequestListener* listener);

        //Sets and Elements
        void putSet(MegaHandle sid, int optionFlags, const char* name, MegaHandle cover,
                    int type, MegaRequestListener* listener = nullptr);
        void removeSet(MegaHandle sid, MegaRequestListener* listener = nullptr);
        void putSetElements(MegaHandle sid, const MegaHandleList* nodes, const MegaStringList* names, MegaRequestListener* listener = nullptr);
        void putSetElement(MegaHandle sid, MegaHandle eid, MegaHandle node, int optionFlags, int64_t order, const char* name, MegaRequestListener* listener = nullptr);
        void removeSetElements(MegaHandle sid, const MegaHandleList* eids, MegaRequestListener* listener = nullptr);
        void removeSetElement(MegaHandle sid, MegaHandle eid, MegaRequestListener* listener = nullptr);
        void exportSet(MegaHandle sid, MegaRequestListener* listener = nullptr);
        void disableExportSet(MegaHandle sid, MegaRequestListener* listener = nullptr);

        MegaSetList* getSets();
        MegaSet* getSet(MegaHandle sid);
        MegaHandle getSetCover(MegaHandle sid);
        unsigned getSetElementCount(MegaHandle sid, bool includeElementsInRubbishBin);
        MegaSetElementList* getSetElements(MegaHandle sid, bool includeElementsInRubbishBin);
        MegaSetElement* getSetElement(MegaHandle sid, MegaHandle eid);
        const char* getPublicLinkForExportedSet(MegaHandle sid);
        void fetchPublicSet(const char* publicSetLink, MegaRequestListener* listener = nullptr);
        MegaSet* getPublicSetInPreview();
        MegaSetElementList* getPublicSetElementsInPreview();
        void getPreviewElementNode(MegaHandle eid, MegaRequestListener* listener = nullptr);
        void stopPublicSetPreview();
        bool isExportedSet(MegaHandle sid);
        bool inPublicSetPreview();

        // returns the Pro level based on the current plan and storage usage (MegaAccountDetails::ACCOUNT_TYPE_XYZ)
        static int calcRecommendedProLevel(MegaPricing& pricing, MegaAccountDetails& accDetails);

    private:
        bool nodeInRubbishCheck(handle) const;
        std::pair<bool, error> checkCreateFolderPrecons(const char* name,
                                                        std::shared_ptr<Node> parent,
                                                        MegaRequestPrivate* request);

    public:
#ifdef ENABLE_SYNC
        //Sync
        OverlayIconCachedPaths mRecentlyNotifiedOverlayIconPaths;
        OverlayIconCachedPaths mRecentlyRequestedOverlayIconPaths;

        int syncPathState(string *path);
        MegaNode *getSyncedNode(const LocalPath& path);
        void syncFolder(const char *localFolder, const char *name, MegaHandle megaHandle, SyncConfig::Type type, const char* driveRootIfExternal = NULL, const char* excludePath = NULL, MegaRequestListener* listener = NULL);
        void loadExternalBackupSyncsFromExternalDrive(const char* externalDriveRoot, MegaRequestListener* listener);
        void closeExternalBackupSyncsFromExternalDrive(const char* externalDriveRoot, MegaRequestListener* listener);
        void copySyncDataToCache(const char *localFolder, const char *name, MegaHandle megaHandle, const char *remotePath,
                                          long long localfp, bool enabled, bool temporaryDisabled, MegaRequestListener *listener = NULL);
        void copyCachedStatus(int storageStatus, int blockStatus, int businessStatus, MegaRequestListener *listener = NULL);
        void importSyncConfigs(const char* configs, MegaRequestListener* listener);
        const char* exportSyncConfigs();
        void removeSyncById(handle backupId, MegaRequestListener *listener=NULL);

        void setSyncRunState(MegaHandle backupId, MegaSync::SyncRunningState targetState, MegaRequestListener *listener);

        void rescanSync(MegaHandle backupId, bool reFingerprint);
        MegaSyncList *getSyncs();

        void setLegacyExcludedNames(vector<string> *excludedNames);
        void setLegacyExcludedPaths(vector<string> *excludedPaths);
        void setLegacyExclusionLowerSizeLimit(unsigned long long limit);
        void setLegacyExclusionUpperSizeLimit(unsigned long long limit);
        long long getNumLocalNodes();
        int isNodeSyncable(MegaNode *megaNode);
        MegaError *isNodeSyncableWithError(MegaNode* node);
        bool isScanning();
        bool isSyncing();

        std::atomic<bool> receivedStallFlag{false};
        std::atomic<bool> receivedNameConflictsFlag{false};
        std::atomic<bool> receivedTotalStallsFlag{false};
        std::atomic<bool> receivedTotalNameConflictsFlag{false};

        MegaSync *getSyncByBackupId(mega::MegaHandle backupId);
        MegaSync *getSyncByNode(MegaNode *node);
        MegaSync *getSyncByPath(const char * localPath);
        void getMegaSyncStallList(MegaRequestListener* listener);
        void clearStalledPath(MegaSyncStall*);

        void moveToDebris(const char* path, MegaHandle syncBackupId, MegaRequestListener* listener = nullptr);

        AddressedStallFilter mAddressedStallFilter;

#endif // ENABLE_SYNC

        void moveOrRemoveDeconfiguredBackupNodes(MegaHandle deconfiguredBackupRoot, MegaHandle backupDestination, MegaRequestListener* listener = NULL);

        MegaScheduledCopy *getScheduledCopyByTag(int tag);
        MegaScheduledCopy *getScheduledCopyByNode(MegaNode *node);
        MegaScheduledCopy *getScheduledCopyByPath(const char * localPath);

        void update();
        int isWaiting();
        bool isSyncStalled();
        bool isSyncStalledChanged() override;

        //Statistics
        int getNumPendingUploads();
        int getNumPendingDownloads();
        int getTotalUploads();
        int getTotalDownloads();
        void resetTotalDownloads();
        void resetTotalUploads();
        void updateStats();
        void setLRUCacheSize(unsigned long long size);
        unsigned long long getNumNodesAtCacheLRU() const;
        unsigned long long getNumNodes();
        unsigned long long getAccurateNumNodes();
        long long getTotalDownloadedBytes();
        long long getTotalUploadedBytes();
        long long getTotalDownloadBytes();
        long long getTotalUploadBytes();

        //Filesystem
		int getNumChildren(MegaNode* parent);
		int getNumChildFiles(MegaNode* parent);
        int getNumChildFolders(MegaNode* parent);
        MegaNodeList* getChildren(const MegaSearchFilter* filter, int order, CancelToken cancelToken, const MegaSearchPage* searchPage);
        MegaNodeList* getChildren(const MegaNode *parent, int order, CancelToken cancelToken = CancelToken());
        MegaNodeList* getChildren(MegaNodeList *parentNodes, int order);
        MegaNodeList* getVersions(MegaNode *node);
        int getNumVersions(MegaNode *node);
        bool hasVersions(MegaNode *node);
        void getFolderInfo(MegaNode *node, MegaRequestListener *listener);
        bool isSensitiveInherited(MegaNode* node);
        MegaNodeList* getChildrenFromType(MegaNode* p, int type, int order = 1, CancelToken cancelToken = CancelToken());
        bool hasChildren(MegaNode *parent);
        MegaNode *getChildNode(MegaNode *parent, const char* name);
        MegaNode* getChildNodeOfType(MegaNode *parent, const char *name, int type = TYPE_UNKNOWN);
        MegaNode *getParentNode(MegaNode *node);
        char *getNodePath(MegaNode *node);
        char *getNodePathByNodeHandle(MegaHandle handle);
        MegaNode *getNodeByPath(const char *path, MegaNode *n = NULL);
        MegaNode *getNodeByPathOfType(const char* path, MegaNode* n, int type);
        MegaNode *getNodeByHandle(handle handler);
        MegaContactRequest *getContactRequestByHandle(MegaHandle handle);
        MegaUserList* getContacts();
        MegaUser* getContact(const char* uid);
        MegaUserAlertList* getUserAlerts();
        int getNumUnreadUserAlerts();
        MegaNodeList *getInShares(MegaUser* user, int order);
        MegaNodeList *getInShares(int order);
        MegaShareList *getInSharesList(int order);
        MegaShareList *getUnverifiedInShares(int order);
        MegaUser *getUserFromInShare(MegaNode *node, bool recurse = false);
        bool isPendingShare(MegaNode *node);
        MegaShareList *getOutShares(int order);
        MegaShareList *getOutShares(MegaNode *node);
private:
        sharedNode_vector getOutShares();
public:
        MegaShareList *getPendingOutShares();
        MegaShareList *getPendingOutShares(MegaNode *megaNode);
        MegaShareList *getUnverifiedOutShares(int order);
        bool isPrivateNode(MegaHandle h);
        bool isForeignNode(MegaHandle h);
        MegaNodeList *getPublicLinks(int order);
        MegaContactRequestList *getIncomingContactRequests();
        MegaContactRequestList *getOutgoingContactRequests();

        int getAccess(MegaNode* node);
        long long getSize(MegaNode *node);
        static void removeRecursively(const char *path);

        //Fingerprint
        char *getFingerprint(const char *filePath);
        char *getFingerprint(MegaNode *node);
        char *getFingerprint(MegaInputStream *inputStream, int64_t mtime);
        MegaNode *getNodeByFingerprint(const char* fingerprint);
        MegaNodeList *getNodesByFingerprint(const char* fingerprint);
        MegaNodeList *getNodesByOriginalFingerprint(const char* originalfingerprint, MegaNode* parent);
        MegaNode *getExportableNodeByFingerprint(const char *fingerprint, const char *name = NULL);
        MegaNode *getNodeByFingerprint(const char *fingerprint, MegaNode* parent);
        bool hasFingerprint(const char* fingerprint);

        //CRC
        char *getCRC(const char *filePath);
        char *getCRCFromFingerprint(const char *fingerprint);
        char *getCRC(MegaNode *node);
        MegaNode* getNodeByCRC(const char *crc, MegaNode* parent);

        //Permissions
        MegaError checkAccess(MegaNode* node, int level);
        MegaError* checkAccessErrorExtended(MegaNode* node, int level);
        MegaError checkMove(MegaNode* node, MegaNode* target);
        MegaError* checkMoveErrorExtended(MegaNode* node, MegaNode* target);

        bool isFilesystemAvailable();
        MegaNode *getRootNode();
        MegaNode* getVaultNode();
        MegaNode *getRubbishNode();
        MegaNode *getRootNode(MegaNode *node);
        bool isInRootnode(MegaNode *node, int index);

        void setDefaultFilePermissions(int permissions);
        int getDefaultFilePermissions();
        void setDefaultFolderPermissions(int permissions);
        int getDefaultFolderPermissions();

        long long getBandwidthOverquotaDelay();

        MegaRecentActionBucketList* getRecentActions(unsigned days = 90, unsigned maxnodes = 500);
        void getRecentActionsAsync(unsigned days, unsigned maxnodes, MegaRequestListener *listener = NULL);

        MegaNodeList* search(const MegaSearchFilter* filter, int order, CancelToken cancelToken, const MegaSearchPage* searchPage);

        // deprecated
        MegaNodeList* search(MegaNode *node, const char *searchString, CancelToken cancelToken, bool recursive = true, int order = MegaApi::ORDER_NONE, int mimeType = MegaApi::FILE_TYPE_DEFAULT, int target = MegaApi::SEARCH_TARGET_ALL, bool includeSensitive = true);

    private:
        sharedNode_vector searchInNodeManager(const MegaSearchFilter* filter, int order, CancelToken cancelToken, const MegaSearchPage* searchPage);

        // deprecated
        MegaNodeList* searchWithFlags(MegaNode* node, const char* searchString, CancelToken cancelToken, bool recursive, int order, int mimeType = MegaApi::FILE_TYPE_DEFAULT, int target = MegaApi::SEARCH_TARGET_ALL, Node::Flags requiredFlags = Node::Flags(), Node::Flags excludeFlags = Node::Flags(), Node::Flags excludeRecursiveFlags = Node::Flags());

    public:
        bool processMegaTree(MegaNode* node, MegaTreeProcessor* processor, bool recursive = 1);

        MegaNode *createForeignFileNode(MegaHandle handle, const char *key, const char *name, m_off_t size, m_off_t mtime, const char* fingerprintCrc,
                                       MegaHandle parentHandle, const char *privateauth, const char *publicauth, const char *chatauth);
        MegaNode *createForeignFolderNode(MegaHandle handle, const char *name, MegaHandle parentHandle,
                                         const char *privateauth, const char *publicauth);

        MegaNode *authorizeNode(MegaNode *node);
        void authorizeMegaNodePrivate(MegaNodePrivate *node);
        MegaNode *authorizeChatNode(MegaNode *node, const char *cauth);

        const char *getVersion();
        char *getOperatingSystemVersion();
        void getLastAvailableVersion(const char *appKey, MegaRequestListener *listener = NULL);
        void getLocalSSLCertificate(MegaRequestListener *listener = NULL);
        void queryDNS(const char *hostname, MegaRequestListener *listener = NULL);
        void downloadFile(const char *url, const char *dstpath, MegaRequestListener *listener = NULL);
        const char *getUserAgent();
        const char *getBasePath();

        void contactLinkCreate(bool renew = false, MegaRequestListener *listener = NULL);
        void contactLinkQuery(MegaHandle handle, MegaRequestListener *listener = NULL);
        void contactLinkDelete(MegaHandle handle, MegaRequestListener *listener = NULL);

        void keepMeAlive(int type, bool enable, MegaRequestListener *listener = NULL);
        void acknowledgeUserAlerts(MegaRequestListener *listener = NULL);

        void getPSA(bool urlSupported, MegaRequestListener *listener = NULL);
        void setPSA(int id, MegaRequestListener *listener = NULL);

        void disableGfxFeatures(bool disable);
        bool areGfxFeaturesDisabled();

        void changeApiUrl(const char *apiURL, bool disablepkp = false);

        bool setLanguage(const char* languageCode);
        string generateViewId();
        void setLanguagePreference(const char* languageCode, MegaRequestListener *listener = NULL);
        void getLanguagePreference(MegaRequestListener *listener = NULL);
        bool getLanguageCode(const char* languageCode, std::string* code);

        void setFileVersionsOption(bool disable, MegaRequestListener *listener = NULL);
        void getFileVersionsOption(MegaRequestListener *listener = NULL);

        void setContactLinksOption(bool disable, MegaRequestListener *listener = NULL);
        void getContactLinksOption(MegaRequestListener *listener = NULL);

        void retrySSLerrors(bool enable);
        void setPublicKeyPinning(bool enable);
        void pauseActionPackets();
        void resumeActionPackets();

        static std::function<bool (Node*, Node*)>getComparatorFunction(int order, MegaClient& mc);
        static void sortByComparatorFunction(sharedNode_vector&v, int order, MegaClient& mc);
        static bool nodeNaturalComparatorASC(Node *i, Node *j);
        static bool nodeNaturalComparatorDESC(Node *i, Node *j);
        static bool nodeComparatorDefaultASC  (Node *i, Node *j);
        static bool nodeComparatorDefaultDESC (Node *i, Node *j);
        static bool nodeComparatorSizeASC  (Node *i, Node *j);
        static bool nodeComparatorSizeDESC (Node *i, Node *j);
        static bool nodeComparatorCreationASC  (Node *i, Node *j);
        static bool nodeComparatorCreationDESC  (Node *i, Node *j);
        static bool nodeComparatorModificationASC  (Node *i, Node *j);
        static bool nodeComparatorModificationDESC  (Node *i, Node *j);
        /*deprecated*/ static bool nodeComparatorPhotoASC(Node *i, Node *j, MegaClient& mc);
        /*deprecated*/ static bool nodeComparatorPhotoDESC(Node *i, Node *j, MegaClient& mc);
        /*deprecated*/ static bool nodeComparatorVideoASC(Node *i, Node *j, MegaClient& mc);
        /*deprecated*/ static bool nodeComparatorVideoDESC(Node *i, Node *j, MegaClient& mc);
        static bool nodeComparatorPublicLinkCreationASC(Node *i, Node *j);
        static bool nodeComparatorPublicLinkCreationDESC(Node *i, Node *j);
        static bool nodeComparatorLabelASC(Node *i, Node *j);
        static bool nodeComparatorLabelDESC(Node *i, Node *j);
        static bool nodeComparatorFavASC(Node *i, Node *j);
        static bool nodeComparatorFavDESC(Node *i, Node *j);
        static int typeComparator(Node *i, Node *j);
        static bool userComparatorDefaultASC (User *i, User *j);
        static m_off_t sizeDifference(Node *i, Node *j);

        char* escapeFsIncompatible(const char *filename, const char *dstPath);
        char* unescapeFsIncompatible(const char* name, const char *path);

        bool createThumbnail(const char* imagePath, const char *dstPath);
        bool createPreview(const char* imagePath, const char *dstPath);
        bool createAvatar(const char* imagePath, const char *dstPath);

        // these two: MEGA proxy use only
        void getUploadURL(int64_t fullFileSize, bool forceSSL, MegaRequestListener *listener);
        void completeUpload(const char* utf8Name, MegaNode *parent, const char* fingerprint, const char* fingerprintoriginal,
                                               const char *string64UploadToken, const char *string64FileKey, MegaRequestListener *listener);

        void getFileAttributeUploadURL(MegaHandle nodehandle, int64_t fullFileSize, int faType, bool forceSSL, MegaRequestListener *listener);


        void backgroundMediaUploadRequestUploadURL(int64_t fullFileSize, MegaBackgroundMediaUpload* state, MegaRequestListener *listener);
        void backgroundMediaUploadComplete(MegaBackgroundMediaUpload* state, const char* utf8Name, MegaNode *parent, const char* fingerprint, const char* fingerprintoriginal,
            const char *string64UploadToken, MegaRequestListener *listener);

        bool ensureMediaInfo();
        void setOriginalFingerprint(MegaNode* node, const char* originalFingerprint, MegaRequestListener *listener);

        bool isOnline();

#ifdef HAVE_LIBUV
        // start/stop
        bool httpServerStart(bool localOnly = true, int port = 4443, bool useTLS = false, const char *certificatepath = NULL, const char *keypath = NULL, bool useIPv6 = false);
        void httpServerStop();
        int httpServerIsRunning();

        // management
        char *httpServerGetLocalLink(MegaNode *node);
        char *httpServerGetLocalWebDavLink(MegaNode *node);
        MegaStringList *httpServerGetWebDavLinks();
        MegaNodeList *httpServerGetWebDavAllowedNodes();
        void httpServerRemoveWebDavAllowedNode(MegaHandle handle);
        void httpServerRemoveWebDavAllowedNodes();
        void httpServerSetMaxBufferSize(int bufferSize);
        int httpServerGetMaxBufferSize();
        void httpServerSetMaxOutputSize(int outputSize);
        int httpServerGetMaxOutputSize();

        // permissions
        void httpServerEnableFileServer(bool enable);
        bool httpServerIsFileServerEnabled();
        void httpServerEnableFolderServer(bool enable);
        bool httpServerIsFolderServerEnabled();
        bool httpServerIsOfflineAttributeEnabled();
        void httpServerSetRestrictedMode(int mode);
        int httpServerGetRestrictedMode();
        bool httpServerIsLocalOnly();
        void httpServerEnableOfflineAttribute(bool enable);
        void httpServerEnableSubtitlesSupport(bool enable);
        bool httpServerIsSubtitlesSupportEnabled();

        void httpServerAddListener(MegaTransferListener *listener);
        void httpServerRemoveListener(MegaTransferListener *listener);

        void fireOnStreamingStart(MegaTransferPrivate *transfer);
        void fireOnStreamingTemporaryError(MegaTransferPrivate *transfer, unique_ptr<MegaErrorPrivate> e);
        void fireOnStreamingFinish(MegaTransferPrivate *transfer, unique_ptr<MegaErrorPrivate> e);

        //FTP
        bool ftpServerStart(bool localOnly = true, int port = 4990, int dataportBegin = 1500, int dataPortEnd = 1600, bool useTLS = false, const char *certificatepath = NULL, const char *keypath = NULL);
        void ftpServerStop();
        int ftpServerIsRunning();

        // management
        char *ftpServerGetLocalLink(MegaNode *node);
        MegaStringList *ftpServerGetLinks();
        MegaNodeList *ftpServerGetAllowedNodes();
        void ftpServerRemoveAllowedNode(MegaHandle handle);
        void ftpServerRemoveAllowedNodes();
        void ftpServerSetMaxBufferSize(int bufferSize);
        int ftpServerGetMaxBufferSize();
        void ftpServerSetMaxOutputSize(int outputSize);
        int ftpServerGetMaxOutputSize();

        // permissions
        void ftpServerSetRestrictedMode(int mode);
        int ftpServerGetRestrictedMode();
        bool ftpServerIsLocalOnly();

        void ftpServerAddListener(MegaTransferListener *listener);
        void ftpServerRemoveListener(MegaTransferListener *listener);

        void fireOnFtpStreamingStart(MegaTransferPrivate *transfer);
        void fireOnFtpStreamingTemporaryError(MegaTransferPrivate *transfer, unique_ptr<MegaErrorPrivate> e);
        void fireOnFtpStreamingFinish(MegaTransferPrivate *transfer, unique_ptr<MegaErrorPrivate> e);

#endif

#ifdef ENABLE_CHAT
        void createChat(bool group, bool publicchat, MegaTextChatPeerList* peers, const MegaStringMap* userKeyMap = NULL, const char* title = NULL, bool meetingRoom = false, int chatOptions = MegaApi::CHAT_OPTIONS_EMPTY, const MegaScheduledMeeting* scheduledMeeting = nullptr, MegaRequestListener* listener = NULL);
        void setChatOption(MegaHandle chatid, int option, bool enabled, MegaRequestListener* listener = NULL);
        void inviteToChat(MegaHandle chatid, MegaHandle uh, int privilege, bool openMode, const char *unifiedKey = NULL, const char *title = NULL, MegaRequestListener *listener = NULL);
        void removeFromChat(MegaHandle chatid, MegaHandle uh = INVALID_HANDLE, MegaRequestListener *listener = NULL);
        void getUrlChat(MegaHandle chatid, MegaRequestListener *listener = NULL);
        void grantAccessInChat(MegaHandle chatid, MegaNode *n, MegaHandle uh,  MegaRequestListener *listener = NULL);
        void removeAccessInChat(MegaHandle chatid, MegaNode *n, MegaHandle uh,  MegaRequestListener *listener = NULL);
        void updateChatPermissions(MegaHandle chatid, MegaHandle uh, int privilege, MegaRequestListener *listener = NULL);
        void truncateChat(MegaHandle chatid, MegaHandle messageid, MegaRequestListener *listener = NULL);
        void setChatTitle(MegaHandle chatid, const char *title, MegaRequestListener *listener = NULL);
        void setChatUnifiedKey(MegaHandle chatid, const char *unifiedKey, MegaRequestListener *listener = NULL);
        void getChatPresenceURL(MegaRequestListener *listener = NULL);
        void registerPushNotification(int deviceType, const char *token, MegaRequestListener *listener = NULL);
        void sendChatStats(const char *data, int port, MegaRequestListener *listener = NULL);
         void sendChatLogs(const char *data, MegaHandle userid, MegaHandle callid = INVALID_HANDLE, int port = 0, MegaRequestListener *listener = NULL);
        MegaTextChatList *getChatList();
        MegaHandleList *getAttachmentAccess(MegaHandle chatid, MegaHandle h);
        bool hasAccessToAttachment(MegaHandle chatid, MegaHandle h, MegaHandle uh);
        const char* getFileAttribute(MegaHandle h);
        void archiveChat(MegaHandle chatid, int archive, MegaRequestListener *listener = NULL);
        void setChatRetentionTime(MegaHandle chatid, unsigned int period, MegaRequestListener *listener = NULL);
        void requestRichPreview(const char *url, MegaRequestListener *listener = NULL);
        void chatLinkHandle(MegaHandle chatid, bool del, bool createifmissing, MegaRequestListener *listener = NULL);
        void getChatLinkURL(MegaHandle publichandle, MegaRequestListener *listener = NULL);
        void chatLinkClose(MegaHandle chatid, const char *title, MegaRequestListener *listener = NULL);
        void chatLinkJoin(MegaHandle publichandle, const char *unifiedkey, MegaRequestListener *listener = NULL);
        void enableRichPreviews(bool enable, MegaRequestListener *listener = NULL);
        void isRichPreviewsEnabled(MegaRequestListener *listener = NULL);
        void shouldShowRichLinkWarning(MegaRequestListener *listener = NULL);
        void setRichLinkWarningCounterValue(int value, MegaRequestListener *listener = NULL);
        void enableGeolocation(MegaRequestListener *listener = NULL);
        void isGeolocationEnabled(MegaRequestListener *listener = NULL);
        bool isChatNotifiable(MegaHandle chatid);
        void startChatCall(MegaHandle chatid, bool notRinging, MegaRequestListener* listener = nullptr);
        void joinChatCall(MegaHandle chatid, MegaHandle callid, MegaRequestListener* listener = nullptr);
        void endChatCall(MegaHandle chatid, MegaHandle callid, int reason = 0, MegaRequestListener *listener = nullptr);
        void ringIndividualInACall(MegaHandle chatid, MegaHandle userid, MegaRequestListener* listener = nullptr);
        void setSFUid(int sfuid);
        void createOrUpdateScheduledMeeting(const MegaScheduledMeeting* scheduledMeeting, const char* chatTitle,  MegaRequestListener* listener = NULL);
        void removeScheduledMeeting(MegaHandle chatid, MegaHandle schedId, MegaRequestListener* listener = NULL);
        void fetchScheduledMeeting(MegaHandle chatid, MegaHandle schedId, MegaRequestListener* listener = NULL);
        void fetchScheduledMeetingEvents(MegaHandle chatid, MegaTimeStamp since, MegaTimeStamp until, unsigned int count, MegaRequestListener* listener = NULL);
#endif

        void setMyChatFilesFolder(MegaHandle nodehandle, MegaRequestListener *listener = NULL);
        void getMyChatFilesFolder(MegaRequestListener *listener = NULL);
        void setCameraUploadsFolder(MegaHandle nodehandle, bool secondary, MegaRequestListener *listener = NULL);
        void setCameraUploadsFolders(MegaHandle primaryFolder, MegaHandle secondaryFolder, MegaRequestListener *listener);
        void getCameraUploadsFolder(bool secondary, MegaRequestListener *listener = NULL);
        void setMyBackupsFolder(const char *localizedName, MegaRequestListener *listener = nullptr);
        void getUserAlias(MegaHandle uh, MegaRequestListener *listener = NULL);
        void setUserAlias(MegaHandle uh, const char *alias, MegaRequestListener *listener = NULL);

        void getPushNotificationSettings(MegaRequestListener *listener = NULL);
        void setPushNotificationSettings(MegaPushNotificationSettings *settings, MegaRequestListener *listener = NULL);

        bool isSharesNotifiable();
        bool isContactsNotifiable();

        void getAccountAchievements(MegaRequestListener *listener = NULL);
        void getMegaAchievements(MegaRequestListener *listener = NULL);

        void catchup(MegaRequestListener *listener = NULL);
        void getPublicLinkInformation(const char *megaFolderLink, MegaRequestListener *listener);

        void sendSMSVerificationCode(const char* phoneNumber, MegaRequestListener *listener = NULL, bool reverifying_whitelisted = false);
        void checkSMSVerificationCode(const char* verificationCode, MegaRequestListener *listener = NULL);

        void getCountryCallingCodes(MegaRequestListener *listener = NULL);

        void getBanners(MegaRequestListener *listener);
        void dismissBanner(int id, MegaRequestListener *listener);

        void setBackup(int backupType, MegaHandle targetNode, const char* localFolder, const char* backupName, int state, int subState, MegaRequestListener* listener = nullptr);
        void updateBackup(MegaHandle backupId, int backupType, MegaHandle targetNode, const char* localFolder, const char *backupName, int state, int subState, MegaRequestListener* listener = nullptr);
        void removeBackup(MegaHandle backupId, MegaRequestListener *listener = nullptr);
        void removeFromBC(MegaHandle backupId, MegaHandle moveDestination, MegaRequestListener* listener = nullptr);
        void getBackupInfo(MegaRequestListener* listener = nullptr);
        void sendBackupHeartbeat(MegaHandle backupId, int status, int progress, int ups, int downs, long long ts, MegaHandle lastNode, MegaRequestListener *listener);

        void fetchAds(int adFlags, MegaStringList *adUnits, MegaHandle publicHandle, MegaRequestListener *listener = nullptr);
        void queryAds(int adFlags, MegaHandle publicHandle = INVALID_HANDLE, MegaRequestListener *listener = nullptr);

        void setCookieSettings(int settings, MegaRequestListener *listener = nullptr);
        void getCookieSettings(MegaRequestListener *listener = nullptr);
        bool cookieBannerEnabled();

        bool startDriveMonitor();
        void stopDriveMonitor();
        bool driveMonitorEnabled();

        void enableRequestStatusMonitor(bool enable);
        bool requestStatusMonitorEnabled();

        /* MegaVpnCredentials */
        void getVpnRegions(MegaRequestListener* listener = nullptr);
        void getVpnCredentials(MegaRequestListener* listener = nullptr);
        void putVpnCredential(const char* region, MegaRequestListener* listener = nullptr);
        void delVpnCredential(int slotID, MegaRequestListener* listener = nullptr);
        void checkVpnCredential(const char* userPubKey, MegaRequestListener* listener = nullptr);
        /* MegaVpnCredentials end */

        // Password Manager
        void getPasswordManagerBase(MegaRequestListener *listener = nullptr);
        bool isPasswordNodeFolder(MegaHandle node) const;
        void createPasswordNode(const char *name, const MegaNode::PasswordNodeData *data,
                                MegaHandle parent, MegaRequestListener *listener = nullptr);
        void updatePasswordNode(MegaHandle node, const MegaNode::PasswordNodeData* newData,
                                MegaRequestListener *listener = NULL);

        void fetchCreditCardInfo(MegaRequestListener* listener = nullptr);

        void fireOnTransferStart(MegaTransferPrivate *transfer);
        void fireOnTransferFinish(MegaTransferPrivate *transfer, unique_ptr<MegaErrorPrivate> e); // deletes `transfer` !!
        void fireOnTransferUpdate(MegaTransferPrivate *transfer);
        void fireOnFolderTransferUpdate(MegaTransferPrivate *transfer, int stage, uint32_t foldercount, uint32_t createdfoldercount, uint32_t filecount, const LocalPath* currentFolder, const LocalPath* currentFileLeafname);
        void fireOnTransferTemporaryError(MegaTransferPrivate *transfer, unique_ptr<MegaErrorPrivate> e);
        map<int, MegaTransferPrivate *> transferMap;


        MegaClient *getMegaClient();
        static FileFingerprint *getFileFingerprintInternal(const char *fingerprint);

        error processAbortBackupRequest(MegaRequestPrivate *request);
        void fireOnBackupStateChanged(MegaScheduledCopyController *backup);
        void fireOnBackupStart(MegaScheduledCopyController *backup);
        void fireOnBackupFinish(MegaScheduledCopyController *backup, unique_ptr<MegaErrorPrivate> e);
        void fireOnBackupUpdate(MegaScheduledCopyController *backup);
        void fireOnBackupTemporaryError(MegaScheduledCopyController *backup, unique_ptr<MegaErrorPrivate> e);

        void yield();
        void lockMutex();
        void unlockMutex();
        bool tryLockMutexFor(long long time);

        void getVisibleWelcomeDialog(MegaRequestListener* listener);

        void setVisibleWelcomeDialog(bool visible, MegaRequestListener* listener);

        void createNodeTree(const MegaNode* parentNode,
                            MegaNodeTree* nodeTree,
                            MegaRequestListener* listener);

        void getVisibleTermsOfService(MegaRequestListener* listener = nullptr);

        void setVisibleTermsOfService(bool visible, MegaRequestListener* listener = nullptr);

        MegaIntegerList* getEnabledNotifications() const;
        void enableTestNotifications(const MegaIntegerList* notificationIds, MegaRequestListener* listener);
        void getNotifications(MegaRequestListener* listener);
        void setLastReadNotification(uint32_t notificationId, MegaRequestListener* listener);
        void getLastReadNotification(MegaRequestListener* listener);
        void setLastActionedBanner(uint32_t notificationId, MegaRequestListener* listener);
        void getLastActionedBanner(MegaRequestListener* listener);
        MegaFlagPrivate* getFlag(const char* flagName, bool commit, MegaRequestListener* listener = nullptr);

        void deleteUserAttribute(int type, MegaRequestListener* listener = NULL);

    private:
        void init(MegaApi *api, const char *appKey, std::unique_ptr<GfxProc> gfxproc, const char *basePath /*= NULL*/, const char *userAgent /*= NULL*/, unsigned clientWorkerThreadCount /*= 1*/, int clientType);

        static void *threadEntryPoint(void *param);

        MegaTransferPrivate* getMegaTransferPrivate(int tag);

        void fireOnRequestStart(MegaRequestPrivate *request);
        void fireOnRequestFinish(MegaRequestPrivate *request, unique_ptr<MegaErrorPrivate> e, bool callbackIsFromSyncThread = false);
        void fireOnRequestUpdate(MegaRequestPrivate *request);
        void fireOnRequestTemporaryError(MegaRequestPrivate *request, unique_ptr<MegaErrorPrivate> e);
        bool fireOnTransferData(MegaTransferPrivate *transfer);
        void fireOnUsersUpdate(MegaUserList *users);
        void fireOnUserAlertsUpdate(MegaUserAlertList *alerts);
        void fireOnNodesUpdate(MegaNodeList *nodes);
        void fireOnAccountUpdate();
        void fireOnSetsUpdate(MegaSetList* sets);
        void fireOnSetElementsUpdate(MegaSetElementList* elements);
        void fireOnContactRequestsUpdate(MegaContactRequestList *requests);
        void fireOnEvent(MegaEventPrivate *event);

#ifdef ENABLE_SYNC
        void fireOnGlobalSyncStateChanged();
        void fireOnSyncStateChanged(MegaSyncPrivate *sync);
        void fireOnSyncStatsUpdated(MegaSyncStatsPrivate*);
        void fireOnSyncAdded(MegaSyncPrivate *sync);
        void fireOnSyncDeleted(MegaSyncPrivate *sync);
        void fireOnFileSyncStateChanged(MegaSyncPrivate *sync, string *localPath, int newState);
#endif

#ifdef ENABLE_CHAT
        void fireOnChatsUpdate(MegaTextChatList *chats);
#endif

        void processTransferPrepare(Transfer *t, MegaTransferPrivate *transfer);
        void processTransferUpdate(Transfer *tr, MegaTransferPrivate *transfer);
        void processTransferComplete(Transfer *tr, MegaTransferPrivate *transfer);
        void processTransferFailed(Transfer *tr, MegaTransferPrivate *transfer, const Error &e, dstime timeleft);
        void processTransferRemoved(Transfer *tr, MegaTransferPrivate *transfer, const Error &e);

        // deprecated
        // if seachString == "" type must not be default
        sharedNode_vector searchInNodeManager(MegaHandle nodeHandle, const char* searchString, int mimeType, bool recursive, Node::Flags requiredFlags, Node::Flags excludeFlags, Node::Flags excludeRecursiveFlags, CancelToken cancelToken);

        bool isValidTypeNode(const Node *node, int type) const;

        MegaApi *api;
        std::thread thread;
        std::thread::id threadId;
        MegaClient *client;
        MegaHttpIO *httpio;
        shared_ptr<MegaWaiter> waiter;
        unique_ptr<MegaFileSystemAccess> fsAccess;
        MegaDbAccess *dbAccess;
        GfxProc *gfxAccess;
        string basePath;
        bool nocache;

        // for fingerprinting off-thread
        // one at a time is enough
        mutex fingerprintingFsAccessMutex;
        MegaFileSystemAccess fingerprintingFsAccess;

        mutex mLastRecievedLoggedMeMutex;
        sessiontype_t mLastReceivedLoggedInState = NOTLOGGEDIN;
        handle mLastReceivedLoggedInMeHandle = UNDEF;
        string mLastReceivedLoggedInMyEmail;

        unique_ptr<MegaNode> mLastKnownRootNode;
        unique_ptr<MegaNode> mLastKnownVaultNode;
        unique_ptr<MegaNode> mLastKnownRubbishNode;

#ifdef HAVE_LIBUV
        MegaHTTPServer *httpServer;
        int httpServerMaxBufferSize;
        int httpServerMaxOutputSize;
        bool httpServerEnableFiles;
        bool httpServerEnableFolders;
        bool httpServerOfflineAttributeEnabled;
        int httpServerRestrictedMode;
        bool httpServerSubtitlesSupportEnabled;
        set<MegaTransferListener *> httpServerListeners;

        MegaFTPServer *ftpServer;
        int ftpServerMaxBufferSize;
        int ftpServerMaxOutputSize;
        int ftpServerRestrictedMode;
        set<MegaTransferListener *> ftpServerListeners;
#endif

        map<int, MegaScheduledCopyController *> backupsMap;

        RequestQueue requestQueue;
        TransferQueue transferQueue;
        map<int, MegaRequestPrivate *> requestMap;

        // sc requests to close existing wsc and immediately retrieve pending actionpackets
        RequestQueue scRequestQueue;

        int pendingUploads;
        int pendingDownloads;
        int totalUploads;
        int totalDownloads;
        long long totalDownloadedBytes;
        long long totalUploadedBytes;
        long long totalDownloadBytes;
        long long totalUploadBytes;
        long long notificationNumber;
        set<MegaRequestListener *> requestListeners;
        set<MegaTransferListener *> transferListeners;
        set<MegaScheduledCopyListener *> backupListeners;

#ifdef ENABLE_SYNC
        std::unique_ptr<BackupMonitor> mHeartBeatMonitor;
        MegaSyncPrivate* cachedMegaSyncPrivateByBackupId(const SyncConfig&);
        unique_ptr<MegaSyncPrivate> mCachedMegaSyncPrivate;
#endif

        set<MegaGlobalListener *> globalListeners;
        set<MegaListener *> listeners;
        retryreason_t waitingRequest;
        mutable std::recursive_timed_mutex sdkMutex;
        using SdkMutexGuard = std::unique_lock<std::recursive_timed_mutex>;   // (equivalent to typedef)
        MegaTransferPrivate *currentTransfer;
        string appKey;

        std::unique_ptr<MegaPushNotificationSettingsPrivate> getMegaPushNotificationSetting(); // returns lastest-seen settings (to be able to filter notifications)

        MegaTimeZoneDetails *mTimezones;

        std::atomic<bool> syncPathStateLockTimeout{ false };
        set<LocalPath> syncPathStateDeferredSet;
        mutex syncPathStateDeferredSetMutex;

        int threadExit;
        void loop();

        int maxRetries;

        // a request-level error occurred
        void request_error(error) override;
        void request_response_progress(m_off_t, m_off_t) override;

        // login result
        void prelogin_result(int, string*, string*, error) override;
        void login_result(error) override;
        void logout_result(error, MegaRequestPrivate*);
        void userdata_result(string*, string*, string*, Error) override;
        void pubkey_result(User *) override;

        // ephemeral session creation/resumption result

        // check the reason of being blocked
        void ephemeral_result(error) override;
        void ephemeral_result(handle, const byte*) override;
        void cancelsignup_result(error) override;

        // check the reason of being blocked
        void whyamiblocked_result(int) override;

        // contact link management
        void contactlinkcreate_result(error, handle) override;
        void contactlinkquery_result(error, handle, string*, string*, string*, string*) override;
        void contactlinkdelete_result(error) override;

        // multi-factor authentication
        void multifactorauthsetup_result(string*, error) override;
        void multifactorauthcheck_result(int) override;
        void multifactorauthdisable_result(error) override;

        // fetch time zone
        void fetchtimezone_result(error, vector<string>*, vector<int>*, int) override;

        // keep me alive feature
        void keepmealive_result(error) override;
        void acknowledgeuseralerts_result(error) override;

        // account validation by txted verification code
        void smsverificationsend_result(error) override;
        void smsverificationcheck_result(error, std::string *phoneNumber) override;

        // get country calling codes
        void getcountrycallingcodes_result(error, map<string, vector<string>>*) override;

        // get the current PSA
        void getpsa_result (error, int, string*, string*, string*, string*, string*, string*) override;

        // account creation
        void sendsignuplink_result(error) override;
        void confirmsignuplink2_result(handle, const char*, const char*, error) override;
        void setkeypair_result(error) override;

        // account credentials, properties and history
        void account_details(AccountDetails*,  bool, bool, bool, bool, bool, bool) override;
        void account_details(AccountDetails*, error) override;
        void querytransferquota_result(int) override;

        void unlink_result(handle, error) override;
        void unlinkversions_result(error) override;
        void nodes_updated(sharedNode_vector* nodes, int) override;
        void users_updated(User**, int) override;
        void useralerts_updated(UserAlert::Base**, int) override;
        void account_updated() override;
        void pcrs_updated(PendingContactRequest**, int) override;
        void sequencetag_update(const string&) override;
        void sets_updated(Set**, int) override;
        void setelements_updated(SetElement**, int) override;

        // password change result
        void changepw_result(error) override;

        // user attribute update notification
        void userattr_update(User*, int, const char*) override;

        void nodes_current() override;
        void catchup_result() override;
        void key_modified(handle, attr_t) override;
        void upgrading_security() override;
        void downgrade_attack() override;

        void fetchnodes_result(const Error&) override;
        void putnodes_result(const Error&, targettype_t, vector<NewNode>&, bool targetOverride, int tag) override;

        // contact request results
        void setpcr_result(handle, error, opcactions_t) override;
        void updatepcr_result(error, ipcactions_t) override;

        // file attribute fetch result
        void fa_complete(handle, fatype, const char*, uint32_t) override;
        int fa_failed(handle, fatype, int, error) override;

        // file attribute modification result
        void putfa_result(handle, fatype, error) override;

#ifdef USE_DRIVE_NOTIFICATIONS
        // external drive [dis-]connected
        void drive_presence_changed(bool appeared, const LocalPath& driveRoot) override;
#endif

        // purchase transactions
        void enumeratequotaitems_result(unsigned type, handle product, unsigned prolevel, int gbstorage, int gbtransfer,
                                        unsigned months, unsigned amount, unsigned amountMonth, unsigned localPrice,
                                        const char* description, std::map<std::string, uint32_t>&& features,
                                        const char* iosid, const char* androidid, 
                                        unsigned int testCategory, std::unique_ptr<BusinessPlan>) override;
        void enumeratequotaitems_result(unique_ptr<CurrencyData>) override;
        void enumeratequotaitems_result(error e) override;
        void additem_result(error) override;
        void checkout_result(const char*, error) override;
        void submitpurchasereceipt_result(error) override;
        void creditcardstore_result(error) override;
        void creditcardquerysubscriptions_result(int, error) override;
        void creditcardcancelsubscriptions_result(error) override;
        void getpaymentmethods_result(int, error) override;
        void copysession_result(string*, error) override;

        void userfeedbackstore_result(error) override;
        void sendevent_result(error) override;
        void supportticket_result(error) override;

        // user invites/attributes
        void removecontact_result(error) override;
#ifdef DEBUG
        void delua_result(error) override;
        void senddevcommand_result(int) override;
#endif

        void getuseremail_result(string *, error) override;

        // exported link access result
        void openfilelink_result(const Error&) override;
        void openfilelink_result(handle, const byte*, m_off_t, string*, string*, int) override;

        // retrieval of public link information
        void folderlinkinfo_result(error, handle, handle, string *, string*, m_off_t, uint32_t, uint32_t, m_off_t, uint32_t) override;

        // global transfer queue updates (separate signaling towards the queued objects)
        void file_added(File*) override;
        void file_removed(File*, const Error& e) override;
        void file_complete(File*) override;

        void transfer_complete(Transfer *) override;
        void transfer_removed(Transfer *) override;

        File* file_resume(string*, direction_t *type) override;

        void transfer_prepare(Transfer*) override;
        void transfer_failed(Transfer*, const Error& error, dstime timeleft) override;
        void transfer_update(Transfer*) override;

        dstime pread_failure(const Error&, int, void*, dstime) override;
        bool pread_data(byte*, m_off_t, m_off_t, m_off_t, m_off_t, void*) override;

        void reportevent_result(error) override;
        void sessions_killed(handle sessionid, error e) override;

        void cleanrubbishbin_result(error) override;

        void getrecoverylink_result(error) override;
        void queryrecoverylink_result(error) override;
        void queryrecoverylink_result(int type, const char *email, const char *ip, time_t ts, handle uh, const vector<string> *emails) override;
        void getprivatekey_result(error, const byte *privk = NULL, const size_t len_privk = 0) override;
        void confirmrecoverylink_result(error) override;
        void confirmcancellink_result(error) override;
        void getemaillink_result(error) override;
        void resendverificationemail_result(error) override;
        void resetSmsVerifiedPhoneNumber_result(error) override;
        void confirmemaillink_result(error) override;
        void getversion_result(int, const char*, error) override;
        void getlocalsslcertificate_result(m_time_t, string *certdata, error) override;
        void getmegaachievements_result(AchievementsDetails*, error) override;
        void mediadetection_ready() override;
        void storagesum_changed(int64_t newsum) override;
        void getmiscflags_result(error) override;
        void getbanners_result(error e) override;
        void getbanners_result(vector< tuple<int, string, string, string, string, string, string> >&& banners) override;
        void dismissbanner_result(error e) override;
        void reqstat_progress(int permilprogress) override;

        // for internal use - for worker threads to run something on MegaApiImpl's thread, such as calls to onFire() functions
        void executeOnThread(shared_ptr<ExecuteOnce>);

#ifdef ENABLE_CHAT
        // chat-related commandsresult
        void chatcreate_result(TextChat *, error) override;
        void chatinvite_result(error) override;
        void chatremove_result(error) override;
        void chaturl_result(string*, error) override;
        void chatgrantaccess_result(error) override;
        void chatremoveaccess_result(error) override;
        void chatupdatepermissions_result(error) override;
        void chattruncate_result(error) override;
        void chatsettitle_result(error) override;
        void chatpresenceurl_result(string*, error) override;
        void registerpushnotification_result(error) override;
        void archivechat_result(error) override;
        void setchatretentiontime_result(error) override;

        void chats_updated(textchat_map *, int) override;
        void richlinkrequest_result(string*, error) override;
        void chatlink_result(handle, error) override;
        void chatlinkurl_result(handle, int, string*, string*, int, m_time_t, bool, int, const std::vector<std::unique_ptr<ScheduledMeeting>>*, handle, error) override;
        void chatlinkclose_result(error) override;
        void chatlinkjoin_result(error) override;
#endif

#ifdef ENABLE_SYNC
        // sync status updates and events

        // calls fireOnSyncStateChanged
        void syncupdate_stateconfig(const SyncConfig& config) override;

        void syncupdate_stats(handle backupId, const PerSyncStats& stats) override;

        // this will fill syncMap with a new MegaSyncPrivate, and fire onSyncAdded
        void sync_added(const SyncConfig& config) override;

        // this will fire onSyncStateChange if remote path of the synced node has changed
        virtual void syncupdate_remote_root_changed(const SyncConfig &) override;

        // this will call will fire EVENT_SYNCS_RESTORED
        virtual void syncs_restored(SyncError syncError) override;

        // this will call will fire EVENT_SYNCS_DISABLED
        virtual void syncs_disabled(SyncError syncError) override;

        // removes the sync from syncMap and fires onSyncDeleted callback
        void sync_removed(const SyncConfig& config) override;

        void syncupdate_syncing(bool syncing) override;
        void syncupdate_scanning(bool scanning) override;
        void syncupdate_stalled(bool stalled) override;
        void syncupdate_conflicts(bool conflicts) override;
        void syncupdate_totalstalls(bool totalstalls) override;
        void syncupdate_totalconflicts(bool totalconflicts) override;
        void syncupdate_treestate(const SyncConfig &, const LocalPath&, treestate_t, nodetype_t) override;

        // for the exclusive use of sync_syncable
        unique_ptr<FileAccess> mSyncable_fa;
        std::mutex mSyncable_fa_mutex;
#endif

        void backupput_result(const Error&, handle backupId) override;

        // Notify sdk errors (DB, node serialization, ...) to apps
        void notifyError(const char*, ErrorReason errorReason) override;

        // reload forced automatically by server
        void reloading() override;

        // wipe all users, nodes and shares
        void clearing() override;

        // failed request retry notification
        void notify_retry(dstime, retryreason_t) override;

        // notify about db commit
        void notify_dbcommit() override;

        // notify about a storage event
        void notify_storage(int) override;

        // notify about an automatic change to HTTPS
        void notify_change_to_https() override;

        // notify about account confirmation
        void notify_confirmation(const char*) override;

        // notify about account confirmation after signup link -> user, email have been confirmed
        void notify_confirm_user_email(handle /*user*/, const char* /*email*/) override;

        // network layer disconnected
        void notify_disconnect() override;

        // notify about a finished HTTP request
        void http_result(error, int, byte *, int) override;

        // notify about a business account status change
        void notify_business_status(BizStatus status) override;

        // notify about a finished timer
        void timer_result(error) override;

        // notify credit card Expiry
        void notify_creditCardExpiry() override;

        void sendPendingScRequest();
        void sendPendingRequests();
        unsigned sendPendingTransfers(TransferQueue *queue, MegaRecursiveOperation* = nullptr, m_off_t availableDiskSpace = 0);
        void updateBackups();

        //Internal
        std::shared_ptr<Node> getNodeByFingerprintInternal(const char *fingerprint);
        std::shared_ptr<Node> getNodeByFingerprintInternal(const char *fingerprint, Node *parent);

        void getNodeAttribute(MegaNode* node, int type, const char *dstFilePath, MegaRequestListener *listener = NULL);
        void cancelGetNodeAttribute(MegaNode *node, int type, MegaRequestListener *listener = NULL);
        void setNodeAttribute(MegaNode* node, int type, const char *srcFilePath, MegaHandle attributehandle, MegaRequestListener *listener = NULL);
        void putNodeAttribute(MegaBackgroundMediaUpload* bu, int type, const char *srcFilePath, MegaRequestListener *listener = NULL);
        void setUserAttr(int type, const char *value, MegaRequestListener *listener = NULL);
        void getUserAttr(User* user, attr_t type, MegaRequestPrivate* request);
        void getUserAttr(const std::string& email, attr_t type, const char* ph, MegaRequestPrivate* request);
        void getua_completion(error, MegaRequestPrivate* request);
        void getua_completion(byte*, unsigned, attr_t, MegaRequestPrivate* request);
        void getua_completion(TLVstore *, attr_t, MegaRequestPrivate* request);
        static char *getAvatarColor(handle userhandle);
        static char *getAvatarSecondaryColor(handle userhandle);
        bool isGlobalNotifiable(MegaPushNotificationSettingsPrivate* pushSettings);

        // return false if there's a schedule and it currently does not apply. Otherwise, true
        bool isScheduleNotifiable(MegaPushNotificationSettingsPrivate* pushSettings);

        // deletes backups, requests and transfers. Reset total stats for down/uploads
        void abortPendingActions(error preverror = API_OK);

        bool hasToForceUpload(const Node &node, const MegaTransferPrivate &transfer) const;

        void exportSet(MegaHandle sid, bool create, MegaRequestListener* listener = nullptr);

        // Password Manager - private
        void createPasswordManagerBase(MegaRequestPrivate*);
        std::unique_ptr<AttrMap> toPasswordNodeData(const MegaNode::PasswordNodeData* data) const;

        friend class MegaBackgroundMediaUploadPrivate;
        friend class MegaFolderDownloadController;
        friend class MegaFolderUploadController;
        friend class MegaRecursiveOperation;

        void setCookieSettings_sendPendingRequests(MegaRequestPrivate* request);
        error getCookieSettings_getua_result(byte* data, unsigned len, MegaRequestPrivate* request);

        error performRequest_backupPut(MegaRequestPrivate* request);
        error performRequest_verifyCredentials(MegaRequestPrivate* request);
        error performRequest_completeBackgroundUpload(MegaRequestPrivate* request);
        error performRequest_getBackgroundUploadURL(MegaRequestPrivate* request);
        error performRequest_getAchievements(MegaRequestPrivate* request);
#ifdef ENABLE_CHAT
        error performRequest_chatStats(MegaRequestPrivate* request);
#endif
        error performRequest_getUserData(MegaRequestPrivate* request);
        error performRequest_enumeratequotaitems(MegaRequestPrivate* request);
        error performRequest_getChangeEmailLink(MegaRequestPrivate* request);
        error performRequest_getCancelLink(MegaRequestPrivate* request);
        error performRequest_confirmAccount(MegaRequestPrivate* request);
        error performRequest_sendSignupLink(MegaRequestPrivate* request);
        error performRequest_createAccount(MegaRequestPrivate* request);
        error performRequest_retryPendingConnections(MegaRequestPrivate* request);
        error performRequest_setAttrNode(MegaRequestPrivate* request);
        error performRequest_setAttrFile(MegaRequestPrivate* request);
        error performRequest_setAttrUser(MegaRequestPrivate* request);
        error performRequest_getAttrUser(MegaRequestPrivate* request);
        error performRequest_logout(MegaRequestPrivate* request);
        error performRequest_changePw(MegaRequestPrivate* request);
        error performRequest_export(MegaRequestPrivate* request);
        error performRequest_passwordLink(MegaRequestPrivate* request);
        error performRequest_importLink_getPublicNode(MegaRequestPrivate* request);
        error performRequest_copy(MegaRequestPrivate* request);
        error copyTreeFromOwnedNode(shared_ptr<Node> node, const char *newName, shared_ptr<Node> target, vector<NewNode>& treeCopy);
        error performRequest_login(MegaRequestPrivate* request);
        error performRequest_tagNode(MegaRequestPrivate* request);
        void CRUDNodeTagOperation(MegaNode* node,
                                  int operationType,
                                  const char* tag,
                                  const char* oldTag,
                                  MegaRequestListener* listener);

        error performTransferRequest_cancelTransfer(MegaRequestPrivate* request, TransferDbCommitter& committer);
        error performTransferRequest_moveTransfer(MegaRequestPrivate* request, TransferDbCommitter& committer);

        void multiFactorAuthEnableOrDisable(const char* pin, bool enable, MegaRequestListener* listener);
#ifdef ENABLE_SYNC
        void addSyncByRequest(MegaRequestPrivate* request, SyncConfig sc, MegaClient::UndoFunction revertOnError);
#endif
        void CompleteFileDownloadBySkip(MegaTransferPrivate* transfer, m_off_t size, uint64_t nodehandle, int nextTag, const LocalPath& localPath);

        void performRequest_enableTestNotifications(MegaRequestPrivate* request);
        error performRequest_getNotifications(MegaRequestPrivate * request);
        void performRequest_setLastReadNotification(MegaRequestPrivate* request);
        error getLastReadNotification_getua_result(byte* data, unsigned len, MegaRequestPrivate* request);
        void performRequest_setLastActionedBanner(MegaRequestPrivate* request);
        error getLastActionedBanner_getua_result(byte* data, unsigned len, MegaRequestPrivate* request);
};

class MegaHashSignatureImpl
{
	public:
		MegaHashSignatureImpl(const char *base64Key);
		~MegaHashSignatureImpl();
		void init();
		void add(const char *data, unsigned size);
        bool checkSignature(const char *base64Signature);

	protected:
		HashSignature *hashSignature;
		AsymmCipher* asymmCypher;
};

class ExternalInputStream : public InputStreamAccess
{
    MegaInputStream *inputStream;

public:
    ExternalInputStream(MegaInputStream *inputStream);
    m_off_t size() override;
    bool read(byte *buffer, unsigned size) override;
};

#ifdef HAVE_LIBUV
class StreamingBuffer
{
public:
    StreamingBuffer();
    ~StreamingBuffer();
    // Allocate buffer and reset class members
    void init(size_t capacity);
    // Reset positions for body writting ("forgets" buffered external data such as headers, which use the same buffer) [Default: 0 -> the whole buffer]
    void reset(bool freeData, size_t sizeToReset = 0);
    // Add data to the buffer. This will mainly come from the Transfer (or from a cache file if it's included someday).
    size_t append(const char *buf, size_t len);
    // Get buffered data size
    size_t availableData() const;
    // Get free space available in buffer
    size_t availableSpace() const;
    // Get total buffer capacity
    size_t availableCapacity() const;
    // Get the uv_buf_t for the consumer with as much buffered data as possible
    uv_buf_t nextBuffer();
    // Increase the free data counter
    void freeData(size_t len);
    // Set upper bound limit for capacity
    void setMaxBufferSize(unsigned int bufferSize);
    // Set upper bound limit for chunk size to write to the consumer
    void setMaxOutputSize(unsigned int outputSize);
    // Set file size
    void setFileSize(m_off_t fileSize);
    // Set media length in seconds
    void setDuration(int duration);
    // Rate between file size and its duration (only for media files)
    m_off_t getBytesPerSecond() const;
    // Get upper bound limit for capacity
    unsigned getMaxBufferSize();
    // Get upper bound limit for chunk size to write to the consumer
    unsigned getMaxOutputSize();
    // Get the actual buffer state for debugging purposes
    std::string bufferStatus() const;

    static const unsigned int MAX_BUFFER_SIZE = 2097152;
    static const unsigned int MAX_OUTPUT_SIZE = MAX_BUFFER_SIZE / 10;

private:
    // Rate between partial file size and its duration (only for media files)
    m_off_t partialDuration(m_off_t partialSize) const;
    // Recalculate maxBufferSize and maxOutputSize taking into accout the byteRate (for media files) and DirectReadSlot read chunk size.
    void calcMaxBufferAndMaxOutputSize();

protected:
    // Circular buffer to store data to feed the consumer
    char* buffer;
    // Total buffer size
    size_t capacity;
    // Buffered data size
    size_t size;
    // Available free space in buffer
    size_t free;
    // Index for last buffered data
    size_t inpos;
    // Index for last written data (to the consumer)
    size_t outpos;
    // Upper bound limit for capacity
    size_t maxBufferSize;
    // Upper bound limit for chunk size to write to the consumer
    size_t maxOutputSize;

    // File size
    m_off_t fileSize;
    // Media length in seconds (for media files)
    int duration;
};

class MegaTCPServer;
class MegaTCPContext : public MegaTransferListener, public MegaRequestListener
{
public:
    MegaTCPContext();
    virtual ~MegaTCPContext();

    // Connection management
    MegaTCPServer *server;
    uv_tcp_t tcphandle;
    uv_async_t asynchandle;
    uv_mutex_t mutex;
    MegaApiImpl *megaApi;
    m_off_t bytesWritten;
    m_off_t size;
    char *lastBuffer;
    size_t lastBufferLen;
    bool nodereceived;
    bool finished;
    bool failed;
    bool pause;

#ifdef ENABLE_EVT_TLS
    //tls stuff:
    evt_tls_t *evt_tls;
    bool invalid;
#endif
    std::list<char*> writePointers;

    // Request information
    bool range;
    m_off_t rangeStart;
    m_off_t rangeEnd;
    m_off_t rangeWritten;
    MegaNode *node;
    std::string path;
    std::string nodehandle;
    std::string nodekey;
    std::string nodename;
    m_off_t nodesize;
    int resultCode;

};

class MegaTCPServer
{
protected:
    static void *threadEntryPoint(void *param);
    static http_parser_settings parsercfg;

    uv_loop_t uv_loop;

    set<handle> allowedHandles;
    handle lastHandle;
    list<MegaTCPContext*> connections;
    uv_async_t exit_handle;
    MegaApiImpl *megaApi;
    bool semaphoresdestroyed;
    uv_sem_t semaphoreStartup;
    uv_sem_t semaphoreEnd;
    MegaThread *thread;
    uv_tcp_t server;
    int maxBufferSize;
    int maxOutputSize;
    int restrictedMode;
    bool localOnly;
    bool started;
    int port;
    bool closing;
    int remainingcloseevents;

#ifdef ENABLE_EVT_TLS
    // TLS
    bool evtrequirescleaning;
    evt_ctx_t evtctx;
    std::string certificatepath;
    std::string keypath;
#endif

    // libuv callbacks
    static void onNewClient(uv_stream_t* server_handle, int status);
    static void onDataReceived(uv_stream_t* tcp, ssize_t nread, const uv_buf_t * buf);
    static void allocBuffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t* buf);
    static void onClose(uv_handle_t* handle);

#ifdef ENABLE_EVT_TLS
    //libuv tls
    static void onNewClient_tls(uv_stream_t* server_handle, int status);
    static void onWriteFinished_tls_async(uv_write_t* req, int status);
    static void on_tcp_read(uv_stream_t *stream, ssize_t nrd, const uv_buf_t *data);
    static int uv_tls_writer(evt_tls_t *evt_tls, void *bfr, int sz);
    static void on_evt_tls_close(evt_tls_t *evt_tls, int status);
    static void on_hd_complete( evt_tls_t *evt_tls, int status);
    static void evt_on_rd(evt_tls_t *evt_tls, char *bfr, int sz);
#endif


    static void onAsyncEventClose(uv_handle_t* handle);
    static void onAsyncEvent(uv_async_t* handle);
    static void onExitHandleClose(uv_handle_t* handle);

    static void onCloseRequested(uv_async_t* handle);

    static void onWriteFinished(uv_write_t* req, int status); //This might need to go to HTTPServer
#ifdef ENABLE_EVT_TLS
    static void onWriteFinished_tls(evt_tls_t *evt_tls, int status);
#endif
    static void closeConnection(MegaTCPContext *tcpctx);
    static void closeTCPConnection(MegaTCPContext *tcpctx);

    void run();
    void initializeAndStartListening();

    void answer(MegaTCPContext* tcpctx, const char *rsp, size_t rlen);


    //virtual methods:
    virtual void processReceivedData(MegaTCPContext *tcpctx, ssize_t nread, const uv_buf_t * buf);
    virtual void processAsyncEvent(MegaTCPContext *tcpctx);
    virtual MegaTCPContext * initializeContext(uv_stream_t *server_handle) = 0;
    virtual void processWriteFinished(MegaTCPContext* tcpctx, int status) = 0;
    virtual void processOnAsyncEventClose(MegaTCPContext* tcpctx);
    virtual bool respondNewConnection(MegaTCPContext* tcpctx) = 0; //returns true if server needs to start by reading
    virtual void processOnExitHandleClose(MegaTCPServer* tcpServer);

public:
    const bool useIPv6;
    const bool useTLS;
    MegaFileSystemAccess *fsAccess;

    std::string basePath;

    MegaTCPServer(MegaApiImpl *megaApi, std::string basePath, bool useTLS = false, std::string certificatepath = std::string(), std::string keypath = std::string(), bool useIPv6 = false);
    virtual ~MegaTCPServer();
    bool start(int port, bool localOnly = true);
    void stop(bool doNotWait = false);
    int getPort();
    bool isLocalOnly();
    void setMaxBufferSize(int bufferSize);
    void setMaxOutputSize(int outputSize);
    int getMaxBufferSize();
    int getMaxOutputSize();
    void setRestrictedMode(int mode);
    int getRestrictedMode();
    bool isHandleAllowed(handle h);
    void clearAllowedHandles();
    char* getLink(MegaNode *node, std::string protocol = "http");
    bool isCurrentThread() {
        return thread->isCurrentThread();
    }

    set<handle> getAllowedHandles();
    void removeAllowedHandle(MegaHandle handle);

    void readData(MegaTCPContext* tcpctx);
};


class MegaTCServer;
class MegaHTTPServer;
class MegaHTTPContext : public MegaTCPContext
{

public:
    MegaHTTPContext();
    ~MegaHTTPContext();

    // Connection management
    StreamingBuffer streamingBuffer;
    std::unique_ptr<MegaTransferPrivate> transfer;
    http_parser parser;
    char *lastBuffer;
    size_t lastBufferLen;
    bool nodereceived;
    bool failed;
    bool pause;

    // Request information
    bool range;
    m_off_t rangeStart;
    m_off_t rangeEnd;
    m_off_t rangeWritten;
    MegaNode *node;
    std::string path;
    std::string nodehandle;
    std::string nodekey;
    std::string nodename;
    m_off_t nodesize;
    std::string nodepubauth;
    std::string nodeprivauth;
    std::string nodechatauth;
    int resultCode;


    // WEBDAV related
    int depth;
    std::string lastheader;
    std::string subpathrelative;
    const char *messageBody;
    size_t messageBodySize;
    std::string host;
    std::string destination;
    bool overwrite;
    std::unique_ptr<FileAccess> tmpFileAccess;
    std::string tmpFileName;
    std::string newname; //newname for moved node
    MegaHandle nodeToMove; //node to be moved after delete
    MegaHandle newParentNode; //parent node for moved after delete

    uv_mutex_t mutex_responses;
    std::list<std::string> responses;

    virtual void onTransferStart(MegaApi *, MegaTransfer *transfer);
    virtual bool onTransferData(MegaApi *, MegaTransfer *transfer, char *buffer, size_t size);
    virtual void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError *e);
    virtual void onRequestFinish(MegaApi* api, MegaRequest *request, MegaError *e);
};

class MegaHTTPServer: public MegaTCPServer
{
protected:
    set<handle> allowedWebDavHandles;

    bool fileServerEnabled;
    bool folderServerEnabled;
    bool offlineAttribute;
    bool subtitlesSupportEnabled;

    //virtual methods:
    virtual void processReceivedData(MegaTCPContext *ftpctx, ssize_t nread, const uv_buf_t * buf);
    virtual void processAsyncEvent(MegaTCPContext *ftpctx);
    virtual MegaTCPContext * initializeContext(uv_stream_t *server_handle);
    virtual void processWriteFinished(MegaTCPContext* tcpctx, int status);
    virtual void processOnAsyncEventClose(MegaTCPContext* tcpctx);
    virtual bool respondNewConnection(MegaTCPContext* tcpctx);
    virtual void processOnExitHandleClose(MegaTCPServer* tcpServer);


    // HTTP parser callback
    static int onMessageBegin(http_parser* parser);
    static int onHeadersComplete(http_parser* parser);
    static int onUrlReceived(http_parser* parser, const char* url, size_t length);
    static int onHeaderField(http_parser* parser, const char* at, size_t length);
    static int onHeaderValue(http_parser* parser, const char* at, size_t length);
    static int onBody(http_parser* parser, const char* at, size_t length);
    static int onMessageComplete(http_parser* parser);

    static void sendHeaders(MegaHTTPContext *httpctx, string *headers);
    static void sendNextBytes(MegaHTTPContext *httpctx);
    static int streamNode(MegaHTTPContext *httpctx);

    //Utility funcitons
    static std::string getHTTPMethodName(int httpmethod);
    static std::string getHTTPErrorString(int errorcode);
    static std::string getResponseForNode(MegaNode *node, MegaHTTPContext* httpctx);

    // WEBDAV related
    static std::string getWebDavPropFindResponseForNode(std::string baseURL, std::string subnodepath, MegaNode *node, MegaHTTPContext* httpctx);
    static std::string getWebDavProfFindNodeContents(MegaNode *node, std::string baseURL, bool offlineAttribute);

    static void returnHttpCodeBasedOnRequestError(MegaHTTPContext* httpctx, MegaError *e, bool synchronous = true);
    static void returnHttpCode(MegaHTTPContext* httpctx, int errorCode, std::string errorMessage = string(), bool synchronous = true);

public:

    static void returnHttpCodeAsyncBasedOnRequestError(MegaHTTPContext* httpctx, MegaError *e);
    static void returnHttpCodeAsync(MegaHTTPContext* httpctx, int errorCode, std::string errorMessage = string());

    MegaHTTPServer(MegaApiImpl *megaApi, string basePath, bool useTLS = false, std::string certificatepath = std::string(), std::string keypath = std::string(), bool useIPv6 = false);
    virtual ~MegaHTTPServer();
    char *getWebDavLink(MegaNode *node);

    void clearAllowedHandles();
    bool isHandleWebDavAllowed(handle h);
    set<handle> getAllowedWebDavHandles();
    void removeAllowedWebDavHandle(MegaHandle handle);
    void enableFileServer(bool enable);
    void enableFolderServer(bool enable);
    bool isFileServerEnabled();
    bool isFolderServerEnabled();
    void enableOfflineAttribute(bool enable);
    bool isOfflineAttributeEnabled();
    bool isSubtitlesSupportEnabled();
    void enableSubtitlesSupport(bool enable);

};

class MegaFTPServer;
class MegaFTPDataServer;
class MegaFTPContext : public MegaTCPContext
{
public:

    int command;
    std::string arg1;
    std::string arg2;
    int resultcode;
    int pasiveport;
    MegaFTPDataServer * ftpDataServer;

    std::string tmpFileName;

    MegaNode *nodeToDeleteAfterMove;

    uv_mutex_t mutex_responses;
    std::list<std::string> responses;

    uv_mutex_t mutex_nodeToDownload;

    //status
    MegaHandle cwd;
    bool atroot;
    bool athandle;
    MegaHandle parentcwd;

    std::string cwdpath;

    MegaFTPContext();
    ~MegaFTPContext();

    virtual void onTransferStart(MegaApi *, MegaTransfer *transfer);
    virtual bool onTransferData(MegaApi *, MegaTransfer *transfer, char *buffer, size_t size);
    virtual void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError *e);
    virtual void onRequestFinish(MegaApi* api, MegaRequest *request, MegaError *e);
};

class MegaFTPDataServer;
class MegaFTPServer: public MegaTCPServer
{
protected:
    enum{
        FTP_CMD_INVALID = -1,
        FTP_CMD_USER = 1,
        FTP_CMD_PASS,
        FTP_CMD_ACCT,
        FTP_CMD_CWD,
        FTP_CMD_CDUP,
        FTP_CMD_SMNT,
        FTP_CMD_QUIT,
        FTP_CMD_REIN,
        FTP_CMD_PORT,
        FTP_CMD_PASV,
        FTP_CMD_TYPE,
        FTP_CMD_STRU,
        FTP_CMD_MODE,
        FTP_CMD_RETR,
        FTP_CMD_STOR,
        FTP_CMD_STOU,
        FTP_CMD_APPE,
        FTP_CMD_ALLO,
        FTP_CMD_REST,
        FTP_CMD_RNFR,
        FTP_CMD_RNTO,
        FTP_CMD_ABOR,
        FTP_CMD_DELE,
        FTP_CMD_RMD,
        FTP_CMD_MKD,
        FTP_CMD_PWD,
        FTP_CMD_LIST,
        FTP_CMD_NLST,
        FTP_CMD_SITE,
        FTP_CMD_SYST,
        FTP_CMD_STAT,
        FTP_CMD_HELP,
        FTP_CMD_FEAT,  //rfc2389
        FTP_CMD_SIZE,
        FTP_CMD_PROT,
        FTP_CMD_EPSV, //rfc2428
        FTP_CMD_PBSZ, //rfc2228
        FTP_CMD_OPTS, //rfc2389
        FTP_CMD_NOOP
    };

    std::string crlfout;

    MegaHandle nodeHandleToRename;

    int pport;
    int dataportBegin;
    int dataPortEnd;

    std::string getListingLineFromNode(MegaNode *child, std::string nameToShow = string());

    MegaNode *getBaseFolderNode(std::string path);
    MegaNode *getNodeByFullFtpPath(std::string path);
    void getPermissionsString(int permissions, char *permsString);


    //virtual methods:
    virtual void processReceivedData(MegaTCPContext *tcpctx, ssize_t nread, const uv_buf_t * buf);
    virtual void processAsyncEvent(MegaTCPContext *tcpctx);
    virtual MegaTCPContext * initializeContext(uv_stream_t *server_handle);
    virtual void processWriteFinished(MegaTCPContext* tcpctx, int status);
    virtual void processOnAsyncEventClose(MegaTCPContext* tcpctx);
    virtual bool respondNewConnection(MegaTCPContext* tcpctx);
    virtual void processOnExitHandleClose(MegaTCPServer* tcpServer);

public:

    std::string newNameAfterMove;

    MegaFTPServer(MegaApiImpl *megaApi, string basePath, int dataportBegin, int dataPortEnd, bool useTLS = false, std::string certificatepath = std::string(), std::string keypath = std::string());
    virtual ~MegaFTPServer();

    static std::string getFTPErrorString(int errorcode, std::string argument = string());

    static void returnFtpCodeBasedOnRequestError(MegaFTPContext* ftpctx, MegaError *e);
    static void returnFtpCode(MegaFTPContext* ftpctx, int errorCode, std::string errorMessage = string());

    static void returnFtpCodeAsyncBasedOnRequestError(MegaFTPContext* ftpctx, MegaError *e);
    static void returnFtpCodeAsync(MegaFTPContext* ftpctx, int errorCode, std::string errorMessage = string());
    MegaNode * getNodeByFtpPath(MegaFTPContext* ftpctx, std::string path);
    std::string cdup(handle parentHandle, MegaFTPContext* ftpctx);
    std::string cd(string newpath, MegaFTPContext* ftpctx);
    std::string shortenpath(std::string path);
};

class MegaFTPDataContext;
class MegaFTPDataServer: public MegaTCPServer
{
protected:

    //virtual methods:
    virtual void processReceivedData(MegaTCPContext *tcpctx, ssize_t nread, const uv_buf_t * buf);
    virtual void processAsyncEvent(MegaTCPContext *tcpctx);
    virtual MegaTCPContext * initializeContext(uv_stream_t *server_handle);
    virtual void processWriteFinished(MegaTCPContext* tcpctx, int status);
    virtual void processOnAsyncEventClose(MegaTCPContext* tcpctx);
    virtual bool respondNewConnection(MegaTCPContext* tcpctx);
    virtual void processOnExitHandleClose(MegaTCPServer* tcpServer);

    void sendNextBytes(MegaFTPDataContext *ftpdatactx);


public:
    MegaFTPContext *controlftpctx;

    std::string resultmsj;
    MegaNode *nodeToDownload;
    std::string remotePathToUpload;
    std::string newNameToUpload;
    MegaHandle newParentNodeHandle;
    m_off_t rangeStartREST;
    void sendData();
    bool notifyNewConnectionRequired;

    MegaFTPDataServer(MegaApiImpl *megaApi, string basePath, MegaFTPContext * controlftpctx, bool useTLS = false, std::string certificatepath = std::string(), std::string keypath = std::string());
    virtual ~MegaFTPDataServer();
    string getListingLineFromNode(MegaNode *child);
};

class MegaFTPDataServer;
class MegaFTPDataContext : public MegaTCPContext
{
public:

    MegaFTPDataContext();
    ~MegaFTPDataContext();

    void setControlCodeUponDataClose(int code, std::string msg = string());

    // Connection management
    StreamingBuffer streamingBuffer;
    MegaTransferPrivate *transfer;
    char *lastBuffer;
    size_t lastBufferLen;
    bool failed;
    int ecode;
    bool pause;
    MegaNode *node;

    m_off_t rangeStart;
    m_off_t rangeWritten;

    std::string tmpFileName;
    std::unique_ptr<FileAccess> tmpFileAccess;
    size_t tmpFileSize;

    bool controlRespondedElsewhere;
    string controlResponseMessage;
    int controlResponseCode;

    virtual void onTransferStart(MegaApi *, MegaTransfer *transfer);
    virtual bool onTransferData(MegaApi *, MegaTransfer *transfer, char *buffer, size_t size);
    virtual void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError *e);
    virtual void onRequestFinish(MegaApi* api, MegaRequest *request, MegaError *e);
};
#endif

#ifdef ENABLE_CHAT
class MegaScheduledFlagsPrivate: public MegaScheduledFlags
{
public:
    MegaScheduledFlagsPrivate();
    MegaScheduledFlagsPrivate(const unsigned long numericValue);
    MegaScheduledFlagsPrivate(const MegaScheduledFlagsPrivate* flags);
    MegaScheduledFlagsPrivate(const ScheduledFlags* flags);
    ~MegaScheduledFlagsPrivate() override = default;
    MegaScheduledFlagsPrivate(const MegaScheduledFlagsPrivate&) = delete;
    MegaScheduledFlagsPrivate(const MegaScheduledFlagsPrivate&&) = delete;
    MegaScheduledFlagsPrivate& operator=(const MegaScheduledFlagsPrivate&) = delete;
    MegaScheduledFlagsPrivate& operator=(const MegaScheduledFlagsPrivate&&) = delete;

    void reset() override;
    void setSendEmails(bool enabled);
    void importFlagsValue(unsigned long val) override;

    bool sendEmails() const;
    unsigned long getNumericValue() const override;

    MegaScheduledFlagsPrivate* copy() const override { return new MegaScheduledFlagsPrivate(this); }
    bool isEmpty() const override;
    unique_ptr<ScheduledFlags> getSdkScheduledFlags() const;

private:
    unique_ptr<ScheduledFlags> mScheduledFlags;
};

class MegaScheduledRulesPrivate : public MegaScheduledRules
{
public:
    MegaScheduledRulesPrivate(const int freq,
                              const int interval = INTERVAL_INVALID,
                              const MegaTimeStamp until = MEGA_INVALID_TIMESTAMP,
                              const MegaIntegerList* byWeekDay = nullptr,
                              const MegaIntegerList* byMonthDay = nullptr,
                              const MegaIntegerMap* byMonthWeekDay = nullptr);

    MegaScheduledRulesPrivate(const MegaScheduledRulesPrivate* rules);
    MegaScheduledRulesPrivate(const ScheduledRules* rules);
    ~MegaScheduledRulesPrivate() override = default;
    MegaScheduledRulesPrivate(const MegaScheduledRulesPrivate&) = delete;
    MegaScheduledRulesPrivate(const MegaScheduledRulesPrivate&&) = delete;
    MegaScheduledRulesPrivate& operator=(const MegaScheduledRulesPrivate&) = delete;
    MegaScheduledRulesPrivate& operator=(const MegaScheduledRulesPrivate&&) = delete;

    int freq() const override;
    int interval() const override;
    MegaTimeStamp until() const override;
    const mega::MegaIntegerList* byWeekDay() const override;
    const mega::MegaIntegerList* byMonthDay() const override;
    const mega::MegaIntegerMap* byMonthWeekDay() const override;

    MegaScheduledRulesPrivate* copy() const override { return new MegaScheduledRulesPrivate(this); }
    unique_ptr<ScheduledRules> getSdkScheduledRules() const;
    static bool isValidFreq(const int freq);
    static bool isValidInterval(const int interval);
    static bool isValidUntil(const m_time_t until);

private:
    unique_ptr<ScheduledRules> mScheduledRules;
    // temp memory must be held somewhere since there is a data transformation and ownership is not returned in the getters
    // (probably removed after checking MegaAPI redesign)
    mutable std::unique_ptr<mega::MegaIntegerList> mTransformedByWeekDay;
    mutable std::unique_ptr<mega::MegaIntegerList> mTransformedByMonthDay;
    mutable std::unique_ptr<mega::MegaIntegerMap> mTransformedByMonthWeekDay;
};

class MegaScheduledMeetingPrivate: public MegaScheduledMeeting
{
public:
    MegaScheduledMeetingPrivate(const MegaHandle chatid,
                                const char* timezone,
                                const MegaTimeStamp startDateTime,
                                const MegaTimeStamp endDateTime,
                                const char* title,
                                const char* description,
                                const MegaHandle schedId = INVALID_HANDLE,
                                const MegaHandle parentSchedId = INVALID_HANDLE,
                                const MegaHandle organizerUserId = INVALID_HANDLE,
                                const int cancelled = -1,
                                const char* attributes = nullptr,
                                const MegaTimeStamp overrides = MEGA_INVALID_TIMESTAMP,
                                const MegaScheduledFlags* flags = nullptr,
                                const MegaScheduledRules* rules = nullptr);

    MegaScheduledMeetingPrivate(const MegaScheduledMeetingPrivate *scheduledMeeting);
    MegaScheduledMeetingPrivate(const ScheduledMeeting* scheduledMeeting);
    ~MegaScheduledMeetingPrivate() override = default;
    MegaScheduledMeetingPrivate(const MegaScheduledMeetingPrivate&) = delete;
    MegaScheduledMeetingPrivate(const MegaScheduledMeetingPrivate&&) = delete;
    MegaScheduledMeetingPrivate& operator=(const MegaScheduledMeetingPrivate&) = delete;
    MegaScheduledMeetingPrivate& operator=(const MegaScheduledMeetingPrivate&&) = delete;

    MegaHandle chatid() const override;
    MegaHandle schedId() const override;
    MegaHandle parentSchedId() const override;
    MegaHandle organizerUserid() const override;
    const char* timezone() const override;
    MegaTimeStamp startDateTime() const override;
    MegaTimeStamp endDateTime() const override;
    const char* title() const override;
    const char* description() const override;
    const char* attributes() const override;
    MegaTimeStamp overrides() const override;
    int cancelled() const override;
    MegaScheduledFlags* flags() const override; // ownership returned
    MegaScheduledRules* rules() const override; // ownership returned

    MegaScheduledMeetingPrivate* copy() const override { return new MegaScheduledMeetingPrivate(this); }
    const ScheduledMeeting* scheduledMeeting() const   { return mScheduledMeeting.get(); }

private:
    unique_ptr<ScheduledMeeting> mScheduledMeeting;
};

class MegaScheduledMeetingListPrivate: public MegaScheduledMeetingList
{
public:
    MegaScheduledMeetingListPrivate();
    MegaScheduledMeetingListPrivate(const MegaScheduledMeetingListPrivate &);
    ~MegaScheduledMeetingListPrivate();

    MegaScheduledMeetingListPrivate *copy() const override;

    // getters
    unsigned long size() const override;
    MegaScheduledMeeting* at(unsigned long i) const override;

    // returns the first MegaScheduledMeeting, whose schedId matches with h
    // note that schedId is globally unique for all chats (in case of scheduled meetings), but this class
    // can be used to store scheduled meetings occurrences (it can contains multiple items with the same schedId)
    MegaScheduledMeeting* getBySchedId(MegaHandle h) const override;

    // setters
    void insert(MegaScheduledMeeting *sm) override;
    void clear() override;

private:
    std::vector<std::unique_ptr<MegaScheduledMeeting>> mList;
};
#endif

class MegaVpnCredentialsPrivate : public MegaVpnCredentials
{
public:
    using MapSlotIDToCredentialInfo = CommandGetVpnCredentials::MapSlotIDToCredentialInfo;
    using MapClusterPublicKeys = CommandGetVpnCredentials::MapClusterPublicKeys;

    MegaVpnCredentialsPrivate(MapSlotIDToCredentialInfo&&, MapClusterPublicKeys&&, MegaStringList*);
    MegaVpnCredentialsPrivate(const MegaVpnCredentialsPrivate&);
    ~MegaVpnCredentialsPrivate();

    MegaIntegerList* getSlotIDs() const override;
    MegaStringList* getVpnRegions() const override;
    const char* getIPv4(int slotID) const override;
    const char* getIPv6(int slotID) const override;
    const char* getDeviceID(int slotID) const override;
    int getClusterID(int slotID) const override;
    const char* getClusterPublicKey(int clusterID) const override;
    MegaVpnCredentials* copy() const override;

private:
    MapSlotIDToCredentialInfo mMapSlotIDToCredentialInfo;
    MapClusterPublicKeys mMapClusterPubKeys;
    std::unique_ptr<MegaStringList> mVpnRegions;
};

class MegaNodeTreePrivate: public MegaNodeTree
{
public:
    MegaNodeTreePrivate(const MegaNodeTree* nodeTreeChild,
                        const std::string& name,
                        const std::string& s4AttributeValue,
                        const MegaCompleteUploadData* completeUploadData,
                        MegaHandle sourceHandle,
                        MegaHandle nodeHandle);
    ~MegaNodeTreePrivate() override = default;
    MegaNodeTree* getNodeTreeChild() const override;
    const std::string& getName() const;
    const std::string& getS4AttributeValue() const;
    const MegaCompleteUploadData* getCompleteUploadData() const;
    MegaHandle getNodeHandle() const override;
    void setNodeHandle(const MegaHandle& nodeHandle);
    const MegaHandle& getSourceHandle() const { return mSourceHandle; }
    MegaNodeTree* copy() const override;

private:
    std::unique_ptr<MegaNodeTree> mNodeTreeChild;
    std::string mName;
    std::string mS4AttributeValue;
    // new leaf-file-node is created from upload-token or as a 
    // copy of an existing node (cannot use both at the same time)

    // data to create node from upload-token
    std::unique_ptr<const MegaCompleteUploadData> mCompleteUploadData;
    // handle of an existing file node to be copied
    MegaHandle mSourceHandle;

    // output param: handle give to new node
    MegaHandle mNodeHandle;
};

class MegaCompleteUploadDataPrivate: public MegaCompleteUploadData
{
public:
    MegaCompleteUploadDataPrivate(const std::string& fingerprint,
                                  const std::string& string64UploadToken,
                                  const std::string& string64FileKey);
    ~MegaCompleteUploadDataPrivate() override = default;
    const std::string& getFingerprint() const;
    const std::string& getString64UploadToken() const;
    const std::string& getString64FileKey() const;
    MegaCompleteUploadData* copy() const override;

private:
    std::string mFingerprint;
    std::string mString64UploadToken;
    std::string mString64FileKey;
};

class MegaNotificationPrivate : public MegaNotification
{
public:
    MegaNotificationPrivate(DynamicMessageNotification&& n) :
        mNotification{std::move(n)}, mCall1{&mNotification.callToAction1}, mCall2{&mNotification.callToAction2} {}
    MegaNotificationPrivate(const DynamicMessageNotification& n) :
        mNotification{n}, mCall1{&mNotification.callToAction1}, mCall2{&mNotification.callToAction2} {}

    int64_t getID() const override { return mNotification.id; }
    const char* getTitle() const override { return mNotification.title.c_str(); }
    const char* getDescription() const override { return mNotification.description.c_str(); }
    const char* getImageName() const override { return mNotification.imageName.c_str(); }
    const char* getIconName() const override { return mNotification.iconName.c_str(); }
    const char* getImagePath() const override { return mNotification.imagePath.c_str(); }
    int64_t getStart() const override { return mNotification.start; }
    int64_t getEnd() const override { return mNotification.end; }
    bool showBanner() const override { return mNotification.showBanner; }
    const MegaStringMap* getCallToAction1() const override { return &mCall1; }
    const MegaStringMap* getCallToAction2() const override { return &mCall2; }
    MegaNotificationPrivate* copy() const override { return new MegaNotificationPrivate(*this); }

private:
    const DynamicMessageNotification mNotification;
    const MegaStringMapPrivate mCall1;
    const MegaStringMapPrivate mCall2;
};

class MegaNotificationListPrivate : public MegaNotificationList
{
public:
    MegaNotificationListPrivate(std::vector<DynamicMessageNotification>&& ns)
    {
        mNotifications.reserve(ns.size());
        for (const auto& n : ns)
        {
            mNotifications.emplace_back(std::move(n));
        }
    }

    MegaNotificationListPrivate* copy() const override { return new MegaNotificationListPrivate(*this); }

    const MegaNotification* get(unsigned i) const override { return i < size() ? &mNotifications[i] : nullptr; }
    unsigned size() const override { return static_cast<unsigned>(mNotifications.size()); }

private:
    vector<MegaNotificationPrivate> mNotifications;
};

class MegaFuseExecutorFlagsPrivate
  : public MegaFuseExecutorFlags
{
    fuse::TaskExecutorFlags& mFlags;

public:
    MegaFuseExecutorFlagsPrivate(fuse::TaskExecutorFlags& flags);

    size_t getMinThreadCount() const override;

    size_t getMaxThreadCount() const override;

    size_t getMaxThreadIdleTime() const override;

    bool setMaxThreadCount(size_t max) override;

    void setMinThreadCount(size_t min) override;

    void setMaxThreadIdleTime(size_t max) override;
}; // MegaFuseExecutorFlagsPrivate

class MegaFuseInodeCacheFlagsPrivate
  : public MegaFuseInodeCacheFlags
{
    fuse::InodeCacheFlags& mFlags;

public:
    explicit MegaFuseInodeCacheFlagsPrivate(fuse::InodeCacheFlags& flags);

    size_t getCleanAgeThreshold() const override;

    size_t getCleanInterval() const override;

    size_t getCleanSizeThreshold() const override;

    size_t getMaxSize() const override;

    void setCleanAgeThreshold(std::size_t seconds) override;

    void setCleanInterval(std::size_t seconds) override;

    void setCleanSizeThreshold(std::size_t size) override;

    void setMaxSize(std::size_t size) override;
}; // MegaFuseExecutorFlagsPrivate

class MegaFuseFlagsPrivate
  : public MegaFuseFlags
{
    fuse::ServiceFlags mFlags;
    MegaFuseInodeCacheFlagsPrivate mInodeCacheFlags;
    MegaFuseExecutorFlagsPrivate mMountExecutorFlags;
    MegaFuseExecutorFlagsPrivate mSubsystemExecutorFlags;

public:
    MegaFuseFlagsPrivate(const fuse::ServiceFlags& flags);

    MegaFuseFlags* copy() const override;

    const fuse::ServiceFlags& getFlags() const;

    size_t getFlushDelay() const override;

    int getLogLevel() const override;

    MegaFuseInodeCacheFlags* getInodeCacheFlags() override;

    MegaFuseExecutorFlags* getMountExecutorFlags() override;

    MegaFuseExecutorFlags* getSubsystemExecutorFlags() override;

    void setFlushDelay(size_t seconds) override;

    void setLogLevel(int level) override;
}; // MegaFuseFlagsPrivate

using MegaMountFlagsPtr = std::unique_ptr<MegaMountFlags>;
using MegaMountPtr = std::unique_ptr<MegaMount>;
using MegaMountPtrVector = std::vector<MegaMountPtr>;

class MegaMountPrivate
  : public MegaMount
{
    MegaMountFlagsPtr mFlags;
    MegaHandle mHandle;
    std::string mPath;

public:
    MegaMountPrivate();

    MegaMountPrivate(const fuse::MountInfo& info);

    MegaMountPrivate(const MegaMountPrivate& other);

    fuse::MountInfo asInfo() const;

    MegaMount* copy() const override;

    MegaMountFlags* getFlags() const override;

    MegaHandle getHandle() const override;

    const char* getPath() const override;

    void setFlags(const MegaMountFlags* flags) override;

    void setHandle(MegaHandle handle) override;
    
    void setPath(const char* path) override;
}; // MegaMountPrivate

class MegaMountFlagsPrivate
  : public MegaMountFlags
{
    fuse::MountFlags mFlags;

public:
    MegaMountFlagsPrivate() = default;

    MegaMountFlagsPrivate(const fuse::MountFlags& flags);

    MegaMountFlagsPrivate(const MegaMountFlagsPrivate& other) = default;

    MegaMountFlags* copy() const override;

    bool getEnableAtStartup() const override;

    const fuse::MountFlags& getFlags() const;

    const char* getName() const override;

    bool getPersistent() const override;

    bool getReadOnly() const override;

    void setEnableAtStartup(bool enable) override;

    void setName(const char* name) override;

    void setPersistent(bool persistent) override;

    void setReadOnly(bool readOnly) override;

}; // MegaMountFlagsPrivate

class MegaMountListPrivate
  : public MegaMountList
{
    MegaMountPtrVector mMounts;

public:
    MegaMountListPrivate(fuse::MountInfoVector&& mounts);

    MegaMountListPrivate(const MegaMountListPrivate& other);

    MegaMountList* copy() const override;

    const MegaMount* get(size_t index) const override;

    size_t size() const override;
}; // MegaMountListPrivate

}

#endif //MEGAAPI_IMPL_H
