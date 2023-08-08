#ifndef MEGA_SCCR_RAIDPROXY_H
#define MEGA_SCCR_RAIDPROXY_H 1


#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include "mega/raid.h"
#include "mega.h"


namespace mega::SCCR {

#define RAIDPARTS 6
#define RAIDSECTOR 16
#define RAIDLINE ((RAIDPARTS-1)*RAIDSECTOR)
#define NUMLINES 16384 // 16 KBs
#define MAXRETRIES 10
#define READAHEAD ((m_off_t)NUMLINES*RAIDSECTOR)

// number of readdata() requests until the next interval check is conducted
#define LAGINTERVAL 256

typedef uint128_t raidsector_t;
using HttpReqType = HttpReqDL;
using HttpReqPtr = std::shared_ptr<HttpReqType>;
using HttpInputBuf = mega::HttpReq::http_buf_t;

#pragma pack(push,1)
struct RaidPart
{
    std::string tempUrl;
};
#pragma pack(pop)

class RaidReqPool;

class PartFetcher
{
    class RaidReq* rr;

    std::string url;
    raidTime delayuntil;

    std::unique_ptr<HttpInputBuf> inbuf;
    m_off_t partStartPos;

    bool skip_setposrem;
    void setposrem();
    bool setremfeed(unsigned = NUMLINES*RAIDSECTOR);

    char consecutive_errors;

public:
    char part;
    bool connected;
    bool finished;
    unsigned remfeed;

    int errors;

    raidTime lastdata;
    raidTime lastconnect;

    std::chrono::time_point<std::chrono::system_clock> postStartTime;
    int64_t timeInflight;
    m_off_t reqBytesReceived;
    bool postCompleted;

    m_off_t sourcesize;
    m_off_t pos, rem;
    map<m_off_t, pair<byte*, unsigned>> readahead;   // read-ahead data

    bool setsource(const std::string&, RaidReq*, int);
    int trigger(raidTime = 0, bool = false);
    bool directTrigger(bool = true);
    void closesocket(bool = false);
    int io();
    void cont(int);
    bool feedreadahead();
    void resume(bool = false);
    int onFailure();
    m_off_t getSocketSpeed();

    PartFetcher();
    ~PartFetcher();
};

class RaidReq
{
    friend class PartFetcher;
    friend class RaidReqPool;
    RaidReqPool& pool;
    std::shared_ptr<CloudRaid> cloudRaid;
    recursive_mutex rr_lock;
    std::vector<HttpReqPtr> httpReqs;
    std::array<PartFetcher, RAIDPARTS> fetcher;

    int partpos[RAIDPARTS];                          // incoming part positions relative to dataline
    std::atomic<unsigned> feedlag[RAIDPARTS];        // accumulated remfeed at shiftata() to identify slow sources
    int lagrounds;                                   // number of accumulated additions to feedlag[]

    typedef deque<HttpReqPtr> socket_deque;
    socket_deque pendingio;
    void handlependingio();
    void dispatchio(const HttpReqPtr&);
    void shiftdata(m_off_t);

    alignas(RAIDSECTOR) byte data[NUMLINES*RAIDLINE];       // always starts on a RAID line boundary
    alignas(RAIDSECTOR) byte parity[NUMLINES*RAIDSECTOR];   // parity sectors
    char invalid[NUMLINES];             // bitfield indicating which sectors have yet to be received
    m_off_t dataline;                     // data's position relative to the file's beginning in RAID lines
    m_off_t rem;                          // bytes remaining for this request
    m_off_t paddedpartsize;               // the size of the biggest part (0) rounded up to the next RAIDSECTOR boundary
    int skip;                           // bytes to skip from start of data
    int completed;                      // valid data RAID lines in data

    raidTime lastdata;                   // timestamp of RaidReq creation or last data chunk forwarded to user
    bool haddata;                       // flag indicating whether any data was forwarded to user on this RaidReq
    bool reported;
    bool missingsource;                 // disable all-channel logic
    m_off_t reqStartPos;
    m_off_t maxRequestSize;

    bool allconnected(int = RAIDPARTS);
    int numPartsUnfinished();

public:
    std::chrono::time_point<std::chrono::system_clock> downloadStartTime;
    size_t filesize;

    void procdata(int, byte*, m_off_t, int);
    m_off_t readdata(byte*, m_off_t);

    void resumeall(int = RAIDPARTS);
    void procreadahead();
    void watchdog();
    void disconnect();
    int processFeedLag();

    struct Params
    {
        std::vector<std::string> tempUrls;
        size_t filesize;
        m_off_t reqStartPos;
        size_t reqlen;
        m_off_t maxRequestSize;
        int skippart;
        Params(const std::vector<std::string>& tempUrls, size_t cfilesize, m_off_t creqStartPos, size_t creqlen, m_off_t cmaxRequestSize, int cskippart)
            : tempUrls(tempUrls), filesize(cfilesize), reqStartPos(creqStartPos), reqlen(creqlen), maxRequestSize(cmaxRequestSize), skippart(cskippart) {}
    };

    RaidReq(const Params&, RaidReqPool&, const std::shared_ptr<CloudRaid>&);
    ~RaidReq();

    static size_t raidPartSize(int part, size_t fullfilesize);
};

template <class T1, class T2>
class ts_ptr_map    // thread safe pointer map - needed in order to decouple socketrrs from RaidReq locks
{
    map<T1, T2*> m;
    mutex x;
public:
    void set(T1 t1, T2* t2) { lock_guard<mutex> g(x); m[t1] = t2; }
    void del(T1 t1) { lock_guard<mutex> g(x); m.erase(t1); }
    T2* lookup(T1 t1) { lock_guard<mutex> g(x); auto it = m.find(t1); return it == m.end() ? nullptr : it->second; }
    size_t size() { lock_guard<mutex> g(x); return m.size(); }
};


class RaidReqPool
{
    // runs a thread and manages all the RaidReq assigned to it on that thread
    friend class PartFetcher;
    friend class RaidReq;
    ts_ptr_map<HttpReqPtr, RaidReq> socketrrs; // to be able to set up the handling of epoll events from any thread safely

    recursive_mutex rrp_lock, rrp_queuelock;
    std::thread rrp_thread;
    std::atomic<bool> isRunning;

    void raidproxyiothread();
    static void raidproxyiothreadstart(RaidReqPool* rrp);

    unique_ptr<RaidReq> raidReq;
    typedef set<pair<raidTime, HttpReqPtr>> timesocket_set;
    typedef deque<HttpReqPtr> directsocket_queue;
    timesocket_set scheduledio;
    set<HttpReqPtr> directio_set;


public:
    RaidReqPool();
    ~RaidReqPool();
    void request(const RaidReq::Params& p, const std::shared_ptr<CloudRaid>&);
    bool addScheduledio(raidTime, const HttpReqPtr&);
    bool addDirectio(const HttpReqPtr&);
    RaidReq* rr() { return raidReq.get(); }
};

} // namespace

#endif
