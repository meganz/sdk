#ifndef MEGA_SCCR_RAIDPROXY_H
#define MEGA_SCCR_RAIDPROXY_H 1


#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include "chunkedhash.h"

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

#pragma pack(push,1)
struct RaidPart
{
    unsigned short serverid;                    // source server
    unsigned char hash[ChunkedHash::HASHLEN];   // hash of RAID part
};

struct DirectTicket
{
    unsigned char hash[ChunkedHash::HASHLEN];
    size_t size, pos, rem;
    mtime_t timestamp;  // timestamp of underlying user ticket
    short partshard;    // bits 0...9: shard, bits 10+: RAID part
};
#pragma pack(pop)

class RaidReqPool;
class RaidReqPoolArray; 

class PartFetcher
{
    class RaidReq* rr;

    char hash[ChunkedHash::HASHLEN];

    mtime_t delayuntil;

    struct sockaddr_in6 target;
    char outbuf[96];

    bool skip_setposrem;
    void setposrem();

    char consecutive_errors;

public:
    char part;
    bool connected;
    unsigned short serverid;

    unsigned remfeed;

    int errors;

    mtime_t lastdata;
    mtime_t lastconnect;

    off_t sourcesize;
    off_t pos, rem;
    map<off_t, pair<char*, unsigned>> readahead;   // read-ahead data

    static bool updateGlobalBytesReceived;
    static atomic<uint64_t> globalBytesReceived;

    bool setsource(short, byte*, RaidReq*, int);
    int trigger(int = 0);
    void closesocket();
    int io();
    void cont(int);
    bool isslow();
    bool feedreadahead();
    void resume();

    PartFetcher();
    ~PartFetcher();
};

class RaidReq
{
    friend class PartFetcher;
    friend class RaidReqPool;
    RaidReqPool& pool;
    mutex rr_lock;
    array<int, RAIDPARTS> sockets; // fast lookup on a single cache line
    array<PartFetcher, RAIDPARTS> fetcher;

    int partpos[RAIDPARTS];             // incoming part positions relative to dataline
    unsigned feedlag[RAIDPARTS];        // accumulated remfeed at shiftata() to identify slow sources
    int lagrounds;                      // number of accumulated additions to feedlag[]

    typedef deque<int> socket_deque;
    socket_deque pendingio;
    void handlependingio();
    void dispatchio(int);
    int notifyeventfd = -1;

    alignas(RAIDSECTOR) char data[NUMLINES*RAIDLINE];       // always starts on a RAID line boundary
    alignas(RAIDSECTOR) char parity[NUMLINES*RAIDSECTOR];   // parity sectors
    char invalid[NUMLINES];             // bitfield indicating which sectors have yet to be received
    off_t dataline;                     // data's position relative to the file's beginning in RAID lines
    off_t rem;                          // bytes remaining for this request
    off_t paddedpartsize;               // the size of the biggest part (0) rounded up to the next RAIDSECTOR boundary
    int skip;                           // bytes to skip from start of data
    int completed;                      // valid data RAID lines in data

    mtime_t lastdata;                   // timestamp of RaidReq creation or last data chunk forwarded to user
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
    mtime_t tickettime;
    short shard;

    enum errortype { NOERR, READERR, WRITEERR, CONNECTERR }; // largest is the one reported

    void procdata(int, char*, off_t, int);

    off_t readdata(char*, off_t);
    off_t senddata(int, off_t);

    void resumeall();

    void procreadahead();
    void watchdog();

    string getfaildescription();

    struct Params
    {
        unsigned short serverid0;
        unsigned char* hash0;
        RaidPart* p1to5;
        size_t filesize;
        off_t start;
        size_t reqlen;
        short shard;
        mtime_t tickettime;
        int skippart;
        Params(unsigned short s0, unsigned char* h0, RaidPart* cp1to5, size_t cfilesize, off_t cstart, size_t creqlen, short cshard, mtime_t ctickettime, int cskippart)
            : serverid0(s0), hash0(h0), p1to5(cp1to5), filesize(cfilesize), start(cstart), reqlen(creqlen), shard(cshard), tickettime(ctickettime), skippart(cskippart) {}
    };

    RaidReq(const Params&, RaidReqPool&, int notifyfd);

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
    ts_ptr_map<int, RaidReq> socketrrs; // to be able to set up the handling of epoll events from any thread safely

    RaidReqPoolArray& array;
    mutex rrp_lock;
    int efd;

    void* raidproxyiothread();
    static void* raidproxyiothreadstart(void* arg);
    
    std::map<RaidReq*, unique_ptr<RaidReq>> rrs;

    typedef set<pair<mtime_t, int>> timesocket_set;
    timesocket_set scheduledio;

public:
    RaidReqPool(RaidReqPoolArray& ar);
    RaidReq* request(const RaidReq::Params& p, int notifyfd);
    void removerequest(RaidReq* rr);
    int rrcount();
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

    // set up with the specified number of pools to process RaidReq requests, each poll has a dedicated thread
    void start(unsigned n);

    // ask the least busy pool to process a new raid request
    Token balancedRequest(const RaidReq::Params&, int notifyfd);

    // when the RaidReq has succeeded or failed, clean up with this
    void remove(Token& t);
};

} // namespace

#endif