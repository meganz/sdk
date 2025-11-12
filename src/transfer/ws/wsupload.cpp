#include "mega/wsupload.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef> // offsetof
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
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
#include "meganet.h"

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

    Transfer& transfer() const noexcept
    {
        return mTransfer;
    }

    bool inPool() const noexcept
    {
        return mPool != nullptr;
    }

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
    bool hasPool() const;

    bool ensureOpen(std::mutex& engineMutex)
    {
        if (mFAOpened)
            return true;
        engineMutex.unlock();
        const auto ok = mFA->openf(FSLogging::logOnError); // does sysopen() if needed
        engineMutex.lock();
        mFAOpened = ok;
        if (!ok)
            markFailed();
        return ok;
    }

    void closeFA()
    {
        if (mFA && mFAOpened)
        {
            mFA->closef();
            mFAOpened = false;
        }
        mFA = nullptr;
    }

    // I/O (open on first read) — engineMutex is the single engine mutex
    bool readData(char* buf, const m_off_t pos, const int len, std::mutex& engineMutex)
    {
        LOG_debug << "[WsUploadFile::readData] BEGIN [buf=" << (void*)buf << "] [pos=" << pos
                  << "] [len=" << len << "] [this = " << this
                  << "] [thread_id=" << std::this_thread::get_id() << "]";
        // open on first use
        if (!mFA)
        {
            LOG_debug << "[WsUploadFile::readData] !mFA -> newfileaccess for localname="
                      << mFile->getLocalname() << " [this = " << this
                      << "] [thread_id=" << std::this_thread::get_id() << "]";
            engineMutex.unlock();
            auto fa = mClient.fsaccess->newfileaccess();
            const bool okOpen =
                fa->fopen(mFile->getLocalname(), true, false, FSLogging::logOnError);
            LOG_debug << "[WsUploadFile::readData] okOpen=" << okOpen
                      << " [localname=" << mFile->getLocalname() << "] [this = " << this
                      << "] [thread_id=" << std::this_thread::get_id() << "]";
            engineMutex.lock();

            if (!okOpen)
            {
                LOG_debug << "[WsUploadFile::readData] !okOpen -> markFailed() and return false "
                             "[localname="
                          << mFile->getLocalname() << "] [this = " << this
                          << "] [thread_id=" << std::this_thread::get_id() << "]";
                markFailed();
                return false;
            }
            mFA = std::move(fa);
            mSize = mFA->size;
            mMtime = mFA->mtime;
            LOG_debug << "[WsUploadFile::readData] mFA=" << (void*)mFA.get() << " mSize=" << mSize
                      << " mMtime=" << mMtime << " [localname=" << mFile->getLocalname()
                      << "] [this = " << this << "] [thread_id=" << std::this_thread::get_id()
                      << "]";
        }

        if (!ensureOpen(engineMutex))
        {
            LOG_debug << "[WsUploadFile::readData] !ensureOpen -> return false [localname="
                      << mFile->getLocalname() << "] [this = " << this
                      << "] [thread_id=" << std::this_thread::get_id() << "]";
            return false;
        }

        engineMutex.unlock();
        auto okRead = false;
        {
            std::lock_guard<std::mutex> io(mReadMutex);
            // Some platforms declare frawread(pos) as unsigned — cast explicitly to avoid
            // -Wconversion
            okRead = mFA->frawread(reinterpret_cast<byte*>(buf),
                                   static_cast<unsigned>(len),
                                   pos,
                                   /*caller_opened=*/true,
                                   FSLogging::logOnError);
        }
        LOG_debug << "[WsUploadFile::readData] okRead=" << okRead
                  << " [localname=" << mFile->getLocalname() << "] [this = " << this
                  << "] [thread_id=" << std::this_thread::get_id() << "]";
        engineMutex.lock();

        if (!okRead)
        {
            // optional one-shot recover: reopen + retry once
            LOG_debug << "[WsUploadFile::readData] !okRead -> reopen + retry once [localname="
                      << mFile->getLocalname() << "] [this = " << this
                      << "] [thread_id=" << std::this_thread::get_id() << "]";
            mFA->closef();
            const bool reok = mFA->openf(FSLogging::logOnError);
            LOG_debug << "[WsUploadFile::readData] reok=" << reok
                      << " [localname=" << mFile->getLocalname() << "] [this = " << this
                      << "] [thread_id=" << std::this_thread::get_id() << "]";
            mFAOpened = reok;
            if (reok)
            {
                LOG_debug << "[WsUploadFile::readData] reok -> reread [localname="
                          << mFile->getLocalname() << "] [this = " << this
                          << "] [thread_id=" << std::this_thread::get_id() << "]";
                engineMutex.unlock();
                {
                    std::lock_guard<std::mutex> io(mReadMutex);
                    okRead = mFA->frawread(reinterpret_cast<byte*>(buf),
                                           static_cast<unsigned>(len),
                                           pos,
                                           /*caller_opened=*/true,
                                           FSLogging::logOnError);
                }
                engineMutex.lock();
            }
        }

        if (!okRead)
        {
            LOG_debug << "[WsUploadFile::readData] !okRead -> markFailed() [localname="
                      << mFile->getLocalname() << "] [this = " << this
                      << "] [thread_id=" << std::this_thread::get_id() << "]";
            closeFA();
            markFailed();
        }
        LOG_debug << "[WsUploadFile::readData] return okRead=" << okRead
                  << " [localname=" << mFile->getLocalname() << "] [this = " << this
                  << "] [thread_id=" << std::this_thread::get_id() << "]";
        return okRead;
    }

    // server-confirmed progression
    void onServerConfirmedBytes(const m_off_t bytes)
    {
        mBytesConfirmed += bytes;
        mClientActiveFilesTick = true;
    }

    // progress coalescing (server-confirmed): report if delta or time threshold hit
    bool progressReportDue(const dstime now)
    {
        constexpr m_off_t kStepBytes = 64 * 1024; // 64 KiB
        constexpr dstime kStepTime = 5; // 0.5s (ds)
        const bool bytesDue = (mBytesConfirmed - mLastProgressReportBytes) >= kStepBytes;
        const bool timeDue =
            SteadyTime::difference(now, mLastProgressReportDs) >= static_cast<int32_t>(kStepTime);
        if (bytesDue || timeDue)
        {
            mLastProgressReportBytes = mBytesConfirmed;
            mLastProgressReportDs = now ? now : SteadyTime::ds();
            return true;
        }
        return false;
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
        LOG_debug << "[WsUploadFile::fileno] return mFileNo=" << mFileNo << " [this = " << this
                  << "]";
        return mFileNo;
    }

    bool paused() const noexcept
    {
        LOG_debug << "[WsUploadFile::paused] return mPaused=" << mPaused << " [this = " << this
                  << "]";
        return mPaused;
    }

    void setPaused(const bool p) noexcept
    {
        LOG_debug << "[WsUploadFile::setPaused] p=" << p << " [this = " << this << "]";
        mPaused = p;
        closeFA();
    }

    bool aborted() const noexcept
    {
        LOG_debug << "[WsUploadFile::aborted] return mAborted=" << mAborted << " [this = " << this
                  << "]";
        return mAborted;
    }

    void cancel() noexcept
    {
        LOG_debug << "[WsUploadFile::cancel] call [this = " << this << "]";
        mAborted = true;
        closeFA();
    }

    void setUploadStart(const dstime t) noexcept
    {
        LOG_debug << "[WsUploadFile::setUploadStart] t=" << t << " [this = " << this << "]";
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
        closeFA();
        LOG_warn << "[WsUploadFile::uploadFailed] file failed, reason=" << static_cast<int>(reason)
                 << " [this = " << this << "]";
    }

    void uploadCompleted(const char* response, const int len)
    {
        mUploadCompletionTime = SteadyTime::ds();
        unsetPool();
        closeFA();
        const auto dsElapsed = SteadyTime::difference(mUploadCompletionTime, mUploadStartTime);
        const auto kbps = dsElapsed ? (mBytesConfirmed / dsElapsed * 10 / 1024) : 0;
        LOG_info << "[WsUploadFile::uploadCompleted] upload completed (server payload len=" << len
                 << ") Progress: " << mBytesConfirmed << " of " << mSize << " bytes @ ~" << kbps
                 << " KB/s [this = " << this << "]";
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
        LOG_debug << "[WsUploadFile::maybeReportThroughput] " << mBytesConfirmed << " of " << mSize
                  << " bytes @ ~" << kbps << " KB/s [this = " << this << "]";
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
    bool mFAOpened{false};
    std::mutex mReadMutex; // needed for Android because of lseek64+read

    std::uint32_t mFileNo{0};

    // progress/state
    m_off_t mSize{0};
    m_off_t mHeadPos{0};
    m_off_t mBytesConfirmed{0};
    m_off_t mLastReportedBytesConfirmed{0};
    m_off_t mLastProgressReportBytes{0};
    dstime mLastProgressReportDs{0};
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
        LOG_debug << "[WsConn::senddata] BEGIN [buf=" << buf << "] [data=" << (void*)data
                  << "] [len=" << len << "] [bufferedAmount=" << bufferedAmount
                  << "] [this = " << this << "]";
        mBufs[buf].add(data, len);
        bufferedAmount += len;
        LOG_debug << "[WsConn::senddata] END [bufferedAmount=" << bufferedAmount
                  << "] [this = " << this << "]";
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
        LOG_debug << "[WsPoolThread::~WsPoolThread] BEGIN -> t.join() [this = " << this << "]";
        t.join();
        LOG_debug << "[WsPoolThread::~WsPoolThread] END [this = " << this << "]";
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

    unsigned char mNumberOfConnections{3};
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
        LOG_debug << "[WsPool] constructed [mMinFileSize=" << mMinFileSize
                  << " mMaxFileSize=" << mMaxFileSize << "] [this = " << this << "]";
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
    LOG_debug << "[WsUploadFile::unsetPool] mPool=" << (void*)mPool << " [this = " << this << "]";
    if (mPool)
    {
        mPool->decreaseNumPoolFiles();
        mPool = nullptr;
    }
}

inline void WsUploadFile::setPool(WsPool& p)
{
    LOG_debug << "[WsUploadFile::setPool] p=" << (void*)&p << " [this = " << this << "]";
    assert(!mPool);
    mPool = &p;
    assert(mPool);
    mPool->increaseNumPoolFiles();
    LOG_debug << "[WsUploadFile::setPool] END [this = " << this << "]";
}

inline bool WsUploadFile::hasPool() const
{
    return mPool != nullptr;
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
        LOG_debug << "[WsPoolMgr::setCurlResponseProc] call [curl=" << (void*)curl
                  << "] [proc=" << (void*)proc << "] [this = " << this << "]";
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

    ~Impl()
    {
        LOG_debug << "[UploadEngine::Impl::~Impl] BEGIN";
        {
            // Stop everything under the same mutex the workers use
            std::lock_guard<std::mutex> g(uploadMutex);
            paused = true;
            uploadThreadRunning = false; // lets run() break out

            // Ask pool worker threads to exit promptly
            for (auto& p: poolMgr.mPools)
            {
                p->mNumberOfConnections = 0; // avoids new workers
                for (auto& th: p->mActiveThreads)
                    th->terminate = true;
                for (auto& th: p->mExitingThreads)
                    th->terminate = true;
            }
        }

        // Manager thread may be waiting up to 500ms in curl_multi_poll; then it exits.
        if (uploadThread.joinable())
            uploadThread.join();

        LOG_debug << "[UploadEngine::Impl::~Impl] END";
    }

    // Queue mirrors TransferList ordering and priority.
    void enqueue(Transfer& t)
    {
        LOG_debug << "[UploadEngine::Impl::enqueue] t=" << t.localfilename
                  << " files.size=" << files.size() << " [this = " << this << "]";
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

    bool isUploading(const Transfer& t) const
    {
        std::lock_guard<std::mutex> g(uploadMutex);
        auto it = files.find(const_cast<Transfer*>(&t));
        if (it == files.end() || !it->second)
            return false;
        const WsUploadFile* f = it->second.get();
        return f->inPool();
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
        LOG_debug << "[UploadEngine::Impl::kick] BEGIN [this = " << this << "]";
        std::lock_guard<std::mutex> g(uploadMutex);
        // if (poolMgr.mPools.empty())
        //     poolMgr.refreshPools();
        for (auto& p: poolMgr.mPools)
        {
            LOG_debug << "[UploadEngine::Impl::kick] pool(" << (void*)p.get()
                      << ") checkThreads() [this = " << this << "]";
            p->checkThreads();
        }
        LOG_debug << "[UploadEngine::Impl::kick] END [numPools=" << poolMgr.mPools.size()
                  << "] [this = " << this << "]";
    }

    // Called by pools to pick next file that matches [min,max)
    WsUploadFile* nextEligible(const m_off_t min, const m_off_t max)
    {
        LOG_debug << "[UploadEngine::Impl::nextEligible] BEGIN [fileList.size=" << fileList.size()
                  << "] [this = " << this << "]";

        cycleNextIt();

        bool consecutive = true;
        for (auto it = nextIt; it != fileList.end(); ++it)
        {
            WsUploadFile* f = *it;
            if (!f)
            {
                LOG_debug << "[UploadEngine::Impl::nextEligible] !f -> continue [this = " << this
                          << "]";
                continue;
            }

            if (!f->hasPool() && !f->paused() && f->continuingUpload(currentTime))
            {
                LOG_debug << "[UploadEngine::Impl::nextEligible] !f->hasPool() && "
                             "!f->paused() && f->continuingUpload -> process file"
                          << " [this = " << this << "]";
                if (f->size() >= min && (!max || f->size() < max))
                {
                    LOG_debug << "[UploadEngine::Impl::nextEligible] f->size(=" << f->size()
                              << ") >= min(=" << min << ") && (!max(=" << max
                              << ") || f->size(=" << f->size() << ") < max(=" << max
                              << ")) -> process file: setUploadStart(currentTime) and return file "
                                 "[consecutive="
                              << consecutive << "]"
                              << " [this = " << this << "]";
                    if (consecutive)
                    {
                        LOG_debug << "[UploadEngine::Impl::nextEligible] consecutive=true -> "
                                     "advanceNextItFrom(it) before setting upload start [this = "
                                  << this << "]";
                        advanceNextItFrom(it);
                    }
                    f->setUploadStart(currentTime);
                    return f;
                }
                LOG_debug << "[UploadEngine::Impl::nextEligible] !f->size(=" << f->size()
                          << ") >= min(=" << min << ") && (!max(=" << max
                          << ") || f->size(=" << f->size() << ") < max(=" << max
                          << ")) -> no process file, set consecutive=false and continue [this = "
                          << this << "]";

                consecutive = false;
            }
            else
            {
                LOG_debug << "[UploadEngine::Impl::nextEligible] f->hasPool() || "
                             "f->paused() || "
                             "!f->continuingUpload -> continue [this = "
                          << this << "]";
            }
        }
        LOG_debug << "[UploadEngine::Impl::nextEligible] END - return nullptr [this = " << this
                  << "]";
        return nullptr;
    }

    // Manager thread
    void run()
    {
        uploadThreadRunning = true;
        std::unique_lock<std::mutex> lk(uploadMutex);
        while (uploadThreadRunning)
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
        uploadThreadRunning = false;
    }

    // --- state ---
    MegaClient& client;

    using ListWsUploadFile = std::list<WsUploadFile*>;
    ListWsUploadFile fileList;
    std::unordered_map<Transfer*, std::unique_ptr<WsUploadFile>> files;
    std::unordered_set<WsUploadFile*> inQueue;
    std::unordered_map<std::uint32_t, WsUploadFile*> fileByNo;

    ListWsUploadFile::iterator nextIt = fileList.begin();
    std::uint32_t queueVersion{0};
    UploadEngine::Callbacks mCb{};

    WsPoolMgr poolMgr;

    mutable std::mutex uploadMutex;
    std::thread uploadThread;
    std::atomic<bool> uploadThreadRunning{false};

    dstime currentTime{0};
    std::atomic<std::uint32_t> nextFileNo{1};
    bool paused{false};

private:
    template<class F>
    void withFile(Transfer& t, F&& fn)
    {
        LOG_debug << "[UploadEngine::Impl::withFile] BEGIN [t=" << t.localfilename
                  << "] [this = " << this << "]";
        std::lock_guard<std::mutex> g(uploadMutex);
        auto it = files.find(&t);
        if (it == files.end() || !it->second)
            return;
        fn(*it->second);
        LOG_debug << "[UploadEngine::Impl::withFile] END [this = " << this << "]";
    }

    void cycleNextIt() noexcept
    {
        if (!fileList.empty() && nextIt == fileList.end())
            nextIt = fileList.begin();
    }

    void advanceNextItFrom(ListWsUploadFile::iterator it) noexcept
    {
        nextIt = std::next(it);
        cycleNextIt();
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

/*
bool WsConn::connectWS()
{
    LOG_debug << "[WsConn::connectWS] BEGIN [this = " << this << "]";
    if (curl)
    {
        LOG_debug << "[WsConn::connectWS] curl already exists, cleanup [this = " << this << "]";
        curl_easy_cleanup(curl);
    }
    curl = curl_easy_init();
    if (!curl)
    {
        LOG_debug << "[WsConn::connectWS] curl_easy_init failed, return false [this = " << this
                  << "]";
        return false;
    }

    // Share CurlHttpIO settings but don't attach to its multi
    if (auto* cio = dynamic_cast<CurlHttpIO*>(mPool->mImpl->client.httpio))
    {
        LOG_debug << "[WsConn::connectWS] configureWsEasy [curl=" << (void*)curl << "] [this = " << this << "]";
        cio->configureWsEasy(curl, false);
    }
    else
    {
        LOG_warn << "[WsConn::connectWS] dynamic_cast<CurlHttpIO*>(mPool->mImpl->client.httpio) failed, return false [curl=" << (void*)curl << "] [this = " << this << "]";
        return false;
    }

    readyState = ReadyState::CONNECTING;

    char err[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, err);

    curl_easy_setopt(curl, CURLOPT_URL, mPool->mUrl.c_str());
    // WebSocket mode
    curl_easy_setopt(curl, CURLOPT_CONNECT_ONLY, 2L);

    const CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK)
    {
        LOG_debug << "[WsConn::connectWS] curl_easy_perform success, set readyState=OPEN and call "
                     "onopen() -> return true [this = "
                  << this << "]";
        readyState = ReadyState::OPEN;
        onopen();
        return true;
    }
    LOG_debug << "[WsConn::connectWS] curl_easy_perform failed, set readyState=CLOSED and return "
                 "false [res="
              << res << "] [strError=" << curl_easy_strerror(res) << "] [err=" << err
              << "] [this = " << this << "]";
    readyState = ReadyState::CLOSED;
    return false;
}
*/

bool WsConn::connectWS()
{
    LOG_debug << "[WsConn::connectWS] BEGIN [this = " << this << "]";
    if (curl)
    {
        LOG_debug << "[WsConn::connectWS] curl already exists, cleanup [this = " << this << "]";
        curl_easy_cleanup(curl);
        curl = nullptr;
    }
    readyState = ReadyState::CONNECTING;

    struct Baton
    {
        std::mutex m;
        std::condition_variable cv;
        CURL* easy = nullptr;
        bool done = false;
    } baton;

    const std::string url = mPool->mUrl;

    mPool->mImpl->client.wsPostToClientThread(
        [this, &baton, url](MegaClient& client, TransferDbCommitter&)
        {
            LOG_debug << "[WsConn::connectWS] [client.wsPostToClientThread] BEGIN [this = " << this
                      << "]";
            auto* cio = dynamic_cast<CurlHttpIO*>(client.httpio);
            std::string err;
            CURL* e = cio ? cio->wsHandshake(url, /*timeoutMs*/ 15000, &err) : nullptr;

            {
                std::lock_guard<std::mutex> g(baton.m);
                baton.easy = e;
                baton.done = true;
                if (cio)
                {
                    if (!err.empty())
                    {
                        LOG_debug << "[WsConn::connectWS] [client.wsPostToClientThread] "
                                     "cio->wsHandshake failed, err="
                                  << err << ", set baton.easy=" << (void*)e << " [this = " << this
                                  << "]";
                    }
                    else
                    {
                        LOG_debug << "[WsConn::connectWS] [client.wsPostToClientThread] "
                                     "cio->wsHandshake success, set baton.easy="
                                  << (void*)e << " [this = " << this << "]";
                    }
                }
                else
                {
                    LOG_debug << "[WsConn::connectWS] [client.wsPostToClientThread] "
                                 "dynamic_cast<CurlHttpIO*>(client.httpio) "
                                 "failed, set baton.easy=nullptr [this = "
                              << this << "]";
                }
            }
            baton.cv.notify_one();
            LOG_debug << "[WsConn::connectWS] [client.wsPostToClientThread] END [this = " << this
                      << "]";
        });

    // Wait here on the worker thread
    std::unique_lock<std::mutex> lk(baton.m);
    if (!baton.cv.wait_for(lk,
                           std::chrono::seconds(20),
                           [&]
                           {
                               return baton.done;
                           }))
    {
        LOG_debug << "[WsConn::connectWS] !baton.cv.wait_for -> readyState=CLOSED and return false "
                     "[this = "
                  << this << "]";
        readyState = ReadyState::CLOSED;
        return false;
    }

    if (!baton.easy)
    {
        LOG_debug
            << "[WsConn::connectWS] !baton.easy -> readyState=CLOSED and return false [this = "
            << this << "]";
        readyState = ReadyState::CLOSED;
        return false;
    }

    curl = baton.easy; // worker thread exclusively owns the handle now
    readyState = ReadyState::OPEN;
    onopen(); // your existing callback
    LOG_debug << "[WsConn::connectWS] END -> success, return true [this = " << this << "]";
    return true;
}

void WsConn::closeWS()
{
    LOG_debug << "[WsConn::closeWS] BEGIN [this = " << this << "]";
    if (readyState == ReadyState::CLOSED)
    {
        LOG_debug << "[WsConn::closeWS] readyState=CLOSED, return [this = " << this << "]";
        return;
    }
    readyState = ReadyState::CLOSED;
    onclose();
    LOG_debug << "[WsConn::closeWS] END [this = " << this << "]";
}

void WsConn::onopen()
{
    LOG_debug << "[WsConn::onopen] Connected to " << mPool->mUrl;
}

void WsConn::onclose()
{
    LOG_debug << "[WsConn::onclose] BEGIN [Disconnected from " << mPool->mUrl
              << "] [this = " << this << "]";
    if (mPool)
    {
        LOG_debug << "[WsConn::onclose] mPool->retryChunksOnTheWire(this) [this = " << this << "]";
        mPool->retryChunksOnTheWire(this);
    }
    LOG_debug << "[WsConn::onclose] END [this = " << this << "]";
}

void WsConn::curlSend()
{
    LOG_debug << "[WsConn::curlSend] BEGIN [readyState=" << static_cast<int>(readyState)
              << "] [this = " << this << "]";
    if (readyState != ReadyState::OPEN)
    {
        LOG_debug << "[WsConn::curlSend] readyState != ReadyState::OPEN, return [this = " << this
                  << "]";
        return;
    }
    while (mBufs[static_cast<unsigned char>(mCurBuf)].sendWS(this, bufferedAmount))
        mCurBuf = !mCurBuf;
    LOG_debug << "[WsConn::curlSend] END [this = " << this << "]";
}

void WsConn::curlRecv()
{
    LOG_debug << "[WsConn::curlRecv] BEGIN [readyState=" << static_cast<int>(readyState)
              << "] [this = " << this << "]";
    if (readyState != ReadyState::OPEN)
    {
        LOG_debug << "[WsConn::curlRecv] readyState != ReadyState::OPEN, return [this = " << this
                  << "]";
        return;
    }

    const struct curl_ws_frame* meta = nullptr;
    size_t recv = 0;

    for (;;)
    {
        LOG_debug << "[WsConn::curlRecv] curl_ws_recv(curl, mInBuf(=" << (void*)mInBuf
                  << "), sizeof(mInBuf)(=" << sizeof(mInBuf) << "), &recv, &meta) [this = " << this
                  << "]";
        const CURLcode res = curl_ws_recv(curl, mInBuf, sizeof(mInBuf), &recv, &meta);
        if (res == CURLE_OK && meta && !meta->bytesleft && recv > 0)
        {
            LOG_debug << "[WsConn::curlRecv] res == CURLE_OK && meta && !meta->bytesleft && recv(="
                      << recv
                      << ") > 0 -> onmessage(mInBuf, static_cast<int>(recv)) [this = " << this
                      << "]";
            onmessage(mInBuf, static_cast<int>(recv));
        }
        else
        {
            if (res != CURLE_AGAIN || meta)
            {
                LOG_debug << "[WsConn::curlRecv] res(=" << res
                          << ") != CURLE_AGAIN || meta -> closeWS() [this = " << this << "]";
                closeWS();
            }
            LOG_debug << "[WsConn::curlRecv] res(=" << res
                      << ") != CURLE_OK || !meta || meta->bytesleft(="
                      << (meta ? meta->bytesleft : 0) << ") > 0 || recv(=" << recv
                      << ") <= 0 -> break [this = " << this << "]";
            break;
        }
    }
    LOG_debug << "[WsConn::curlRecv] END [this = " << this << "]";
}

void WsConn::onmessage(const char* msg, const int len)
{
    LOG_debug << "[WsConn::onmessage] BEGIN [len=" << len << "] [this = " << this << "]";
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
    LOG_debug << "[WsConn::onmessage] response->fileno=" << response->fileno
              << " response->chunkpos=" << response->chunkpos
              << " response->event=" << static_cast<int>(response->event) << " [this = " << this
              << "]";
    WsChunk chunk;

    WsUploadFile* uf = mPool->findFile(response->fileno, *mPool->mImpl);
    if (!uf)
    {
        LOG_debug << "[WsConn::onmessage] !uf -> return [this = " << this << "]";
        return; // file cancelled or moved
    }

    if (response->event < 4 || response->event == 7)
    {
        if (response->event < 0)
        {
            LOG_debug << "[WsConn::onmessage] response->event < 0 -> "
                         "uf->uploadFailed(FailReason::ServerError) [this = "
                      << this << "]";
            uf->uploadFailed(FailReason::ServerError);
            return;
        }

        LOG_debug << "[WsConn::onmessage] response->event >= 0 -> chunk.pos = -1 [this = " << this
                  << "]";
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
        LOG_debug << "[WsConn::onmessage] response->event == 13 -> "
                     "uf->uploadFailed(FailReason::Unknown) [this = "
                  << this << "]";
        uf->uploadFailed(FailReason::Unknown);
        return;
    }

    switch (response->event)
    {
        case 1: // chunk ingested (non-final)
            LOG_debug
                << "[WsConn::onmessage] response->event == 1 chunk ingested (non-final) [chunk.len="
                << chunk.len << "] [this = " << this << "]";
            if (chunk.len)
            {
                uf->onServerConfirmedBytes(chunk.len);
                if (mPool->mImpl->mCb.onProgress && uf->progressReportDue(SteadyTime::ds()))
                    mPool->mImpl->mCb.onProgress(uf->transfer(), uf->bytesConfirmed());
                if (uf->bytesConfirmed() >= uf->size())
                {
                    // server confirmed last chunk (or more) rather than completing upload:
                    LOG_debug
                        << "[WsConn::onmessage] uf->bytesConfirmed(=" << uf->bytesConfirmed()
                        << ") >= uf->size(=" << uf->size()
                        << ") -> server confirmed last chunk (or more) rather than completing "
                           "upload -> uf->uploadFailed(FailReason::StateLost) [this = "
                        << this << "]";
                    uf->uploadFailed(FailReason::StateLost);
                    break;
                }
            }
            mPool->mImpl->poolMgr.mActiveFiles.insert(uf);
            break;

        case 7: // final data ingested (server knows file is complete)
            LOG_debug << "[WsConn::onmessage] response->event == 7 final data ingested (server "
                         "knows file is complete) [chunk.len="
                      << chunk.len << "] [this = " << this << "]";
            if (chunk.len)
            {
                uf->onServerConfirmedBytes(chunk.len);
                if (uf->bytesConfirmed() > uf->size())
                {
                    // Something happened here, let's just debug it meanwile
                    LOG_debug << "[WsConn::onmessage] uf->bytesConfirmed(=" << uf->bytesConfirmed()
                              << ") > uf->size(=" << uf->size()
                              << ") -> something happened here !? [this = " << this << "]";
                }
            }
            if (mPool->mImpl->mCb.onProgress)
                mPool->mImpl->mCb.onProgress(uf->transfer(), uf->bytesConfirmed());
            mPool->mImpl->poolMgr.mActiveFiles.insert(uf);
            break;

        case 2: // already on server (after reconnect)
            LOG_debug << "[WsConn::onmessage] response->event == 2 already on server (after "
                         "reconnect) -> break [this = "
                      << this << "]";
            break;

        case 3: // CRC failed
            LOG_warn << "[WsConn::onmessage] response->event == 3 CRC failed -> "
                        "mPool->retryChunk(chunk) [this = "
                     << this << "]";
            mPool->retryChunk(chunk);
            break;

        case 4: // upload completed
        {
            LOG_debug << "[WsConn::onmessage] response->event == 4 upload completed -> "
                         "mPool->applyInFlight(response->fileno) [this = "
                      << this << "]";
            mPool->applyInFlight(response->fileno);
            uf->maybeReportThroughput(mPool->mImpl->currentTime);

            const int payLen = static_cast<unsigned char>(msg[13]); // payload length
            uf->uploadCompleted(msg + 14, payLen);
            if (mPool->mImpl->mCb.onComplete)
            {
                const char* payload = (payLen > 0) ? (msg + 14) : nullptr;
                mPool->mImpl->mCb.onComplete(uf->transfer(), payload, payLen);
            }
            break;
        }

        case 5: // distress → refresh pools
            LOG_warn
                << "[WsConn::onmessage] response->event == 5 distress -> server requested pool "
                   "refresh -> refresh pools -> mPool->mImpl->poolMgr.refreshPools() [this = "
                << this << "]";
            mPool->mImpl->poolMgr.refreshPools();
            break;

        case 6: // throttle (ms) → ds
            LOG_debug
                << "[WsConn::onmessage] response->event == 6 throttle (ms) -> ds -> "
                   "mPool->pauseSending(static_cast<dstime>(response->chunkpos / 100 + 1)) [this = "
                << this << "]";
            mPool->pauseSending(static_cast<dstime>(response->chunkpos / 100 + 1));
            break;

        default:
            LOG_debug << "[WsConn::onmessage] response->event == "
                      << static_cast<int>(response->event)
                      << " -> unknown server opcode=" << static_cast<int>(response->event)
                      << " -> break [this = " << this << "]";
            break;
    }
    if (response->event < 0)
    {
        if (mPool->mImpl->mCb.onFail)
            mPool->mImpl->mCb.onFail(uf->transfer(), static_cast<int>(response->event));
    }
    LOG_debug << "[WsConn::onmessage] END [this = " << this << "]";
}

void WsConn::sendChunkData(const std::uint32_t fileno,
                           const m_off_t pos,
                           const char* data,
                           const int len)
{
    LOG_debug << "[WsConn::sendChunkData] BEGIN [fileno=" << fileno << "] [pos=" << pos
              << "] [len=" << len << "] [this = " << this << "]";
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

    LOG_debug << "[WsConn::sendChunkData] senddata(bufIdx, reinterpret_cast<const char*>(&header), "
                 "static_cast<int>(sizeof header)) [bufIdx="
              << bufIdx << "] [this = " << this << "]";
    senddata(bufIdx, reinterpret_cast<const char*>(&header), static_cast<int>(sizeof header));
    LOG_debug << "[WsConn::sendChunkData] senddata(bufIdx, data, len) [bufIdx=" << bufIdx
              << "] [this = " << this << "]";
    senddata(bufIdx, data, len);
    LOG_debug << "[WsConn::sendChunkData] END [this = " << this << "]";
}

// ========== WsPoolThread (ctor after WsPool complete) ==========
WsPoolThread::WsPoolThread(WsPool* pool):
    t(&WsPool::poolWorkerThread, pool, this)
{
    LOG_debug << "[WsPoolThread::WsPoolThread] pool=" << (void*)pool << " [this = " << this << "]";
}

// ========== WsBuf ==========
bool WsBuf::sendWS(WsConn* ws, int& bufferedAmount)
{
    LOG_debug << "[WsBuf::sendWS] BEGIN [this = " << this << "]";
    if (mSendPos >= mDataLen)
    {
        LOG_debug << "[WsBuf::sendWS] mSendPos(=" << mSendPos << ") >= mDataLen(=" << mDataLen
                  << ") -> return false [this = " << this << "]";
        return false;
    }

    size_t sent = 0;
    LOG_debug << "[WsBuf::sendWS] curl_ws_send(ws->curl, buf(=" << (void*)buf
              << ") + mSendPos(=" << mSendPos << ") = " << (void*)(buf + mSendPos)
              << ", mDataLen(=" << mDataLen << ") - mSendPos(=" << mSendPos
              << ") = " << (mDataLen - mSendPos) << ", &sent, 0, CURLWS_BINARY) [this = " << this
              << "]";
    const CURLcode res =
        curl_ws_send(ws->curl, buf + mSendPos, mDataLen - mSendPos, &sent, 0, CURLWS_BINARY);
    if (res == CURLE_OK)
    {
        LOG_debug << "[WsBuf::sendWS] res == CURLE_OK -> mSendPos(=" << mSendPos
                  << ") += static_cast<int>(sent(=" << sent
                  << ")), bufferedAmount(=" << bufferedAmount
                  << ") -= static_cast<int>(sent(=" << sent << ")) [this = " << this << "]";
        mSendPos += static_cast<int>(sent);
        bufferedAmount -= static_cast<int>(sent);
        if (mSendPos == mDataLen)
        {
            LOG_debug << "[WsBuf::sendWS] mSendPos(=" << mSendPos << ") == mDataLen(=" << mDataLen
                      << ") -> reset() && return true [this = " << this << "]";
            reset();
            return true;
        }
        LOG_debug << "[WsBuf::sendWS] mSendPos(=" << mSendPos << ") != mDataLen(=" << mDataLen
                  << ") -> return false [this = " << this << "]";
        return false;
    }
    if (res != CURLE_AGAIN)
    {
        LOG_debug << "[WsBuf::sendWS] res(=" << res << ") != CURLE_AGAIN(=" << CURLE_AGAIN
                  << ") -> ws->closeWS() [this = " << this << "]";
        ws->closeWS();
    }
    LOG_debug << "[WsBuf::sendWS] res(=" << res << ") != CURLE_OK -> return false [this = " << this
              << "]";
    return false;
}

// ========== WsPool ==========
bool WsPool::getWsUploadFile(const dstime now, UploadEngine::Impl& impl)
{
    if (mUploadingFile && !mUploadingFile->paused() && mUploadingFile->continuingUpload(now) &&
        mUFTQversion == impl.queueVersion)
    {
        LOG_debug << "[WsPool::getWsUploadFile] mUploadingFile->paused()=false && "
                     "mUploadingFile->continuingUpload(now) && mUFTQversion(="
                  << mUFTQversion << ") == impl.queueVersion(=" << impl.queueVersion
                  << ") -> return true [this = " << this << "]";
        return true;
    }

    if (mRetiring)
    {
        LOG_debug << "[WsPool::getWsUploadFile] mRetiring=true -> return false [this = " << this
                  << "]";
        return false;
    }

    if (auto* f = impl.nextEligible(mMinFileSize, mMaxFileSize))
    {
        LOG_debug << "[WsPool::getWsUploadFile] BEGIN -> auto* f = impl.nextEligible(mMinFileSize(="
                  << mMinFileSize << "), mMaxFileSize(=" << mMaxFileSize << "))  [this = " << this
                  << "]";
        // Back‑pressure gate: defer starting a new file while FA pipeline is saturated
        if (impl.mCb.canStartAnotherFile && !impl.mCb.canStartAnotherFile())
        {
            LOG_debug << "[WsPool::getWsUploadFile] impl.mCb.canStartAnotherFile=true && "
                         "!impl.mCb.canStartAnotherFile() -> return false [this = "
                      << this << "]";
            return false;
        }

        // Preflight: let MegaClient run its legacy "prep" (FA scheduling/metadata etc).
        // If it returns false, keep the file queued and try again later.
        if (impl.mCb.preflightStart && !impl.mCb.preflightStart(f->transfer()))
        {
            LOG_debug << "[WsPool::getWsUploadFile] impl.mCb.preflightStart=true && "
                         "!impl.mCb.preflightStart(f->transfer()) -> return false [this = "
                      << this << "]";
            return false;
        }

        if (mUploadingFile != f)
        {
            LOG_debug << "[WsPool::getWsUploadFile] mUploadingFile(=" << (void*)mUploadingFile
                      << ") != f(=" << (void*)f << ") -> picking file fileno=" << f->fileno()
                      << " for [" << mMinFileSize << "," << mMaxFileSize
                      << ") pool and set mUFTQversion(=" << impl.queueVersion
                      << ") = impl.queueVersion(=" << impl.queueVersion << ") [this = " << this
                      << "]";
            mUploadingFile = f;
            mUFTQversion = impl.queueVersion;
            mUploadingFile->setPool(*this);
            if (impl.mCb.onStart)
                impl.mCb.onStart(mUploadingFile->transfer());
        }
        else
        {
            LOG_debug << "[WsPool::getWsUploadFile] mUploadingFile(=" << (void*)mUploadingFile
                      << ") == f(=" << (void*)f << ") -> return true [this = " << this << "]";
        }
        return true;
    }
    LOG_debug << "[WsPool::getWsUploadFile] !f -> return false [this = " << this << "]";
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
    LOG_debug << "[WsPool::nextChunk] BEGIN [this = " << this << "]";
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
    {
        LOG_debug << "[WsPool::nextChunk] impl.paused=true -> return false [this = " << this << "]";
        return false;
    }

    while (getWsUploadFile(impl.currentTime, impl))
    {
        if (mUploadingFile->headPos() < mUploadingFile->size() || !mUploadingFile->eofSet())
        {
            LOG_debug << "[WsPool::nextChunk] mUploadingFile->headPos(="
                      << mUploadingFile->headPos()
                      << ") < mUploadingFile->size(=" << mUploadingFile->size()
                      << ") || !mUploadingFile->eofSet(=" << mUploadingFile->eofSet()
                      << ") -> process chunk"
                      << " [this = " << this << "]";
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
                mUploadingFile->advanceHead(chunk.len);
            }

            mLastActive = impl.currentTime;
            return true;
        }
        LOG_debug << "[WsPool::nextChunk] mUploadingFile->headPos() < mUploadingFile->size() || "
                     "!mUploadingFile->eofSet() -> mUploadingFile = nullptr [this = "
                  << this << "]";
        mUploadingFile = nullptr; // done with this file
    }
    LOG_debug << "[WsPool::nextChunk] END - !getWsUploadFile -> return false [this = " << this
              << "]";
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
    {
        LOG_debug << "[WsPool::sendChunk] ws->readyState=" << static_cast<int>(ws->readyState)
                  << " (ReadyState::OPEN=" << static_cast<int>(WsConn::ReadyState::OPEN)
                  << ") ws->haveSpace=" << ws->haveSpace() << " -> return false [this = " << this
                  << "]";
        return false;
    }

    WsChunk chunk;
    if (!nextChunk(chunk, impl))
    {
        LOG_debug << "[WsPool::sendChunk] !nextChunk -> return false [this = " << this << "]";
        return false;
    }

    static thread_local std::unique_ptr<char[]> tlsBuf;
    if (!tlsBuf)
    {
        LOG_debug << "[WsPool::sendChunk] !tlsBuf -> tlsBuf.reset(new char[MB]) [this = " << this
                  << "]";
        tlsBuf.reset(new char[MB]);
    }

    WsUploadFile* uf = findFile(chunk.fileno, impl);
    if (uf && !uf->aborted() &&
        (chunk.len == 0 || uf->readData(tlsBuf.get(), chunk.pos, chunk.len, impl.uploadMutex)))
    {
        LOG_debug << "[WsPool::sendChunk] uf->readData(tlsBuf.get(), chunk.pos, chunk.len, "
                     "impl.uploadMutex) -> emplace mChunksInFlight, sendChunkData && return true "
                     "[mNumChunksInFlight="
                  << mNumChunksInFlight << "] [this = " << this << "]";
        ws->mChunksInFlight.emplace_back(
            chunk,
            ChunkFingerprintMacUpdate(*uf, chunk.pos, tlsBuf.get(), chunk.len));
        ws->sendChunkData(chunk.fileno, chunk.pos, tlsBuf.get(), chunk.len);

        ++mNumChunksInFlight;
        return true;
    }
    LOG_debug
        << "[WsPool::sendChunk] !uf->aborted() && (chunk.len == 0 || uf->readData(tlsBuf.get(), "
           "chunk.pos, chunk.len, impl.uploadMutex)) -> return false [this = "
        << this << "]";
    return false;
}

void WsPool::poolWorkerThread(WsPoolThread* th)
{
    int retryCount{0};
    std::uint32_t lastQueueVersion = mImpl->queueVersion;
    WsConn ws(this);

    LOG_debug << "[WsPool::poolWorkerThread] BEGIN [lastQueueVersion=" << lastQueueVersion
              << "] [this = " << this << "]";

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
                {
                    LOG_debug << "[WsPool::poolWorkerThread] retryCount=" << retryCount
                              << " -> continue [this = " << this << "]";
                    continue;
                }

                if (!mRetiring && lastQueueVersion != mImpl->queueVersion)
                {
                    LOG_debug << "[WsPool::poolWorkerThread] !mRetiring && lastQueueVersion("
                              << lastQueueVersion << ") != mImpl->queueVersion("
                              << mImpl->queueVersion << ") -> refreshPools [this = " << this << "]";
                    mImpl->poolMgr.refreshPools();
                }

                LOG_debug << "[WsPool::poolWorkerThread] !ok -> continue [retryCount=" << retryCount
                          << "] [this = " << this << "]";

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
            LOG_debug << "[WsPool::poolWorkerThread] throttledByServer=true -> continue [this = "
                      << this << "]";
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
            LOG_debug << "[WsPool::poolWorkerThread] haveSpace=false -> continue [this = " << this
                      << "]";
            mImpl->uploadMutex.unlock();
            SteadyTime::sleep_ds(2);
            mImpl->uploadMutex.lock();
            continue;
        }
        if (!ws.readyForData())
        {
            LOG_debug << "[WsPool::poolWorkerThread] readyForData=false -> continue [this = "
                      << this << "]";
            mImpl->uploadMutex.unlock();
            SteadyTime::sleep_ds(2);
            mImpl->uploadMutex.lock();
            continue;
        }

        // fetch & enqueue next chunk (unlocks around disk I/O internally)
        if (!sendChunk(&ws, *mImpl))
        {
            LOG_debug << "[WsPool::poolWorkerThread] sendChunk=false -> continue [this = " << this
                      << "]";
            mImpl->uploadMutex.unlock();
            SteadyTime::sleep_ds(10);
            mImpl->uploadMutex.lock();
            continue;
        }
    }
    th->terminated = true;
    LOG_debug << "[WsPool::poolWorkerThread] END [lastQueueVersion=" << lastQueueVersion
              << "] [this = " << this << "]";
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
    while ((msg = curl_multi_info_read(curlm, &msgs_left)) != nullptr)
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

    mImpl->client.wsPostToClientThread(
        [this](MegaClient& client, TransferDbCommitter&)
        {
            auto* cio = dynamic_cast<CurlHttpIO*>(client.httpio);
            if (!cio)
            {
                LOG_warn << "[WsPoolMgr::refreshPools] [wsPostToClientThread] "
                            "dynamic_cast<CurlHttpIO*>(client.httpio) "
                            "failed, return [this = "
                         << this << "]";
                return;
            }

            // Plain POST handled in client thread; reuse JSON posture.
            std::string body, err;
            CURL* easy = curl_easy_init();
            if (!easy)
            {
                LOG_warn << "[WsPoolMgr::refreshPools] [wsPostToClientThread] curl_easy_init "
                            "failed, return [this = "
                         << this << "]";
                return;
            }

            cio->configureWsEasy(easy, /*isPostJson*/ true);

            static const char* kCS = "https://staging.api.mega.co.nz/cs";
            curl_easy_setopt(easy, CURLOPT_URL, kCS);
            curl_easy_setopt(easy, CURLOPT_POSTFIELDS, "[{\"a\":\"usc\"}]");
            curl_easy_setopt(
                easy,
                CURLOPT_WRITEFUNCTION,
                +[](char* p, size_t s, size_t n, void* u) -> size_t
                {
                    auto* out = static_cast<std::string*>(u);
                    out->append(p, s * n);
                    return s * n;
                });
            curl_easy_setopt(easy, CURLOPT_WRITEDATA, &body);

            CURLcode rc = curl_easy_perform(easy);
            curl_easy_cleanup(easy);

            if (rc == CURLE_OK)
            {
                LOG_debug << "[WsPoolMgr::refreshPools] [wsPostToClientThread] rc == CURLE_OK -> "
                             "refreshPoolsResponse(body) [this = "
                          << this << "]";
                std::lock_guard<std::mutex> g(mImpl->uploadMutex);
                refreshPoolsResponse(body); // your existing parser unchanged
            }
            else
            {
                LOG_warn << "[WsPoolMgr::refreshPools] [wsPostToClientThread] rc != CURLE_OK -> "
                            "USC fetch failed: "
                         << rc << " [this = " << this << "]";
            }
        });
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
            if (q = std::strchr(p, '"'); q)
            {
                url = "wss://";
                url.append(p, static_cast<size_t>(q - p));
                url.append("/");

                p = q + 3;
                if (q = std::strchr(p, '"'); q)
                {
                    url.append(p, static_cast<size_t>(q - p));
                }

                if (q[1] == ',')
                {
                    apiSizeClasses.emplace_back(url, static_cast<m_off_t>(atoll(q + 2)));
                    if (p = std::strchr(q, ']'); p && !std::memcmp(p, "],[\"", 4))
                    {
                        continue;
                    }
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

void UploadEngine::setCallbacks(UploadEngine::Callbacks cb)
{
    pImpl->mCb = std::move(cb);
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
