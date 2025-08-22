#include "mega/wsupload.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef> // offsetof
#include <cstdint>
#include <cstring>
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

// Phase 1: keep prototype's WS + threads. Phase 3: move into CurlHttpIO reactor.
#include <curl/curl.h>

#include <zlib.h>

namespace mega
{
namespace ws
{

// ---------- small time helper (deciseconds) ----------
using dstime = std::uint32_t;

struct SteadyTime
{
    static dstime ds()
    {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
        const dstime v = static_cast<dstime>(ms / 100);
        return v ? v : 1; // never return 0 (0 used as sentinel)
    }

    static void sleep_ds(const dstime ds)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(ds * 100));
    }

    static std::int32_t difference(const dstime a, const dstime b)
    {
        return static_cast<std::int32_t>(a - b);
    }
};

// ---------- CRC32 wrapper ----------
struct CRC32
{
    static std::uint32_t crc32b(const char* data, const int len, const std::uint32_t seed = 0)
    {
        return static_cast<std::uint32_t>(
            ::crc32(seed, reinterpret_cast<const Bytef*>(data), static_cast<uInt>(len)));
    }
};

// ---------- Chunk map ----------
struct ChunkMap
{
    static constexpr int SEGSIZE = 131072;
    std::map<m_off_t, int> chunkmap;

    int chunksize(const m_off_t pos) const
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

static ChunkMap g_chunkMap;

// ---------- forward decls ----------
struct WsPool;
struct WsConn;
struct WsPoolThread;
class CurlResponseProc;

class UploadEngine; // from header

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

// ---------- WsUploadFile (uses SDK File/FileAccess) ----------
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
        mSize = mFile->size;
        mMtime = mFile->mtime;
        LOG_debug << "WsUploadFile: " << mFile->getLocalname() << " fileno=" << mFileNo
                  << " size=" << mSize << " mtime=" << mMtime << " [this = " << this << "]";
    }

    ~WsUploadFile() = default;

    // queue/pool bookkeeping
    bool continuingUpload(const dstime now) const
    {
        if (!mAborted && mUploadCompletionTime == 0)
            return true;
        if (mUploadFailedTime != 0 &&
            SteadyTime::difference(now, mUploadFailedTime) > RETRYINTERVAL)
            return true;
        return false;
    }

    void setPool(WsPool& p); // defined after WsPool
    void unsetPool(); // defined after WsPool

    // I/O (open on first read) — engineMutex is the single engine mutex
    bool readData(char* buf, const m_off_t pos, const int len, std::mutex& engineMutex)
    {
        // open on first use
        if (!mFA)
        {
            engineMutex.unlock();
            auto fa = mClient.fsaccess->newfileaccess();
            const bool okOpen =
                fa->fopen(mFile->getLocalname(), true, false, FSLogging::logOnError);
            engineMutex.lock();

            if (!okOpen)
            {
                markFailed();
                return false;
            }
            mFA = std::move(fa);
            mSize = mFA->size;
            mMtime = mFA->mtime;
        }

        engineMutex.unlock();
        // Some platforms declare frawread(pos) as unsigned — cast explicitly to avoid -Wconversion
        const bool okRead = mFA->frawread(reinterpret_cast<byte*>(buf),
                                          static_cast<unsigned>(pos),
                                          static_cast<unsigned>(len),
                                          false,
                                          FSLogging::logOnError);
        engineMutex.lock();

        if (!okRead)
            markFailed();
        return okRead;
    }

    // server-confirmed progression
    void onServerConfirmedBytes(const m_off_t bytes)
    {
        mBytesConfirmed += bytes;
        mClientActiveFilesTick = true;
    }

    // accessors
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

    bool eofSet() const noexcept
    {
        return mEofSet;
    }

    void markEOF() noexcept
    {
        mEofSet = true;
    }

    std::uint32_t fileno() const noexcept
    {
        return mFileNo;
    }

    bool paused() const noexcept
    {
        return mPaused;
    }

    void setPaused(const bool p) noexcept
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

    m_off_t bytesConfirmed() const noexcept
    {
        return mBytesConfirmed;
    }

    // hooks (Phase 2/5 will notify Transfer/app + crypto)
    void uploadFailed(const FailReason reason)
    {
        mUploadFailedTime = SteadyTime::ds();
        unsetPool();
        LOG_warn << "WsUpload: file failed, reason=" << static_cast<int>(reason);
    }

    void uploadCompleted(const char* response, const int len)
    {
        mUploadCompletionTime = SteadyTime::ds();
        unsetPool();
        LOG_info << "WsUpload: upload completed (server payload len=" << len << ")";
        (void)response; // Phase 5: use payload (e.g. MAC/fingerprint)
    }

    // debug throughput (server-ACK basis)
    void maybeReportThroughput(const dstime now)
    {
        if (mUploadCompletionTime || mUploadFailedTime)
            return;
        if (mBytesConfirmed == mLastReportedBytesConfirmed)
            return;

        mLastReportedBytesConfirmed = mBytesConfirmed;

        const auto dsElapsed = SteadyTime::difference(now, mUploadStartTime);
        const auto kbps = dsElapsed ? (mBytesConfirmed / dsElapsed * 10 / 1024) : 0;
        LOG_debug << "wsupload: " << (mBytesConfirmed / 1048576) << " MB of " << (mSize / 1048576)
                  << " MB @ ~" << kbps << " KB/s";
    }

    bool isUploading() const noexcept
    {
        LOG_debug << "[WsUploadFile::isUploading] mUploadStartTime= " << mUploadStartTime
                  << " mPaused=" << mPaused << " mAborted=" << mAborted
                  << " mUploadFailedTime=" << mUploadFailedTime << " returning "
                  << (mUploadStartTime != 0 && !mPaused && !mAborted && mUploadFailedTime == 0)
                  << " [this = " << this << "]";
        return mUploadStartTime != 0 && !mPaused && !mAborted && mUploadFailedTime == 0;
    }

public: // accessed by engine
    bool mClientActiveFilesTick{false};
    WsPool* mPool{nullptr};

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
    File* mFile{nullptr}; // non-owning
    std::unique_ptr<FileAccess> mFA{}; // open on first read

    std::uint32_t mFileNo{0};

    // progress/state
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
};

// ---------- WS payloads ----------
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
    m_off_t pos{0};
    int len{0};
    std::uint32_t fileno{0};
};

struct WsBuf
{
    char buf[20 + MB];
    int mSendPos{0};
    int mDataLen{0};

    void add(const char* data, const int len)
    {
        std::memcpy(buf + mDataLen, data, static_cast<size_t>(len));
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
    // Phase 5: integrate SDK crypto (chunkmacs)
    ChunkFingerprintMacUpdate(WsUploadFile&, m_off_t, const char*, int) {}

    void apply(m_off_t /*pos*/) {}
};

// ---------- WebSocket connection (per thread) ----------
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

    // server response (small frames)
    char mInBuf[64];
    int mInPos{0};

    WsPool* mPool{nullptr};
    std::unordered_set<WsConn*>::iterator mConns_it;

    // in-flight chunk log (for resends on drop)
    std::vector<std::pair<WsChunk, ChunkFingerprintMacUpdate>> mChunksInFlight;

    explicit WsConn(WsPool* pool);
    ~WsConn();

    bool connectWS();
    void closeWS();

    void curlSend();
    void curlRecv();

    void onopen();
    void onclose();
    void onmessage(const char* msg, int len);

    bool haveSpace() const
    {
        return !bufferedAmount || !mBufs[0].mDataLen || !mBufs[1].mDataLen;
    }

    bool readyForData() const
    {
        return !mClosing;
    }

    void senddata(const int buf, const char* data, const int len)
    {
        mBufs[buf].add(data, len);
        bufferedAmount += len;
    }

    void sendChunkData(std::uint32_t fileno, m_off_t pos, const char* data, int len);
};

struct WsPoolThread
{
    std::thread t;
    bool terminate{false};
    bool terminated{false};

    explicit WsPoolThread(WsPool* pool); // defined after WsPool

    ~WsPoolThread()
    {
        t.join();
    }
};

// ---------- Pool per size class ----------
struct WsPool
{
    static constexpr std::int32_t CONNRETRYINTERVAL = 5 * 10;
    static constexpr std::int32_t UPLOADTIMEOUT = 180 * 10;

    UploadEngine::Impl* mImpl{nullptr};

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

    explicit WsPool(std::pair<std::string, m_off_t> urlmaxsize,
                    m_off_t minsize,
                    UploadEngine::Impl* impl):
        mImpl(impl),
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

    bool stillActive()
    {
        if (mNumPoolFiles)
            return true; // grace via active files
        if (!mNumberOfConnections)
        {
            checkThreads();
            return !mActiveThreads.empty() || !mExitingThreads.empty();
        }
        setPoolNumConn(0);
        return true;
    }

    bool freshAndSameHostMaxSize(const std::pair<std::string, m_off_t>& urlMaxSize,
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

    bool getWsUploadFile(dstime now, class UploadEngine::Impl& impl);
    WsUploadFile* findFile(std::uint32_t fileno, class UploadEngine::Impl& impl);
    bool nextChunk(WsChunk& chunk, class UploadEngine::Impl& impl);

    void retryChunk(const WsChunk& chunk)
    {
        mToResend.push_back(chunk);
    }

    void retryChunksOnTheWire(WsConn* ws);

    void applyInFlight(const std::uint32_t fileno)
    {
        for (auto& conn: mConns)
        {
            for (int j = static_cast<int>(conn->mChunksInFlight.size()); j--;)
            {
                if (conn->mChunksInFlight[j].first.fileno == fileno)
                {
                    conn->mChunksInFlight.erase(conn->mChunksInFlight.begin() + j);
                    mNumChunksInFlight--;
                }
            }
        }
    }

    bool sendChunk(WsConn* ws, class UploadEngine::Impl& impl);
};

// WsUploadFile pool bindings — must be defined after WsPool is complete
inline void WsUploadFile::unsetPool()
{
    if (mPool)
    {
        mPool->decreaseNumPoolFiles();
        mPool = nullptr;
    }
}

inline void WsUploadFile::setPool(WsPool& p)
{
    mPool = &p;
    assert(mPool);
    mPool->increaseNumPoolFiles();
}

// ---------- Curl response proc (USC) ----------
class CurlResponseProc
{
public:
    virtual ~CurlResponseProc() = default;
    std::string response;

    virtual void curlIO() {}

    virtual bool done(bool /*success*/) = 0;
};

// ---------- Pool manager (USC refresh + cURL multi) ----------
struct WsPoolMgr
{
    static constexpr std::int32_t POOLCONNKEEPALIVE = 60 * 10;
    static constexpr std::int32_t POOLFRESHNESS = 24 * 3600 * 10;
    const std::int32_t SERVERTIMEOUT = 20 * 10;

    CURLM* curlm = nullptr; // Phase 1: private multi (USC only)
    UploadEngine::Impl* mImpl{nullptr}; // backpointer

    std::vector<std::unique_ptr<WsPool>> mPools;
    std::unordered_map<CURL*, CurlResponseProc*> mCurlProcs;

    std::unordered_set<WsUploadFile*> mActiveFiles; // progress reporting
    dstime mLastNetRead{0};

    WsPoolMgr()
    {
        curlm = curl_multi_init();
    }

    ~WsPoolMgr() = default;

    void curlIO(class UploadEngine::Impl& impl); // defined later
    void checkPools(class UploadEngine::Impl& impl); // defined later
    void refreshPools(); // defined later
    bool refreshPoolsResponse(std::string& response); // defined later

    void setCurlResponseProc(CURL* curl, CurlResponseProc* proc)
    {
        mCurlProcs[curl] = proc;
        curl_easy_setopt(
            curl,
            CURLOPT_WRITEFUNCTION,
            +[](void* ptr, size_t /*size*/, size_t nmemb, void* opaque) -> size_t
            {
                auto* p = static_cast<CurlResponseProc*>(opaque);
                p->response.append(static_cast<const char*>(ptr), nmemb);
                return nmemb;
            });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, proc);
        curl_multi_add_handle(curlm, curl);
    }

    void bumpLastNetRead(const dstime now)
    {
        if (SteadyTime::difference(now, mLastNetRead) > 0)
            mLastNetRead = now;
    }

    void bumpAllPools(const dstime now)
    {
        for (auto& p: mPools)
        {
            p->mLastActive = now;
            p->mLastServerResponse = now;
        }
    }
};

// ========== UploadEngine::Impl (queue + mgr + thread) ==========
class UploadEngine::Impl
{
public:
    explicit Impl(MegaClient& c):
        client(c)
    {
        curl_global_init(CURL_GLOBAL_ALL);
        LOG_debug << "[UploadEngine::Impl] constructed";
    }

    ~Impl() = default;

    // Queue mirrors TransferList ordering and priority.
    void enqueue(Transfer& t)
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        if (!nextFileNo)
            nextFileNo = 1;

        auto uf = std::make_unique<WsUploadFile>(client, t, nextFileNo++);
        auto raw = uf.get();

        fileList.push_back(raw);
        files.emplace(&t, std::move(uf));
        inQueue.emplace(raw);
        fileByNo.emplace(raw->fileno(), raw);

        if (fileList.size() == 1)
            nextIt = fileList.begin();
        ++queueVersion;
    }

    void reposition(Transfer& t, Transfer* before)
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        auto it = files.find(&t);
        if (it == files.end())
            return;

        auto* f = it->second.get();
        if (!f || !inQueue.count(f))
            return;

        // remove from current spot
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

        // insert before (if valid), else push back
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
                        ++queueVersion;
                        return;
                    }
                }
            }
        }

        fileList.push_back(f);
        ++queueVersion;
    }

    void pause(Transfer& t)
    {
        withFile(t,
                 [](WsUploadFile& f)
                 {
                     f.setPaused(true);
                 });
    }

    void unpause(Transfer& t)
    {
        withFile(t,
                 [](WsUploadFile& f)
                 {
                     f.setPaused(false);
                 });
    }

    void remove(Transfer& t)
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        auto it = files.find(&t);
        if (it == files.end())
            return;

        auto* f = it->second.get();
        if (!f)
            return;

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
        fileByNo.erase(f->fileno());
        files.erase(it);
        ++queueVersion;
    }

    bool isUploading(Transfer& t) const
    {
        LOG_debug << "[UploadEngine::Impl::isUploading] t=" << t.localfilename
                  << " [this = " << this << "]";
        std::lock_guard<std::mutex> g(uploadMutex);
        if (const auto it = files.find(&t); it != files.end())
            return it->second->isUploading();
        LOG_debug << "[UploadEngine::Impl::isUploading] t=" << t.localfilename
                  << " not found [this = " << this << "]";
        return false;
    }

    void start()
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        if (uploadThread.joinable())
            return;

        poolMgr.mImpl = this;
        poolMgr.refreshPools();

        uploadThread = std::thread(
            [this]
            {
                this->run();
            });
    }

    void kick()
    {
        LOG_debug << "[UploadEngine::Impl] Kick";
        std::lock_guard<std::mutex> g(uploadMutex);
        if (poolMgr.mPools.empty())
            poolMgr.refreshPools();
        for (auto& p: poolMgr.mPools)
            p->checkThreads();
    }

    // Called by pools to pick next file that matches [min,max)
    WsUploadFile* nextEligible(const m_off_t min, const m_off_t max)
    {
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
                        nextIt = std::next(it);
                        f->setUploadStart(currentTime);
                        return f;
                    }
                }
                consecutive = false;
            }
        }
        return nullptr;
    }

    // Manager thread
    void run()
    {
        std::unique_lock<std::mutex> lk(uploadMutex);
        for (;;)
        {
            currentTime = SteadyTime::ds();

            // Pump USC/aux cURL
            poolMgr.curlIO(*this);

            // per-file throughput (server-ack basis)
            for (WsUploadFile* f: poolMgr.mActiveFiles)
                f->maybeReportThroughput(currentTime);
            poolMgr.mActiveFiles.clear();

            if (!paused)
                poolMgr.checkPools(*this);
        }
    }

    // --- state ---
    MegaClient& client;

    std::list<WsUploadFile*> fileList;
    std::unordered_map<Transfer*, std::unique_ptr<WsUploadFile>> files;
    std::unordered_set<WsUploadFile*> inQueue;
    std::unordered_map<std::uint32_t, WsUploadFile*> fileByNo;

    std::list<WsUploadFile*>::iterator nextIt = fileList.begin();
    std::uint32_t queueVersion{0};

    WsPoolMgr poolMgr;

    mutable std::mutex uploadMutex;
    std::thread uploadThread;

    dstime currentTime{0};
    std::atomic<std::uint32_t> nextFileNo{1};
    bool paused{false};

    template<class F>
    void withFile(Transfer& t, F&& fn)
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        auto it = files.find(&t);
        if (it == files.end() || !it->second)
            return;
        fn(*it->second);
    }
};

// ========== WsConn ==========
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
        return false;

    readyState = ReadyState::CONNECTING;

    curl_easy_setopt(curl, CURLOPT_URL, mPool->mUrl.c_str());
    // WebSocket mode
    curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L);

    const CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK)
    {
        readyState = ReadyState::OPEN;
        onopen();
        return true;
    }
    readyState = ReadyState::CLOSED;
    return false;
}

void WsConn::closeWS()
{
    if (readyState == ReadyState::CLOSED)
        return;
    readyState = ReadyState::CLOSED;
    onclose();
}

void WsConn::onopen()
{
    LOG_debug << "WsUpload: Connected to " << mPool->mUrl;
}

void WsConn::onclose()
{
    LOG_debug << "WsUpload: Disconnected from " << mPool->mUrl;
    if (mPool)
        mPool->retryChunksOnTheWire(this);
}

void WsConn::curlSend()
{
    if (readyState != ReadyState::OPEN)
        return;
    while (mBufs[static_cast<unsigned char>(mCurBuf)].sendWS(this, bufferedAmount))
        mCurBuf = !mCurBuf;
}

void WsConn::curlRecv()
{
    if (readyState != ReadyState::OPEN)
        return;

    const struct curl_ws_frame* meta = nullptr;
    size_t recv = 0;

    for (;;)
    {
        const CURLcode res = curl_ws_recv(curl, mInBuf, sizeof(mInBuf), &recv, &meta);
        if (res == CURLE_OK && meta && !meta->bytesleft && recv > 0)
        {
            onmessage(mInBuf, static_cast<int>(recv));
        }
        else
        {
            if (res != CURLE_AGAIN || meta)
            {
                closeWS();
            }
            break;
        }
    }
}

void WsConn::onmessage(const char* msg, const int len)
{
    mPool->mLastActive = mPool->mImpl->currentTime;
    mPool->mLastServerResponse = mPool->mImpl->currentTime;
    mPool->mImpl->poolMgr.bumpLastNetRead(mPool->mImpl->currentTime);

    if (len < 9)
    {
        LOG_warn << "WsUpload: invalid server msg len=" << len;
        closeWS();
        return;
    }

    const auto trailerCrc = *reinterpret_cast<const std::uint32_t*>(
        msg + len - static_cast<int>(sizeof(std::uint32_t)));
    if (trailerCrc != CRC32::crc32b(msg, len - static_cast<int>(sizeof(std::uint32_t))))
    {
        LOG_warn << "WsUpload: inbound CRC failed, byteLength=" << len;
        closeWS();
        return;
    }

#pragma pack(push, 1)

    struct ChunkResponse
    {
        std::uint32_t fileno;
        m_off_t chunkpos;
        char event;
    };

#pragma pack(pop)

    const auto* response = reinterpret_cast<const ChunkResponse*>(msg);
    WsChunk chunk;

    WsUploadFile* uf = mPool->findFile(response->fileno, *mPool->mImpl);
    if (!uf)
        return; // file cancelled or moved

    if (response->event < 4 || response->event == 7)
    {
        if (response->event < 0)
        {
            uf->uploadFailed(FailReason::ServerError);
            return;
        }

        chunk.pos = -1;
        for (int i = 0; i < static_cast<int>(mChunksInFlight.size()); ++i)
        {
            if (mChunksInFlight[i].first.pos == response->chunkpos &&
                mChunksInFlight[i].first.fileno == response->fileno)
            {
                chunk = mChunksInFlight[i].first;
                // Phase 5: mChunksInFlight[i].second.apply(chunk.pos);
                mChunksInFlight.erase(mChunksInFlight.begin() + i);
                mPool->mNumChunksInFlight--;
                break;
            }
        }
        if (chunk.pos < 0)
        {
            LOG_warn << "WsUpload: PROTOCOL - acked chunk not in-flight [pos=" << response->chunkpos
                     << " fileno=" << response->fileno
                     << " type=" << static_cast<int>(response->event) << "]";
            return;
        }
    }

    if (len == 13)
    {
        uf->uploadFailed(FailReason::Unknown);
        return;
    }

    switch (response->event)
    {
        case 1: // chunk ingested (non-final)
            if (chunk.len)
            {
                uf->onServerConfirmedBytes(chunk.len);
                if (uf->bytesConfirmed() >= uf->size())
                {
                    // server confirmed last chunk (or more) rather than completing upload:
                    uf->uploadFailed(FailReason::StateLost);
                    break;
                }
            }
            mPool->mImpl->poolMgr.mActiveFiles.insert(uf);
            break;

        case 7: // final data ingested (server knows file is complete)
            mPool->mImpl->poolMgr.mActiveFiles.insert(uf);
            break;

        case 2: // already on server (after reconnect)
            break;

        case 3: // CRC failed
            LOG_warn << "WsUpload: server CRC fail; rescheduling chunk";
            mPool->retryChunk(chunk);
            break;

        case 4: // upload completed
        {
            mPool->applyInFlight(response->fileno);
            uf->maybeReportThroughput(mPool->mImpl->currentTime);

            const int payLen = static_cast<unsigned char>(msg[13]); // payload length
            uf->uploadCompleted(msg + 14, payLen);
            break;
        }

        case 5: // distress → refresh pools
            LOG_warn << "WsUpload: server requested pool refresh";
            mPool->mImpl->poolMgr.refreshPools();
            break;

        case 6: // throttle (ms) → ds
            mPool->pauseSending(static_cast<dstime>(response->chunkpos / 100 + 1));
            break;

        default:
            LOG_debug << "WsUpload: unknown server opcode=" << static_cast<int>(response->event);
    }
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
                               CRC32::crc32b(reinterpret_cast<const char*>(&header),
                                             static_cast<int>(offsetof(ChunkHeader, crc))));

    char bufIdx = mCurBuf;
    if (mBufs[static_cast<unsigned char>(bufIdx)].mDataLen)
        bufIdx = !bufIdx;

    senddata(bufIdx, reinterpret_cast<const char*>(&header), static_cast<int>(sizeof header));
    senddata(bufIdx, data, len);
}

// ========== WsPoolThread (ctor after WsPool complete) ==========
WsPoolThread::WsPoolThread(WsPool* pool):
    t(&WsPool::poolWorkerThread, pool, this)
{}

// ========== WsBuf ==========
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
            reset();
        return true;
    }
    if (res != CURLE_AGAIN)
    {
        ws->closeWS();
    }
    return false;
}

// ========== WsPool ==========
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
        mUFTQversion = impl.queueVersion;
        mUploadingFile->setPool(*this);
        return true;
    }
    return false;
}

WsUploadFile* WsPool::findFile(const std::uint32_t fileno, UploadEngine::Impl& impl)
{
    if (mUploadingFile && mUploadingFile->fileno() == fileno)
        return (mUploadingFile->mPool == this) ? mUploadingFile : nullptr;

    auto it = impl.fileByNo.find(fileno);
    if (it == impl.fileByNo.end())
        return nullptr;
    return (it->second && it->second->mPool == this) ? it->second : nullptr;
}

bool WsPool::nextChunk(WsChunk& chunk, UploadEngine::Impl& impl)
{
    // queued retry first
    while (!mToResend.empty())
    {
        chunk = mToResend.front();
        mToResend.erase(mToResend.begin());
        LOG_debug << "WsUpload: resending chunk pos=" << chunk.pos << " len=" << chunk.len
                  << " fileno=" << chunk.fileno;
        if (findFile(chunk.fileno, impl))
            return true; // file still valid?
    }

    if (impl.paused)
        return false;

    while (getWsUploadFile(impl.currentTime, impl))
    {
        if (mUploadingFile->headPos() < mUploadingFile->size() || !mUploadingFile->eofSet())
        {
            chunk.fileno = mUploadingFile->fileno();

            if (mUploadingFile->headPos() == mUploadingFile->size())
            {
                // empty EOF chunk
                mUploadingFile->markEOF();
                chunk.pos = mUploadingFile->headPos();
                chunk.len = 0;
            }
            else
            {
                chunk.pos = mUploadingFile->headPos();

                const int advance = g_chunkMap.chunksize(chunk.pos);
                m_off_t newHead = chunk.pos + advance;

                if (newHead > mUploadingFile->size())
                {
                    newHead = mUploadingFile->size();
                    mUploadingFile->markEOF();
                }

                chunk.len = static_cast<int>(newHead - chunk.pos);
                mUploadingFile->advanceHead(advance);
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
    LOG_debug << "WsUpload: WS to " << mUrl << " lost; rescheduling " << ws->mChunksInFlight.size()
              << " in-flight chunks";
    for (auto& p: ws->mChunksInFlight)
    {
        mToResend.push_back(p.first);
    }
    mNumChunksInFlight -= static_cast<int>(ws->mChunksInFlight.size());
    ws->mChunksInFlight.clear();
}

bool WsPool::sendChunk(WsConn* ws, UploadEngine::Impl& impl)
{
    if (ws->readyState != WsConn::ReadyState::OPEN || !ws->haveSpace())
        return false;

    WsChunk chunk;
    if (!nextChunk(chunk, impl))
        return false;

    static thread_local std::unique_ptr<char[]> tlsBuf;
    if (!tlsBuf)
        tlsBuf.reset(new char[MB]);

    WsUploadFile* uf = findFile(chunk.fileno, impl);
    if (uf && !uf->aborted() &&
        (chunk.len == 0 || uf->readData(tlsBuf.get(), chunk.pos, chunk.len, impl.uploadMutex)))
    {
        ws->mChunksInFlight.emplace_back(
            chunk,
            ChunkFingerprintMacUpdate(*uf, chunk.pos, tlsBuf.get(), chunk.len));
        ws->sendChunkData(chunk.fileno, chunk.pos, tlsBuf.get(), chunk.len);

        ++mNumChunksInFlight;
        return true;
    }
    return false;
}

void WsPool::poolWorkerThread(WsPoolThread* th)
{
    int retryCount{0};
    std::uint32_t lastQueueVersion = mImpl->queueVersion;
    WsConn ws(this);

    std::unique_lock<std::mutex> lk(mImpl->uploadMutex);
    while (!th->terminate)
    {
        if (ws.readyState == WsConn::ReadyState::CLOSED)
        {
            // unlock around blocking connect
            mImpl->uploadMutex.unlock();
            const bool ok = ws.connectWS();
            mImpl->uploadMutex.lock();

            if (!ok)
            {
                if (!retryCount++)
                    continue;

                if (!mRetiring && lastQueueVersion != mImpl->queueVersion)
                    mImpl->poolMgr.refreshPools();

                mImpl->uploadMutex.unlock();
                SteadyTime::sleep_ds(CONNRETRYINTERVAL);
                mImpl->uploadMutex.lock();
                continue;
            }
            retryCount = 0;
        }

        lastQueueVersion = mImpl->queueVersion;

        // recv server frames
        mImpl->uploadMutex.unlock();
        ws.curlRecv();
        mImpl->uploadMutex.lock();

        // server throttle?
        if (throttledByServer())
        {
            mImpl->uploadMutex.unlock();
            SteadyTime::sleep_ds(1);
            mImpl->uploadMutex.lock();
            continue;
        }

        // flush send buffers
        mImpl->uploadMutex.unlock();
        ws.curlSend();
        mImpl->uploadMutex.lock();

        if (!ws.haveSpace())
        {
            mImpl->uploadMutex.unlock();
            SteadyTime::sleep_ds(2);
            mImpl->uploadMutex.lock();
            continue;
        }
        if (!ws.readyForData())
        {
            mImpl->uploadMutex.unlock();
            SteadyTime::sleep_ds(2);
            mImpl->uploadMutex.lock();
            continue;
        }

        // fetch & enqueue next chunk (unlocks around disk I/O internally)
        if (!sendChunk(&ws, *mImpl))
        {
            mImpl->uploadMutex.unlock();
            SteadyTime::sleep_ds(10);
            mImpl->uploadMutex.lock();
            continue;
        }
    }
    th->terminated = true;
}

void WsPool::checkThreads()
{
    while (mActiveThreads.size() < mNumberOfConnections)
    {
        auto thr = std::make_unique<WsPoolThread>(this);
        mActiveThreads.push_back(std::move(thr));
    }
    while (mActiveThreads.size() > mNumberOfConnections)
    {
        mActiveThreads.back()->terminate = true;
        auto thr = std::move(mActiveThreads.back());
        mActiveThreads.pop_back();
        mExitingThreads.push_back(std::move(thr));
    }

    for (int i = static_cast<int>(mExitingThreads.size()); i--;)
    {
        if (mExitingThreads[i]->terminated)
            mExitingThreads.erase(mExitingThreads.begin() + i);
    }
}

// ========== WsPoolMgr ==========
void WsPoolMgr::curlIO(UploadEngine::Impl& impl)
{
    int still_running = 0, msgs_left = 0;
    curl_multi_perform(curlm, &still_running);

    for (auto& kv: mCurlProcs)
        if (kv.second)
            kv.second->curlIO();

    CURLMsg* msg;
    while ((msg = curl_multi_info_read(curlm, &msgs_left)))
    {
        if (msg->msg == CURLMSG_DONE)
        {
            CURL* curl = msg->easy_handle;
            auto it = mCurlProcs.find(curl);
            if (it != mCurlProcs.end())
            {
                CurlResponseProc* proc = it->second;
                const bool ok = (msg->data.result == CURLE_OK);
                if (proc && proc->done(ok))
                {
                    curl_multi_remove_handle(curlm, curl);
                    mCurlProcs.erase(it);
                    delete proc;
                }
            }
        }
    }

    impl.uploadMutex.unlock();
    (void)curl_multi_poll(curlm, nullptr, 0, 500, nullptr);
    impl.uploadMutex.lock();
}

void WsPoolMgr::checkPools(UploadEngine::Impl& impl)
{
    // update last net read from pools
    for (int i = static_cast<int>(mPools.size()); i--;)
        if (SteadyTime::difference(mPools[i]->mLastServerResponse, mLastNetRead) > 0)
            mLastNetRead = mPools[i]->mLastServerResponse;

    // close idle retiring pools
    for (int i = static_cast<int>(mPools.size()); i-- && mPools[i]->mRetiring;)
        if (!mPools[i]->stillActive())
        {
            LOG_info << "WsUpload: closing idle pool " << i << " (" << mPools[i]->mUrl << ")";
            mPools.erase(mPools.begin() + i);
        }

    // trim connections / refresh stale or stalled pools
    for (int i = static_cast<int>(mPools.size()) - 1; i >= 0; --i)
    {
        if (mPools[i]->mNumberOfConnections > 1 &&
            SteadyTime::difference(impl.currentTime, mPools[i]->mLastActive) > POOLCONNKEEPALIVE)
        {
            mPools[i]->mNumberOfConnections = 1;
            mPools[i]->checkThreads();
        }

        if (SteadyTime::difference(impl.currentTime, mPools[i]->mPoolCreationTime) > POOLFRESHNESS)
            refreshPools();

        if ((mPools[i]->mUploadingFile || mPools[i]->mNumChunksInFlight ||
             !mPools[i]->mToResend.empty()) &&
            SteadyTime::difference(impl.currentTime, mPools[i]->mLastActive) > SERVERTIMEOUT)
            refreshPools();
    }

    // throughput display tick (server-acked)
    for (auto* uf: mActiveFiles)
        uf->mClientActiveFilesTick = false;
    mActiveFiles.clear();
}

// USC: staging (no session) — Phase 4 will move to SDK Command channel
struct CurlResponseProcRefreshPools: CurlResponseProc
{
    WsPoolMgr* mgr{nullptr};

    explicit CurlResponseProcRefreshPools(WsPoolMgr* m):
        mgr(m)
    {}

    bool done(const bool success) override
    {
        if (!success || !mgr->refreshPoolsResponse(response))
        {
            LOG_warn << "USC refresh failed";
        }
        return true;
    }
};

void WsPoolMgr::refreshPools()
{
    if (!mImpl)
        return;

    static const char* kStagingCS = "https://staging.api.mega.co.nz/cs";

    CURL* curl = curl_easy_init();
    if (!curl)
        return;

    curl_easy_setopt(curl, CURLOPT_URL, kStagingCS);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "[{\"a\":\"usc\"}]");

    setCurlResponseProc(curl, new CurlResponseProcRefreshPools(this));
}

bool WsPoolMgr::refreshPoolsResponse(std::string& response)
{
    std::vector<std::pair<std::string, m_off_t>> apiSizeClasses;

    const char* p = response.c_str();
    const char* q = nullptr;
    std::string url;
    bool ok{false};

    LOG_debug << "USC response: " << response;

    if (!std::memcmp(p, "[[[\"", 3))
    {
        for (;;)
        {
            p += 4;
            if ((q = std::strchr(p, '"')))
            {
                url = "wss://";
                url.append(p, static_cast<size_t>(q - p));
                url.append("/");

                p = q + 3;
                if ((q = std::strchr(p, '"')))
                    url.append(p, static_cast<size_t>(q - p));

                if (q[1] == ',')
                {
                    apiSizeClasses.emplace_back(url, static_cast<m_off_t>(std::atoll(q + 2)));
                    if ((p = std::strchr(q, ']')) && !std::memcmp(p, "],[\"", 4))
                        continue;
                }
                else
                {
                    apiSizeClasses.emplace_back(url, m_off_t(0));
                    ok = true;
                    break;
                }
            }
            LOG_warn << "Invalid USC response: " << response;
            break;
        }

        if (ok)
        {
            const dstime oldest = SteadyTime::ds() - POOLFRESHNESS;

            // mark existing not-retiring pools retiring
            for (int i = 0; i < static_cast<int>(mPools.size()) && !mPools[i]->mRetiring; ++i)
                mPools[i]->mRetiring = true;

            for (int i = 0; i < static_cast<int>(apiSizeClasses.size()); ++i)
            {
                int j;
                for (j = static_cast<int>(mPools.size()); j--;)
                {
                    if (mPools[j]->freshAndSameHostMaxSize(apiSizeClasses[i], oldest))
                    {
                        if (j != i)
                            std::swap(mPools[i], mPools[j]);
                        mPools[i]->mRetiring = false;
                        break;
                    }
                }
                if (j < 0)
                {
                    mPools.insert(mPools.begin() + i,
                                  std::make_unique<WsPool>(apiSizeClasses[i],
                                                           i ? apiSizeClasses[i - 1].second : 0,
                                                           mImpl));
                }
            }
            bumpAllPools(mImpl->currentTime);
            LOG_info << "WsUpload: refreshed pools (" << apiSizeClasses.size() << " size classes)";
        }
    }
    return ok;
}

// ========== UploadEngine (public facade) ==========
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

bool UploadEngine::isUploading(Transfer& t) const
{
    return pImpl->isUploading(t);
}

void UploadEngine::kick()
{
    pImpl->kick();
}

// Simple feature gate for now (could later inspect client caps/settings)
bool wsEnabled(const MegaClient&)
{
    return true;
}

} // namespace ws
} // namespace mega
