#ifndef MEGA_RAIDPROXY_H
#define MEGA_RAIDPROXY_H 1


#include <memory>
#include <vector>
#include <chrono>
#include "mega/types.h"
#include "mega/http.h"
#include "raid.h"

namespace mega {

namespace RaidProxy {

#define MAX_NUMLINES 4096                                     // max number of lines for the RaidReq::data array
#define MAXRETRIES 10                                         // max number of consecutive errors for a failing part
#define LAGINTERVAL 256                                       // number of readdata() requests until the next interval check is conducted
#define MAX_ERRORS_FOR_IDLE_GOOD_SOURCE 3                     // Error tolerance to consider a source as a candidate to be switched with a hanging source

#if defined(SUPPORTS_TI_EMULATION_MODE)

typedef unsigned int uint128_t __attribute__((mode(TI)));

#else // SUPPORTS_TI_EMULATION_MODE

struct uint128_t
{
    uint64_t parts[2];

    uint128_t& operator=(const uint128_t& other)
    {
        parts[0] = other.parts[0];
        parts[1] = other.parts[1];
        return *this;
    }

    uint128_t& operator^=(const uint128_t& other)
    {
        parts[0] ^= other.parts[0];
        parts[1] ^= other.parts[1];
        return *this;
    }
};

#endif // ! SUPPORTS_TI_EMULATION_MODE

typedef uint128_t raidsector_t;
using HttpReqType = HttpReqDL;
using HttpReqPtr = std::shared_ptr<HttpReqType>;
using HttpInputBuf = ::mega::HttpReq::http_buf_t;
using raidTime = ::mega::dstime;

class RaidReq;
class RaidReqPool;

class PartFetcher
{
    friend class RaidReq;

    RaidReq* rr{nullptr};                                     // pointer to the underlying RaidReq
    std::string mUrl;                                         // part tempURL
    std::unique_ptr<HttpInputBuf> mInbuf{nullptr};            // buffer containing the whole data for the HttpReq after successing (internal HttpReq buffer is released)
    m_off_t mPartStartPos{};                                  // starting position relative to the filesize (the underlying RaidReq can be requesting a part of the file)
    raidTime mDelayuntil{};                                   // delay before this part can be processed (checked in PartFetcher::io)
    uint8_t mConsecutiveErrors{};                             // number of consecutive errors (related to 'errors')

    uint8_t part{};                                           // raid part index
    bool mConnected{};                                        // whether the part is considered as connected. A part is considered "connected" since it is prepared (REQ_PREPARED) for HttpReq::post
    bool mFinished{};                                         // whether this part is finished (there can be pending data to process like readahead, but all the requested HttpReq data is finished)
    uint16_t mErrors{};                                       // number of errors for this part (hanging, failures, etc.)

    raidTime lastdata;                                        // last data for this part's request (keep the same naming as HttpReq, so no 'mLastData')
    std::chrono::time_point<std::chrono::system_clock> mPostStartTime{}; // starting time for HttpReq::post
    int64_t mTimeInflight{};                                  // total time in flight for this part
    m_off_t mReqBytesReceived{};                              // total number of bytes received for this part
    bool mPostCompleted{};                                    // whether a HttpReq::post has been completed

    m_off_t mSourcesize{};                                    // full source size (which can be smaller than RaidReq::paddedpartsize)
    m_off_t mPos{};                                           // part current position
    m_off_t mRem{};                                           // remaining data for this part
    m_off_t mRemfeed{};                                       // active remaining read length (related to 'rem')
    map<m_off_t, pair<byte*, unsigned>> mReadahead;           // read-ahead data

    void setposrem();                                         // sets the next read position (pos) and the remaining read length (rem/remfeed)
    bool setremfeed(m_off_t);                                 // sets the remfeed depending on the number of bytes param and the remaining (rem) part data
    int64_t onFailure(); // Handle request failures
    m_off_t getSocketSpeed() const;                           // Get part throughput in bytes per millisec

public:
    static constexpr raidTime LASTDATA_DSTIME_FOR_HANGING_SOURCE = 300;

    PartFetcher();
    ~PartFetcher();

    bool setsource(const std::string&, RaidReq*, uint8_t);    // Set URL for this source, part start pos and source size
    int64_t trigger(raidTime = 0,
                    bool = false); // Add request for processing in RaidReqPool (with an optional
                                   // delay). Also checks if this part shouldn't be processed.
    bool directTrigger(bool = true);                          // Add request for direct processing in RaidReqPool (with no delay)
    void closesocket(bool = false);                           // reset part and optionally disconnect the HttpReq (depending on if it is going to be used)
    int64_t io(); // process HttpReq
    void cont(m_off_t);                                       // request a further chunk of data from the open connection
    bool feedreadahead();                                     // Process available read ahead for this part and send it to RaidReq::procdata
    void resume(bool = false);                                // resume fetching on a parked source that has become eligible again
    m_off_t progress() const;                                 // get part progress (data inflight, readahead...)
};

class RaidReq
{
    friend class PartFetcher;

    static constexpr raidTime LASTDATA_DSTIME_FOR_REPORTING_FEED_STUCK = 1000;
    static constexpr raidTime LASTDATA_DSTIME_FOR_TIMEOUT = LASTDATA_DSTIME_FOR_REPORTING_FEED_STUCK + (LASTDATA_DSTIME_FOR_REPORTING_FEED_STUCK / 2);
    static constexpr raidTime LASTDATA_DSTIME_FOR_REPORTING_FEED_STUCK_WITH_NO_HANGING_SOURCES = 3000;
    static constexpr raidTime LASTDATA_DSTIME_FOR_TIMEOUT_WITH_NO_HANGING_SOURCES = 6000;

    RaidReqPool& mPool;
    std::shared_ptr<CloudRaid> mCloudRaid;                    // CloudRaid controller
    std::vector<HttpReqPtr> mHttpReqs;                        // Download HttpReqs
    std::array<PartFetcher, RAIDPARTS> mFetcher;

    m_off_t mNumLines;                                        // dynamic NUMLINES for data and parity buffer sizes
    size_t mDataSize;                                         // size of mData
    size_t mParitySize;                                       // size of mParity
    std::array<m_off_t, RAIDPARTS> mPartpos{};                // incoming part positions relative to dataline
    std::array<unsigned, RAIDPARTS> mFeedlag{};               // accumulated remfeed at shiftata() to identify slow sources
    alignas(RAIDSECTOR) std::unique_ptr<byte[]> mData;        // always starts on a RAID line boundary
    alignas(RAIDSECTOR) std::unique_ptr<byte[]> mParity;      // parity sectors
    std::unique_ptr<char[]> mInvalid;                         // bitfield indicating which sectors have yet to be received

    m_off_t mDataline{};                                      // data's position relative to the file's beginning in RAID lines
    m_off_t mCompleted{};                                     // valid data RAID lines in data
    m_off_t mSkip{};                                          // bytes to skip from start of data
    m_off_t mRem;                                             // bytes remaining for this request
    size_t mFilesize;                                         // total file size
    m_off_t mReqStartPos;                                     // RaidReq offset - starting pos (a RaidReq can request just a part of the whole file)
    m_off_t mPaddedpartsize;                                  // the size of the biggest part (0) rounded up to the next RAIDSECTOR boundary

    int mLagrounds{};                                         // number of accumulated additions to feedlag[]
    raidTime lastdata;                                        // timestamp of RaidReq creation or last data chunk forwarded to user
    bool mHaddata{};                                          // flag indicating whether any data was forwarded to user on this RaidReq
    bool mReported{};                                         // whether a feed stuck (RaidReq not progressing) has been already reported
    bool mMissingSource{};                                    // disable all-channel logic
    bool mFaultysourceadded{};                                // whether a failing or hanging source has been added to faulty servers storage
    uint8_t mUnusedRaidConnection;                            // Unused connection or bad source

    void calculateNumLinesAndBufferSizes();                   // Calculate the number of lines (NUMLINES) depending on the file size, and use that value to calculate mData and mParity buffer sizes
    void shiftdata(m_off_t);                                  // shift already served data from the data array
    bool allconnected(uint8_t = RAIDPARTS) const;             // whether all sources are connected, optionally excluding a RAIDPART (default value 'RAIDPARTS' won't exclude any part)
    uint8_t numPartsUnfinished() const;                       // how many parts are unfinished, the unused part will always count as "unfinished"
    uint8_t hangingSources(uint8_t*, uint8_t*);               // how many sources are hanging (lastdata from the HttpReq exceeds the hanging time value 'LASTDATA_DSTIME_FOR_HANGING_SOURCE')
    void watchdog();                                          // check hanging sources
    bool differenceBetweenPartsSpeedIsSignificant(uint8_t part1, uint8_t part2) const;  // return whether there is a significant difference between two parts based on a ratio (part1 should be faster than part2)
    bool getSlowestAndFastestParts(uint8_t&, uint8_t&, bool = true) const; // Get slowest and fastest parts (using feedlag)

public:
    struct Params
    {
        std::vector<std::string> tempUrls;
        size_t filesize;
        m_off_t reqStartPos;
        size_t reqlen;
        Params(const std::vector<std::string>& tempUrls, size_t cfilesize, m_off_t creqStartPos, size_t creqlen)
            : tempUrls(tempUrls), filesize(cfilesize), reqStartPos(creqStartPos), reqlen(creqlen) {}
    };

    RaidReq(const Params&, RaidReqPool&, const std::shared_ptr<CloudRaid>&);
    ~RaidReq();

    void procdata(uint8_t, byte*, m_off_t, m_off_t);          // process HttpReq data, either for read ahead or for assembled data buffer
    m_off_t readdata(byte*, m_off_t);                         // serve completed data to the external byte buffer param

    void dispatchio(const HttpReqPtr&);                       // add active requests to RaidReqPool for HttpReq processing
    void resumeall(uint8_t = RAIDPARTS);                      // resume part fetchers
    void procreadahead();                                     // process read ahead data
    void disconnect();                                        // disconnect all HttpReqs
    uint8_t processFeedLag();                                 // check slow sources
    m_off_t progress() const;                                 // get the progress of the whole RaidReq (including part fetchers)
    uint8_t unusedPart() const;                               // inactive source (RAIDPARTS for no inactive source)
    std::pair<::mega::error, raidTime> checkTransferFailure(); // Check if CloudRaid transfer has failed (it could have happened in other RaidReq)
    bool setNewUnusedRaidConnection(uint8_t part,             // set the shared unused raid connection in CloudRaid. Optionally add them to faulty servers persistent storage.
                                    bool addToFaultyServers = true);

    static size_t raidPartSize(uint8_t part, size_t fullfilesize);  // calculate part size
};

class RaidReqPool
{
    bool mIsRunning{true};                                    // RaidReqPool loop control flag
    std::unique_ptr<RaidReq> mRaidReq{nullptr};               // RaidReq owned by this RaidReqPool
    std::set<HttpReqPtr> mSetHttpReqs;                        // HttpReq set to avoid repetition in scheduledio
    std::set<std::pair<raidTime, HttpReqPtr>> mScheduledio;   // HttpReqs to be processed in raidproxyio() at raidTime

public:
    RaidReqPool();
    ~RaidReqPool();

    void raidproxyio();                                       // process HttpReqs from scheduledio
    void request(const RaidReq::Params& p, const std::shared_ptr<CloudRaid>&); // create and add RaidReq to RaidReqPool
    bool addScheduledio(raidTime, const HttpReqPtr&);         // add HttpReq to scheduledio collection
    bool addDirectio(const HttpReqPtr&);                      // add HttpReq for immediate io processing
    bool lookupHttpReq(const HttpReqPtr& httpReq) { return mSetHttpReqs.find(httpReq) != mSetHttpReqs.end(); }
    bool removeio(const HttpReqPtr&);                         // remove HttpReq from scheduledio
    RaidReq* rr() { return mRaidReq.get(); }                  // returns RaidReq pointer
};

} // namespace RaidProxy

} // namespace mega

#endif
