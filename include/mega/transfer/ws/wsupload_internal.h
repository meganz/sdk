/**
 * @file include/mega/transfer/ws/wsupload_internal.h
 * @brief File-internal types and helpers shared by the websocket-upload
 *        engine implementation. Defines kMiB, WSUPLOAD_CURL_MULTI_POLL_MS,
 *        the WSUPLOAD_TRACE macro, SteadyTime, ScopedUnlock, CRC32, and the
 *        WsBuf/WsChunk/ChunkHeader/ChunkFingerprintMacUpdate/WsConn/
 *        WsPoolThread/WsPool struct cluster.
 *
 *        SDK-internal architecture header under `include/mega/transfer/ws/`
 *        alongside `ws_encryption.h` and `ws_pool_mgr.h`. Sibling translation
 *        units in `src/transfer/ws/` (wsupload.cpp, ws_curl.cpp, ws_conn.cpp,
 *        ws_pool.cpp, ws_pool_mgr.cpp, ws_upload_file.cpp) include it as
 *        `#include "mega/transfer/ws/wsupload_internal.h"`.
 *
 * (c) 2026 by MEGA Privacy Kft, Csomad, Hungary
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the rules set forth in the Terms of Service.
 *
 * @copyright Simplified (2-clause) BSD License.
 */

#ifndef MEGA_TRANSFER_WS_WSUPLOAD_INTERNAL_H
#define MEGA_TRANSFER_WS_WSUPLOAD_INTERNAL_H 1

#ifdef MEGA_USE_WSUPLOAD

#include "mega/logging.h"
#include "mega/transfer/ws/ws_pool_mgr.h"
#include "mega/types.h" // chunkmac_map, dstime, m_off_t
#include "mega/wsupload.h" // UploadEngine, InboundFrameValidationResult, etc.

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef> // offsetof
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include <curl/curl.h>

#include <zlib.h>

namespace mega
{
namespace ws
{

// Full definition of `class WsUploadFile` lives in
// include/mega/transfer/ws/ws_upload_file.h. The public header
// include/mega/transfer/ws/ws_pool_mgr.h declares its own
// `class WsUploadFile;` forward declaration for the `std::unordered_set<
// WsUploadFile*> mActiveFiles;` member; consumers of this internal header
// that need the full type include "mega/transfer/ws/ws_upload_file.h" directly.

// FailReason is shared between the WsUploadFile inline `uploadFailed` body
// (in include/mega/transfer/ws/ws_upload_file.h) and the WsConn::failFileLocked
// body (in src/transfer/ws/ws_conn.cpp). Defined here so the forward decl
// alone (insufficient) becomes a full definition for all consumers.
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

// ---------- WSUPLOAD_TRACE macro ----------
// Quiet by default in Release/Debug. Set MEGA_WSUPLOAD_TRACE_LOGS=1 at compile
// time to surface the per-chunk/per-conn tracing as LOG_debug entries.
#ifndef MEGA_WSUPLOAD_TRACE_LOGS
#define MEGA_WSUPLOAD_TRACE_LOGS 0
#endif

#if MEGA_WSUPLOAD_TRACE_LOGS
#define WSUPLOAD_TRACE LOG_debug
#else
#define WSUPLOAD_TRACE if (true) {} else LOG_debug
#endif

// ---------- small time helper (deciseconds) ----------
// kDsPerSecond / kMsPerDeciSecond and the secondsToDs / msToDs / dsToMs helpers
// live in include/mega/transfer/ws/ws_pool_mgr.h so that WsPoolMgr's
// secondsToDs(...) constexpr member initialisers resolve from that header.

struct SteadyTime
{
    static dstime ds()
    {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
        const dstime v = msToDs(ms);
        return v ? v : 1; // never return 0 (0 used as sentinel)
    }

    static void sleep_ds(const dstime ds)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(dsToMs(ds)));
    }

    static std::int32_t difference(const dstime a, const dstime b)
    {
        return static_cast<std::int32_t>(a - b);
    }
};

// Block timeout for curl_multi_poll() in the WsPoolMgr IO loop. Caps the time
// the manager thread waits for socket activity per iteration; bounded so that
// stop signals (mStopping) and refresh-timer expiries are honoured promptly.
constexpr int WSUPLOAD_CURL_MULTI_POLL_MS = 500;

// Mebibyte (2^20). Used for chunk-size constants and the per-chunk send buffer.
constexpr int kMiB = 1048576;

// ---------- RAII helper: temporarily release a unique_lock and re-acquire it on scope exit ----------
class ScopedUnlock
{
public:
    explicit ScopedUnlock(std::unique_lock<std::mutex>& lock):
        mLock(lock),
        mWasLocked(lock.owns_lock())
    {
        if (mWasLocked)
        {
            mLock.unlock();
        }
    }

    ~ScopedUnlock()
    {
        if (mWasLocked)
        {
            mLock.lock();
        }
    }

    ScopedUnlock(const ScopedUnlock&) = delete;
    ScopedUnlock& operator=(const ScopedUnlock&) = delete;

private:
    std::unique_lock<std::mutex>& mLock;
    bool mWasLocked{false};
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

// ---------- forward decls ----------
struct WsPool;
struct WsConn;
struct WsPoolThread;

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

struct WsChunk
{
    m_off_t pos{0};
    int len{0};
    std::uint32_t fileno{0};
    // Number of times this chunk has been requeued via retryChunkLocked (opcode 3
    // CrcFailed). Value travels with the chunk through mToResend and mChunksInFlight.
    unsigned retryCount{0};
};

struct WsBuf
{
    char buf[20 + kMiB];
    int mSendPos{0};
    int mDataLen{0};
    // True once curl_ws_send accepted PART of this frame (sent < remaining) but the
    // frame is not yet complete. While set, the libcurl easy holds in-progress frame
    // state (enc.payload_remain / sendbuf) that MUST NOT survive a reconnect: closeWS()
    // drops it and the whole chunk is requeued (fix #6). Cleared by reset().
    bool mFrameInProgress{false};

    void add(const char* data, const int len)
    {
        assert((len >= 0) && len <= (static_cast<int>(sizeof(buf)) - mDataLen));

        std::memcpy(buf + mDataLen, data, static_cast<size_t>(len));
        mDataLen += len;
    }

    void reset()
    {
        mSendPos = 0;
        mDataLen = 0;
        mFrameInProgress = false;
    }

    bool sendWS(struct WsConn* ws, int& bufferedAmount);
};

struct ChunkFingerprintMacUpdate
{
    m_off_t pos{0};
    chunkmac_map macs;

    ChunkFingerprintMacUpdate() = default;

    explicit ChunkFingerprintMacUpdate(const m_off_t p):
        pos(p)
    {}

    ChunkFingerprintMacUpdate(const m_off_t p, chunkmac_map&& m):
        pos(p),
        macs(std::move(m))
    {}

    // Defined out-of-line in wsupload.cpp because the body calls
    // WsUploadFile::queueConfirmedChunkMacs which requires the full
    // WsUploadFile definition (kept in src/transfer/ws/wsupload.cpp).
    void apply(const m_off_t confirmedPos, WsUploadFile& file);
};

// ---------- WebSocket connection (per thread) ----------
struct WsConn
{
    CURL* curl{nullptr};
    int bufferedAmount{0};

#ifndef NDEBUG
    // Send-side counters (per-connection). Aggregated to the pool
    // by WsPool::addWsUploadStatsForTesting.
    std::uint64_t mCurlAgainSendCount{0};
    std::uint64_t mCurlAgainRecvCount{0};
    // E-3: Σ bytes curl_ws_send accepted on this connection (incremented in
    // WsBuf::sendWS on the CURLE_OK branch). Surfaced via WsUploadStatsForTesting
    // so a deterministic force-close test can measure whole-chunk re-send waste.
    std::uint64_t mTotalCurlWsSendAcceptedBytes{0};
    // # closeWS() calls that ran while a buffer held a live partial frame (fix #6).
    std::uint64_t mPartialFrameTornDownCount{0};
    int mBufferedAmountHighWater{0};
    unsigned mChunksInFlightHighWater{0};
    // Throttle-recovery telemetry (per-connection). Set on event=6 receipt,
    // sampled on event=1 chunk-ack to compute pause-to-first-ack latency.
    dstime mPauseStartedAtMs{0};
    std::uint64_t mThrottleRecoveryAckSamples{0};
    std::uint64_t mThrottleRecoveryAckTotalMs{0};
    std::uint64_t mThrottleRecoveryAckMaxMs{0};
#endif

    enum class ReadyState : std::uint8_t
    {
        CONNECTING,
        OPEN,
        CLOSING,
        CLOSED
    };
    // Writes happen on the pool worker thread inside connectWS()/closeWS() which run
    // under ScopedUnlock (uploadMutex released during the blocking handshake/close).
    // Cross-thread reads happen on the engine run thread under uploadMutex
    // (countOpenConnectionsLocked, sendChunk). Used as a value, not a synchronisation
    // signal: relaxed atomic suffices and adds zero hot-path overhead on x86/ARM64.
    std::atomic<ReadyState> readyState{ReadyState::CLOSED};

    WsBuf mBufs[2];
    char mCurBuf{0};
    bool mClosing{false};
    // Deferred-close flag: set by onmessage() when it detects an invalid inbound
    // frame (TooShort/BadCrc) so that curlRecv() can finish draining libcurl's
    // recv pipeline before tearing the socket down. Prevents ACK frames already
    // in the recv queue from being silently dropped on reconnect.
    bool mPendingClose{false};

    // Ack-stall watchdog (SDK-5360 fu8 Session 6), two atomic fields:
    //  - mLastInboundFrameDs: TRUE server-liveness stamp for THIS connection, set on
    //    onopen() (baseline so a just-opened conn is not instantly flagged) and on every
    //    validated inbound server frame in onmessage(). Distinct from the pool-level
    //    mLastActive (which our OWN chunk-prep sends bump, so it cannot detect a conn we
    //    keep writing to that never acks) and from the pool-level mLastServerResponse
    //    (which bumpAllPools resets on every refresh and is per-pool, not per-conn).
    //    Read by WsPoolMgr::checkPools; atomic because onopen() runs with uploadMutex
    //    RELEASED (ScopedUnlock around the blocking handshake). Relaxed = value, not a
    //    sync signal (same discipline as readyState).
    //  - mForceReconnect: cross-thread force-close request. Set by checkPools when the
    //    ack-stall window elapses; honoured by the owning pool-worker in its loop next to
    //    the disconnectEpoch check (ScopedUnlock -> closeWS -> onclose ->
    //    retryChunksOnTheWireLocked requeues un-acked chunks). Mirrors disconnectEpoch.
    std::atomic<dstime> mLastInboundFrameDs{0};
    // Ack-stall watchdog progress-guard (fu8 S6): per-conn "last SEND progress" stamp — set on
    // onopen() (baseline) and whenever WsBuf::sendWS gets curl_ws_send to ACCEPT bytes (data is
    // still flowing OUT). The watchdog fires only when BOTH mLastInboundFrameDs (no server acks)
    // AND this (no bytes going out) are stale past the window — so a legitimately SLOW conn
    // (rate-limited via setmaxuploadspeed, or low-bandwidth) that is still sending is NOT
    // force-reconnected (it keeps stamping this), while a genuinely HUNG conn (server silent AND
    // send buffer wedged) trips both. Prevents the over-fire that regressed
    // RepeatedPauseResumeMixedPools + the 240x loss20 over-fire. Atomic (onopen runs with
    // uploadMutex released), relaxed = value not a sync signal.
    std::atomic<dstime> mLastSendProgressDs{0};
    std::atomic<bool> mForceReconnect{false};

    // Goodput-saturation gate (SDK-5360, amendment A): per-conn send-buffer backpressure
    // signal. The pool WORKER stores it (relaxed): true at the !haveSpace() check point
    // (ws_pool.cpp), false when haveSpace()==true, on the server-throttle continue branch,
    // and on close (WsConn::resetBufferedSendState). WsPoolMgr::checkPools READS it
    // cross-thread (WsPool::allOpenConnsBackpressuredLocked) to evaluate the ramp's WIDEN
    // predicate. ATOMIC (HR23) BECAUSE the bufferedAmount / mBufs[].mDataLen that haveSpace()
    // reflects are mutated inside WsBuf::sendWS() under ScopedUnlock (uploadMutex RELEASED) --
    // a raw haveSpace() read from checkPools would be a NEW v2-surface race on those fields.
    // Same relaxed-atomic value-not-sync-signal discipline as mForceReconnect /
    // mLastInboundFrameDs / mLastSendProgressDs (all made atomic for the same reason).
    std::atomic<bool> mBackpressured{false};

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
    void resetBufferedSendState() noexcept;

    // Helpers used by onmessage(). Caller must hold mPool->mImpl->uploadMutex.
    // Purge in-flight/resend state for fileno, mark uf as failed, and dispatch
    // the onFail callback. Control-flow (return/break) stays at the call site.
    void failFileLocked(WsUploadFile* uf,
                        std::uint32_t fileno,
                        FailReason reason,
                        int apierr,
                        m_off_t aux,
                        UploadEngine::FailureDisposition disp);

    // Returns true if the server-confirmed byte count exceeds the file size.
    // When true, failFileLocked() has already been invoked with StateLost / API_EINTERNAL
    // and the caller MUST break out of the switch arm. The fileno is read from
    // uf->fileno() so callers do not need to re-pass it.
    bool handleBytesConfirmedOverflow(WsUploadFile* uf);

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
        WSUPLOAD_TRACE << "[WsConn::senddata] BEGIN [buf=" << buf << "] [data=" << (void*)data
                  << "] [len=" << len << "] [bufferedAmount=" << bufferedAmount
                  << "] [this = " << this << "]";
        mBufs[buf].add(data, len);
        bufferedAmount += len;
#ifndef NDEBUG
        // Track per-conn buffered-bytes high-water.
        if (bufferedAmount > mBufferedAmountHighWater)
            mBufferedAmountHighWater = bufferedAmount;
#endif
        WSUPLOAD_TRACE << "[WsConn::senddata] END [bufferedAmount=" << bufferedAmount
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

    void join()
    {
        if (!t.joinable())
            return;
        if (t.get_id() == std::this_thread::get_id())
        {
            LOG_warn << "[WsPoolThread::join] refusing to join self; detaching [this = " << this
                     << "]";
            t.detach();
            return;
        }
        t.join();
    }

    ~WsPoolThread()
    {
        WSUPLOAD_TRACE << "[WsPoolThread::~WsPoolThread] BEGIN -> t.join() [this = " << this << "]";
        join();
        WSUPLOAD_TRACE << "[WsPoolThread::~WsPoolThread] END [this = " << this << "]";
    }
};

// ---------- Pool per size class ----------
struct WsPool
{
    static constexpr dstime CONNRETRYINTERVAL = secondsToDs(5);
    // Ceiling for the capped-exponential reconnect backoff (fix #3). The flat
    // CONNRETRYINTERVAL is the base; it doubles per consecutive failure up to this
    // cap, with jitter, so N workers do not retry in lockstep (root_cause.md S3c).
    static constexpr dstime CONNRETRYMAXINTERVAL = secondsToDs(30);
    static constexpr dstime UPLOADTIMEOUT = secondsToDs(180);
    // Sustained-handshake-failure escalation window (fix #4b). Separate from (and
    // shorter than) UPLOADTIMEOUT, which retains chunk-phase semantics elsewhere:
    // a pure-handshake-failure loop now surfaces onFail in <=60s instead of 180s.
    static constexpr dstime HANDSHAKEFAILTIMEOUT = secondsToDs(60);
    // Ack-stall watchdog window (SDK-5360 fu8 Session 6). An OPEN connection that still
    // holds in-flight chunks but has received NO inbound server frame for longer than this
    // is treated as silently hung (slow/lossy link, server silent, no TCP drop) and is
    // force-reconnected by WsPoolMgr::checkPools (see WsConn::mLastInboundFrameDs +
    // mForceReconnect). Chosen at 45s: > SERVERTIMEOUT (20s, so the existing pool-refresh
    // trigger fires first on the normal path) and < HANDSHAKEFAILTIMEOUT (60s), so a
    // genuinely-hung chunk recovers well before the 180s that UPLOADTIMEOUT nominally
    // implied but never enforced. Runtime-overridable (ms) via MEGA_WS_ACKSTALL_TIMEOUT_MS
    // through UploadEngine::Impl::ackStallTimeoutDs(); gated by MEGA_WS_ACKSTALL_WATCHDOG.
    static constexpr dstime ACKSTALLTIMEOUT = secondsToDs(45);
    // Max workers allowed to be handshaking at once, per pool (fix #2, de-convoy
    // Design C's C1). 2 keeps a warm spare in flight (a single slow handshake cannot
    // stall the pool) while bounding the client-thread handshake FIFO convoy to O(1)
    // instead of O(mNumberOfConnections) (root_cause.md S3b).
    static constexpr int COLDSTART_HANDSHAKE_CONNS = 2;
    // Single-small-file cold-start: when the entire eligible workload of a cold pool is
    // ONE file no larger than this ceiling, coldStartHandshakeCapLocked() narrows the
    // cold-start cap to 1 (smallfile_investigation.md candidate 3c). A single small file's
    // escalating frames drain on ONE connection (ack-RTT-bound, not bandwidth-bound), so a
    // 2nd cold handshake costs client-thread time + ~3 MiB (one WsConn double-buffer +
    // tlsBuf) with zero throughput gain. 4 MiB is the documented QaExact size (4*kMiB =
    // 32*SEGSIZE) and the largest single file that is ack-RTT-bound on a fast link; files
    // ABOVE it pipeline across connections and keep the warm spare (cap stays 2). The cap
    // is RE-EVALUATED at the gate each loop pass, so the instant a 2nd file queues OR any
    // in-flight/resend appears (incl. the first dropped frame under loss) it reverts to
    // COLDSTART_HANDSHAKE_CONNS — fix #2's de-convoy and the >=20%-loss S3 convoy stay
    // closed. kMiB is int; widen to m_off_t so the <= against WsUploadFile::size() (m_off_t)
    // is signed-to-signed (no -Wsign-conversion).
    static constexpr m_off_t kSingleConnFileSizeCeiling = 4 * static_cast<m_off_t>(kMiB);
    // Loss-gated connection-count bump (lossBoostedConnLimitLocked). When a LONE small
    // file (same single-isolated-small-file detection coldStartHandshakeCapLocked uses)
    // has observed loss (WsPool::mLossObserved sticky), its connection target is widened
    // toward this value so the straggler uploads on ~8 flows like develop does under loss
    // (bench: c1=379s -> c4=198s). INVERSE of the cold-start cap (which NARROWS a clean
    // lone small file to 1 conn). Scoped to the lone small file so datasets (many
    // files/pools, eligibleCount>1) are NOT boosted: boosting every pool to 8 under loss
    // would explode RSS with no benefit (datasets already win at the default conn count).
    static constexpr unsigned char kLossBoostedConnLimit = 8;
    // Loss-gated DATASET connection-count bump (A24, SDK-5360 fu8 Session 5). The COMPLEMENT
    // of the lone-small-file boost above: when a pool carries a DATASET (eligibleCount >= 2,
    // or a lone file above kSingleConnFileSizeCeiling) AND has observed loss, its connection
    // target is widened toward this value so the dataset uploads on more independent TCP flows
    // (root cause = FLOW count, GOAL3_ROOTCAUSE_VERDICT_v2). Gated on MEGA_WS_DATASET_CONN_BUMP
    // (mDatasetConnBump), which is ANDed with the master MEGA_WS_LOSS_RECOVERY. On a clean
    // network mLossObserved is false so the boost helper returns the default limit and the
    // scale-up is byte-identical to the no-bump path. The runtime numeric override
    // MEGA_WS_DATASET_CONN_LIMIT (mDatasetConnLimitOverride, default 0 = use this constant)
    // lets the Queue-B proof bench sweep K on ONE binary. Ship default K=24 (CP1: final K
    // chosen at CP2 from paired bench data).
    // CP2 DECISION (2026-07-02, user): SHIP K=32 -- the Pareto knee from Queue-B paired n=5:
    // 76% of develop throughput @loss5 (3.3x the 8-conn floor) at RSS PARITY (-0.6% vs develop,
    // passes strict HR54). K=24 was RSS-safer but only 62%; K=36 reached 85% but +6.7% RSS
    // (broke strict HR54). The effective in-flight peak is ~30 (a -2 ramp cap), which is exactly
    // the measured K32 arm. Numeric override MEGA_WS_DATASET_CONN_LIMIT retained for testing.
    // DEVICE-CLASS NOTE (ledger N9): widening to K worker threads is desktop-focused; on 32-bit
    // armeabi-v7a (mobile base PUT=3) it risks address-space pressure. Mobile is documented
    // out-of-scope for this session (host app can set MEGA_WS_DATASET_CONN_BUMP=0); follow-up
    // ticket auto-gates it off / lowers K on mobile base==3.
    static constexpr unsigned char kLossBoostedDatasetConnLimit = 32;
    // Global concurrency ceiling FLOOR for dataset-boosted pools (cross-pool RSS bound), applied
    // across pools in WsPoolMgr::checkPools using each pool's LIVE mNumberOfConnections
    // (cross-tick correct). The EFFECTIVE ceiling is max(this, lossBoostedDatasetConnLimit()) --
    // see UploadEngine::Impl::lossBoostedGlobalConnCeiling -- so at the ship default K=32 the
    // effective ceiling is 32: a single dominant pool can reach K, while several concurrent
    // size-class pools SHARE the K-conn budget rather than each reaching K (bounds total RSS to
    // ~K conns). This 28 floor only binds when a smaller K is configured. Empirically (Queue-B)
    // the ship K=32 arm settled at ~30 in-flight / ~101 MB = develop RSS parity.
    static constexpr unsigned char kLossBoostedGlobalConnCeiling = 28;
    static constexpr dstime HAVE_SPACE_RETRY_DS = 1;
    static constexpr dstime READY_FOR_DATA_RETRY_DS = 1;
    static constexpr dstime BACKLOG_EMPTY_RETRY_DS = 2;

    UploadEngine::Impl* mImpl{nullptr};

    std::unordered_set<WsConn*> mConns;
    std::vector<WsChunk> mToResend;

    std::vector<std::unique_ptr<WsPoolThread>> mActiveThreads, mExitingThreads;

    int mNumPoolFiles{0};
    WsUploadFile* mUploadingFile{nullptr};
    std::uint32_t mUFTQversion{0};
    bool mPreflightPending{false};

    dstime mPoolCreationTime{SteadyTime::ds()};
    std::string mUrl;

    m_off_t mMinFileSize{0}, mMaxFileSize{0};
    int mNumChunksInFlight{0};
    // Cold-start handshake gate (fix #2, root_cause.md S3b). Number of THIS pool's
    // workers currently inside a first/reconnect connectWS(). Capped at
    // COLDSTART_HANDSHAKE_CONNS so N workers do not stampede the single client-thread
    // handshake FIFO at once. Written only on the worker thread under
    // mImpl->uploadMutex (same discipline as mNumChunksInFlight).
    int mConnectingCount{0};

    // Per-pool sticky loss flag (loss-gated connection-count bump). Set true at the two
    // runtime loss/requeue sites (WsPool::retryChunksOnTheWireLocked + retryChunkLocked,
    // ws_pool.cpp): chunks
    // only land in mToResend on a connection drop or a server-rejected frame, so this
    // is the authoritative runtime loss signal for this pool. Consumed by
    // lossBoostedConnLimitLocked()'s LONE-SMALL-FILE path only (the DATASET conn-bump is
    // unconditional and does NOT read this -- the WS engine cannot see sub-connection packet
    // loss, so a loss-gated dataset bump would never engage; see ws_pool.cpp). NEVER reset for
    // the pool's lifetime (shipped behavior). On a clean network it stays false, so the
    // lone-small boost returns the default limit. Written only on the worker thread under
    // mImpl->uploadMutex (same discipline as mNumChunksInFlight).
    bool mLossObserved{false};

    dstime mLastActive{0};
    dstime mLastServerResponse{0};
    dstime mPausedByServerUntil{0};

    unsigned char mNumberOfConnections{3};
    bool mRetiring{false};
    bool mPinned{false};

    // ===== Goodput-saturation gate (SDK-5360) =====
    // ALL fields below are touched ONLY under mImpl->uploadMutex: mConfirmedBytesTotal is
    // incremented in WsConn::onmessage (which holds uploadMutex at the ack sites), and the
    // controller state is read+written in WsPoolMgr::checkPools (also under uploadMutex). So
    // they are PLAIN (non-atomic) -- HR23. (Contrast WsConn::mBackpressured, which IS atomic
    // BECAUSE the worker writes it with uploadMutex RELEASED.)
    //
    // Aggregate server-confirmed bytes for THIS pool (monotonic goodput source). Incremented
    // by the newly-acked byte count at each server chunk-ack in WsConn::onmessage. The
    // controller samples its delta over a ~1s window to measure aggregate goodput -- the
    // clean-vs-loss discriminator (a clean bandwidth-limited link does not gain goodput when a
    // flow is added; a loss-limited one does, because each flow is an independent cwnd).
    std::uint64_t mConfirmedBytesTotal{0};
    // Controller (hill-climb) state:
    dstime mGateLastSampleDs{0};            // anchor time of the current goodput window
    std::uint64_t mGateLastSampleBytes{0};  // mConfirmedBytesTotal at mGateLastSampleDs
    double mGateGoodputBeforeIncrease{0.0}; // goodput (bytes/s) measured just before the last +step
    bool mGateProbing{false};               // true while evaluating the most recent +step probe
    dstime mGateNextAdjustDs{0};            // hysteresis: earliest ds the controller may act again

    // Release-safe throttle counters consumed by the bench framework
    // (`tests/integration/bench_framework/BenchReportWriter.cpp`). Incremented
    // with `memory_order_relaxed` on the WS hot path (see ws_conn.cpp,
    // `case WsApiServerEvent::Throttle`). Independent from the DEBUG-only
    // `mThrottleEventCodeCounts`/`mThrottleEvent*` histogram below: those are
    // unbounded accumulators used by `getWsUploadStatsForTesting`; these are
    // *resettable* per-iter accumulators consumed by
    // `getAndResetBenchThrottleStats()`.
    std::atomic<std::int64_t> mBenchThrottleEvent6Count{0};
    std::atomic<std::int64_t> mBenchThrottleEvent6TotalMs{0};
    std::atomic<std::int64_t> mBenchThrottlePauseCount{0};
    std::atomic<std::int64_t> mBenchThrottlePauseTotalMs{0};
#ifndef NDEBUG
    unsigned mMaxConnectionsWithInFlightSeen{0};
    std::uint64_t mUploadingFileSinceMs{0};
    std::uint64_t mUploadingFileOccupiedMs{0};
    std::uint64_t mLastCompletedFileMs{0};
    std::uint64_t mLastFirstByteSentMs{0};
    std::uint64_t mLastAckToNextFirstByteSamples{0};
    std::uint64_t mLastAckToNextFirstByteTotalMs{0};
    std::uint64_t mLastAckToNextFirstByteMaxMs{0};
    std::uint64_t mLastCounterSampleMs{0};
    std::uint64_t mAllChunksInFlightBlockedMs{0};
    std::uint64_t mEligibleFileSampleCount{0};
    std::uint64_t mBlockedByInFlightSampleCount{0};
    std::uint64_t mIdleEligibleConnectionMs{0};
    std::uint64_t mIdleEligibleConnectionSampleCount{0};
    std::uint32_t mLastCompletedFileno{0};
    std::uint32_t mLastFirstByteSentFileno{0};

    // Send-side counters (per-pool aggregator for worker-thread events).
    std::uint64_t mHaveSpaceFalseIters{0};
    std::uint64_t mHaveSpaceFalseWaitMs{0};
    std::uint64_t mReadyForDataFalseIters{0};
    std::uint64_t mReadyForDataFalseWaitMs{0};
    std::uint64_t mThrottleSleepIters{0};
    std::uint64_t mThrottleSleepMs{0};
    std::uint64_t mBacklogEmptyIters{0};
    std::uint64_t mBacklogEmptyMs{0};
    std::uint64_t mChunkPrepTotalMs{0};
    std::uint64_t mChunkPrepMaxMs{0};
    std::uint64_t mChunkPrepN{0};

    // Server throttle telemetry (per-pool aggregator).
    std::uint64_t mThrottleEventCount{0};
    std::uint64_t mThrottleEventTotalDs{0};
    std::uint64_t mThrottleEventSumSqDs{0};
    std::uint64_t mThrottleEventMinDs{0};
    std::uint64_t mThrottleEventMaxDs{0};
    std::uint64_t mThrottleBucket0to1s{0};
    std::uint64_t mThrottleBucket1to5s{0};
    std::uint64_t mThrottleBucket5to30s{0};
    std::uint64_t mThrottleBucket30sPlus{0};
    std::uint64_t mThrottleEventCodeCounts[16]{};
    std::uint64_t mSimultaneousThrottledConnsMax{0};
    std::uint64_t mSimultaneousThrottledConnsSamples{0};
    std::uint64_t mSimultaneousThrottledConnsSum{0};
#endif

    explicit WsPool(std::pair<std::string, m_off_t> urlmaxsize,
                    m_off_t minsize,
                    UploadEngine::Impl* impl,
                    const unsigned char numConnections = 3):
        mImpl(impl),
        mPoolCreationTime(SteadyTime::ds()),
        mUrl(std::move(urlmaxsize.first)),
        mMinFileSize(minsize),
        mMaxFileSize(urlmaxsize.second),
        // Use SteadyTime::ds() directly so these inits do not depend on declaration
        // order with mPoolCreationTime (which currently precedes them, but that is
        // an invariant worth eliminating).
        mLastActive(SteadyTime::ds()),
        mLastServerResponse(SteadyTime::ds()),
        mNumberOfConnections(std::max<unsigned char>(1, numConnections))
    {
        WSUPLOAD_TRACE << "[WsPool] constructed [mMinFileSize=" << mMinFileSize
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

    // Clear mUploadingFile under uploadMutex. In Debug, also rolls up the
    // mUploadingFileOccupiedMs accumulator via assignUploadingFileLocked(nullptr);
    // in Release, this is a plain pointer reset.
    void clearUploadingFileLocked() noexcept
    {
#ifndef NDEBUG
        assignUploadingFileLocked(nullptr);
#else
        mUploadingFile = nullptr;
#endif
    }

    void poolWorkerThread(WsPoolThread* th);
    void checkThreads();

    void setPoolNumConn(const unsigned char n)
    {
        mNumberOfConnections = n;
        checkThreads();
    }

    // Release-safe twin of countOpenConnectionsLocked() (which is #ifndef NDEBUG and
    // so cannot be used in the shipped cold-start gate, fix #2). Early-exits on the
    // first OPEN connection. Caller must hold mImpl->uploadMutex.
    bool anyOpenConnLocked() const
    {
        for (const auto* c: mConns)
        {
            if (c && c->readyState.load(std::memory_order_relaxed) == WsConn::ReadyState::OPEN)
            {
                return true;
            }
        }
        return false;
    }

    // Goodput-saturation gate (SDK-5360): true iff the pool has >=1 OPEN connection AND EVERY
    // open connection is currently backpressured (send buffer full). Reads the per-conn atomic
    // mBackpressured (worker-written with uploadMutex RELEASED) + the atomic readyState, so it
    // is race-safe (HR23) even though checkPools calls it cross-thread. VACUOUSLY FALSE when
    // there are no open conns (never widen a dark pool). Caller must hold mImpl->uploadMutex
    // (mConns is stable then -- same discipline as anyOpenConnLocked / the ack-stall watchdog).
    bool allOpenConnsBackpressuredLocked() const
    {
        unsigned openCount = 0;
        for (const auto* c: mConns)
        {
            if (c && c->readyState.load(std::memory_order_relaxed) == WsConn::ReadyState::OPEN)
            {
                ++openCount;
                if (!c->mBackpressured.load(std::memory_order_relaxed))
                    return false;
            }
        }
        return openCount > 0;
    }

    // Cold-start handshake cap, re-evaluated at the cold-start admission gate
    // (ws_pool.cpp) on every worker loop pass. Returns COLDSTART_HANDSHAKE_CONNS (2)
    // by default, NARROWING to 1 ONLY when the small-file cold-start optimisation is
    // enabled (mImpl->mSmallFileColdStart, env MEGA_WS_SMALLFILE_COLDSTART, independent
    // of MEGA_WS_LOSS_RECOVERY) AND this cold pool's entire eligible workload is a single
    // small file: exactly one queued-eligible file in fileList (bounded scan, NOT the
    // bind-too-late mNumPoolFiles), that file's actual size() <= kSingleConnFileSizeCeiling
    // (NOT the size-class ceiling mMaxFileSize), AND no in-flight/resend state
    // (mNumChunksInFlight == 0 && mToResend.empty()). The instant a 2nd file queues or any
    // in-flight/resend appears (incl. the first dropped frame under loss) the predicate
    // fails on the next pass and the cap reverts to 2, so fix #2's de-convoy and the
    // >=20%-loss S3 convoy stay closed. Pinned pools never narrow (they bypass the gate;
    // the short-circuit here is belt-and-suspenders). Caller must hold
    // mImpl->uploadMutex (reads mToResend / mNumChunksInFlight and scans Impl::fileList).
    // Body in src/transfer/ws/ws_pool.cpp.
    int coldStartHandshakeCapLocked() const;

    // Loss-gated connection-count bump (default ON via env MEGA_WS_LOSS_CONN_BUMP ->
    // mImpl->mLossConnBump). The INVERSE of coldStartHandshakeCapLocked: returns the
    // default impl.poolConnectionLimit() UNLESS this pool is a lone small file that has
    // observed loss, in which case it widens the connection target to
    // max(default, kLossBoostedConnLimit) so the straggler uploads on ~8 flows like
    // develop does under loss. The boost is gated on mLossObserved (sticky runtime loss
    // signal), the sub-knob mLossConnBump, !mPinned, AND the same single-isolated-small-
    // file detection coldStartHandshakeCapLocked uses (eligibleCount<=1 + actual size()
    // <= kSingleConnFileSizeCeiling) -- the eligibleCount<=1 + small-size gate is the
    // RSS-safety crux that keeps datasets (eligibleCount>1 per pool, or large pools) at
    // the default conn count. Unlike the cold-start cap this does NOT also require an
    // empty in-flight/resend set: under loss there IS resend activity, and mLossObserved
    // is the gate. On a clean network mLossObserved stays false so this returns the
    // default limit and the scale-up is byte-identical to the no-bump path. Caller must
    // hold mImpl->uploadMutex (scans Impl::fileList). Body in src/transfer/ws/ws_pool.cpp.
    unsigned char lossBoostedConnLimitLocked(const UploadEngine::Impl& impl) const;

    // Goodput-saturation gate ramp controller (SDK-5360, amendment B). Called from
    // WsPoolMgr::checkPools (under uploadMutex) ONLY when a connection boost is active AND
    // MEGA_WS_DATASET_CONN_GATE is on. `ceiling` is the (already global-ceiling-clamped)
    // targetConnLimit -- the MAX the ramp may reach. Hill-climbs mNumberOfConnections from the
    // base pool limit toward `ceiling` while aggregate goodput keeps rising under full
    // backpressure (WIDEN), and actively steps back down when a +step probe does not lift
    // goodput (RETREAT -- the clean bandwidth-limited case). Mutates pool controller state +
    // mNumberOfConnections (via setPoolNumConn), so non-const. Body in src/transfer/ws/ws_pool.cpp.
    void runGoodputGateLocked(const UploadEngine::Impl& impl, unsigned char ceiling);

#ifndef NDEBUG
    unsigned countOpenConnectionsLocked() const
    {
        unsigned openConnections = 0;
        for (const auto* conn: mConns)
        {
            if (conn && conn->readyState.load(std::memory_order_relaxed) == WsConn::ReadyState::OPEN)
            {
                ++openConnections;
            }
        }
        return openConnections;
    }

    unsigned countConnectionsWithInFlightLocked() const
    {
        unsigned withInFlight = 0;
        for (const auto* conn: mConns)
        {
            if (conn && !conn->mChunksInFlight.empty())
            {
                ++withInFlight;
            }
        }
        return withInFlight;
    }

    void updateMaxConnectionsWithInFlightSeenLocked()
    {
        mMaxConnectionsWithInFlightSeen =
            std::max(mMaxConnectionsWithInFlightSeen, countConnectionsWithInFlightLocked());
    }
#endif

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

    bool sameHostMaxSize(const std::pair<std::string, m_off_t>& urlMaxSize) const
    {
        constexpr std::size_t wsHostPrefixLength = sizeof("wss://") - 1;

        if (mMaxFileSize != urlMaxSize.second)
            return false;

        for (std::size_t i = wsHostPrefixLength; i < urlMaxSize.first.size() && i < mUrl.size(); ++i)
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
        const dstime nowDs = SteadyTime::ds();
        mPausedByServerUntil = nowDs + ds;
        // Touch mLastActive so a legitimate server-throttle window does not age into
        // SERVERTIMEOUT and trigger a redundant refreshPools().
        mLastActive = nowDs;
#ifndef NDEBUG
        // Throttle event histogram + min/max/stdev accumulators. dstime is signed
        // (int64_t); the explicit cast suppresses -Wsign-conversion for the unsigned
        // accumulators. `ds` is a duration here, never negative.
        const std::uint64_t dsU = static_cast<std::uint64_t>(ds);
        ++mThrottleEventCount;
        mThrottleEventTotalDs += dsU;
        mThrottleEventSumSqDs += dsU * dsU;
        if (mThrottleEventMinDs == 0 || dsU < mThrottleEventMinDs) mThrottleEventMinDs = dsU;
        if (dsU > mThrottleEventMaxDs) mThrottleEventMaxDs = dsU;
        // Throttle bucket boundaries in deciseconds (ds = 1/10 s). Field names encode
        // the human-readable second range (0-1s, 1-5s, 5-30s, 30s+).
        constexpr int kThrottleBucket0to1sMaxDs = 10;    // ≤ 1.0 s
        constexpr int kThrottleBucket1to5sMaxDs = 50;    // ≤ 5.0 s
        constexpr int kThrottleBucket5to30sMaxDs = 300;  // ≤ 30.0 s
        if (ds <= kThrottleBucket0to1sMaxDs) ++mThrottleBucket0to1s;
        else if (ds <= kThrottleBucket1to5sMaxDs) ++mThrottleBucket1to5s;
        else if (ds <= kThrottleBucket5to30sMaxDs) ++mThrottleBucket5to30s;
        else ++mThrottleBucket30sPlus;
#endif
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
    WsUploadFile* findPreflightReadyCandidate(class UploadEngine::Impl& impl);
    WsUploadFile* findFile(std::uint32_t fileno, class UploadEngine::Impl& impl);
    bool nextChunk(WsChunk& chunk, class UploadEngine::Impl& impl, dstime* retryAfterDs = nullptr);
    void retryChunkLocked(const WsChunk& chunk);
    void retryChunk(const WsChunk& chunk);
    WsUploadFile* handshakeFailureCandidateLocked(dstime now) const;

    // Capped-exponential reconnect backoff with jitter (fix #3). Reads only the
    // worker-local consecutive-failure counter (no shared pool state), so it is
    // lock-free; named *Locked only for call-site convention (it is invoked from
    // the worker loop where lk may or may not be held). Returns a sleep duration in
    // deciseconds in [CONNRETRYINTERVAL, ~1.5*CONNRETRYMAXINTERVAL]. Defined out-of-
    // line in ws_pool.cpp.
    dstime reconnectBackoffDsLocked(int retryCount) const;

    void retryChunksOnTheWire(WsConn* ws);
    void retryChunksOnTheWireLocked(WsConn* ws);

    // Remove any queued/in-flight chunks for the specified file.
    // Must be called with UploadEngine::Impl::uploadMutex held.
    void purgeFileLocked(const std::uint32_t fileno);

    // Tier 2 D-cancel-only (loss-recovery): drop any QUEUED resend (mToResend) whose
    // (fileno,pos,len) matches exactly, so a chunk the server has already deduped
    // (opcode-2 AlreadyOnServer) is not re-read + re-sent on the next drain (the loss
    // amplifier). Touches ONLY mToResend -- never mChunksInFlight (the ack firewall) and
    // never mBytesConfirmed (no byte credit; the len-blind ack-match makes crediting on
    // opcode-2 unsafe). Caller must hold UploadEngine::Impl::uploadMutex.
    void purgeQueuedResendForRangeLocked(const std::uint32_t fileno,
                                         const m_off_t pos,
                                         const int len);

    void applyInFlightLocked(const std::uint32_t fileno);
    void applyInFlight(const std::uint32_t fileno);
    bool sendChunk(WsConn* ws, class UploadEngine::Impl& impl, dstime* retryAfterDs = nullptr);

    // Atomically `exchange(0)` each Release-safe throttle counter and
    // accumulate the previous values into `out`. Caller iterates over all pools.
    void addAndResetBenchThrottleStatsTo(UploadEngine::BenchThrottleSnapshot& out);

#ifndef NDEBUG
    void assignUploadingFileLocked(WsUploadFile* file) noexcept;
    void recordFirstByteSentLocked(WsUploadFile& file) noexcept;
    void recordUploadCompletedLocked(std::uint32_t fileno) noexcept;
    void recordWsUploadStatsSampleLocked(const class UploadEngine::Impl& impl);
    void addWsUploadStatsForTesting(UploadEngine::WsUploadStatsForTesting& out) const;
#endif
};

} // namespace ws
} // namespace mega

#endif // MEGA_USE_WSUPLOAD

#endif // MEGA_TRANSFER_WS_WSUPLOAD_INTERNAL_H
