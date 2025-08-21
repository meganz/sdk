#include "mega/wsupload.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// --- SDK headers (kept local to .cpp to keep the public header light)
#include "mega/file.h"
#include "mega/filesystem.h"
#include "mega/logging.h"
#include "mega/megaclient.h"
#include "mega/transfer.h"
#include "mega/types.h"

// --- For Phase 1 we keep prototype's WebSocket + threading as-is.
//     In Phase 3 we will move WS handles into CurlHttpIO (src/posix/net.cpp).
#include <curl/curl.h>

#include <zlib.h>

namespace mega
{
namespace ws
{

// ---------- Small time helper (deciseconds) ----------
using dstime = std::uint32_t; // matches SDK name; Phase-3 will use the SDK's waiter source

struct SteadyTime
{
    static dstime ds()
    {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
        dstime v = static_cast<dstime>(ms / 100);
        return v ? v : 1; // never return 0
    }

    static void sleep_ds(dstime ds)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(ds * 100));
    }

    static std::int32_t difference(dstime a, dstime b)
    {
        return static_cast<std::int32_t>(a - b);
    }
};

// ---------- CRC32 wrapper (zlib) ----------
struct CRC32
{
    static std::uint32_t crc32b(const char* data, const int len, const std::uint32_t seed = 0)
    {
        return static_cast<uint32_t>(
            ::crc32(seed, reinterpret_cast<const Bytef*>(data), static_cast<uInt>(len)));
    }
};

// ---------- Chunking map identical to the prototype ----------
struct ChunkMap
{
    static constexpr int SEGSIZE = 131072;
    std::map<m_off_t, int> chunkmap;

    int chunksize(m_off_t pos) const
    {
        const auto it = chunkmap.find(pos);
        return (it == chunkmap.end()) ? 8 * SEGSIZE : it->second;
    }

    ChunkMap()
    {
        m_off_t p{0};
        unsigned dp{0};
        while (dp < 8 * SEGSIZE)
        {
            dp += SEGSIZE;
            chunkmap[p] = dp;
            p += dp;
        }
    }
};

// ---------- Forward decls ----------
struct WsPool;
struct WsConn;

enum class FailReason : std::uint8_t
{
    ServerError,
    OpenFailed,
    ReadFailed,
    StateLost,
    Protocol,
    CrcFailed,
    Unknown
};

// ---------- WsUploadFile: now uses SDK File/FileAccess ----------
class WsUploadFile
{
public:
    static constexpr std::int32_t RETRYINTERVAL{60 * 10}; // ds

    WsUploadFile(MegaClient& client, Transfer& t, const std::uint32_t fileno):
        mClient(client),
        mTransfer(t),
        mFile(frontFile(t)),
        mFileNo(fileno)
    {
        assert(mFile); // Transfer::files.front() must exist for PUT uploads

        // Size/mtime via File (prefer FileAccess later when opened)
        mSize = mFile->size;
        mMtime = mFile->mtime;
    }

    ~WsUploadFile() = default;

    // -- queue/pool bookkeeping
    bool continuingUpload(const dstime now) const
    {
        if (!mAborted && mUploadCompletionTime == 0)
            return true;
        if (mUploadFailedTime != 0 &&
            SteadyTime::difference(now, mUploadFailedTime) > RETRYINTERVAL)
            return true;
        return false;
    }

    void setPool(WsPool& p);
    void unsetPool();

    // -- I/O (Phase 1): use FileAccess with engine mutex UNLOCKED while doing disk I/O
    bool readData(char* buf, const m_off_t pos, const int len, std::mutex& engineMutex)
    {
        // Open on first use
        if (!mFA)
        {
            // Unlock around potentially heavy fs ops
            engineMutex.unlock();
            auto fa = mClient.fsaccess->newfileaccess();
            const bool okOpen =
                fa->fopen(mFile->getLocalname(), true, false, FSLogging::logOnError); // read-only
            engineMutex.lock();

            if (!okOpen)
            {
                markFailed();
                return false;
            }
            mFA = std::move(fa);
            mSize = mFA->size; // authoritative
            mMtime = mFA->mtime; // authoritative
        }

        engineMutex.unlock();
        const bool okRead = mFA->frawread(reinterpret_cast<byte*>(buf),
                                          static_cast<unsigned>(pos),
                                          len,
                                          false,
                                          FSLogging::logOnError);
        engineMutex.lock();

        if (!okRead)
            markFailed();
        return okRead;
    }

    // stats/throughput (unchanged logic)
    void onServerConfirmedBytes(const m_off_t bytes)
    {
        mBytesConfirmed += bytes;
        mClientActiveFilesTick = true;
    }

    // local helpers
    m_off_t size() const noexcept
    {
        return mSize;
    }

    m_off_t headPos() const noexcept
    {
        return mHeadPos;
    }

    void advanceHead(const int by) noexcept
    {
        mHeadPos += by;
    }

    void markEOF() noexcept
    {
        mEofSet = true;
    }

    bool eofSet() const noexcept
    {
        return mEofSet;
    }

    std::uint32_t fileno() const noexcept
    {
        return mFileNo;
    }

    bool paused() const noexcept
    {
        return mPaused;
    }

    void setPaused(bool p) noexcept
    {
        mPaused = p;
    }

    bool aborted() const noexcept
    {
        return mAborted;
    }

    void cancel() noexcept
    {
        mAborted = true;
    }

    void setUploadStart(const dstime t) noexcept
    {
        mUploadStartTime = t;
    }

    // (Phase 5) TODO: crypto: encrypt/MAC and fingerprint apply-on-ACK

    // hooks to signal SDK Transfer later (progress/completion/failure)
    void uploadFailed(const FailReason reason)
    {
        mUploadFailedTime = SteadyTime::ds();
        unsetPool();

        LOG_warn << "WsUpload: Transfer " << this << " failed, reason=" << static_cast<int>(reason);
        // TODO: notify Transfer / MegaClient (Phase 2+)
    }

    void uploadCompleted(const char* response, const int len)
    {
        mUploadCompletionTime = SteadyTime::ds();
        unsetPool();

        // TODO Phase 5: complete MAC chain, compute final fingerprint,
        //               Transfer::completed(), then putnodes.
        LOG_verbose << "WsUpload: Transfer " << this << " completed (payload len=" << len
                    << ") [validResponse = " << (response != nullptr) << "]";
    }

    // debug throughput (server-confirmed only, as in prototype)
    void maybeReportThroughput(const dstime now)
    {
        if (mUploadCompletionTime || mUploadFailedTime)
            return;
        if (mBytesConfirmed == mLastReportedBytesConfirmed)
            return;

        mLastReportedBytesConfirmed = mBytesConfirmed;

        // NOTE: replace with SDK reporting facility later
        const auto dsElapsed = SteadyTime::difference(now, mUploadStartTime);
        const auto kbps = dsElapsed ? (mBytesConfirmed / dsElapsed * 10 / 1024) : 0;
        LOG_verbose << "wsupload: " << (mBytesConfirmed / 1048576) << " MB of " << (mSize / 1048576)
                    << " MB @ ~" << kbps << " KB/s";
    }

private:
    static File* frontFile(Transfer& t)
    {
        return t.files.empty() ? nullptr : *t.files.begin();
    }

    void markFailed()
    {
        mUploadFailedTime = SteadyTime::ds();
    }

private:
    MegaClient& mClient;
    Transfer& mTransfer;
    File* mFile{nullptr}; // non-owning, valid for Transfer lifetime
    std::unique_ptr<FileAccess> mFA; // opened on first read

    std::uint32_t mFileNo{0};

    // progress / state
    m_off_t mSize{0};
    m_off_t mHeadPos{0};
    m_off_t mBytesConfirmed{0};
    m_off_t mLastReportedBytesConfirmed{0};
    m_time_t mMtime{0};

    bool mEofSet{false};
    bool mAborted{false};
    bool mPaused{false};

    dstime mUploadStartTime{0};
    dstime mUploadCompletionTime{0};
    dstime mUploadFailedTime{0};

    // book-keeping: set by WsPool::setPool/unsetPool
    WsPool* mPool{nullptr};

public:
    // helper flags for engine
    bool mClientActiveFilesTick{false};
};

// ---------- (Remaining prototype structs) Chunk header, WsBuf, WsConn, WsPool, WsPoolMgr
// ---------- NOTE: In Phase 1 we keep them close to the original to avoid risk; we only:
//  - switch to m_off_t and SDK types,
//  - call WsUploadFile::readData() instead of pread(),
//  - expose queue mirror entrypoints for TransferList.

#pragma pack(push, 1)

struct ChunkHeader
{
    std::uint32_t fileno;
    m_off_t pos;
    int len;
    std::uint32_t crc;
};

#pragma pack(pop)

static constexpr int MB = 1048576;

struct WsChunk
{
    m_off_t pos;
    int len;
    std::uint32_t fileno;
};

struct WsBuf
{
    char buf[20 + MB];
    int mSendPos = 0;
    int mDataLen = 0;

    void add(const char* data, int len)
    {
        std::memcpy(buf + mDataLen, data, len);
        mDataLen += len;
    }

    void reset()
    {
        mSendPos = 0;
        mDataLen = 0;
    }

    bool sendWS(struct WsConn* ws, int& bufferedAmount);
};

struct ChunkFingerprintMacUpdate
{
    // Phase 5: crypto integration
    ChunkFingerprintMacUpdate(WsUploadFile&, m_off_t, char*, int) {}

    void apply(m_off_t /*pos*/) {}
};

struct WsConn
{
    CURL* curl{nullptr};
    int bufferedAmount{0};

    enum class ReadyState : std::uint8_t
    {
        CONNECTING,
        OPEN,
        CLOSING,
        CLOSED
    };

    ReadyState readyState{ReadyState::CLOSED};

    WsBuf mBufs[2];
    char mCurBuf{0};
    bool mClosing{false};

    char mInBuf[64];
    int mInPos{0};

    WsPool* mPool{nullptr};
    std::unordered_set<WsConn*>::iterator mConns_it;

    std::vector<std::pair<WsChunk, ChunkFingerprintMacUpdate>> mChunksInFlight;

    explicit WsConn(WsPool* pool);
    ~WsConn();

    bool connectWS();
    void closeWS();

    void curlSend();
    void curlRecv();
    void onopen();
    void onclose();
    void onmessage(const char* msg, const int len);

    bool haveSpace() const
    {
        return !bufferedAmount || !mBufs[0].mDataLen || !mBufs[1].mDataLen;
    }

    bool readyForData() const
    {
        return !mClosing;
    }

    void senddata(int buf, const char* data, int len)
    {
        mBufs[buf].add(data, len);
        bufferedAmount += len;
    }

    void sendChunkData(const std::uint32_t fileno,
                       const m_off_t pos,
                       const char* data,
                       const int len);
};

struct WsPoolThread
{
    std::thread t;
    bool terminate{false};
    bool terminated{false};

    explicit WsPoolThread(WsPool* pool);

    ~WsPoolThread()
    {
        t.join();
    }
};

struct WsPool
{
    static constexpr std::int32_t CONNRETRYINTERVAL = 5 * 10;
    static constexpr std::int32_t UPLOADTIMEOUT = 180 * 10;

    std::unordered_set<WsConn*> mConns;
    std::vector<WsChunk> mToResend;

    std::vector<std::unique_ptr<WsPoolThread>> mActiveThreads, mExitingThreads;

    int mNumPoolFiles{0};
    WsUploadFile* mUploadingFile{nullptr};
    std::uint32_t mUFTQversion{0};

    dstime mPoolCreationTime{SteadyTime::ds()};
    std::string mUrl;

    m_off_t mMinFileSize{0}, mMaxFileSize{0};
    int mNumChunksInFlight{0};

    dstime mLastActive{0};
    dstime mLastServerResponse{0};
    dstime mPausedByServerUntil{0};

    unsigned char mNumberOfConnections{1};
    bool mRetiring{false};

    explicit WsPool(std::pair<std::string, m_off_t> urlmaxsize, m_off_t minsize):
        mPoolCreationTime(SteadyTime::ds()),
        mUrl(std::move(urlmaxsize.first)),
        mMinFileSize(minsize),
        mMaxFileSize(urlmaxsize.second)
    {
        checkThreads();
    }

    void increaseNumPoolFiles()
    {
        mNumPoolFiles++;
    }

    void decreaseNumPoolFiles()
    {
        mNumPoolFiles--;
    }

    void poolWorkerThread(WsPoolThread* th);
    void checkThreads();

    void setPoolNumConn(const unsigned char n)
    {
        mNumberOfConnections = n;
        checkThreads();
    }

    bool stillActive();
    bool freshAndSameHostMaxSize(const std::pair<std::string, m_off_t>& urlMaxSize,
                                 const dstime oldestvalid);

    void pauseSending(const dstime ds)
    {
        mPausedByServerUntil = SteadyTime::ds() + ds;
    }

    bool throttledByServer()
    {
        if (!mPausedByServerUntil)
            return false;
        if (SteadyTime::difference(SteadyTime::ds(), mPausedByServerUntil) > 0)
        {
            mPausedByServerUntil = 0;
            return false;
        }
        return true;
    }

    bool getWsUploadFile(const dstime now, class UploadEngine::Impl& impl);

    WsUploadFile* findFile(const std::uint32_t fileno, class UploadEngine::Impl& impl);

    bool nextChunk(WsChunk& chunk, class UploadEngine::Impl& impl);

    void retryChunk(const WsChunk& chunk)
    {
        mToResend.push_back(chunk);
    }

    void retryChunksOnTheWire(WsConn* ws);
    void applyInFlight(std::uint32_t fileno);

    bool sendChunk(WsConn* ws, class UploadEngine::Impl& impl);
};

inline void WsUploadFile::unsetPool()
{
    mPool->decreaseNumPoolFiles();
    mPool = nullptr;
}

inline void WsUploadFile::setPool(WsPool& p)
{
    mPool = &p;
    assert(mPool);
    mPool->increaseNumPoolFiles();
}

struct CurlResponseProc
{
    virtual ~CurlResponseProc() = default;

    virtual void curlIO() {}

    virtual bool done(bool) = 0;
};

struct WsPoolMgr
{
    static constexpr std::int32_t POOLCONNKEEPALIVE = 60 * 10;
    static constexpr std::int32_t POOLFRESHNESS = 24 * 3600 * 10;
    const std::int32_t SERVERTIMEOUT = 20 * 10;

    // (Phase 1 only) keep a private cURL multi. Phase 3 moves this into CurlHttpIO.
    CURLM* curlm = nullptr;

    std::vector<std::unique_ptr<WsPool>> mPools;
    std::unordered_map<CURL*, CurlResponseProc*> mCurlProcs;

    std::unordered_set<WsUploadFile*> mActiveFiles;
    dstime mLastNetRead{0};

    WsPoolMgr()
    {
        curlm = curl_multi_init(); /* Phase 4: refreshPools() via USC */
    }

    ~WsPoolMgr() {}

    void curlIO(class UploadEngine::Impl& impl);

    void bumpLastNetRead(dstime now)
    {
        if (SteadyTime::difference(now, mLastNetRead) > 0)
            mLastNetRead = now;
    }

    void checkPools(class UploadEngine::Impl& impl);

    void setCurlResponseProc(CURL* curl, CurlResponseProc* proc)
    {
        // proc->/*curl*/ /*unused*/;
        mCurlProcs[curl] = proc;
        curl_multi_add_handle(curlm, curl);
    }

    void bumpAllPools(dstime now)
    {
        for (auto& p: mPools)
        {
            p->mLastActive = now;
            p->mLastServerResponse = now;
        }
    }
};

// ---------- Engine PImpl: queue + pool manager + upload thread ----------

class UploadEngine::Impl
{
public:
    explicit Impl(MegaClient& c):
        client(c)
    {
        LOG_verbose << "[UploadEngine::Impl] call"
                    << " [this = " << this << "]";
        curl_global_init(CURL_GLOBAL_ALL);
    }

    ~Impl() = default;

    // Queue mirrors TransferList ordering and priority.
    void enqueue(Transfer& t)
    {
        LOG_verbose << "[UploadEngine::enqueue] BEGIN"
                    << " [this = " << this << "]";
        std::lock_guard<std::mutex> g(uploadMutex);

        if (!nextFileNo)
            nextFileNo = 1;
        auto uf = std::make_unique<WsUploadFile>(client, t, nextFileNo++);
        // uf->setCurrentTimeRef(currentTime);
        auto raw = uf.get();

        // append at end by default; 'before' reposition handles other cases
        fileList.push_back(raw);
        files.emplace(&t, std::move(uf));
        inQueue.emplace(raw);

        bumpQueueVersion();
        if (fileList.size() == 1)
            nextIt = fileList.begin();
        LOG_verbose << "[UploadEngine::enqueue] END"
                    << " [this = " << this << "]";
    }

    void reposition(Transfer& t, Transfer* before)
    {
        LOG_verbose << "[UploadEngine::reposition] BEGIN"
                    << " [this = " << this << "]";
        std::lock_guard<std::mutex> g(uploadMutex);
        auto it = files.find(&t);
        if (it == files.end())
            return;
        auto* f = it->second.get();
        if (!f || !inQueue.count(f))
            return;

        // erase current node
        for (auto lit = fileList.begin(); lit != fileList.end(); ++lit)
        {
            if (*lit == f)
            {
                if (nextIt == lit)
                    ++nextIt;
                fileList.erase(lit);
                break;
            }
        }

        // find 'before' (if any)
        if (before)
        {
            auto itb = files.find(before);
            if (itb != files.end() && inQueue.count(itb->second.get()))
            {
                for (auto lit = fileList.begin(); lit != fileList.end(); ++lit)
                {
                    if (*lit == itb->second.get())
                    {
                        fileList.insert(lit, f);
                        bumpQueueVersion();
                        return;
                    }
                }
            }
        }

        // default: push_back
        fileList.push_back(f);
        bumpQueueVersion();
        LOG_verbose << "[UploadEngine::reposition] END"
                    << " [this = " << this << "]";
    }

    void pause(Transfer& t)
    {
        withFile(t,
                 [&](WsUploadFile& f)
                 {
                     f.setPaused(true);
                 });
    }

    void unpause(Transfer& t)
    {
        LOG_verbose << "[UploadEngine::unpause] BEGIN"
                    << " [this = " << this << "]";
        withFile(t,
                 [&](WsUploadFile& f)
                 {
                     f.setPaused(false);
                 });
        LOG_verbose << "[UploadEngine::unpause] END"
                    << " [this = " << this << "]";
    }

    void remove(Transfer& t)
    {
        LOG_verbose << "[UploadEngine::remove] BEGIN"
                    << " [this = " << this << "]";
        std::lock_guard<std::mutex> g(uploadMutex);
        auto it = files.find(&t);
        if (it == files.end())
            return;
        auto* f = it->second.get();
        if (!f)
            return;

        // remove from queue
        for (auto lit = fileList.begin(); lit != fileList.end(); ++lit)
        {
            if (*lit == f)
            {
                if (nextIt == lit)
                    ++nextIt;
                fileList.erase(lit);
                break;
            }
        }
        inQueue.erase(f);
        files.erase(it);
        bumpQueueVersion();
        LOG_verbose << "[UploadEngine::remove] END"
                    << " [this = " << this << "]";
    }

    void start()
    {
        LOG_verbose << "[UploadEngine::start] BEGIN"
                    << " [this = " << this << "]";
        std::lock_guard<std::mutex> g(uploadMutex);
        if (uploadThread.joinable())
            return;

        // one-time pool scaffolding (Phase 4 will call refreshPools() via USC)
        if (poolMgr.mPools.empty())
        {
            // Temporary bootstrap: single catch-all pool with “no limit”; Phase 4 replaces this.
            poolMgr.mPools.push_back(std::make_unique<WsPool>(
                std::make_pair(std::string("wss://placeholder.invalid/"), m_off_t(0)),
                m_off_t(0)));
        }

        uploadThread = std::thread(
            [this]
            {
                this->run();
            });
        LOG_verbose << "[UploadEngine::start] END"
                    << " [this = " << this << "]";
    }

    void kick()
    { /* Phase 2/6 will gate on queuedfa etc. */
        LOG_verbose << "[UploadEngine::kick] call"
                    << " [this = " << this << "]";
    }

    // Called by pools to pick next file that matches [min,max)
    WsUploadFile* nextEligible(const m_off_t min, const m_off_t max)
    {
        LOG_verbose << "[UploadEngine::nextEligible] BEGIN"
                    << " [this = " << this << "]";
        bool consecutive = true;
        for (auto it = nextIt; it != fileList.end(); ++it)
        {
            WsUploadFile* f = *it;
            if (!f)
                continue;

            if (f->continuingUpload(currentTime) && !f->paused())
            {
                if (f->size() >= min && (!max || f->size() < max))
                {
                    if (consecutive)
                    {
                        auto ret = f;
                        ++it;
                        nextIt = it;
                        return ret;
                    }
                }
                consecutive = false;
            }
        }
        LOG_verbose << "[UploadEngine::nextEligible] END"
                    << " [this = " << this << "]";
        return nullptr;
    }

    // Uploader main loop (manager thread)
    void run()
    {
        LOG_verbose << "[UploadEngine::run] BEGIN"
                    << " [this = " << this << "]";
        std::unique_lock<std::mutex> lk(uploadMutex);
        for (;;)
        {
            currentTime = SteadyTime::ds();

            // cURL pumping for USC (future) and to drive WS conns
            poolMgr.curlIO(*this);

            // progress reports (server-ACKed)
            for (WsUploadFile* f: poolMgr.mActiveFiles)
                f->maybeReportThroughput(currentTime);
            poolMgr.mActiveFiles.clear();

            // while paused, we only keep network reactor pumping
            // (Phase 3: this will live in CurlHttpIO’s event loop)
            if (paused)
                continue;

            poolMgr.checkPools(*this);
        }
        LOG_verbose << "[UploadEngine::run] END"
                    << " [this = " << this << "]";
    }

    // --- state ---
    MegaClient& client;

    // queue mirrors TransferList
    std::list<WsUploadFile*> fileList;
    std::unordered_map<Transfer*, std::unique_ptr<WsUploadFile>> files;
    std::unordered_set<WsUploadFile*> inQueue;
    std::list<WsUploadFile*>::iterator nextIt = fileList.begin();
    std::uint32_t queueVersion{0};

    // pools + curl
    WsPoolMgr poolMgr;

    // engine mutex (same contention model as prototype for Phase 1)
    std::mutex uploadMutex;
    std::thread uploadThread;

    // shared time ref for files to compute deltas
    dstime currentTime{0};

    // bookkeeping
    std::atomic<std::uint32_t> nextFileNo{1};
    bool paused{false};

    // helpers
    template<class F>
    void withFile(Transfer& t, F&& fn)
    {
        LOG_verbose << "[UploadEngine::withFile] call"
                    << " [this = " << this << "]";
        std::lock_guard<std::mutex> g(uploadMutex);
        auto it = files.find(&t);
        if (it == files.end() || !it->second)
            return;
        fn(*it->second);
    }

    void bumpQueueVersion()
    {
        LOG_verbose << "[UploadEngine::bumpQueueVersion] call [current = " << queueVersion << "]"
                    << " [this = " << this << "]";
        ++queueVersion;
    }
};

// ====== WsConn / WsPool implementations (kept close to prototype), trimmed ======

WsConn::WsConn(WsPool* pool):
    mPool(pool)
{
    mConns_it = pool->mConns.insert(this).first;
}

WsConn::~WsConn()
{
    if (mPool)
        mPool->mConns.erase(mConns_it);
    if (curl)
        curl_easy_cleanup(curl);
}

bool WsConn::connectWS()
{
    if (curl)
        curl_easy_cleanup(curl);
    curl = curl_easy_init();
    if (!curl)
    {
        readyState = ReadyState::CLOSED;
        return false;
    }

    readyState = ReadyState::CONNECTING;
    curl_easy_setopt(curl, CURLOPT_URL, mPool->mUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2);

    // NOTE: engine mutex is handled by caller thread (pool worker)

    const CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK)
    {
        readyState = ReadyState::OPEN;
        mBufs[0].reset();
        mBufs[1].reset();
        bufferedAmount = 0;
        onopen();
        return true;
    }
    readyState = ReadyState::CLOSED;
    return false;
}

void WsConn::closeWS()
{
    readyState = ReadyState::CLOSING;
    mClosing = true;
}

void WsConn::onopen()
{
    LOG_debug << "WsUpload: Connected to " << mPool->mUrl << "\n";
}

void WsConn::onclose()
{
    LOG_debug << "WsUpload: Disconnected from " << mPool->mUrl << "\n";
    mPool->retryChunksOnTheWire(this);
}

void WsConn::curlSend()
{
    if (readyState != ReadyState::OPEN)
        return;
    while (mBufs[static_cast<short>(mCurBuf)].sendWS(this, bufferedAmount))
        mCurBuf = !mCurBuf;
}

void WsConn::curlRecv()
{
    const struct curl_ws_frame* meta = nullptr;
    size_t recv = 0;
    for (;;)
    {
        const auto res = curl_ws_recv(curl, mInBuf + mInPos, sizeof(mInBuf) - mInPos, &recv, &meta);
        if (res == CURLE_OK && meta && !meta->bytesleft)
        {
            onmessage(mInBuf, static_cast<int>(recv));
        }
        else
        {
            if (res != CURLE_AGAIN || meta)
            {
                readyState = ReadyState::CLOSED;
                onclose();
            }
            break;
        }
    }
}

// process server message
void WsConn::onmessage(const char* /*msg*/, const int len)
{
    LOG_debug << "WsConn::onmessage -> len = " << len;
    return;
    /*
    //mPool->mLastActive = g_wsUploadMgr.mCurrentTime;  // ToDo:: ensure this is integrated
    //mPool->mLastServerResponse = g_wsUploadMgr.mCurrentTime; //  // ToDo:: ensure this is
integrated
    //g_wsUploadMgr.mPoolMgr.bumpLastNetRead();  // ToDo:: ensure this is integrated

    // parse and action the message from the upload server
    if (len < 9)
    {
        LOG_debug << "WsUpload: Invalid server message length " << len;
        closeWS();
    }
    else if (*(std::uint32_t*)(msg + len - sizeof(std::uint32_t)) != CRC32::crc32b(msg, len -
sizeof(std::uint32_t)))
    {
        // inbound CRC failure is treated as fatal for the connection
        LOG_debug << "WsUpload: CRC failed, byteLength=" << len;
        closeWS();
    }
    else
    {
#pragma pack(push,1)
        struct ChunkResponse
        {
            std::uint32_t fileno;
            off_t chunkpos;
            char event;
        };
#pragma pack(pop)

        // (the amount of line noise required to replace this with a static_cast is unacceptable)
        const ChunkResponse* response {reinterpret_cast<const ChunkResponse*>(msg)};
        WsChunk chunk;

        // ignore messages about files that have been cancelled
        WsUploadFile* responseUploadFile {mPool->findFile(response->fileno)};
        if (!responseUploadFile) return;

        if (response->event < 4 || response->event == 7)
        {
            if (response->event < 0)
            {
                responseUploadFile->uploadFailed(response->event);
                return;
            }

            chunk.pos = -1;

            // also remove from mChunksInFlight - most likely located at the beginning
            for (int i = 0; i < static_cast<int>(mChunksInFlight.size()); i++)
            {
                if (mChunksInFlight[i].first.pos == response->chunkpos &&
mChunksInFlight[i].first.fileno == response->fileno)
                {
                    chunk = mChunksInFlight[i].first;
                    mChunksInFlight[i].second.apply(chunk.pos,
responseUploadFile->mChunkedEncryptMAC, responseUploadFile->mFingerprint);
                    mChunksInFlight.erase(mChunksInFlight.begin() + i);
                    mPool->mNumChunksInFlight--;
                    break;
                }
            }

            if (chunk.pos < 0)
            {
                LOG_debug << "WsUpload: PROTOCOL ERROR - Server confirmed chunk not in flight: pos="
                        << chunk.pos << " fileno=" << chunk.fileno << " type=" <<
static_cast<int>(response->event); return;
            }
        }

        if (len == 13)
        {
            responseUploadFile->uploadFailed(response->event);
            return;
        }
        else switch (response->event)
        {
            case 1:     // non-final chunk ingested by server
                if (chunk.len)
                {
                    responseUploadFile->mBytesConfirmed += chunk.len;

                    if (responseUploadFile->mBytesConfirmed >= responseUploadFile->size)
                    {
                        // server confirmed last chunk (or more) rather than completing upload:
                        // this means that the server has lost its state
                        responseUploadFile->uploadFailed(0);
                        break;
                    }
                }
                // fall through
            case 7:     // final chunk ingested by server - server knows that the file is complete
                //g_wsUploadMgr.mPoolMgr.mActiveFiles.insert(responseUploadFile); // progress will
be shown  // ToDo:: ensure this is integrated break;

            case 2:     // chunk already on server (could happen after a reconnect/retry)
                break;

            case 3:     // CRC failed (unlikely on SSL, but very possible on TCP)
                LOG_debug << "WsUpload: Chunk CRC FAILED on " << mPool->mUrl;
                mPool->retryChunk(chunk);
                break;

            case 4:     // upload completed
                // ensure that MAC and fingerprint are complete
                mPool->applyInFlight(response->fileno, &responseUploadFile->mChunkedEncryptMAC,
&responseUploadFile->mFingerprint);

                responseUploadFile->showThroughput();
                responseUploadFile->uploadComplete(msg + 14, msg[13]);
                break;

            case 5:     // server in distress - refresh pool target URLs from API
                LOG_debug << "WsUpload: Server requested a pool refresh";
                //g_wsUploadMgr.mPoolMgr.refreshPools(); // ToDo:: ensure this is integrated
                break;

            case 6:     // uq too large - stop sending for len seconds
                LOG_debug << "WsUpload: Server requested sending to pause for " <<
response->chunkpos << " ms"; mPool->pauseSending(response->chunkpos / 100 + 1); break;

            default:    // ignore unknown messages for compatibility with future protocol features
                LOG_debug << "WsUpload: Unknown response from server " <<
static_cast<int>(response->event); break;
        }
    }
    */
}

void WsConn::sendChunkData(const std::uint32_t fileno,
                           const m_off_t pos,
                           const char* data,
                           const int len)
{
    ChunkHeader header;
    header.fileno = fileno;
    header.pos = pos;
    header.len = len;
    header.crc = CRC32::crc32b(data,
                               len,
                               CRC32::crc32b(reinterpret_cast<char*>(&header),
                                             static_cast<int>(offsetof(ChunkHeader, crc))));

    char bufIdx = mCurBuf;
    if (mBufs[static_cast<short>(bufIdx)].mDataLen)
        bufIdx = !bufIdx;

    senddata(bufIdx, reinterpret_cast<char*>(&header), static_cast<int>(sizeof header));
    senddata(bufIdx, data, len);
}

bool WsBuf::sendWS(WsConn* ws, int& bufferedAmount)
{
    if (mSendPos >= mDataLen)
        return false;

    size_t sent = 0;
    const CURLcode res =
        curl_ws_send(ws->curl, buf + mSendPos, mDataLen - mSendPos, &sent, 0, CURLWS_BINARY);
    if (res == CURLE_OK)
    {
        mSendPos += static_cast<int>(sent);
        bufferedAmount -= static_cast<int>(sent);
        if (mSendPos == mDataLen)
        {
            reset();
            return true;
        }
    }
    else if (res != 81) // transient?
    {
        ws->readyState = WsConn::ReadyState::CLOSED;
        ws->onclose();
    }
    return false;
}

bool WsPool::stillActive()
{
    if (mNumPoolFiles)
    {
        // grace period (as prototype)
        // ... (unchanged)
        return true;
    }
    if (!mNumberOfConnections)
    {
        checkThreads();
        return !mActiveThreads.empty() || !mExitingThreads.empty();
    }
    setPoolNumConn(0);
    return true;
}

bool WsPool::freshAndSameHostMaxSize(const std::pair<std::string, m_off_t>& urlMaxSize,
                                     const dstime oldestvalid)
{
    if (SteadyTime::difference(mPoolCreationTime, oldestvalid) < 0)
        return false;
    if (mMaxFileSize != urlMaxSize.second)
        return false;

    for (int i = 6;
         i < static_cast<int>(urlMaxSize.first.size()) && i < static_cast<int>(mUrl.size());
         ++i)
    {
        if (mUrl[i] != urlMaxSize.first[i])
            return false;
        if (mUrl[i] == '/')
            return true;
    }
    return false;
}

bool WsPool::getWsUploadFile(const dstime now, UploadEngine::Impl& impl)
{
    if (mUploadingFile && !mUploadingFile->paused() && mUploadingFile->continuingUpload(now) &&
        mUFTQversion == impl.queueVersion)
        return true;
    if (mRetiring)
        return false;

    if (auto* f = impl.nextEligible(mMinFileSize, mMaxFileSize))
    {
        mUploadingFile = f;
        mUploadingFile->setUploadStart(now);
        mUploadingFile->setPool(*this);
        mUFTQversion = impl.queueVersion;
        return true;
    }
    return false;
}

WsUploadFile* WsPool::findFile(const std::uint32_t fileno, UploadEngine::Impl& /*impl*/)
{
    // if (mUploadingFile && mUploadingFile->fileno() == fileno && mUploadingFile->/*pool*/ true)
    //     return mUploadingFile;

    // (Phase 1: the engine stores only the active file; for lookups across queue,
    //           we’d search impl.files if we stored by fileno)
    return (mUploadingFile && mUploadingFile->fileno() == fileno) ? mUploadingFile : nullptr;
}

bool WsPool::nextChunk(WsChunk& chunk, UploadEngine::Impl& impl)
{
    // resend first
    if (!mToResend.empty())
    {
        chunk = mToResend.front();
        mToResend.erase(mToResend.begin());
        return true;
    }

    // global pause (Phase 6 hooks here)
    if (impl.paused)
        return false;

    while (getWsUploadFile(impl.currentTime, impl))
    {
        auto* f = mUploadingFile;
        if (!f)
            break;

        if (f->headPos() < f->size() || !f->eofSet())
        {
            chunk.fileno = f->fileno();

            if (f->headPos() == f->size())
            {
                f->markEOF();
                chunk.pos = f->headPos();
                chunk.len = 0;
            }
            else
            {
                chunk.pos = f->headPos();

                static const ChunkMap g_chunkMap; // local static; identical to prototype
                m_off_t newHead = f->headPos() + g_chunkMap.chunksize(f->headPos());
                if (newHead > f->size())
                {
                    newHead = f->size();
                    f->markEOF();
                }

                chunk.len = static_cast<int>(newHead - chunk.pos);
                f->advanceHead(chunk.len);
            }

            mLastActive = impl.currentTime;
            return true;
        }
        mUploadingFile = nullptr; // done with this file
    }
    return false;
}

void WsPool::retryChunksOnTheWire(WsConn* ws)
{
    for (auto& p: ws->mChunksInFlight)
        mToResend.push_back(p.first);
    mNumChunksInFlight -= static_cast<int>(ws->mChunksInFlight.size());
    ws->mChunksInFlight.clear();
}

void WsPool::applyInFlight(std::uint32_t /*fileno*/)
{
    // Phase 5: apply MAC & fingerprint of in-flight chunks on completion
}

bool WsPool::sendChunk(WsConn* ws, UploadEngine::Impl& impl)
{
    if (ws->readyState != WsConn::ReadyState::OPEN || !ws->haveSpace())
        return false;

    WsChunk chunk;
    if (!nextChunk(chunk, impl))
        return false;

    char buf[MB];
    auto* uf = mUploadingFile;
    if (!uf)
        return false;

    if (!uf->aborted() && uf->readData(buf, chunk.pos, chunk.len, impl.uploadMutex))
    {
        ws->mChunksInFlight.emplace_back(chunk,
                                         ChunkFingerprintMacUpdate(*uf, chunk.pos, buf, chunk.len));
        ws->sendChunkData(chunk.fileno, chunk.pos, buf, chunk.len);
        ++mNumChunksInFlight;
        return true;
    }
    return false;
}

WsPoolThread::WsPoolThread(WsPool* pool):
    t(std::thread(&WsPool::poolWorkerThread, pool, this))
{}

void WsPool::checkThreads()
{
    while (mActiveThreads.size() < mNumberOfConnections)
        mActiveThreads.push_back(std::make_unique<WsPoolThread>(this));

    while (mActiveThreads.size() > mNumberOfConnections)
    {
        auto thr = std::move(mActiveThreads.back());
        mActiveThreads.pop_back();
        thr->terminate = true;
        mExitingThreads.push_back(std::move(thr));
    }

    for (int i = static_cast<int>(mExitingThreads.size()); i--;)
        if (mExitingThreads[i]->terminated)
            mExitingThreads.erase(mExitingThreads.begin() + i);
}

void WsPool::poolWorkerThread(WsPoolThread* th)
{
    int retryCount{0};
    std::uint32_t lastQueueVersion{0};

    WsConn ws{this};
    // NOTE: engine mutex is owned by engine; we’ll lock/unlock via impl

    // We need the engine Impl; capture via this->... (we’ll pass it from caller in Phase 3)
    // For Phase 1, we access it indirectly via the manager thread. This thread work
    // remains close to the prototype and will be folded into CurlHttpIO later.
    UploadEngine::Impl* impl{nullptr}; // TODO (Phase 3): pass in ptr from engine

    // This Phase 1 worker keeps the behavior narrative; wiring to `impl` will be refined in
    // Phase 3.

    (void)th;
    (void)retryCount;
    (void)lastQueueVersion;
    (void)impl;
    // For brevity in this snippet, omit full worker loop (identical to prototype).
    // The important change in Phase 1 is readData() via FileAccess (already done above).
}

// ---------- WsPoolMgr: curlIO + pool checks (manager thread) ----------

void WsPoolMgr::curlIO(UploadEngine::Impl& impl)
{
    int still_running = 0, msgs_left = 0;
    curl_multi_perform(curlm, &still_running);

    CURLMsg* msg;
    for (auto& kv: mCurlProcs)
        if (kv.second)
            kv.second->curlIO();

    while ((msg = curl_multi_info_read(curlm, &msgs_left)))
    {
        if (msg->msg == CURLMSG_DONE)
        {
            CURL* curl = msg->easy_handle;
            auto it = mCurlProcs.find(curl);
            if (it != mCurlProcs.end() && it->second &&
                it->second->done(msg->data.result == CURLE_OK))
            {
                curl_multi_remove_handle(curlm, curl);
                mCurlProcs.erase(it);
            }
        }
    }

    impl.uploadMutex.unlock();
    CURLMcode mc = curl_multi_poll(curlm, nullptr, 0, 500, nullptr);
    impl.uploadMutex.lock();
    if (mc != CURLM_OK)
    {
        LOG_err << "curl_multi_poll failed";
    }
}

void WsPoolMgr::checkPools(UploadEngine::Impl& /*impl*/)
{
    // (trimmed) update mLastNetRead, retire stale pools, reduce conn count, refresh when needed.
    // Progress:
    for (auto* uf: mActiveFiles)
    {
        uf->mClientActiveFilesTick = false;
    }
    mActiveFiles.clear();
}

// ---------- UploadEngine public API ----------

UploadEngine::UploadEngine(MegaClient& client):
    pImpl(new Impl(client))
{}

UploadEngine::~UploadEngine() = default;

void UploadEngine::start()
{
    pImpl->start();
}

void UploadEngine::enqueue(Transfer& t)
{
    pImpl->enqueue(t);
}

void UploadEngine::reposition(Transfer& t, Transfer* before)
{
    pImpl->reposition(t, before);
}

void UploadEngine::pause(Transfer& t)
{
    pImpl->pause(t);
}

void UploadEngine::unpause(Transfer& t)
{
    pImpl->unpause(t);
}

void UploadEngine::remove(Transfer& t)
{
    pImpl->remove(t);
}

void UploadEngine::kick()
{
    pImpl->kick();
}

// runtime switch (compile-time flag in CMake in section 3)
bool wsEnabled(const MegaClient&)
{
    return true; /* Phase 1: opt-in globally; wire to a setting later */
}

} // namespace ws
} // namespace mega
