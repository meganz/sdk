#ifndef MEGA_RAIDPROXY_H
#define MEGA_RAIDPROXY_H 1


#include <memory>
#include <vector>
#include <chrono>
#include "mega/types.h"
#include "mega/http.h"
#include "raid.h"

namespace mega::RaidProxy {

#define NUMLINES 16384 // 16 KBs
#define MAXRETRIES 10
#define READAHEAD ((m_off_t)NUMLINES*RAIDSECTOR)

// number of readdata() requests until the next interval check is conducted
#define LAGINTERVAL 256


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

typedef uint128_t raidsector_t; // RAIDSECTOR = 16
using HttpReqType = HttpReqDL;
using HttpReqPtr = std::shared_ptr<HttpReqType>;
using HttpInputBuf = ::mega::HttpReq::http_buf_t;
using raidTime = ::mega::dstime;

class RaidReqPool;

class PartFetcher
{
    class RaidReq* rr;

    std::string url;
    std::unique_ptr<HttpInputBuf> inbuf;
    m_off_t partStartPos;
    raidTime delayuntil;
    char consecutive_errors;
    bool skip_setposrem;

    void setposrem();
    bool setremfeed(m_off_t = NUMLINES * RAIDSECTOR);

public:
    int part;
    bool connected;
    bool finished;
    m_off_t remfeed;
    int errors;

    raidTime lastdata;
    raidTime lastconnect;

    std::chrono::time_point<std::chrono::system_clock> postStartTime;
    int64_t timeInflight;
    m_off_t reqBytesReceived;
    bool postCompleted;

    m_off_t sourcesize;
    m_off_t pos, rem;
    map<m_off_t, pair<byte*, unsigned>> readahead; // read-ahead data

    bool setsource(const std::string&, RaidReq*, int);
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
    RaidReqPool& pool;
    std::shared_ptr<CloudRaid> cloudRaid;
    std::vector<HttpReqPtr> httpReqs;
    std::array<PartFetcher, RAIDPARTS> fetcher;

    m_off_t partpos[RAIDPARTS];          // incoming part positions relative to dataline
    unsigned feedlag[RAIDPARTS];         // accumulated remfeed at shiftata() to identify slow sources
    int lagrounds;                       // number of accumulated additions to feedlag[]

    alignas(RAIDSECTOR) byte data[NUMLINES*RAIDLINE];       // always starts on a RAID line boundary
    alignas(RAIDSECTOR) byte parity[NUMLINES*RAIDSECTOR];   // parity sectors
    char invalid[NUMLINES];              // bitfield indicating which sectors have yet to be received
    m_off_t dataline;                    // data's position relative to the file's beginning in RAID lines
    m_off_t rem;                         // bytes remaining for this request
    m_off_t paddedpartsize;              // the size of the biggest part (0) rounded up to the next RAIDSECTOR boundary
    m_off_t skip;                        // bytes to skip from start of data
    m_off_t completed;                   // valid data RAID lines in data
    size_t filesize;

    raidTime lastdata;                   // timestamp of RaidReq creation or last data chunk forwarded to user
    bool haddata;                        // flag indicating whether any data was forwarded to user on this RaidReq
    bool reported;
    bool missingsource;                  // disable all-channel logic
    m_off_t reqStartPos;
    m_off_t maxRequestSize;

    void dispatchio(const HttpReqPtr&);
    void shiftdata(m_off_t);
    bool allconnected(int = RAIDPARTS) const;
    int numPartsUnfinished() const;
    uint8_t unusedPart() const;                               // inactive source (RAIDPARTS for no inactive source)

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

    void procdata(int, byte*, m_off_t, m_off_t);
    m_off_t readdata(byte*, m_off_t);

    void resumeall(int = RAIDPARTS);
    void procreadahead();
    void watchdog();
    void disconnect();
    int processFeedLag();
    m_off_t progress() const;

    static size_t raidPartSize(int part, size_t fullfilesize);
};

class RaidReqPool
{
    friend class PartFetcher;
    friend class RaidReq;

    bool isRunning;
    std::unique_ptr<RaidReq> raidReq;
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
