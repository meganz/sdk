#ifndef MEGA_RAIDPROXY_H
#define MEGA_RAIDPROXY_H 1


#include <memory>
#include <vector>
#include <chrono>
#include "mega/types.h"
#include "mega/http.h"
#include "raid.h"

namespace mega::RaidProxy {

#define NUMLINES 16384
#define MAXRETRIES 10
#define READAHEAD (static_cast<m_off_t>(NUMLINES * RAIDSECTOR))

#define LAGINTERVAL 256                   // number of readdata() requests until the next interval check is conducted
#define MAX_ERRORS_FOR_IDLE_GOOD_SOURCE 3 // Error tolerance to consider a source as a candidate to be switched with a hanging source


#if defined(__GNUC__)
typedef unsigned int uint128_t __attribute__((mode(TI)));
#else
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
#endif

typedef uint128_t raidsector_t;
using HttpReqType = HttpReqDL;
using HttpReqPtr = std::shared_ptr<HttpReqType>;
using HttpInputBuf = ::mega::HttpReq::http_buf_t;
using raidTime = ::mega::dstime;

class RaidReq;
class RaidReqPool;

class PartFetcher
{
    RaidReq* rr{nullptr};
    std::string url;
    std::unique_ptr<HttpInputBuf> inbuf{nullptr};
    m_off_t partStartPos{};
    raidTime delayuntil{};
    uint8_t consecutive_errors{};
    bool skip_setposrem{};

    void setposrem();
    bool setremfeed(m_off_t = NUMLINES * RAIDSECTOR);

public:
    static constexpr raidTime LASTDATA_DSTIME_FOR_HANGING_SOURCE = 300;

    uint8_t part{};
    bool connected{};
    bool finished{};
    m_off_t remfeed{};
    uint16_t errors{};

    raidTime lastdata;
    raidTime lastconnect{};

    std::chrono::time_point<std::chrono::system_clock> postStartTime{};
    int64_t timeInflight{};
    m_off_t reqBytesReceived{};
    bool postCompleted{};

    m_off_t sourcesize{};
    m_off_t pos{};
    m_off_t rem{};
    map<m_off_t, pair<byte*, unsigned>> readahead; // read-ahead data

    bool setsource(const std::string&, RaidReq*, uint8_t);
    int trigger(raidTime = 0, bool = false);
    bool directTrigger(bool = true);
    void closesocket(bool = false);
    int io();
    void cont(m_off_t);
    bool feedreadahead();
    void resume(bool = false);
    int onFailure();
    m_off_t getSocketSpeed() const;
    m_off_t progress() const;

    PartFetcher();
    ~PartFetcher();
};

class RaidReq
{
    friend class PartFetcher;
    friend class RaidReqPool;

    static constexpr raidTime LASTDATA_DSTIME_FOR_REPORTING_FEED_STUCK = 1000;
    static constexpr raidTime LASTDATA_DSTIME_FOR_TIMEOUT = LASTDATA_DSTIME_FOR_REPORTING_FEED_STUCK + (LASTDATA_DSTIME_FOR_REPORTING_FEED_STUCK / 2);
    static constexpr raidTime LASTDATA_DSTIME_FOR_REPORTING_FEED_STUCK_WITH_NO_HANGING_SOURCES = 3000;
    static constexpr raidTime LASTDATA_DSTIME_FOR_TIMEOUT_WITH_NO_HANGING_SOURCES = 6000;

    static constexpr size_t DATA_SIZE = NUMLINES * RAIDLINE;
    static constexpr size_t PARITY_SIZE = NUMLINES * RAIDSECTOR;

    RaidReqPool& pool;
    std::shared_ptr<CloudRaid> cloudRaid;                     // CloudRaid controller
    std::vector<HttpReqPtr> httpReqs;                         // Download HttpReqs
    std::array<PartFetcher, RAIDPARTS> fetcher;

    std::array<m_off_t, RAIDPARTS> partpos{};                 // incoming part positions relative to dataline
    std::array<unsigned, RAIDPARTS> feedlag{};                // accumulated remfeed at shiftata() to identify slow sources
    alignas(RAIDSECTOR) std::unique_ptr<byte[]> data;         // always starts on a RAID line boundary
    alignas(RAIDSECTOR) std::unique_ptr<byte[]> parity;       // parity sectors
    std::unique_ptr<char[]> invalid;                          // bitfield indicating which sectors have yet to be received

    m_off_t dataline{};                                       // data's position relative to the file's beginning in RAID lines
    m_off_t completed{};                                      // valid data RAID lines in data
    m_off_t skip{};                                           // bytes to skip from start of data
    m_off_t rem;                                              // bytes remaining for this request
    size_t filesize;                                          // total file size
    m_off_t reqStartPos;                                      // RaidReq offset - starting pos (a RaidReq can request just a part of the whole file)
    m_off_t paddedpartsize;                                   // the size of the biggest part (0) rounded up to the next RAIDSECTOR boundary
    m_off_t maxRequestSize;

    int lagrounds{};                                          // number of accumulated additions to feedlag[]
    raidTime lastdata;                                        // timestamp of RaidReq creation or last data chunk forwarded to user
    bool haddata{};                                           // flag indicating whether any data was forwarded to user on this RaidReq
    bool reported{};                                          // whether a feed stuck (RaidReq not progressing) has been already reported
    bool missingsource{};                                     // disable all-channel logic

    void dispatchio(const HttpReqPtr&);                       // add active requests to RaidReqPool for HttpReq processing
    void shiftdata(m_off_t);                                  // shift already served data from the data array
    bool allconnected(uint8_t = RAIDPARTS) const;             // whether all sources are connected, optionally excluding a RAIDPART (default value 'RAIDPARTS' won't exclude any part)
    uint8_t unusedPart() const;                               // inactive source (RAIDPARTS for no inactive source)
    uint8_t numPartsUnfinished() const;                       // how many parts are unfinished, the unused part will always count as "unfinished"
    uint8_t hangingSources(uint8_t*, uint8_t*);               // how many sources are hanging (lastdata from the HttpReq exceeds the hanging time value 'LASTDATA_DSTIME_FOR_HANGING_SOURCE')

public:
    struct Params
    {
        std::vector<std::string> tempUrls;
        size_t filesize;
        m_off_t reqStartPos;
        size_t reqlen;
        m_off_t maxRequestSize;
        Params(const std::vector<std::string>& tempUrls, size_t cfilesize, m_off_t creqStartPos, size_t creqlen, m_off_t cmaxRequestSize)
            : tempUrls(tempUrls), filesize(cfilesize), reqStartPos(creqStartPos), reqlen(creqlen), maxRequestSize(cmaxRequestSize) {}
    };

    RaidReq(const Params&, RaidReqPool&, const std::shared_ptr<CloudRaid>&);
    ~RaidReq();

    void procdata(uint8_t, byte*, m_off_t, m_off_t);
    m_off_t readdata(byte*, m_off_t);

    void resumeall(uint8_t = RAIDPARTS);
    void procreadahead();
    void watchdog();
    void disconnect();
    uint8_t processFeedLag();
    m_off_t progress() const;

    static size_t raidPartSize(uint8_t part, size_t fullfilesize);
};

class RaidReqPool
{
    friend class PartFetcher;
    friend class RaidReq;

    bool isRunning{true};
    std::unique_ptr<RaidReq> raidReq{nullptr};
    std::set<HttpReqPtr> setHttpReqs;
    std::set<std::pair<raidTime, HttpReqPtr>> scheduledio;

public:
    RaidReqPool();
    ~RaidReqPool();
    void raidproxyio();
    void request(const RaidReq::Params& p, const std::shared_ptr<CloudRaid>&);
    bool addScheduledio(raidTime, const HttpReqPtr&);
    bool addDirectio(const HttpReqPtr&);
    bool removeio(const HttpReqPtr&);
    RaidReq* rr() { return raidReq.get(); }
};

} // namespace

#endif
