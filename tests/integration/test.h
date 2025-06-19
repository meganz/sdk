
#ifndef TEST_H
#define TEST_H 1

#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <functional>
#include <stdio.h>
#include <map>
#include <future>
#include <fstream>
#include <atomic>
#include <random>

#include "gtest/gtest.h"

#include <mega.h>
#include <megaapi_impl.h>
#include "../stdfs.h"

using namespace ::mega;
using namespace ::std;

std::string logTime();
void WaitMillisec(unsigned n);

enum class PROG_OUTPUT_TYPE
{
    TEXT,   // skip \n and concatenate lines; uses fgets()
    BINARY  // read everything just as it was received; uses fread()
};

string runProgram(const string& command, PROG_OUTPUT_TYPE ot);

// platform specific Http POST
void synchronousHttpPOSTFile(const string& url, const string& filepath, string& responsedata);
void synchronousHttpPOSTData(const string& url, const string& senddata, string& responsedata);

class LogStream
{
public:
    LogStream()
      : mBuffer()
    {
    }

    LogStream(LogStream&& other) noexcept
      : mBuffer(std::move(other.mBuffer))
    {
    }

    ~LogStream();

    template<typename T>
    LogStream& operator<<(const T* value)
    {
        mBuffer << value;
        return *this;
    }

    template<typename T, typename = typename std::enable_if<std::is_scalar<T>::value>::type>
    LogStream& operator<<(const T value)
    {
        mBuffer << value;
        return *this;
    }

    template<typename T, typename = typename std::enable_if<!std::is_scalar<T>::value>::type>
    LogStream& operator<<(const T& value)
    {
        mBuffer << value;
        return *this;
    }

private:
    std::ostringstream mBuffer;
}; // LogStream

extern std::string USER_AGENT;
extern bool gResumeSessions;
extern bool gScanOnly;
extern bool gManualVerification;
extern bool gFreeAccounts;

LogStream out();

enum { THREADS_PER_MEGACLIENT = 3 };

class TestFS
{
public:
    // these getters should return std::filesystem::path type, when C++17 will become mandatory
    
    // $WORKSPACE or hard coded path
    // ie. /home/<user>/mega_tests
    static fs::path GetBaseFolder();

    // PID specific directory
    static fs::path GetProcessFolder();

    // directory for "test" within the process folder, often created and deleted per test
    static fs::path GetTestFolder();
    static fs::path GetTrashFolder();

    void DeleteTestFolder() { DeleteFolder(GetTestFolder()); }
    void DeleteTrashFolder() { DeleteFolder(GetTrashFolder()); }
    static void ChangeToProcessFolder();
    static void ClearProcessFolder();

    ~TestFS();

private:
    void DeleteFolder(fs::path folder);

    std::vector<std::thread> m_cleaners;
};

void moveToTrash(const fs::path& p);
fs::path makeNewTestRoot();

std::unique_ptr<::mega::FileSystemAccess> makeFsAccess();
fs::path makeReusableClientFolder(const string& subfolder);

class RequestRetryRecorder
{
    // Convenience.
    using Milliseconds = std::chrono::milliseconds;

    // Describes a particular class of retry.
    struct RetryEntry
    {
        // How many times did this class of retry occur?
        std::size_t mCount = 0;

        // What was the longest time we spent performing this retry?
        Milliseconds mLongest = Milliseconds::min();

        // And the shortest time?
        Milliseconds mShortest = Milliseconds::max();
    }; // Entry

    // Maps retry class to retry entry.
    using RetryEntryMap = std::map<retryreason_t, RetryEntry>;

    // Translates a retry entry into a human-readable string.
    std::string report(const RetryEntryMap::value_type& entry) const
    {
        std::ostringstream ostream;

        ostream << "Requests retried due to "
                << toString(entry.first)
                << " "
                << entry.second.mCount
                << " time(s) [duration "
                << entry.second.mShortest.count()
                << "ms-"
                << entry.second.mLongest.count()
                << "ms]";

        return ostream.str();
    }

    // Tracks statistics about a specific retry class.
    RetryEntryMap mEntries;

    // Serializes access to mEnties.
    mutable std::mutex mEntriesLock;

    // Who's the current recorder?
    static RequestRetryRecorder* mInstance;

public:
    RequestRetryRecorder()
      : mEntries()
      , mEntriesLock()
    {
        // Only one instance should ever exist at a time.
        assert(!mInstance);

        mInstance = this;
    }

    RequestRetryRecorder(const RequestRetryRecorder&) = delete;

    ~RequestRetryRecorder()
    {
        assert(mInstance == this);

        mInstance = nullptr;
    }

    RequestRetryRecorder& operator=(const RequestRetryRecorder&) = delete;

    // Obtain a reference to the current recorder.
    static RequestRetryRecorder& instance()
    {
        assert(mInstance);

        return *mInstance;
    }

    // Record a retry period.
    void record(retryreason_t reason, Milliseconds duration)
    {
        // Acquire lock.
        std::lock_guard<std::mutex> guard(mEntriesLock);

        // Get our hands on the specified entry.
        auto& entry = mEntries[reason];

        // Populate entry.
        entry.mCount = entry.mCount + 1;
        entry.mLongest = std::max(entry.mLongest, duration);
        entry.mShortest = std::min(entry.mShortest, duration);
    }

    // Transform recorded retry entries to a human-readable string.
    template<typename Printer>
    void report(Printer&& printer) const
    {
        // Acquire lock.
        std::lock_guard<std::mutex> guard(mEntriesLock);

        // Print entries.
        for (auto& i : mEntries)
            printer(report(i));
    }

    void reset()
    {
        // Acquire lock.
        std::lock_guard<std::mutex> guard(mEntriesLock);

        // Clear recorded request retries.
        mEntries.clear();
    }
}; // RequestRetryRecorder

class RequestRetryTracker
{
    // Convenience.
    using HRClock = std::chrono::high_resolution_clock;
    using HRTimePoint = HRClock::time_point;

    // Why did our request need to be retried?
    retryreason_t mReason = RETRY_NONE;

    // When were we notified that the request was retried?
    HRTimePoint mWhen = HRTimePoint::max();

public:
    // Signal that a request is being retried.
    void track(const std::string& clientName, retryreason_t reason)
    {
        // Coalesce contiguous retries of the same class.
        if (mReason == reason)
            return;

        // Convenience.
        auto now = HRClock::now();

        // We were already tracking an existing retry.
        if (mReason != RETRY_NONE)
        {
            // Convenience.
            using std::chrono::duration_cast;
            using std::chrono::milliseconds;

            // How long did it take until our request succeeded?
            auto elapsed = duration_cast<milliseconds>(now - mWhen);

            // Log how long the request took.
            out() << clientName
                  << ": request retry completed: reason: "
                  << toString(mReason)
                  << ", duration: "
                  << elapsed.count()
                  << "ms";

            // Record statistics about the retry.
            RequestRetryRecorder::instance().record(mReason, elapsed);
        }

        // Latch new reason and timestamp.
        mReason = reason;
        mWhen = now;

        // No request is being retried.
        if (mReason == RETRY_NONE)
            return;

        out() << clientName
              << ": request retry begun: reason: "
              << toString(mReason);
    }
}; // RequestRetryTracker

#ifdef ENABLE_SYNC

template<typename T>
using shared_promise = std::shared_ptr<promise<T>>;

using PromiseBoolSP     = shared_promise<bool>;
using PromiseErrorSP    = shared_promise<Error>;
using PromiseHandleSP   = shared_promise<handle>;
using PromiseStringSP   = shared_promise<string>;
using PromiseUnsignedSP = shared_promise<unsigned>;
using PromiseVoidSP     = shared_promise<void>;

struct Model
{
    // records what we think the tree should look like after sync so we can confirm it

    struct ModelNode
    {
        enum nodetype { file, folder };
        nodetype type = folder;
        string mCloudName;
        string mFsName;
        string name;
        string content;
        vector<unique_ptr<ModelNode>> kids;
        ModelNode* parent = nullptr;
        bool changed = false;
        bool fsOnly = false;

        ModelNode() = default;

        ModelNode(const ModelNode& other);
        ModelNode& fsName(const string& name);
        const string& fsName() const;
        ModelNode& cloudName(const string& name);
        const string& cloudName() const;
        void generate(const fs::path& path, bool force);
        string path() const;
        string fsPath() const;
        ModelNode* addkid();
        ModelNode* addkid(unique_ptr<ModelNode>&& p);
        bool typematchesnodetype(nodetype_t nodetype) const;
        void print(string prefix="");
        std::unique_ptr<ModelNode> clone();
    };

    Model();
    Model(const Model& other);
    Model& operator=(const Model& rhs);
    ModelNode* addfile(const string& path, const string& content);
    ModelNode* addfile(const string& path);
    ModelNode* addfolder(const string& path);
    ModelNode* addnode(const string& path, ModelNode::nodetype type);
    ModelNode* copynode(const string& src, const string& dst);
    /**
     * @brief Create a folder-type ModelNode with the specified name.
     *
     * @param utf8Name The name of the folder to be created, in UTF-8 encoding
     * @return The created folder ModelNode
     */
    unique_ptr<ModelNode> makeModelSubfolder(const string& utf8Name);
    /**
     * @brief Create a file type ModelNode with the specified name.
     * If content parameter is empty, the content is the specified name,
     * otherwise it is content itself.
     *
     * @param utf8Name The name of the folder to be created, in UTF-8 encoding.
     * @param content  The content of the file. If it is empty, the content is file name utf8Name.
     *                 Default value is empty.
     * @return The created file ModelNode
     */
    unique_ptr<ModelNode> makeModelSubfile(const string& utf8Name, string content = {});
    /**
     * @brief Create a folder tree whose depth is with a specified depth and its root folder is named prefix.
     *
     * The structure is as follows:
     * - Each folder, except for the leaf folders, has `n` child folders. Child folders are named after
     * their parent name with the format <parentName>_<index>, starting with index 0.
     * - Each folder has `filesperdir` number of child files. Child files are named after their parent name
     * with the format file<index>_<parentName>.
     *
     * Examples:
     *   buildModelSubdirs(f, 2, 2, 1)
     *   root -- level 1 -- level 2
     *   f    -- file0_f
     *        -- f_0     -- file0_f_0
     *                   -- f_0_0      -- file0_f_0_0
     *                   -- f_0_1      -- file0_f_0_1
     *        -- f_1     -- file0_f_1
     *                   -- f_1_0      -- file0_f_1_0
     *                   -- f_1_1      -- file0_f_1_1
     *
     * @param prefix       The name of the root folder
     * @param n            The number of child folders under each folder except the leaf folder.
     * @param recurselevel The depth of folder tree. For example, 0 means only root folder.
     *                     1 means one level folder under the root.
     * @param filesperdir  The number of files under each folder
     * @return the root folder ModelNode
     */
    unique_ptr<ModelNode> buildModelSubdirs(const string& prefix, int n, int recurselevel, int filesperdir);
    ModelNode* childnodebyname(ModelNode* n, const std::string& s);
    ModelNode* findnode(string path, ModelNode* startnode = nullptr);
    unique_ptr<ModelNode> removenode(const string& path);
    bool movenode(const string& sourcepath, const string& destpath);
    bool movetosynctrash(unique_ptr<ModelNode>&& node, const string& syncrootpath);
    bool movetosynctrash(const string& path, const string& syncrootpath);
    void ensureLocalDebrisTmpLock(const string& syncrootpath);
    bool removesynctrash(const string& syncrootpath, const string& subpath = "");
    void emulate_rename(std::string nodepath, std::string newname);
    void emulate_move(std::string nodepath, std::string newparentpath);
    void emulate_copy(std::string nodepath, std::string newparentpath);
    void emulate_rename_copy(std::string nodepath, std::string newparentpath, std::string newname);
    void emulate_delete(std::string nodepath);
    void generate(const fs::path& path, bool force = false);
    void swap(Model& other);
    unique_ptr<ModelNode> root;
};

struct StandardClient;

class CloudItem
{
public:
    CloudItem(const Node* node);

    CloudItem(const Node& node);

    CloudItem(const string& path, bool fromRoot = false);

    CloudItem(const char* path, bool fromRoot = false);

    CloudItem(const NodeHandle& nodeHandle);

    CloudItem(handle nodeHandle);

    std::shared_ptr<Node> resolve(StandardClient& client) const;

private:
    NodeHandle mNodeHandle;
    string mPath;
    bool mFromRoot = false;
}; // CloudItem

struct SyncOptions
{
    string drivePath = string(1, '\0');
    string excludePath;
    string logName;
    bool legacyExclusionsEligible = false;
    bool isBackup = false;
    bool uploadIgnoreFile = false;
}; // SyncOptions

struct SyncStallInfoTests
{
    SyncStallInfo::CloudStallInfoMap cloud;
    SyncStallInfo::LocalStallInfoMap local;

    void extractFrom(SyncStallInfo& stallInfo)
    {
        for (auto& syncStallInfoMapPair : stallInfo.syncStallInfoMaps)
        {
            auto& syncStallInfoMap = syncStallInfoMapPair.second;
            for (auto& stallEntry: syncStallInfoMap.cloud) cloud.insert(stallEntry);
            for (auto& stallEntry: syncStallInfoMap.local) local.insert(stallEntry);
        }
    }

    void clear()
    {
        cloud.clear();
        local.clear();
    }

    bool empty() const
    {
        return cloud.empty() && local.empty();
    }

}; // SyncStallInfoTests

class StandardSyncController
  : public SyncController
{
    using Callback = std::function<bool(const fs::path&)>;

    bool call(const Callback& callback, const LocalPath& path) const;

    void set(Callback& callback, Callback value);

    Callback mDeferPutnode;
    Callback mDeferPutnodeCompletion;
    Callback mDeferUpload;
    mutable std::mutex mLock;

public:
    StandardSyncController() = default;

    bool deferPutnode(const LocalPath& path) const override;

    bool deferPutnodeCompletion(const LocalPath& path) const override;

    bool deferUpload(const LocalPath& path) const override;

    void setDeferPutnodeCallback(Callback callback);

    void setDeferPutnodeCompletionCallback(Callback callback);

    void setDeferUploadCallback(Callback callback);
}; // StandardSyncController

// Convenience.
template<typename T>
class SynchronizedFunction;

template<typename R, typename... P>
class SynchronizedFunction<R(P...)>
{
    std::function<R(P...)> mFunction;
    mutable std::mutex mLock;

public:
    SynchronizedFunction(std::function<R(P...)> function = nullptr)
      : mFunction(std::move(function))
    {
    }

    operator bool() const
    {
        return operator!=(nullptr);
    }

    R operator()(P... arguments)
    {
        std::function<R(P...)> function;

        {
            std::lock_guard guard(mLock);
            function = mFunction;
        }

        function(arguments...);
    }

    SynchronizedFunction& operator=(const SynchronizedFunction& rhs)
    {
        if (this == &rhs)
            return *this;

        std::unique_lock l(mLock, std::defer_lock);
        std::unique_lock r(mLock, std::defer_lock);

        std::lock(l, r);

        mFunction = rhs.mFunction;

        return *this;
    }

    SynchronizedFunction& operator=(std::function<R(P...)> rhs)
    {
        std::lock_guard guard(mLock);

        mFunction = std::move(rhs);

        return *this;
    }

    SynchronizedFunction& operator=(std::nullptr_t)
    {
        std::lock_guard guard(mLock);

        mFunction = nullptr;

        return *this;
    }

    bool operator==(std::nullptr_t) const
    {
        std::lock_guard guard(mLock);

        return mFunction == nullptr;
    }

    bool operator!=(std::nullptr_t) const
    {
        return !(*this == nullptr);
    }
}; // SynchronizedFunction<(R, P...)>

struct StandardClient : public MegaApp
{
    shared_ptr<WAIT_CLASS> waiter;
#ifdef GFX_CLASS
    GfxProc gfx;
#endif

    string client_dbaccess_path;
    std::unique_ptr<HttpIO> httpio;
    mutable std::recursive_mutex clientMutex;
    MegaClient client;
    std::atomic<bool> clientthreadexit{false};
    bool fatalerror = false;
    string clientname;
    std::function<void()> nextfunctionMC;
    std::function<void()> nextfunctionSC;
    string nextfunctionMC_sourcefile, nextfunctionSC_sourcefile;
    int nextfunctionMC_sourceline = -1, nextfunctionSC_sourceline = -1;
    std::condition_variable functionDone;
    std::mutex functionDoneMutex;
    std::string salt;
    std::set<fs::path> localFSFilesThatMayDiffer;

    fs::path fsBasePath;

    handle basefolderhandle = UNDEF;

    enum resultprocenum { PRELOGIN, LOGIN, FETCHNODES, PUTNODES, UNLINK, CATCHUP,
        COMPLETION };  // use COMPLETION when we use a completion function, rather than trying to match tags on callbacks

    struct ResultProc
    {
        StandardClient& client;
        ResultProc(StandardClient& c) : client(c) {}

        struct id_callback
        {
            int request_tag = 0;
            handle h = UNDEF;
            std::function<bool(error)> f;
            id_callback(std::function<bool(error)> cf, int tag, handle ch) : request_tag(tag), h(ch), f(cf) {}
            id_callback(id_callback&&) = default;
        };

        recursive_mutex mtx;  // recursive because sometimes we need to set up new operations during a completion callback
        map<resultprocenum, map<int, id_callback>> m;

        // f is to return true if no more callbacks are expected, and the expected-entry will be removed
        void prepresult(resultprocenum rpe, int tag, std::function<void()>&& requestfunc, std::function<bool(error)>&& f, handle h = UNDEF);
        void processresult(resultprocenum rpe, error e, handle h, int tag);
    } resultproc;

    // thread as last member so everything else is initialised before we start it
    std::thread clientthread;

    string ensureDir(const fs::path& p);

    /**
     * @brief Sets the minimum permissible configurable values for upload throttling.
     *
     * Default upload throttling values can be unsuitable for tests.
     * This method sets them as follows:
     * throttleUpdateRate -> to the lowest value allowed (current: 1min)
     * maxUploadsBeforeThrottle -> to the maximum value allowed (current: 5)
     *
     * If any of the operations fails, the result is logged with a warn, but there are no asserts.
     * The reasons are: 1) This method is meant to be called within the StandardClient constructor,
     * and we avoid propagating ASSERT_NO_FATAL_FAILURE all along the code. 2) The specific code for
     * setting these values for the UploadThrottlingManager is already exercised in its own test
     * suite. 3) Just a few tests will fall into the upload throttling logic, so for most test it
     * doesn't really matter if this method fails for some very unexpected reason.
     */
    void setMinimumUploadThrottleSettings();

    StandardClient(const fs::path& basepath, const string& name, const fs::path& workingFolder = fs::path());
    ~StandardClient();
    void localLogout();
    bool logout(bool keepSyncsConfigFile);

    static mutex om;
    bool logcb = false;
    chrono::steady_clock::time_point lastcb = std::chrono::steady_clock::now();

    string lp(LocalNode* ln);

    void onCallback();

    SynchronizedFunction<void(const SyncConfig&)> onAutoResumeResult;
    SynchronizedFunction<void(const SyncConfig&)> onRemovedSync;

    void sync_added(const SyncConfig& config) override;
    void sync_removed(const SyncConfig& config) override;

    std::atomic<bool> received_syncs_restored{false};
    void syncs_restored(SyncError syncError) override;

    std::atomic<bool> received_node_actionpackets{false};
    std::condition_variable nodes_updated_cv;

    void nodes_updated(sharedNode_vector* nodes, int numNodes) override;
    bool waitForNodesUpdated(unsigned numSeconds);
    void syncupdate_stateconfig(const SyncConfig& config) override;

    std::atomic<bool> received_user_alerts{false};
    std::condition_variable user_alerts_updated_cv;

    void useralerts_updated(UserAlert::Base**, int) override;
    bool waitForUserAlertsUpdated(unsigned numSeconds);

    std::atomic<bool> received_user_actionpackets{false};
    std::mutex user_actionpackets_mutex;
    std::condition_variable user_updated_cv;
    void users_updated(User**users, int size) override;

    // If none lambda is register with createsOnUserUpdateLamda, any user action package generates an event for stop waiting period.
    // If a lambda is register, waiting period only finished if lambda returns true when it is called
    // Once waiting period is finised, removeOnUserUpdateLamda should be called
    bool waitForUserUpdated(unsigned numSeconds);
    std::mutex mUserActionPackageMutex;
    std::function<bool(User*)> mCheckUserChange;
    void createsOnUserUpdateLamda(std::function<bool(User*)> onUserUpdateLambda);
    // Should be called to remove registered lamda
    void removeOnUserUpdateLamda();

    SynchronizedFunction<void(const SyncConfig&)> mOnSyncStateConfig;

    void syncupdate_scanning(bool b) override;

    std::atomic<bool> mStallDetected{false};
    std::atomic<bool> mConflictsDetected{false};
    std::atomic<bool> mTotalStallsUpdated{false};
    std::atomic<bool> mTotalConflictsUpdated{false};

    void syncupdate_conflicts(bool state) override;
    void syncupdate_stalled(bool state) override;
    void syncupdate_totalconflicts(bool state) override;
    void syncupdate_totalstalls(bool state) override;
    void file_added(File* file) override;
    void file_complete(File* file) override;

    std::atomic<unsigned> transfersAdded{0}, transfersRemoved{0}, transfersPrepared{0}, transfersFailed{0}, transfersUpdated{0}, transfersComplete{0};

    void transfer_added(Transfer* transfer) override
    {
        onCallback();

        ++transfersAdded;

        if (mOnTransferAdded)
            mOnTransferAdded(*transfer);
    }

    SynchronizedFunction<void(Transfer&)> mOnTransferAdded;

    void transfer_removed(Transfer*) override { onCallback(); ++transfersRemoved; }
    void transfer_prepare(Transfer*) override { onCallback(); ++transfersPrepared; }
    void transfer_failed(Transfer*,  const Error&, dstime = 0) override { onCallback(); ++transfersFailed; }
    void transfer_update(Transfer*) override { onCallback(); ++transfersUpdated; }

    SynchronizedFunction<void(Transfer*)> onTransferCompleted;


    bool waitForAttrDeviceIdIsSet(unsigned numSeconds, bool& updated);
    bool waitForAttrMyBackupIsSet(unsigned numSeconds, bool& newBackupIsSet);

    bool isUserAttributeSet(attr_t attr, unsigned numSeconds, error& err);

    void transfer_complete(Transfer* transfer) override
    {
        onCallback();

        if (onTransferCompleted)
            onTransferCompleted(transfer);

        ++transfersComplete;
    }

    RequestRetryTracker mRetryTracker;

    void notify_retry(dstime t, retryreason_t r) override;
    void request_error(error e) override;
    void request_response_progress(m_off_t a, m_off_t b) override;
    void threadloop();

    void updateClientDowaitDs(const dstime lastClientDoWait);
    dstime consumeClientDowaitDs(const dstime timeGranularity = 1);
    void resetClientDowaitDs();

private:
    dstime mClientDowaitDs{};

public:
    static bool debugging;  // turn this on to prevent the main thread timing out when stepping in the MegaClient

    template <class PROMISE_VALUE>
    future<PROMISE_VALUE> thread_do(std::function<void(MegaClient&, shared_promise<PROMISE_VALUE>)> f, string sf, int sl)
    {
        unique_lock<mutex> guard(functionDoneMutex);
        std::shared_ptr<promise<PROMISE_VALUE>> promiseSP(new promise<PROMISE_VALUE>());
        nextfunctionMC = [this, promiseSP, f](){ f(this->client, promiseSP); };
        nextfunctionMC_sourcefile = sf;
        nextfunctionMC_sourceline = sl;
        waiter->notify();
        while (!functionDone.wait_until(guard, chrono::steady_clock::now() + chrono::seconds(600), [this]() { return !nextfunctionMC; }))
        {
            if (!debugging)
            {
                promiseSP->set_value(PROMISE_VALUE());
                break;
            }
        }
        return promiseSP->get_future();
    }

    template <class PROMISE_VALUE>
    future<PROMISE_VALUE> thread_do(std::function<void(StandardClient&, shared_promise<PROMISE_VALUE>)> f, string sf, int sl)
    {
        unique_lock<mutex> guard(functionDoneMutex);
        std::shared_ptr<promise<PROMISE_VALUE>> promiseSP(new promise<PROMISE_VALUE>());
        nextfunctionSC_sourcefile = sf;
        nextfunctionSC_sourceline = sl;
        nextfunctionSC = [this, promiseSP, f]() { f(*this, promiseSP); };
        waiter->notify();
        while (!functionDone.wait_until(guard, chrono::steady_clock::now() + chrono::seconds(600), [this]() { return !nextfunctionSC; }))
        {
            if (!debugging)
            {
                promiseSP->set_value(PROMISE_VALUE());
                break;
            }
        }
        return promiseSP->get_future();
    }

    void preloginFromEnv(const string& userenv, PromiseBoolSP pb);
    void loginFromEnv(const string& userenv, const string& pwdenv, PromiseBoolSP pb);
    void loginFromSession(const string& session, PromiseBoolSP pb);

#if defined(MEGA_MEASURE_CODE) || defined(DEBUG)
    void sendDeferredAndReset();
#endif

    class BasicPutNodesCompletion
    {
    public:
        BasicPutNodesCompletion(std::function<void(const Error&)>&& callable)
            : mCallable(std::move(callable))
        {}

        void operator()(const Error& e,
                        targettype_t,
                        vector<NewNode>&,
                        bool,
                        int /*tag*/,
                        const map<string, string>& /*fileHandles*/)
        {
            mCallable(e);
        }

    private:
        std::function<void(const Error&)> mCallable;
    }; // BasicPutNodesCompletion

    bool copy(const CloudItem& source,
              const CloudItem& target,
              const string& name,
              VersioningOption versioningPolicy = NoVersioning);

    bool copy(const CloudItem& source,
              const CloudItem& target,
              VersioningOption versioningPolicy = NoVersioning);

    void copy(const CloudItem& source,
              const CloudItem& target,
              string name,
              PromiseBoolSP result,
              VersioningOption versioningPolicy);

    bool putnodes(const CloudItem& parent,
                  VersioningOption versioningPolicy,
                  std::vector<NewNode>&& nodes);

    void putnodes(const CloudItem& parent,
                  VersioningOption versioningPolicy,
                  std::vector<NewNode>&& nodes,
                  PromiseBoolSP result);

    void uploadFolderTree_recurse(handle parent, handle& h, const fs::path& p, vector<NewNode>& newnodes);
    void uploadFolderTree(fs::path p, CloudItem n2, PromiseBoolSP pb);

    // Necessary to make sure we release the file once we're done with it.
    struct FileGet : public File {
        void completed(Transfer* t, putsource_t source) override
        {
            File::completed(t, source);
            result->set_value(true);
            delete this;
        }

        void terminated(error) override
        {
            result->set_value(false);
            delete this;
        }

        PromiseBoolSP result;
    }; // FileGet

    void downloadFile(const CloudItem& item, const fs::path& destination, PromiseBoolSP result);
    bool downloadFile(const CloudItem& item, const fs::path& destination);

    struct FilePut : public File {

        std::function<void(bool)> completion;

        FilePut(std::function<void(bool)>&& c) : completion(c) {}

        void completed(Transfer* t, putsource_t source) override
        {
            // do the same thing as File::completed(t, source), but only execute our functor completion() after putnodes completes

            assert(!transfer || t == transfer);
            assert(source == PUTNODES_APP);  // derived class for sync doesn't use this code path
            assert(t->type == PUT);

            auto finalCompletion = std::move(completion);
            sendPutnodesOfUpload(
                t->client,
                t->uploadhandle,
                *t->ultoken,
                t->filekey,
                source,
                NodeHandle(),
                [finalCompletion](const Error&,
                                  targettype_t,
                                  vector<NewNode>&,
                                  bool /*targetOverride*/,
                                  int /*tag*/,
                                  const std::map<std::string, std::string>& /*fileHandles*/)
                {
                    if (finalCompletion)
                        finalCompletion(true);
                },
                nullptr,
                false);

            delete this;
        }

        void terminated(error) override
        {
            if (completion) completion(false);
            delete this;
        }
    }; // FilePut

    bool uploadFolderTree(fs::path p, const CloudItem& item);

    void uploadFile(const fs::path& path, const string& name, const Node* parent, TransferDbCommitter& committer, std::function<void(bool)>&& completion, VersioningOption vo = NoVersioning);
    void uploadFile(const fs::path& path, const string& name, const Node* parent, std::function<void(bool)>&& completion, VersioningOption vo = NoVersioning);

    bool uploadFile(const fs::path& path, const string& name, const CloudItem& parent, int timeoutSeconds = 30, VersioningOption vo = NoVersioning);

    bool uploadFile(const fs::path& path, const CloudItem& parent, int timeoutSeconds = 30, VersioningOption vo = NoVersioning);

    void uploadFilesInTree_recurse(const Node* target, const fs::path& p, std::atomic<int>& inprogress, TransferDbCommitter& committer, VersioningOption vo);
    bool uploadFilesInTree(fs::path p, const CloudItem& n2, VersioningOption vo = NoVersioning);

    void uploadFile(const fs::path& sourcePath,
                    const string& targetName,
                    const CloudItem& parent,
                    std::function<void(error)> completion,
                    const VersioningOption versioningPolicy = NoVersioning);

    void uploadFile(const fs::path& sourcePath,
                    const CloudItem& parent,
                    std::function<void(error)> completion,
                    const VersioningOption versioningPolicy = NoVersioning);

    class TreeProcPrintTree : public TreeProc
    {
    public:
        void proc(MegaClient*, std::shared_ptr<Node> /*n*/) override
        {
            // out() << "fetchnodes tree: " << n->displaypath();
        }
    };

    // mark node as removed and notify

    SynchronizedFunction<void(StandardClient& mc, PromiseBoolSP pb)> onFetchNodes;

    void fetchnodes(bool noCache, bool loadSyncs, bool reloadingMidSession, PromiseBoolSP pb);
    bool fetchnodes(bool noCache, bool loadSyncs, bool reloadingMidSession);
    NewNode makeSubfolder(const string& utf8Name);

    void catchup(std::function<void(error)> completion);
    void catchup(PromiseBoolSP pb);

    unsigned deleteTestBaseFolder(bool mayNeedDeleting);
    void deleteTestBaseFolder(bool mayNeedDeleting, bool deleted, PromiseUnsignedSP result);

    void ensureTestBaseFolder(bool mayneedmaking, PromiseBoolSP pb);
    NewNode* buildSubdirs(list<NewNode>& nodes, const string& prefix, int n, int recurselevel);
    bool makeCloudSubdirs(const string& prefix, int depth, int fanout);
    void makeCloudSubdirs(const string& prefix, int depth, int fanout, PromiseBoolSP pb, const string& atpath = "");

    struct SyncInfo
    {
        NodeHandle h;
        fs::path localpath;
        string remotepath;
    };

    SyncConfig syncConfigByBackupID(handle backupID) const;
    bool syncSet(handle backupId, SyncInfo& info) const;
    SyncInfo syncSet(handle backupId);
    SyncInfo syncSet(handle backupId) const;
    std::shared_ptr<Node> getcloudrootnode();
    std::shared_ptr<Node> gettestbasenode();
    std::shared_ptr<Node> getcloudrubbishnode();
    std::shared_ptr<Node> getsyncdebrisnode();
    std::shared_ptr<Node> drillchildnodebyname(std::shared_ptr<Node> n, const string& path);
    vector<std::shared_ptr<Node>> drillchildnodesbyname(Node* n, const string& path);

    // setupBackup is implicitly in Vault
    handle setupBackup_mainthread(const string& rootPath);
    handle setupBackup_mainthread(const string& rootPath,
                                const SyncOptions& syncOptions);

    void setupBackup_inThread(const string& rootPath,
                            const SyncOptions& syncOptions,
                            PromiseHandleSP result);

    // isBackup here allows configuring backups that are not in vault
    handle setupSync_mainthread(const string& rootPath,
                                const CloudItem& remoteItem,
                                const bool isBackup,
                                const bool uploadIgnoreFile,
                                const string& drivePath = string(1, '\0'));

    handle setupSync_mainthread(const string& rootPath,
                                const CloudItem& remoteItem,
                                const SyncOptions& syncOptions);

    void setupSync_inThread(const string& rootPath,
                            const CloudItem& remoteItem,
                            const SyncOptions& syncOptions,
                            PromiseHandleSP result);

    void importSyncConfigs(string configs, PromiseBoolSP result);
    bool importSyncConfigs(string configs);
    string exportSyncConfigs();
    void delSync_inthread(handle backupId, PromiseBoolSP result);

    struct CloudNameLess
    {
        bool operator()(const string& lhs, const string& rhs) const
        {
            return compare(lhs, rhs) < 0;
        }

        static int compare(const string& lhs, const string& rhs)
        {
            return compareUtf(lhs, false, rhs, false, false);
        }

        static bool equal(const string& lhs, const string& rhs)
        {
            return compare(lhs, rhs) == 0;
        }
    }; // CloudNameLess

    bool recursiveConfirm(Model::ModelNode* mn, Node* n, int& descendants, const string& identifier, int depth, bool& firstreported, bool expectFail, bool skipIgnoreFile);

    bool localNodesMustHaveNodes = true;

    auto equal_range_utf8EscapingCompare(multimap<string, LocalNode*, CloudNameLess>& ns, const string& cmpValue, bool unescapeValue, bool unescapeMap, bool caseInsensitive) -> std::pair<multimap<string, LocalNode*>::iterator, multimap<string, LocalNode*>::iterator>;
    bool recursiveConfirm(Model::ModelNode* mn, LocalNode* n, int& descendants, const string& identifier, int depth, bool& firstreported, bool expectFail, bool skipIgnoreFile);
    bool recursiveConfirm(Model::ModelNode* mn, fs::path p, int& descendants, const string& identifier, int depth, bool ignoreDebris, bool& firstreported, bool expectFail, bool skipIgnoreFile);
    Sync* syncByBackupId(handle backupId);
    bool setSyncPausedByBackupId(handle id, bool pause);
    void enableSyncByBackupId(handle id, PromiseBoolSP result, const string& logname);
    bool enableSyncByBackupId(handle id, const string& logname);
    void backupIdForSyncPath(const fs::path& path, PromiseHandleSP result);
    handle backupIdForSyncPath(fs::path path);

    enum Confirm
    {
        CONFIRM_LOCALFS = 0x01,
        CONFIRM_LOCALNODE = 0x02,
        CONFIRM_LOCAL = CONFIRM_LOCALFS | CONFIRM_LOCALNODE,
        CONFIRM_REMOTE = 0x04,
        CONFIRM_ALL = CONFIRM_LOCAL | CONFIRM_REMOTE,
    };

    bool confirmModel_mainthread(handle id, Model::ModelNode* mRoot, Node* rRoot, bool expectFail, bool skipIgnoreFile);
    bool confirmModel_mainthread(handle id, Model::ModelNode* mRoot, LocalNode* lRoot, bool expectFail, bool skipIgnoreFile);
    bool confirmModel_mainthread(handle id, Model::ModelNode* mRoot, fs::path lRoot, bool ignoreDebris, bool expectFail, bool skipIgnoreFile);
    bool confirmModel(handle id, Model::ModelNode* mRoot, Node* rRoot, bool expectFail, bool skipIgnoreFile);
    bool confirmModel(handle id, Model::ModelNode* mRoot, LocalNode* lRoot, bool expectFail, bool skipIgnoreFile);
    bool confirmModel(handle id, Model::ModelNode* mRoot, fs::path lRoot, bool ignoreDebris, bool expectFail, bool skipIgnoreFile);
    bool confirmModel(handle backupId, Model::ModelNode* mnode, const int confirm, const bool ignoreDebris, bool expectFail, bool skipIgnoreFile);
    void prelogin_result(int, string*, string* salt, error e) override;
    void login_result(error e) override;
    void fetchnodes_result(const Error& e) override;
    bool setattr(const CloudItem& item, attr_map&& updates);
    void setattr(const CloudItem& item, attr_map&& updates, PromiseBoolSP result);
    bool rename(const CloudItem& item, const string& newName);
    void unlink_result(handle h, error e) override;

    handle lastPutnodesResultFirstHandle = UNDEF;

    void putnodes_result(const Error& e,
                         targettype_t tt,
                         vector<NewNode>& nn,
                         bool targetOverride,
                         int tag,
                         const std::map<std::string, std::string>& fileHandles) override;
    void catchup_result() override;

    void disableSync(handle id, SyncError error, bool enabled, bool keepSyncDB, PromiseBoolSP result);
    bool disableSync(handle id, SyncError error, bool enabled, bool keepSyncDB);

    template<typename ResultType, typename Callable>
    ResultType withWait(Callable&& callable, ResultType&& defaultValue = ResultType())
    {
        using std::future_status;
        using std::shared_ptr;

        using PromiseType = promise<ResultType>;
        using PointerType = shared_ptr<PromiseType>;

        auto promise = PointerType(new PromiseType());
        auto future = promise->get_future();

        callable(std::move(promise));

        auto status = future.wait_for(std::chrono::seconds(20));

        if (status == future_status::ready)
        {
            return future.get();
        }

        LOG_warn << "Timed out in withWait";

        return std::move(defaultValue);
    }

    void deleteremote(const CloudItem& item, PromiseBoolSP result);
    bool deleteremote(const CloudItem& item);

    bool deleteremotedebris();
    void deleteremotedebris(PromiseBoolSP result);
    void deleteremotenodes(vector<std::shared_ptr<Node> > ns, PromiseBoolSP pb);

    bool movenode(const CloudItem& source,
                  const CloudItem& target,
                  const string& newName = "");

    void movenode(const CloudItem& source,
                  const CloudItem& target,
                  const string& newName,
                  PromiseBoolSP result);

    void movenodetotrash(string path, PromiseBoolSP pb);
    void exportnode(std::shared_ptr<Node> n, int del, m_time_t expiry, bool writable, bool megaHosted, promise<Error>& pb);
    void getpubliclink(Node* n, int del, m_time_t expiry, bool writable, bool megaHosted, promise<Error>& pb);
    void waitonsyncs(chrono::seconds d = chrono::seconds(2));
    /**
     * @brief Collect syncs problems (stall issues and name conflicts)
     * @param problems SyncProblems struct where sync problems will be stored
     */
    void syncproblemsDetected(SyncProblems& problems);
    bool conflictsDetected(list<NameConflict>& conflicts);
    bool stallsDetected(SyncStallInfoTests& stalls);
    bool syncStallDetected(SyncStallInfoTests& si) const;
    bool login_reset(bool noCache = false);
    bool login_reset(const string& user, const string& pw, bool noCache = false, bool resetBaseCloudFolder = true);
    bool resetBaseFolderMulticlient(StandardClient* c2 = nullptr, StandardClient* c3 = nullptr, StandardClient* c4 = nullptr);
    void cleanupForTestReuse(int loginIndex);
    bool login_reset_makeremotenodes(const string& prefix, int depth = 0, int fanout = 0, bool noCache = false);
    bool login_reset_makeremotenodes(const string& user, const string& pw, const string& prefix, int depth, int fanout, bool noCache = false);
    void ensureSyncUserAttributes(PromiseBoolSP result);
    bool ensureSyncUserAttributes();
    void copySyncConfig(SyncConfig config, PromiseHandleSP result);
    handle copySyncConfig(const SyncConfig& config);
    bool login(const string& user, const string& pw);
    bool login_fetchnodes(const string& user, const string& pw, bool makeBaseFolder = false, bool noCache = false);
    bool login_fetchnodesFromSession(const string& session);
    bool delSync_mainthread(handle backupId);
    bool confirmModel_mainthread(Model::ModelNode* mnode, handle backupId, bool ignoreDebris = false, int confirm = CONFIRM_ALL, bool expectFail = false, bool skipIgnoreFile = true);
    bool match(handle id, const Model::ModelNode* source);
    void match(handle id, const Model::ModelNode* source, PromiseBoolSP result);
    bool match(NodeHandle handle, const Model::ModelNode* source);
    void match(NodeHandle handle, const Model::ModelNode* source, PromiseBoolSP result);
    bool waitFor(std::function<bool(StandardClient&)> predicate,
                 std::chrono::seconds timeout,
                 std::chrono::milliseconds sleepIncrement = std::chrono::milliseconds(500));
    bool waitForSyncTotalStallsStateUpdateTrue(const std::chrono::seconds timeout);
    bool match(const Node& destination, const Model::ModelNode& source) const;
    bool makeremotenodes(const string& prefix, int depth, int fanout);
    bool backupOpenDrive(const fs::path& drivePath);
    void triggerPeriodicScanEarly(handle backupID);

    /**
     * @brief Checks synchronization problems for a given backupId.
     *
     * This function validates synchronization issues (conflicts) related to the specified backup
     * by:
     * - Ensuring that the backup ID exists in the conflicts map.
     * - Verifying that the number of conflicts matches the expected count.
     * - Checking that the last conflict entry has the expected local path.
     * - Comparing the clashing local names with the provided file names.
     *
     * @param backupId The handle representing the ID of the backup to check.
     * @param backupIdsCount The total number of backup IDs.
     * @param expectedConflicts The total expected number of conflicts for the given backup.
     * @param localPath The expected local path associated with the most recent conflict.
     * @param f1 The first file name expected to be clashing in the conflict.
     * @param f2 The second file name expected to be clashing in the conflict.
     */
    void checkSyncProblems(const handle backupId,
                           const int backupIdsCount,
                           const unsigned int totalExpectedConflicts,
                           const LocalPath& localPath,
                           const std::string& f1,
                           const std::string& f2);

    /**
     * @brief Creates a hard link from a File node.
     *
     * @param src The source file path for the hard link
     * @param dst The destination file path for the hard link
     * @param sourcePath A reference to a `LocalPath` object where the source path will be stored.
     * @param targetPath A reference to a `LocalPath` object where the destination path will be
     * stored.
     */
    void createHardLink(const fs::path& src,
                        const fs::path& dst,
                        LocalPath& sourcePath,
                        LocalPath& targetPath);

    /**
     * @brief Deletes a file using the client fileAccess.
     *
     * It uses DEFAULTWAIT for retries if the operation ends with a transient error.
     */
    void unlinklocal(const LocalPath& localPath);

    /**
     * @brief Checks for synchronization stall issues related to a specific BackupId.
     *
     * This function validates synchronization stall issues by checking the number of detected
     * stalls. It verifies the following:
     * - The expected number of stalls matches the actual size of the stall info maps.
     * - At least one stall issue is detected.
     * - A specific stall issue involving the given source and target paths is present,
     *   along with the correct reasons and path problems.
     *
     * @param backupId The handle representing the ID of the backup to check.
     * @param expectedStalls The expected number of stall issues detected for the given backup.
     * @param sourcePath A reference to a `LocalPath` object representing the source file path
     * @param targetPath A reference to a `LocalPath` object representing the target file path
     */
    void checkStallIssues(const handle backupId,
                          const unsigned int expectedStalls,
                          LocalPath& sourcePath,
                          LocalPath& targetPath);

    handle getNodeHandle(const CloudItem& item);
    void getNodeHandle(const CloudItem& item, PromiseHandleSP result);

    FileFingerprint fingerprint(const fs::path& fsPath);

    vector<FileFingerprint> fingerprints(const string& path);

#ifndef NDEBUG
    virtual void move_begin(const LocalPath& source, const LocalPath& target) override
    {
        if (mOnMoveBegin)
            mOnMoveBegin(source, target);
    }

    SynchronizedFunction<void(const LocalPath&, const LocalPath&)> mOnMoveBegin;
#endif // ! NDEBUG

    void backupOpenDrive(const fs::path& drivePath, PromiseBoolSP result);

    void ipcr(handle id, ipcactions_t action, PromiseBoolSP result);
    bool ipcr(handle id, ipcactions_t action);
    bool ipcr(handle id);

    void   opcr(const string& email, opcactions_t action, PromiseHandleSP result);
    handle opcr(const string& email, opcactions_t action);
    bool   opcr(const string& email);

    bool iscontact(const string& email);
    bool isverified(const string& email);
    bool verifyCredentials(const string& email);
    bool resetCredentials(const string& email);

    void rmcontact(const string& email, PromiseBoolSP result);
    bool rmcontact(const string& email);

    void  opensharedialog(const CloudItem& item, PromiseErrorSP result);
    Error opensharedialog(const CloudItem& item);

    void  share(const CloudItem& item, const string& email, accesslevel_t permissions, PromiseErrorSP result);
    Error share(const CloudItem& item, const string& email, accesslevel_t permissions);

    void upgradeSecurity(PromiseBoolSP result);

    SynchronizedFunction<void(File&)> mOnFileAdded;
    SynchronizedFunction<void(File&)> mOnFileComplete;
    SynchronizedFunction<void(bool)> mOnStall;
    SynchronizedFunction<void(bool)> mOnConflictsDetected;
    SynchronizedFunction<void(bool)> mOnTotalStallsUpdate;
    SynchronizedFunction<void(bool)> mOnTotalConflictsUpdate;

    void setHasImmediateStall(HasImmediateStallPredicate predicate);

    void setIsImmediateStall(IsImmediateStallPredicate predicate);

    void setSyncController(SyncControllerPtr controller);

    void setDownloadSpeed(m_off_t downloadSpeed);

    void setUploadSpeed(m_off_t uploadSpeed);

    void prepareOneFolder(NewNode* node, const std::string& name, bool canChangeVault);
    void prepareOneFolder(NewNode* node, const char* name, bool canChangeVault);

    bool requestsCompleted() const;

    bool transfersCompleted(direction_t type) const;
};

struct StandardClientInUseEntry
{
    bool inUse = false;
    shared_ptr<StandardClient> ptr;
    string name;
    int loginIndex;

    StandardClientInUseEntry(bool iu, shared_ptr<StandardClient> sp, string n, int index)
    : inUse(iu)
    , ptr(sp)
    , name(n)
    , loginIndex(index)
    {}
};


class StandardClientInUse
{
    list<StandardClientInUseEntry>::iterator entry;

public:

    StandardClientInUse(list<StandardClientInUseEntry>::iterator i)
    : entry(i)
    {
        assert(!entry->inUse);
        entry->inUse = true;
    }

    ~StandardClientInUse()
    {
        entry->ptr->cleanupForTestReuse(entry->loginIndex);
        entry->inUse = false;
    }

    StandardClient* operator->()
    {
        return entry->ptr.get();
    }

    operator StandardClient*()
    {
        return entry->ptr.get();
    }

    operator StandardClient&()
    {
        return *entry->ptr;
    }

};

class ClientManager
{
    // reuse the same client for subsequent tests, to save all the time of logging in, fetchnodes, etc.

    map<int, list<StandardClientInUseEntry>> clients;

public:

    StandardClientInUse getCleanStandardClient(int loginIndex, fs::path workingFolder);

    void clear();
    ~ClientManager();
};

template<class T>
bool debugTolerantWaitOnFuture(std::future<T> f, size_t numSeconds)
{
    // don't just use get() as we will stall an entire jenkins run if the promise is not fulfilled
    // rather, wait with a timeout
    // if we stop in the debugger, continue the wait after the debugger continues
    // otherwise, things fail on timeout immediately
    for (size_t i = 0; i < numSeconds*10; ++i)
    {
        if (future_status::ready == f.wait_for(std::chrono::milliseconds(100)))
        {
            return true;
        }
    }
    return false;
}


extern ClientManager* g_clientManager;

#endif // ENABLE_SYNC

// common base class for test suites so we 
// always change into the process directory
// for each test
class SdkTestBase : public ::testing::Test
{
public:
    static bool clearProcessFolderEachTest;
    // set to check that the tests are independednt
    // by clearing the process's folder
    // slow as remove the database
    
    // run before each test
    void SetUp() override;
};

fs::path getLinkExtractSrciptPath();

// Convenience.
bool isFileHidden(const LocalPath& path);
bool isFileHidden(const fs::path& path);

// Useful utilities.
bool createFile(const fs::path& path,
                const void* data,
                const size_t data_length);

bool createFile(const fs::path &path,
                const std::string &data);

bool createFile(const fs::path& path,
                const std::string& data,
                std::chrono::seconds delta);

std::string randomData(const std::size_t length);


#ifndef _WIN32
// Helper class to handle directory permissions
class PermissionHandler
{
    fs::path mDPath;
    std::optional<fs::perms> mOriginalPermissions{std::nullopt};
    bool mPermissionsRemoved{false};

public:
    explicit PermissionHandler(const std::string& dPath) : mDPath(dPath)
    {
        try
        {
            // Retrieve and store the current permissions
            mOriginalPermissions = fs::status(mDPath).permissions();
        }
        catch (const fs::filesystem_error& e)
        {
            LOG_debug << "Failed to retrieve original permissions for directory: '" << mDPath << "': " << e.what();
        }
    }

    ~PermissionHandler() { restorePermissions(); }

    bool removePermissions(fs::perms permissionsToRemove)
    {
        if (!mOriginalPermissions)
        {
            LOG_debug << "Original permissions needed";
            return false;
        }
        if (mPermissionsRemoved)
        {
            LOG_debug << "Permissions were already removed before, they should be restored first";
            return false;
        }

        try
        {
            fs::permissions(mDPath, *mOriginalPermissions & ~permissionsToRemove, fs::perm_options::replace);
            mPermissionsRemoved = true;
            LOG_verbose << "Successfuly removed permissions for directory: " << mDPath;
        }
        catch (const fs::filesystem_error& e)
        {
            LOG_debug << "Failed to remove permissions for directory: " << mDPath << " with error: " << e.what();
            mPermissionsRemoved = false;
        }
        return mPermissionsRemoved;
    }

    bool restorePermissions()
    {
        if (!mOriginalPermissions)
        {
            LOG_debug << "Original permissions needed";
            return false;
        }
        if (!mPermissionsRemoved) return true;

        try
        {
            fs::permissions(mDPath, *mOriginalPermissions, fs::perm_options::replace);
            mPermissionsRemoved = false; // This restores the flag, as the permissions have been restored too.
            LOG_verbose << "Successfuly restored permissions for directory: " << mDPath;
        }
        catch (const fs::filesystem_error& e)
        {
            LOG_debug << "Failed to restore permissions for directory: " << mDPath << " with error: " << e.what();
        }
        return !mPermissionsRemoved;
    }

    bool originalPermissionsAvailable() const
    {
        return mOriginalPermissions.has_value();
    }
};
#endif

#endif // TEST_H

