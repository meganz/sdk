#ifndef MEGA_SCCR_RAIDPROXY_H
#define MEGA_SCCR_RAIDPROXY_H 1


#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
//#include "raidstub.h"
//#include "mega/base64.h"
#include "mega/raid.h"
#include "mega.h"


namespace mega::SCCR {

#define RAIDPARTS 6
#define RAIDSECTOR 16
#define RAIDLINE ((RAIDPARTS-1)*RAIDSECTOR)

#define NUMLINES 4096

#define MAXRETRIES 10

#define READAHEAD ((off_t)NUMLINES*RAIDSECTOR)

// number of senddata() requests until the next interval check is conducted
#define LAGINTERVAL 256

typedef uint128_t raidsector_t;
using HttpReqType = HttpReqDL;
using HttpReqPtr = std::shared_ptr<HttpReqType>;
using HttpInputBuf = mega::HttpReq::http_buf_t;
//using raidTime = mega::dstime;

#pragma pack(push,1)
struct RaidPart
{
    std::string tempUrl;

    //RaidPart(const char* tempUrl_) : tempUrl(tempUrl_) {}
};
/*
struct DirectTicket
{
    unsigned char hash[ChunkedHash::HASHLEN];
    size_t size, pos, rem;
    raidTime timestamp;  // timestamp of underlying user ticket
    short partshard;    // bits 0...9: shard, bits 10+: RAID part
};
*/
#pragma pack(pop)

class RaidReqPool;
class RaidReqPoolArray; 

class PartFetcher
{
    class RaidReq* rr;

    //char hash[ChunkedHash::HASHLEN];
    std::string url;

    raidTime delayuntil;

    struct sockaddr_in6 target;
    std::unique_ptr<HttpInputBuf> inbuf;
    char outbuf[96];

    bool skip_setposrem;
    void setposrem();

    char consecutive_errors;

public:
    char part;
    bool connected;
    unsigned remfeed;
    std::chrono::time_point<std::chrono::system_clock> postTime;

    int errors;

    raidTime lastdata;
    raidTime lastconnect;

    off_t sourcesize;
    off_t pos, rem;
    map<off_t, pair<byte*, unsigned>> readahead;   // read-ahead data

    static bool updateGlobalBytesReceived;
    static std::atomic<uint64_t> globalBytesReceived;

    bool setsource(const std::string&, RaidReq*, int);
    int trigger(raidTime = 0, bool = false);
    bool directTrigger(bool = true);
    void closesocket(bool = false);
    int io();
    void cont(int);
    bool isslow();
    bool feedreadahead();
    void resume(bool = false);
    int onFailure();

    PartFetcher();
    ~PartFetcher();
};

class RaidReq
{
    friend class PartFetcher;
    friend class RaidReqPool;
    RaidReqPool& pool;
    std::shared_ptr<CloudRaid> cloudRaid;
    mutex rr_lock;
    //std::array<HttpReqPtr, RAIDPARTS> sockets; // fast lookup on a single cache line
    std::vector<HttpReqPtr> sockets;
    std::array<PartFetcher, RAIDPARTS> fetcher;

    int partpos[RAIDPARTS];             // incoming part positions relative to dataline
    unsigned feedlag[RAIDPARTS];        // accumulated remfeed at shiftata() to identify slow sources
    int lagrounds;                      // number of accumulated additions to feedlag[]

    typedef deque<HttpReqPtr> socket_deque;
    socket_deque pendingio;
    void handlependingio();
    void dispatchio(const HttpReqPtr&);
    int notifyeventfd = -1;

    alignas(RAIDSECTOR) byte data[NUMLINES*RAIDLINE];       // always starts on a RAID line boundary
    alignas(RAIDSECTOR) byte parity[NUMLINES*RAIDSECTOR];   // parity sectors
    char invalid[NUMLINES];             // bitfield indicating which sectors have yet to be received
    off_t dataline;                     // data's position relative to the file's beginning in RAID lines
    off_t rem;                          // bytes remaining for this request
    off_t paddedpartsize;               // the size of the biggest part (0) rounded up to the next RAIDSECTOR boundary
    int skip;                           // bytes to skip from start of data
    int completed;                      // valid data RAID lines in data

    raidTime lastdata;                   // timestamp of RaidReq creation or last data chunk forwarded to user
    bool haddata;                       // flag indicating whether any data was forwarded to user on this RaidReq
    bool reported;
    bool missingsource;                 // disable all-channel logic

    void setfast();
    void setslow(int, int);
    char slow1, slow2;                  // slow mode: the two slowest sources. otherwise, fast mode: slow1 == -1.

    void shiftdata(off_t);

    // for error reporting in raidrepair
    byte err_type = NOERR;
    short err_server = 0;
    int err_errno = 0;

    bool allconnected();

public:
    size_t filesize;
    short shard;

    enum errortype { NOERR, READERR, WRITEERR, CONNECTERR }; // largest is the one reported

    void procdata(int, byte*, off_t, int);

    off_t readdata(byte*, off_t);
    off_t senddata(byte*, off_t);

    void resumeall();

    void procreadahead();
    void watchdog();
    bool isSocketConnected(size_t);
    void disconnect();

    string getfaildescription();
    //int setClient(MegaClient* mClient) { if (!mClient) return 0; client = mClient; return 1; }

    struct Params
    {
        std::vector<std::string> tempUrls;
        size_t filesize;
        m_off_t start;
        size_t reqlen;
        int skippart;
        Params(const std::vector<std::string>& tempUrls, size_t cfilesize, m_off_t cstart, size_t creqlen, int cskippart)
            : tempUrls(tempUrls), filesize(cfilesize), start(cstart), reqlen(creqlen), skippart(cskippart) {}
    };

    RaidReq(const Params&, RaidReqPool&, const std::shared_ptr<CloudRaid>&, int notifyfd);

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

    RaidReqPoolArray& array;
    recursive_mutex rrp_lock, rrp_queuelock;
    //pthread_t rrp_thread;
    std::thread rrp_thread;
    int efd;
    std::atomic<bool> isRunning;
 
    void* raidproxyiothread();
    static void* raidproxyiothreadstart(void* arg);
    
    std::map<RaidReq*, unique_ptr<RaidReq>> rrs;

    typedef set<pair<raidTime, HttpReqPtr>> timesocket_set;
    typedef set<HttpReqPtr> directsocket_set;
    typedef deque<HttpReqPtr> directsocket_queue;
    timesocket_set scheduledio;
    directsocket_set directio_set;
    directsocket_queue directio;


public:
    RaidReqPool(RaidReqPoolArray& ar);
    ~RaidReqPool();
    RaidReq* request(const RaidReq::Params& p, const std::shared_ptr<CloudRaid>&, int notifyfd);
    void removerequest(RaidReq* rr);
    int rrcount();
    bool addScheduledio(raidTime, const HttpReqPtr&);
    bool addDirectio(const HttpReqPtr&);
};


class RaidReqPoolArray
{
    vector<unique_ptr<RaidReqPool>> rrps;

public:
    struct Token 
    {
        int poolId = -1;
        RaidReq* rr = nullptr;
        operator bool() { return poolId >= 0 && rr; }
    };

    // get size
    size_t size()
    {
        return rrps.size();
    }

    // set up with the specified number of pools to process RaidReq requests, each poll has a dedicated thread
    void start(unsigned n);

    // ask the least busy pool to process a new raid request
    Token balancedRequest(const RaidReq::Params&, const std::shared_ptr<CloudRaid>&, int notifyfd);

    // when the RaidReq has succeeded or failed, clean up with this
    void remove(Token& t);
};

} // namespace

#endif