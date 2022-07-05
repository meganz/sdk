#include "mega/sccloudraid/raidproxy.h"
#include "mega/sccloudraid/mega.h"
#include <algorithm>
#include <map>
#include <sstream>

using namespace mega::SCCR;

// DONE: add connection re-use between RaidReqs
// DONE: allow multiple CloudRAID threads with RaidReq-level mutexes
// FIXME: read local files directly
// FIXME: use predictive HTTP GET request pipelining to avoid RTTs
// DONE: use all connections
// FIXME: switch back to five connections when beneficial (less reconnection overhead)

#define MAXEPOLLEVENTS 1024
#define MAX_DELAY_IN_SECONDS 30

bool PartFetcher::updateGlobalBytesReceived;
std::atomic<uint64_t> PartFetcher::globalBytesReceived;

static std::atomic<int> current_readahead, highest_readahead;
static std::atomic<int> current_fast, current_slow;
static std::atomic<int> connest, connerr, readerr;
static std::atomic<int> bytesreceived;

void proxylog()
{
    static raidTime lasttime;
    syslog(LOG_INFO, "RAIDPROXY ra=%d f=%d s=%d conn=%d connerr=%d readerr=%d bytes/s=%d", (int)highest_readahead, (int)current_fast, (int)current_slow, (int)connest, (int)connerr, (int)readerr, (int)bytesreceived/(currtime-lasttime+1));
    highest_readahead = 0;
    connest = 0;
    connerr = 0;
    readerr = 0;
    lasttime = currtime;
    bytesreceived = 0;
}

PartFetcher::PartFetcher()
{
    rr = NULL;
    connected = false;
    skip_setposrem = false;

    pos = 0; 
    rem = 0;

    errors = 0;
    consecutive_errors = 0;
    lastdata = currtime;

    delayuntil = 0;

    /*
    memset(&target, 0, sizeof(target));
    target.sin6_family = AF_INET6;
    target.sin6_port = ntohs(80);   // we don't use SSL for inter-server encrypted file transfer
    */
}

PartFetcher::~PartFetcher()
{
    std::cout << "[PartFetcher::~PartFetcher] [part="<<std::to_string(part)<<"] BEGIN [connected="<<connected<<", rr="<<rr<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    if (rr)
    {
        closesocket();

        while (!readahead.empty())
        {
            free(readahead.begin()->second.first);
            readahead.erase(readahead.begin());
current_readahead--;
        }
    }
    std::cout << "[PartFetcher::~PartFetcher] [part="<<std::to_string(part)<<"] END [connected="<<connected<<", rr="<<rr<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
}

bool PartFetcher::setsource(/*short cserverid, byte* chash*/const std::string& partUrl, RaidReq* crr, int cpart)
{
    url = partUrl;
    part = cpart;
    rr = crr;
    /*
    memcpy(hash, chash, sizeof hash);

    char buf[32];

    sprintf(buf, "storage.%d", serverid);

    if (!config.getallips(buf, &target.sin6_addr, 1))
    {
        LOGF("E 10801 Unknown CloudRAID target server: %d", serverid);
        errors++;
        return false;
    }
    */

    sourcesize = RaidReq::raidPartSize(part, rr->filesize);
    return true;
}

size_t RaidReq::raidPartSize(int part, size_t fullfilesize)
{
    // compute the size of this raid part based on the original file size len
    int r = fullfilesize % RAIDLINE;   // residual part
    
    // parts 0 (parity) & 1 (largest data) are the same size
    int t = r-(part-!!part)*RAIDSECTOR;

    // (excess length will be found in the following sectors,
    // negative length in the preceding sectors)
    if (t < 0) t = 0;
    else if (t > RAIDSECTOR) t = RAIDSECTOR;

    return (fullfilesize-r)/(RAIDPARTS-1)+t;
}

#define OWNREADAHEAD 64
#define ISOWNREADAHEAD(X) ((X) & OWNREADAHEAD)
#define VALIDPARTS(X) ((X) & (OWNREADAHEAD-1))

#define SECTORFLOOR(X) ((X) & -RAIDSECTOR)

// sets the next read position (pos) and the remaining read length (rem/remfeed),
// taking into account all readahead data and ongoing reads on other connected
// fetchers.
void PartFetcher::setposrem()
{
    std::cout << "[PartFetcher::setposrem] [part="<<std::to_string(part)<<"] BEGIN [pos="<<pos<<", rem="<<rem<<", remfeed="<<remfeed<<", sourcesize="<<sourcesize<<", connected="<<connected<<", rr->paddedpartsize="<<rr->paddedpartsize<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
    // we want to continue reading at the 2nd lowest position:
    // take the two lowest positions and use the higher one.
    static thread_local map<off_t, char> chunks;
    off_t basepos = rr->dataline*RAIDSECTOR;
    off_t curpos = basepos+rr->partpos[(int)part];

    // determine the next suitable read range to ensure the availability
    // of RAIDPARTS-1 sources based on ongoing reads and stored readahead data.
    for (int i = RAIDPARTS; i--; )
    {
        // compile boundaries of data chunks that have been or are being fetched
        // (we do not record the beginning, as position 0 is implicitly valid)
        // a) already read data in the PartReq buffer
        chunks[SECTORFLOOR(basepos+rr->partpos[i])]--;

        // b) ongoing fetches on *other* channels
        if (i != part)
        {
            if (rr->fetcher[i].rem)
            {
                chunks[SECTORFLOOR(rr->fetcher[i].pos)]++;
                chunks[SECTORFLOOR(rr->fetcher[i].pos+rr->fetcher[i].rem)]--;
            }
        }

        // c) existing readahead data
        auto it = rr->fetcher[i].readahead.begin();
        auto end = rr->fetcher[i].readahead.end();

        while (it != end)
        {
            off_t t = it->first;

            // we mark our own readahead as always valid to prevent double reads
            char delta = 1;
            if (i == part) delta += OWNREADAHEAD;

            chunks[SECTORFLOOR(t)] += delta;

            // (concatenate contiguous readahead chunks to reduce chunks inserts)
            do {
                t += it->second.second;
            }  while (++it != end && t == it->first);

            chunks[SECTORFLOOR(t)] -= delta;
        }
    }

    // find range from the first position after the current read position
    // with less than RAIDPARTS-1 valid sources
    // (where we need to start fetching) to the first position thereafter with
    // RAIDPARTS-1 valid sources, if any (where we would need to stop fetching)
    char valid = RAIDPARTS;
    off_t startpos = -1, endpos = -1;

    for (auto it = chunks.begin(); it != chunks.end(); )
    {
        auto next_it = it;
        next_it++;

        valid += it->second;

        assert(valid >= 0 && VALIDPARTS(valid) < RAIDPARTS);

        if (startpos == -1)
        {
            // no startpos yet (our own readahead is excluded by valid being bumped by OWNREADAHEAD)
            if (valid < RAIDPARTS-1)
            {
                if (curpos < it->first)
                {
                    startpos = it->first;
                }
                else if (next_it == chunks.end() || curpos < next_it->first)
                {
                    startpos = curpos;
                }
            }
        }
        else
        {
            // if this is a slow fetch, we only look ahead as far READAHEAD 
            if (isslow() && it->first-startpos > READAHEAD) break;

            // startpos valid, look for suitable endpos
            // (must not cross own readahead data or any already sufficient raidparts)
            if (valid >= RAIDPARTS-1)
            {
                endpos = it->first;
                break;
            }
        }

        it = next_it;
    }

    // clear early, hoping that much of chunks is still in the L1/L2 cache
    chunks.clear();

    // there is always a startpos, even though it may be at sourcesize
    assert(startpos != -1);

    // we always resume fetching at a raidsector boundary.
    // (this may result in the unnecessary retransmission of up to 15 bytes
    // in case we resume on the same part.)
    startpos &= -RAIDSECTOR;

    if (endpos == -1)
    {
        // no sufficient number of sources past startpos, we read to the end
        rem = rr->paddedpartsize-startpos;

        if (rem > READAHEAD && isslow())
        {
            if (startpos-rr->dataline*RAIDSECTOR > 3*READAHEAD)
            {
                // prevent runaway readahead and shut down channel until raidline hs moved closer
                rem = 0;
                return;
            }

            rem = READAHEAD;
        }
    }
    else
    {
        assert(endpos >= startpos);
        rem = endpos-startpos;
    }

    pos = startpos;

    // request 1 less RAIDSECTOR as we will add up to 1 sector of 0 bytes at the end of the file - this leaves enough buffer space in the buffer pased to procdata for us to write past the reported length
    remfeed = std::min(rem, (off_t)NUMLINES*RAIDSECTOR);
    if (sourcesize-pos < remfeed) remfeed = sourcesize-pos; // we only read to the physical end of the part

    std::cout << "[PartFetcher::setposrem] [part="<<std::to_string(part)<<"] END (startpos="<<startpos<<", endpos="<<endpos<<") [pos="<<pos<<", rem="<<rem<<", remfeed="<<remfeed<<", sourcesize="<<sourcesize<<", connected="<<connected<<", rr->paddedpartsize="<<rr->paddedpartsize<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
}

bool RaidReq::allconnected()
{
    for (int i = RAIDPARTS; i--; ) if (!fetcher[i].connected) return false;

    return true;
}

// close socket
void PartFetcher::closesocket(bool disconnect, bool socketDisconnect)
{
    if (socketDisconnect && !disconnect) disconnect = true;
    std::cout << "[PartFetcher::closesocket] [part="<<std::to_string(part)<<"] BEGIN [connected="<<connected<<"] [disconnect=" << std::to_string(disconnect) << ", socketDisconnect="<<std::to_string(socketDisconnect)<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    if (skip_setposrem)
    {
        skip_setposrem = false;
    }
    else if (disconnect)
    {
        rem = 0;
        remfeed = 0;    // need to clear remfeed so that the disconnected channel does not corrupt feedlag
    }

    if (connected)
    {
        std::cout << "[PartFetcher::closesocket] [part=" << std::to_string(part) << "] connected -> dothings [connected=" << connected << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        auto &s = rr->sockets[part];
        if (s)
        {
            std::cout << "[PartFetcher::closesocket] [part="<<std::to_string(part)<<"] [connected] s -> proceed (s="<<s<<")  [disconnect=" << std::to_string(disconnect) << ", socketDisconnect="<<std::to_string(socketDisconnect)<<", s->status="<<std::to_string(s->status)<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        //if (s->disconnect()) perror("Error closing socket");
            assert(!disconnect || s->status != REQ_INFLIGHT);
            if (socketDisconnect || s->status == REQ_INFLIGHT)
            {
                rr->cloudRaid->disconnect(s);
            }
            //s = -1;
            if (disconnect) 
            {
                s->status = REQ_READY;
                rr->pool.socketrrs.del(s);
            }
            std::cout << "[PartFetcher::closesocket] [part="<<std::to_string(part)<<"] [connected] [s] after proceed -> NEW s->status="<<std::to_string(s->status)<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        }
        else std::cout << "[PartFetcher::closesocket] [part="<<std::to_string(part)<<"] [connected] !s -> nothing (just set connected to false after this if disconnect = true) [connected="<<connected<<"] [disconnect=" << std::to_string(disconnect) << ", socketDisconnect="<<std::to_string(socketDisconnect)<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        if (disconnect)
        {
            connected = false;
        }
    }
    else std::cout << "[PartFetcher::closesocket] [part="<<std::to_string(part)<<"] !connected -> nothing [connected="<<connected<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    std::cout << "[PartFetcher::closesocket] [part="<<std::to_string(part)<<"] END [connected="<<connected<<"] [disconnect=" << std::to_string(disconnect) << ", socketDisconnect="<<std::to_string(socketDisconnect)<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
}

// (re)create, set up socket and start (optionally delayed) io on it
int PartFetcher::trigger(raidTime delay, bool reusesocket)
{
    std::cout << "[PartFetcher::trigger] BEGIN [delay=" << delay << ", reusesocket=" << reusesocket << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    if (delay == MAX_DELAY_IN_SECONDS)
    {
        rr->cloudRaid->onTransferFailure();
        return -1;
    }
    assert(!url.empty());

    /*
    if (!reusesocket)
    {
        closesocket();
    }
    */
    closesocket(!reusesocket);

    if (!rem)
    {
        std::cout << "[PartFetcher::trigger] [part="<<std::to_string(part)<<"] !rem [pos=" << pos << ", rr->paddedpartsize=" << rr->paddedpartsize << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        assert(pos <= rr->paddedpartsize);
        if (pos == rr->paddedpartsize) std::cout << "[PartFetcher::trigger] [part="<<std::to_string(part)<<"] [!rem] END -> (pos == rr->paddedpartsize) -> return -1 [pos=" << pos << ", rr->paddedpartsize=" << rr->paddedpartsize << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        if (pos == rr->paddedpartsize) return -1;
    }

    std::cout << "[PartFetcher::trigger] [part="<<std::to_string(part)<<"] Getting socket for directIO [part=" << std::to_string(part) << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    
    /*
    auto s = rr->sockets[part];

    if (!s)
    {
        std::cout << "[PartFetcher::trigger] [part="<<std::to_string(part)<<"] !s [s=" << s << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        LOGF("E 10800 Can't get CloudRAID socket (%d)", errno);
        exit(0);
    }
    */

    /*
    if ((s = socket(AF_INET6, SOCK_STREAM, 0)) < 0)
    {
        LOGF("E 10800 Can't get CloudRAID socket (%d)", errno);
        exit(0);
    }

    TcpServer::makenblock(s);
    */

    //rr->pool.socketrrs.set(s, rr);  // set up for event to be handled immediately on the wait thread 

    /*
    epoll_event event;
    event.data.fd = s;
    event.events = EPOLLRDHUP | EPOLLHUP | EPOLLIN | EPOLLOUT | EPOLLET;

    int t = epoll_ctl(rr->pool.efd, EPOLL_CTL_ADD, s, &event);

    if (t < 0)
    {
        perror("CloudRAID EPOLL_CTL_ADD failed");
        exit(0);
    }
    */
   /*
    if (directTrigger())
    {
        //rr->pool.socketrrs.set(s, rr);  // set up for event to be handled immediately on the wait thread
    }
    else
    {
        std::cout << "[PartFetcher::trigger] [part="<<std::to_string(part)<<"] !(rr->pool.addDirectio(s="<<s<<")) -> already added ????" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        //std::cout << "[PartFetcher::trigger] !(rr->pool.addDirectio(s="<<s<<")) -> CloudRAID failed -> exit(0)" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        //exit(0);
    }
    */
    if (delay < 0) delay = 0;
    directTrigger(!delay);
    //directTrigger(false);

    if (delay) delayuntil = currtime+delay;

    std::cout << "[PartFetcher::trigger] [part="<<std::to_string(part)<<"] END [delay=" << delay << ", currtime=" << currtime << ", delayuntil=" << delayuntil << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    return delay;
}

bool PartFetcher::directTrigger(bool addDirectio)
{
    std::cout << "[PartFetcher::directTrigger] [part="<<std::to_string(part)<<"] BEGIN [addDirectio="<<addDirectio<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    auto s = rr->sockets[part];

    assert(s != nullptr);
    if (!s)
    {
        std::cout << "[PartFetcher::directTrigger] [part="<<std::to_string(part)<<"] END -> !s [s=" << s << "] -> ALERT !!!! Can't get CloudRAID socket -> exit(0)" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        LOGF("E 10800 Can't get CloudRAID socket (%d)", errno);
        exit(0);
    }
    if (!connected)
    {
        std::cout << "[PartFetcher::directTrigger] [part="<<std::to_string(part)<<"] (!connected) -> rr->pool.socketrrs.set(s="<<s<<", rr) [addDirectio="<<addDirectio<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        if (addDirectio) std::cout << "[PartFetcher::trigger] [part="<<std::to_string(part)<<"] ALERT WTF -> !connected && addDirectio !!!!!!" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        rr->pool.socketrrs.set(s, rr); // set up for event to be handled immediately on the wait thread
    }
    if (addDirectio && rr->pool.addDirectio(s))
    {
        std::cout << "[PartFetcher::directTrigger] [part="<<std::to_string(part)<<"] END -> return true (rr->pool.addDirectio(s="<<s<<") == true) [addDirectio="<<addDirectio<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        return true;
    }
    if (addDirectio) std::cout << "[PartFetcher::directTrigger] [part="<<std::to_string(part)<<"] END -> return false !(rr->pool.addDirectio(s="<<s<<")) -> already added ???? [addDirectio="<<addDirectio<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    else std::cout << "[PartFetcher::directTrigger] [part="<<std::to_string(part)<<"] END -> return true ! -> addDirectio=false [addDirectio="<<addDirectio<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    return !addDirectio;
}

bool PartFetcher::isslow()
{
    return part == rr->slow1 || part == rr->slow2;
}

void RaidReq::setfast()
{
    assert(slow1 >= 0);

    current_slow--;
    current_fast++;

    slow1 = -1;
    slow2 = -1;
}

void RaidReq::setslow(int newslow1, int newslow2)
{
    if (slow1 < 0)
    {
        current_fast--;
        current_slow++;
    }

    int oldslow1 = slow1;
    int oldslow2 = slow2;

    slow1 = newslow1;
    slow2 = newslow2;

    // resume regular ops on formerly slow channels
    if (oldslow1 >= 0 && oldslow1 != newslow1 && oldslow1 != newslow2)
    {
        fetcher[oldslow1].trigger();
    }

    if (oldslow2 >= 0 && oldslow2 != newslow1 && oldslow2 != newslow2)
    {
        fetcher[oldslow2].trigger();
    }

    // start slow ops on newly slow channels
    if (newslow1 >= 0 && newslow1 != oldslow1 && newslow1 != oldslow2)
    {
        fetcher[newslow1].trigger();
    }

    if (newslow2 >= 0 && newslow2 != oldslow1 && newslow2 != oldslow2)
    {
        fetcher[newslow2].trigger();
    }
}

// perform I/O on socket (which is assumed to exist)
int PartFetcher::io()
{
    std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] BEGIN [connected="<<connected<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
    //int t;
    auto s = rr->sockets[part];
    std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] s="<<s<<", s->status="<<s->status<<" " << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;

    // FIXME: retrigger io() at retrytime
    if (!connected || s->status == REQ_READY || s->status == REQ_PREPARED || s->status == REQ_FAILURE)
    {
        assert(s->status != REQ_INFLIGHT && s->status != REQ_SUCCESS);
        // prevent spurious epoll events from triggering a delayed reconnect early
        if (currtime < delayuntil) std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] !connected ->  (currtime < delayuntil) -> return -1" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
        if (currtime < delayuntil) return -1;

        lastconnect = currtime;
        //rr->cloudRaid->prepareRequest(s, url, pos, npos);
        //t = connect(s, (const sockaddr*)&target, sizeof target);

        //if (t < 0 && errno != EINPROGRESS && errno != EALREADY)
        //if (s->status != REQ_INFLIGHT && errno != EINPROGRESS && errno != EALREADY)
        if (s->status != REQ_READY && s->status != REQ_PREPARED)
        {
            std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] [!connected] (s->status != REQ_READY && s->status != REQ_PREPARED) -> return onFailure(); [connected="<<connected<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
            return onFailure();
        }

        if (s->status == REQ_READY)
        {
            std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] [!connected] (s->status == REQ_READY) -> connected = true [connected="<<connected<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
            connected = true;
connest++;
            if (rr->slow1 < 0 && rr->allconnected())
            {
                std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] [!connected] [s->status == REQ_READY] (rr->slow1 < 0 && rr->allconnected()) -> closesocket() && return -1 (fast mode, close slowest connection)  [connected="<<connected<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
                // we only need RAIDPARTS-1 connections in fast mode, so shut down the slowest one
                closesocket();
                return -1;
            }

            // unless the fetch position/length for the connection has been computed
            // before, we do so *after* the connection so that the order in which
            // the connections are established are the first criterion for slow source
            // heuristics
            if (!rem) setposrem();

            if (rem <= 0)
            {
                std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] (rem <= 0) -> closeSocket && rr->resumeall()" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
                closesocket();
                rr->resumeall();
                return -1;
            }

            std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] prepareRequest(s="<<s<<", url='"<<url<<"', pos="<<pos<<", rem="<<rem<<") [sourcesize=" << sourcesize << "] " << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
            if (inbuf) inbuf.reset();
            rr->cloudRaid->prepareRequest(s, url, pos, pos+rem);
            assert(s->status == REQ_PREPARED);
        }

        if (s->status == REQ_PREPARED)
        {
            std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] (s->status == REQ_PREPARED) -> connected = true [connected="<<connected<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
            bool postDone = rr->cloudRaid->post(s);
            if (postDone)
            {
                lastdata = currtime;
                postTime = std::chrono::system_clock::now();
                std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] postDone -> s->status = " << std::to_string(s->status) << " (should be 0x6 -> REQ_INFLIGHT !!!)" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
            }
            else
            {
                std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] !postDone -> return onFailure() [s->status = " << std::to_string(s->status) << "]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
                
                /*
                if (s->status != REQ_FAILURE)
                {
                    // try the same server, with a small delay to avoid hammering
                    return trigger(MAX_DELAY_IN_SECONDS);
                }
                */
                return onFailure();
            }

            /*
            DirectTicket dt;

            memcpy(dt.hash, hash, sizeof dt.hash);
            dt.size = sourcesize;
            dt.pos = pos;
            dt.rem = rem;
            dt.timestamp = rr->tickettime;
            dt.partshard = rr->shard | (part << 10);

            strcpy(outbuf, "GET /dl/");
            strcpy(outbuf+8+Base64::btoa((const byte*)&dt, sizeof dt, outbuf+8), " HTTP/0.9\r\n\r\n");
            */
        }
    }

    if (connected)
    {
        if (s->status == REQ_SUCCESS)
        {
            std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] REQ_SUCCESS [inbuf="<<(inbuf?inbuf.get():(void*)0x0)<<", s->buffer_released="<<s->buffer_released<<", s->dlpos="<<s->dlpos<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
            assert(!inbuf || s->buffer_released);
            if (!inbuf)
            {
                std::chrono::time_point<std::chrono::system_clock> postEndTime = std::chrono::system_clock::now();
                std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] REQ_SUCCESS -> TIME ELAPSED = " << std::chrono::duration_cast<std::chrono::milliseconds>(postEndTime - postTime).count() << " ms" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
                assert(pos == s->dlpos);
                std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] Asserts passed [s->buf.datalen()="<<(s->bufpos-s->inpurge)<<", s->bufpos="<<s->bufpos<<", s->inpurge="<<s->inpurge<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
                inbuf.reset(s->release_buf());
                s->buffer_released = true;
            }
            else std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] Inbuf already owned -> go on processing it [pos="<<pos<<", s->dlpos="<<s->dlpos<<", s->buffer_released="<<std::to_string(s->buffer_released)<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
            std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] connected && REQ_SUCCESS -> process inputBuffer [pos="<<pos<<", s->dlpos="<<s->dlpos<<", s->buffer_released="<<std::to_string(s->buffer_released)<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
            /*
            while (*outbuf)
            {
                t = write(s, outbuf, strlen(outbuf));

                if (t <= 0)
                {
                    if (!t || errno != EAGAIN)
                    {
                        //int save_errno = errno;
                        LOGF("E 10803 CloudRAID request write to %d failed (%d)", url, errno);
                        trigger();
                        return -1;
                    }

                    break;
                }
                else
                {
                    // this is efficient due to outbuf being small and always NUL-terminated
                    strcpy(outbuf, outbuf+t);
                }
            }
            */

            // feed from network
            //s->status = REQ_READY;
            //char inbuf[NUMLINES*RAIDSECTOR];
            assert(inbuf->datalen() - pos > 0);
            while (remfeed)
            {
                size_t t = inbuf->datalen() - pos;
                if (remfeed < t) t = remfeed;
                std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] [while remfeed] t="<<t<<", remfeed="<<remfeed<<", inbuf->datalen-pos="<<inbuf->datalen()<<"-"<<"pos="<<(inbuf->datalen()-pos)<<") -> process"  << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;

                //t = read(s, inbuf, t);

                if (t == 0)
                {
                    std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] [while remfeed] t="<<t<<", remfeed="<<remfeed<<" -> t=0 -> ALERT CloudRAID data read from url='"<<url<<"' failed (probably will return -1)"  << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
                    /*if (!t || errno != EAGAIN)*/
                    //{
                        errors++;
                        int save_errno = t ? errno : 0;
                        LOGF("E 10804 CloudRAID data read from %d failed (%d)", url, save_errno);
    readerr++;
                        errors++;

                        // read error: first try previously unused source
                        if (rr->slow1 < 0)
                        {
                            int i;

                            for (i = RAIDPARTS; i--; )
                            {
                                if (!rr->fetcher[i].connected)
                                {
                                    if (!rr->fetcher[i].errors)
                                    {
                                        closesocket();
                                        rr->fetcher[i].trigger();
                                        return -1;
                                    }

                                    break;
                                }
                            }

                            if (i >= 0)
                            {
                                // no previously unused source: switch to slow mode
                                //cout << "Switching to slow mode on " << rr->slow1 << "/" << rr->slow2 << " after read error" << endl;
                                rr->setslow(part, i);
                                return -1;
                            }
                        }
                        else
                        {
                            std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] [while remfeed] [t=0] setfast() && resumeall && return trigger with delay"  << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
                            if (consecutive_errors > MAXRETRIES)
                            {
                                rr->setfast();
                            }
                            else
                            {
                                consecutive_errors++;
                            }
                        }

                        rem = 0;    // no useful data will come out of this connection
                        rr->resumeall();

                        // try the same server, with a small delay to avoid hammering
                        s->status = REQ_PREPARED;
                        return trigger(50);
                        //return trigger(MAX_DELAY_IN_SECONDS);
                    //}

                    //break;
                }
                else
                {
                    if (updateGlobalBytesReceived) globalBytesReceived += t;  // atomically added
    bytesreceived += t;
                    remfeed -= t;
                    rem -= t;

                    // completed a read: reset consecutive_errors
                    if (!rem && consecutive_errors) consecutive_errors = 0;

                    std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] [while remfeed] t="<<t<<" > 0 -> rr->procdata(part="<<std::to_string(part)<<", inbuf.datastart()+pos, pos="<<pos<<", t="<<t<<") [remfeed="<<remfeed<<", rem="<<rem<<"]"  << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
                    rr->procdata(part, inbuf->datastart()+pos, pos, t);

                    if (!connected) std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] [while reemfeed] [t > 0] ALERT!!!!! !connected -> break" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                    if (!connected) break;

                    std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] [while remfeed] [t="<<t<<">0] -> pos += t (pos="<<pos<<", t="<<t<<", pos+=t="<<pos+t<<") [remfeed="<<remfeed<<", rem="<<rem<<"]"  << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
                    pos += t;
                }
            }

            lastdata = currtime;
            //connected = false;

            if (!remfeed && pos == sourcesize && sourcesize < rr->paddedpartsize)
            {
                std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] (!remfeed && pos="<<pos<<" == sourcesize="<<sourcesize<<") && sourcesize < rr->paddedpartsize="<<rr->paddedpartsize<<") we have reached the end of a part requires padding -> rr->procdata(...), rem = 0, pos = rr->paddedpartsize [remfeed="<<remfeed<<", rem="<<rem<<", sourcesize="<<sourcesize<<", rr->paddedpartsize="<<rr->paddedpartsize<<"]"  << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
                // we have reached the end of a part requires padding
                static byte nulpad[RAIDSECTOR];

                rr->procdata(part, nulpad, pos, rr->paddedpartsize-sourcesize);
                rem = 0;
                pos = rr->paddedpartsize;
            }

            rr->procreadahead();

            if (!rem)
            {
                if (readahead.empty() && pos == rr->paddedpartsize)
                {
                    std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] [!rem] (readahead.empty() && pos == rr->paddedpartsize) -> closesocket() && rr->resumeall() [readahead.size()="<<readahead.size()<<", pos="<<pos<<", rr->paddedpartsize="<<rr->paddedpartsize<<"] [connected="<<connected<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
                    closesocket();
                    rr->resumeall();
                    std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] [!rem] END -> (readahead.empty() && pos == rr->paddedpartsize) -> return -1 [connected="<<connected<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
                    return -1;
                }
                else
                {
                    std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] [!rem] (!readahead.empty() || pos != rr->paddedpartsize) -> should call directTrigger() [readahead.size()="<<readahead.size()<<", pos="<<pos<<", rr->paddedpartsize="<<rr->paddedpartsize<<"] [connected="<<connected<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
                    // WATCH: SHOULD THIS CALL DIRECTTRIGGER() ? ANY OPTION OF CHANGING REM TO POSITIVE VALUE FROM HERE????? -> probably not: better call directTrigger()
                }

                //std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] END -> return -1 (!rem) [connected="<<connected<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
                //return -1;
            }
            else
            {
                //std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] END -> return -1 (rem) -> s->status = REQ_READY [connected="<<connected<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
                std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] (rem) -> s->status="<<s->status<<" -> lastdata = currtime[connected="<<connected<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
                //s->status = REQ_READY;
                lastdata = currtime;
            }
        }
        assert(s->status == REQ_READY || s->status == REQ_SUCCESS || s->status == REQ_INFLIGHT || s->status == REQ_FAILURE);
        std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] Check s->status="<<s->status<<" [s="<<s<<", connected="<<connected<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
        if (s->status == REQ_READY || s->status == REQ_INFLIGHT || s->status == REQ_SUCCESS)
        {
            std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] (s->status = REQ_READY || s->status == REQ_INFLIGHT || s->status == REQ_SUCCESS) -> directTrigger() [connected="<<connected<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
            directTrigger();
            //std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] (s->status = REQ_READY || s->status == REQ_INFLIGHT) -> rr->pool.addDirectio(s="<<s<<") [connected="<<connected<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
            //rr->pool.addDirectio(s);

        }
        /*
        else if (s->status == REQ_SUCCESS)
        {
            std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] (s->status == REQ_SUCCESS) -> trigger(0, reusesocket=true) [connected="<<connected<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
            //std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] (s->status = REQ_READY || s->status == REQ_INFLIGHT) -> rr->pool.addDirectio(s="<<s<<") [connected="<<connected<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
            //rr->pool.addDirectio(s);

        }
        */
        else if (s->status == REQ_FAILURE)
        {
            std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] (s->status == REQ_FAILURE) -> ALERT !!! REQ_FAILED REQ_FAILURE !!!! -> trigger(MAX_DELAY_IN_SECONDS="<<MAX_DELAY_IN_SECONDS<<") [connected="<<connected<<", s->httpstatus="<<s->httpstatus<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
           return onFailure();
        }
    }
    std::cout << "[PartFetcher::io] [part="<<std::to_string(part)<<"] END -> return -1 [connected="<<connected<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
    return -1;
}

int PartFetcher::onFailure()
{
    auto& s = rr->sockets[part];
    assert(s != nullptr);
    std::cout << "[PartFetcher::onFailure [part="<<std::to_string(part)<<"] BEGIN [s->status="<<s->status<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
    if (s->status == REQ_FAILURE)
    {
        raidTime backoff = 0;
        std::cout << "[PartFetcher::onFailu re] [part=" << std::to_string(part) << "] s->status == REQ_FAILURE [s->status=" << s->status << "]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
        {
            if (rr->cloudRaid->onRequestFailure(s, part, backoff))
            {
                assert(!backoff || s->status == REQ_PREPARED);
                std::cout << "[PartFetcher::onFailure] [part=" << std::to_string(part) << "] [s->status == REQ_FAILURE] onRequestFailure -> [backoff="<<backoff<<", s->status=" << s->status << "]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
                if (s->status == REQ_PREPARED)
                {
                    std::cout << "[PartFetcher::onFailure] [part=" << std::to_string(part) << "] [s->status == REQ_FAILURE] (s->status == REQ_PREPARED) -> return trigger(backoff, true); [backoff="<<backoff<<", s->status=" << s->status << "]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
                    return trigger(backoff);
                }
                else if (s->status == REQ_FAILURE)
                {
                    std::cout << "[PartFetcher::onFailure] [part=" << std::to_string(part) << "] [s->status == REQ_FAILURE] (s->status == REQ_FAILURE) -> Transfer failed probably -> return -1; [backoff="<<backoff<<", s->status=" << s->status << "]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
                    return -1;
                }
            }
            else std::cout << "[PartFetcher::onFailure] [part=" << std::to_string(part) << "] [s->status == REQ_FAILURE] onRequestFailure == false!!! WTF!! [backoff="<<backoff<<", s->status=" << s->status << "]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
        }

        std::cout << "[PartFetcher::onFailure] [part=" << std::to_string(part) << "] Go on handling the error [backoff="<<backoff<<", s->status=" << s->status << "]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
        int save_errno = errno;

        /*
        if (save_errno == EADDRNOTAVAIL)
        {
            LOGF("E 10809 CloudRAID proxy subsystem ran out of local ports");
            exit(0);    // we ran out of local ports: restart
        }
        */

        static raidTime lastlog;
        if (currtime > lastlog)
        {
            std::cout << "[PartFetcher::onFailure] [part=" << std::to_string(part) << "] (currtime > lastlog) -> CloudRAID connection to url=" << url << "' failed" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
            LOGF("E 10802 CloudRAID connection to %d failed (%d)", url, save_errno);
            lastlog = currtime;
        }
        connerr++;
        errors++;

        if (consecutive_errors > MAXRETRIES || s->status == REQ_READY)
        {
            std::cout << "[PartFetcher::onFailure] [part=" << std::to_string(part) << "] (consecutive_errors=" << consecutive_errors << " > MAXRETRIES=" << MAXRETRIES << " || s->status == REQ_READY) [s->status="<<s->status<<", backoff="<<backoff<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
            // we are in slow mode and have ten connection failures in a row on this
            // server - switch back to fast mode
            if (rr->slow1 >= 0)
            {
                std::cout << "[PartFetcher::onFailure] [part=" << std::to_string(part) << "] (consecutive_errors=" << consecutive_errors << " > MAXRETRIES=" << MAXRETRIES << " || s->status == REQ_READY) -> (rr->slow1 >= 0) -> Switch back to fast mode -> closesocket && setfast [s->status="<<s->status<<", backoff="<<backoff<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
                closesocket();
                rr->setfast();
                for (int i = RAIDPARTS; i--;)
                {
                    if (i != part && !rr->fetcher[i].connected)
                    {
                        rr->fetcher[i].trigger();
                    }
                }
            }

            std::cout << "[PartFetcher::onFailure] [part=" << std::to_string(part) << "] END (consecutive_errors=" << consecutive_errors << " > MAXRETRIES=" << MAXRETRIES << " || s->status == REQ_READY) -> return -1 [s->status="<<s->status<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
            return -1;
        }
        else
        {
            std::cout << "[PartFetcher::onFailure] [part=" << std::to_string(part) << "] !(consecutive_errors=" << consecutive_errors << " > MAXRETRIES=" << MAXRETRIES << " && s->status != REQ_READY) -> trigger with delay [s->status="<<s->status<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
            consecutive_errors++;
            for (int i = RAIDPARTS; i--;)
            {
                if (i != part && !rr->fetcher[i].connected)
                {
                    rr->fetcher[i].trigger();
                }
            }

            // return trigger(save_errno == ETIMEDOUT ? -1 : 3);
            std::cout << "[PartFetcher::onFailure] [part="<<std::to_string(part)<<"] return trigger(backoff) [backoff="<<backoff<<"] [s->status="<<s->status<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
            //return trigger(MAX_DELAY_IN_SECONDS);
            //return trigger(backoff ? backoff : MAX_DELAY_IN_SECONDS);
            return trigger(backoff);
        }
    }
    std::cout << "[PartFetcher::onFailure] [part="<<std::to_string(part)<<"] END -> return -1 [s->status="<<s->status<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
    return -1;
}

// request a further chunk of data from the open connection
// (we cannot call io() directly due procdata() being non-reentrant)
void PartFetcher::cont(int numbytes)
{
    std::cout << "[PartFetcher::cont] [part="<<std::to_string(part)<<"] BEGIN (numbytes="<<numbytes<<") [connected="<<connected<<", pos="<<pos<<", rr->paddedpartsize="<<rr->paddedpartsize<<"] [rem="<<rem<<", remfeed="<<remfeed<<", sourcesize="<<sourcesize<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
    if (connected && pos < rr->paddedpartsize)
    {
        remfeed = numbytes;
        if (rem < remfeed) remfeed = rem;
        if (sourcesize-pos < remfeed) remfeed = sourcesize-pos; // we only read to the physical end of the part

        auto& s = rr->sockets[part];
//assert(rr->isSocketConnected(part));
assert(s != nullptr);
        std::cout << "[PartFetcher::cont] [part="<<std::to_string(part)<<"] (numbytes="<<numbytes<<") (connected && pos < rr->paddedpartsize) -> rr->pendingio.push_back(s="<<s<<") [connected="<<connected<<", pos="<<pos<<", rr->paddedpartsize="<<rr->paddedpartsize<<"] UPDATED:[rem="<<rem<<", remfeed="<<remfeed<<", sourcesize="<<sourcesize<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
        rr->pendingio.push_back(s);
    }
    else std::cout << "[PartFetcher::cont] [part="<<std::to_string(part)<<"] (numbytes="<<numbytes<<") !(connected && pos < rr->paddedpartsize) -> NOTHING !!!!! [connected="<<connected<<", pos="<<pos<<", rr->paddedpartsize="<<rr->paddedpartsize<<"] UPDATED:[rem="<<rem<<", remfeed="<<remfeed<<", sourcesize="<<sourcesize<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
    std::cout << "[PartFetcher::cont] [part="<<std::to_string(part)<<"] END (numbytes="<<numbytes<<") [connected="<<connected<<", pos="<<pos<<", rr->paddedpartsize="<<rr->paddedpartsize<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
}

RaidReq::RaidReq(const Params& p, RaidReqPool& rrp, const std::shared_ptr<CloudRaid>& cloudRaid, int notifyfd)
    : pool(rrp)
    , cloudRaid(cloudRaid)
    , notifyeventfd(notifyfd)
{
    std::cout << "[RaidReq::RaidReq] BEGIN" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    sockets.resize(p.tempUrls.size());
    for(auto& s : sockets)
    {
        s = std::make_shared<HttpReqType>();
    }
    skip = p.start % RAIDLINE;
    dataline = p.start/RAIDLINE;
    rem = p.reqlen;

    slow1 = -1;
    slow2 = -1;
current_fast++;
    memset(partpos, 0, sizeof partpos);
    memset(feedlag, 0, sizeof feedlag);
    lagrounds = 0;

    memset(invalid, (1 << RAIDPARTS)-1, sizeof invalid);
    completed = 0;

    filesize = p.filesize;
    paddedpartsize = (raidPartSize(0, filesize)+RAIDSECTOR-1) & -RAIDSECTOR;

    tickettime = p.tickettime;
    //shard = p.shard;

    lastdata = currtime;
    haddata = false;
    reported = false;
    missingsource = false;

    for (int i = RAIDPARTS; i--; ) 
    {
        //sockets[i] = -1;

        if (!p.tempUrls[i].empty())
        {
            // we don't trigger I/O on unknown source servers (which shouldn't exist in normal ops anyway)
            if (fetcher[i].setsource(p.tempUrls[i], this, i))
            {
                std::cout << "[RaidReq::RaidReq] [i="<<i<<"] setsource -> fetcher[i].trigger()" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                // this kicks off I/O on that source
                fetcher[i].trigger();
            }
            else
            {
                std::cout << "[RaidReq::RaidReq] [i="<<i<<"] !setsource ALERT Unknown source: url='" << p.tempUrls[i] << "', please update config" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                std::cout << "Unknown source: " << p.tempUrls[i] << ", please update config" << std::endl;
            }
        }
        else
        {
            std::cout << "[RaidReq::RaidReq] [i="<<i<<"] p.tempUrls[i].empty() !!!! [missingsource="<<std::to_string(missingsource)<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
            if (missingsource) std::cout << "[RaidReq::RaidReq] [i="<<i<<"] missingsource already true -> ALERT Can't operate with more than one missing source server [missingsource="<<std::to_string(missingsource)<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
            if (missingsource) std::cout << "Can't operate with more than one missing source server" << std::endl;
            missingsource = true;
        }
    }
    std::cout << "[RaidReq::RaidReq] END" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
}

// resume fetching on a parked source that has become eligible again
void PartFetcher::resume()
{
    std::cout << "[PartFetcher::resume] [part="<<std::to_string(part)<<"] BEGIN" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
    // no readahead or end of readahead within READAHEAD?
    if (!connected && rr && pos < rr->paddedpartsize)
    {
        std::cout << "[PartFetcher::resume] [part="<<std::to_string(part)<<"] (rr && rr->sockets[part] < 0 && pos < rr->paddedpartsize)" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
        setposrem();

        if (rem)
        {
            skip_setposrem = true;
            trigger();
        }
    }
    std::cout << "[PartFetcher::resume] [part="<<std::to_string(part)<<"] END" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
}

// try to resume fetching on all sources
void RaidReq::resumeall()
{
    if (slow1 >= 0) for (int i = RAIDPARTS; i--; ) fetcher[i].resume();
}

// feed suitable readahead data
bool PartFetcher::feedreadahead()
{
    std::cout << "[PartFetcher::feedreadahead] [part="<<std::to_string(part)<<"] BEGIN [readahead.size() = " << readahead.size() << "]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
    if (readahead.empty()) std::cout << "[PartFetcher::feedreadahead] [part="<<std::to_string(part)<<"] END (readahead.empty()) [readahead.size() = " << readahead.size() << "]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
    if (readahead.empty()) return false;

    int total = readahead.size();
    int remaining = total;

    while (remaining)
    {
        auto it = readahead.begin();

        // make sure that we feed gaplessly
        assert(it->first >= (rr->dataline+rr->completed)*RAIDSECTOR);

        // we only take over from any source if we match the completed boundary precisely
        if (it->first == (rr->dataline+rr->completed)*RAIDSECTOR) rr->partpos[(int)part] = it->first-rr->dataline*RAIDSECTOR;

        // always continue at any position on our own source
        if (it->first != rr->dataline*RAIDSECTOR+rr->partpos[(int)part]) break;

        // we do not feed chunks that cannot even be processed in part (i.e. that start at or past the end of the buffer)
        if (it->first-rr->dataline*RAIDSECTOR >= NUMLINES*RAIDSECTOR) break;

        off_t p = it->first;
        byte* d = it->second.first;
        unsigned l = it->second.second;
        readahead.erase(it);
current_readahead--;

        rr->procdata(part, d, p, l);
        free(d);

        remaining--;
    }

    std::cout << "[PartFetcher::feedreadahead] [part="<<std::to_string(part)<<"] END -> total && total != remaining [total="<<total<<", remaining="<<remaining<<"] [readahead.size() = " << readahead.size() << "]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
    return total && total != remaining;
}

// feed relevant read-ahead data to procdata
// returns true if any data was processed
void RaidReq::procreadahead()
{
    std::cout << "[RaidReq::procreadahead] BEGIN" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
    bool fed;

    do {
        fed = false;

        for (int i = RAIDPARTS; i--; )
        {
            if (fetcher[i].feedreadahead()) fed = true;
        }
    } while (fed);
    std::cout << "[RaidReq::procreadahead] END" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
}

// procdata() handles input in any order/size and will push excess data to readahead
// data is assumed to be 0-padded to paddedpartsize at EOF
void RaidReq::procdata(int part, byte* ptr, off_t pos, int len)
{
    std::cout << "[RaidReq::procdata] BEGIN [part="<<part<<", ptr="<<(void*)ptr<<", pos="<<pos<<", len="<<len<<", completed="<<completed<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
    off_t basepos = dataline*RAIDSECTOR;

    // we never read backwards
    assert((pos & -RAIDSECTOR) >= (basepos+(partpos[part] & -RAIDSECTOR)));

    bool consecutive = pos == basepos+partpos[part];

    // is the data non-consecutive (i.e. a readahead), OR is it extending past the end of our buffer?
    if (!consecutive || pos+len > basepos+NUMLINES*RAIDSECTOR)
    {
        auto ahead_ptr = ptr;
        auto ahead_pos = pos;
        auto ahead_len = len;

        // if this is a consecutive feed, we store the overflowing part as readahead data
        // and process the non-overflowing part normally 
        if (consecutive)
        {
            ahead_pos = basepos+NUMLINES*RAIDSECTOR;
            ahead_ptr = ptr+(ahead_pos-pos);
            ahead_len = len-(ahead_pos-pos);
        }

        // enqueue for future processing
        // FIXME: reallocate existing until it becomes too big to copy around?
        byte* p = (byte*)malloc(ahead_len);
        memcpy(p, ahead_ptr, ahead_len);
        fetcher[part].readahead[ahead_pos] = pair<byte*, unsigned>(p, ahead_len);
// FIXME: race condition below
if (++current_readahead > highest_readahead) highest_readahead = (int)current_readahead;
        // if this is a pure readahead, we're done
        if (!consecutive) std::cout << "[RaidReq::procdata] (!consecutive) -> END -> (if this is a pure readahead, we're done) [part="<<part<<", ptr="<<(void*)ptr<<", pos="<<pos<<", len="<<len<<", completed="<<completed<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
        if (!consecutive) return;

        len = ahead_pos-pos;
    }

    // non-readahead data must flow contiguously
    assert(pos == partpos[part]+dataline*RAIDSECTOR);

    //int eof = partpos[part]+len == paddedpartsize-dataline*RAIDSECTOR;

    partpos[part] += len;

    unsigned t = pos-dataline*RAIDSECTOR;
    std::cout << "[RaidReq::procdata] t = pos-(dataline*RAIDSECTOR) = ("<<pos<<"-("<<dataline<<")*"<<RAIDSECTOR<<") = "<<t<<" [part="<<part<<", ptr="<<(void*)ptr<<", pos="<<pos<<", len="<<len<<", completed="<<completed<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;

    // ascertain absence of overflow (also covers parity part)
    assert(t+len <= sizeof data/(RAIDPARTS-1));

    // set valid bit for every block that's been received in full
    char partmask = 1 << part;
    int until = (t+len)/RAIDSECTOR;
    std::cout << "[RaidReq::procdata] until = (t+len)/RAIDSECTOR = ("<<t<<"+"<<len<<")/"<<RAIDSECTOR<<" = "<<until<<" [t="<<t<<", until="<<until<<", partmask="<<(int)partmask<<"] [part="<<part<<", ptr="<<(void*)ptr<<", pos="<<pos<<", len="<<len<<", completed="<<completed<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
    for (int i = t/RAIDSECTOR; i < until; i++)
    {
        //std::cout << "[RaidReq::procdata] for (int i = t/RAIDSECTOR; i < until; i++) invalid[i] -= partmask [t="<<t<<", until="<<until<<", i="<<i<<", partmask="<<(int)partmask<<"] [invalid[i]="<<(int)invalid[i]<<", postInvalid[i]="<<(int)(invalid[i]-partmask)<<"] [part="<<part<<", ptr="<<(void*)ptr<<", pos="<<pos<<", len="<<len<<", completed="<<completed<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
        assert(invalid[i] & partmask);
        invalid[i] -= partmask;
    }

    // copy (partial) blocks to data or parity buf
    if (part)
    {
        part--;

        byte* ptr2 = ptr;
        int len2 = len;
        byte* target = data + part * RAIDSECTOR + (t / RAIDSECTOR) * RAIDLINE;
        int partialSector = t % RAIDSECTOR;
        if (partialSector != 0)
        {
            target += partialSector;
            auto sectorBytes = std::min<int>(len2, RAIDSECTOR - partialSector);
            memcpy(target, ptr2, sectorBytes);
            target += sectorBytes + RAIDSECTOR * (RAIDPARTS - 2);
            len2 -= sectorBytes;
            ptr2 += sectorBytes;
        }
        while (len2 >= RAIDSECTOR)
        {
            *(raidsector_t*)target = *(raidsector_t*)ptr2;
            target += RAIDSECTOR * (RAIDPARTS - 1);
            ptr2 += RAIDSECTOR;
            len2 -= RAIDSECTOR;
        }
        partialSector = len2;
        if (partialSector != 0)
        {
            memcpy(target, ptr2, partialSector);
        }
    }
    else
    {
        // store parity data for subsequent merging
        memcpy(parity+t, ptr, len);
    }

    // merge new consecutive completed RAID lines so they are ready to be sent, direct from the data[] array
    auto old_completed = completed;
    std::cout << "[RaidReq::procdata] For/Loop -> merge new consecutive completed RAID lines so they are ready to be sent, direct from the data[] array[part="<<part<<", ptr="<<(void*)ptr<<", pos="<<pos<<", len="<<len<<", completed="<<completed<<", until="<<until<<", old_completed="<<old_completed<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
    for (; completed < until; completed++)
    {
        //std::cout << "[RaidReq::procdata] for (; completed < until; completed++) BEGIN [part="<<part<<", ptr="<<(void*)ptr<<", pos="<<pos<<", len="<<len<<", completed="<<completed<<", until="<<until<<", old_completed="<<old_completed<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
        unsigned char mask = invalid[completed];
        auto bitsSet = __builtin_popcount(mask);   // machine instruction for number of bits set

        //std::cout << "[RaidReq::procdata] for (; completed < until; completed++) bitsSet="<<bitsSet<<", mask="<<(int)mask<<" [part="<<part<<", ptr="<<(void*)ptr<<", pos="<<pos<<", len="<<len<<", completed="<<completed<<", until="<<until<<", old_completed="<<old_completed<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
        assert(bitsSet);
        if (bitsSet > 1)
        {
            std::cout << "[RaidReq::procdata] for (; completed < until; completed++) END ((bitsSet="<<bitsSet<<" > 1) -> break) [bitsSet="<<bitsSet<<", mask="<<(int)mask<<"] [part="<<part<<", ptr="<<(void*)ptr<<", pos="<<pos<<", len="<<len<<", completed="<<completed<<", until="<<until<<", old_completed="<<old_completed<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
            break;
        }
        else
        {
            if (!(mask & 1))
            {
                // parity involved in this line

                int index = __builtin_ctz(mask);  // counts least significant consecutive 0 bits (ie 0-based index of least significant 1 bit).  Windows equivalent is _bitScanForward
                if (index > 0 && index < RAIDLINE)
                {
                    auto sectors = (raidsector_t*)(data + RAIDLINE * completed);
                    raidsector_t& target = sectors[index - 1];

                    target = ((raidsector_t*)parity)[completed];
                    if (!(mask & (1 << 1))) target ^= sectors[0];  // this method requires source and target are both aligned to their size
                    if (!(mask & (1 << 2))) target ^= sectors[1];
                    if (!(mask & (1 << 3))) target ^= sectors[2];
                    if (!(mask & (1 << 4))) target ^= sectors[3];
                    if (!(mask & (1 << 5))) target ^= sectors[4];
                    assert(RAIDPARTS == 6);
                }
            }
        }
        //std::cout << "[RaidReq::procdata] for (; completed < until; completed++) END [part="<<part<<", ptr="<<(void*)ptr<<", pos="<<pos<<", len="<<len<<", completed="<<completed<<", until="<<until<<", old_completed="<<old_completed<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
    }

    if (completed > old_completed && notifyeventfd >= 0)
    {
        std::cout << "[RaidReq::procdata] (completed > old_completed && notifyeventfd >= 0) -> write(notifyeventfd, 1) [part="<<part<<", ptr="<<(void*)ptr<<", pos="<<pos<<", len="<<len<<", completed="<<completed<<", old_completed="<<old_completed<<", notifyeventfd="<<notifyeventfd<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
        uint64_t n = 1;
        write(notifyeventfd, &n, sizeof(n)); // signal eventfd to wake up writer thread's epoll_wait
    }
    std::cout << "[RaidReq::procdata] END [part="<<part<<", ptr="<<(void*)ptr<<", pos="<<pos<<", len="<<len<<", completed="<<completed<<"]" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
}

void RaidReq::shiftdata(off_t len)
{
    std::cout << "[RaidReq::shiftdata] BEGIN (len=" << len << ") [skip="<<skip<<", rem="<<rem<<", completed="<<completed<<",  haddata="<<haddata<<", lastdata="<<lastdata<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    skip += len;
    rem -= len;

    if (rem)
    {
        int shiftby = skip/RAIDLINE;

        completed -= shiftby;

        skip %= RAIDLINE;

        // we remove completed sectors/lines from the beginning of all state buffers
        int eobData = 0, eobAll = 0;
        for (int i = RAIDPARTS; i--; ) //if (i != inactive)
        {
            if (i > 0 && partpos[i] > eobData) eobData = partpos[i];
            if (partpos[i] > eobAll) eobAll = partpos[i];
        }
        eobData = (eobData + RAIDSECTOR - 1) / RAIDSECTOR;
        eobAll = (eobAll + RAIDSECTOR - 1) / RAIDSECTOR;

        if (eobData > shiftby)
        {
            memmove(data, data+shiftby*RAIDLINE, (eobData-shiftby)*RAIDLINE);
        }

        if (eobAll > shiftby)
        {
            memmove(invalid, invalid + shiftby, eobAll - shiftby);
            memset(invalid + eobAll - shiftby, (1 << RAIDPARTS)-1, shiftby);
        }
        else
        {
            memset(invalid, (1 << RAIDPARTS)-1, shiftby);
        }

        dataline += shiftby;
        shiftby *= RAIDSECTOR;

        if (partpos[0] > shiftby) memmove(parity, parity+shiftby, partpos[0]-shiftby);

        // shift partpos[] by the dataline increment and retrigger data flow
        for (int i = RAIDPARTS; i--; ) //if (i != inactive)
        {
            partpos[i] -= shiftby;
            feedlag[i] += fetcher[i].remfeed;

            if (partpos[i] < 0) partpos[i] = 0;
            else
            {
                // request 1 less RAIDSECTOR as we will add up to 1 sector of 0 bytes at the end of the file - this leaves enough buffer space in the buffer passed to procdata for us to write past the reported length
                fetcher[i].cont(NUMLINES*RAIDSECTOR-partpos[i]);
            }
         }

        if (++lagrounds > LAGINTERVAL)
        {
            // fast mode:
            // if we have dominant accumulated lag on one connection, switch to slow mode

            // slow mode:
            // if we have dominant accumulated lag on a fast connection, make that connection slow

            // (dominance is defined as the ratio between fastest and slowest)
            int highest = 0, lowest = 0;

            for (int i = RAIDPARTS; --i; )
            {
                if (feedlag[i] > feedlag[highest]) highest = i;
                else if (feedlag[i] < feedlag[lowest]) lowest = i;
            }

            if (!missingsource && feedlag[highest] > NUMLINES*RAIDSECTOR*LAGINTERVAL*3/4 && feedlag[highest] > 16*feedlag[lowest])
            {
                // slow channel detected
                if (slow1 >= 0)
                {
                    if (highest == slow1 || highest == slow2)
                    {
//                        cout << "Slow channel already marked as such, no action taken" << endl;
                        std::cout << "[RaidReq::shiftdata] Slow channel already marked as such, no action taken" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                    }
                    else
                    {
//                        cout << "Switching " << highest << " to slow" << endl;
                        std::cout << "[RaidReq::shiftdata] Switching " << highest << " to slow" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                        setslow(highest, slow1);
                    }
                }
                else
                {
                    fetcher[highest].errors++;

                    // check if we have a fresh and idle channel left to try
                    int fresh;
                    for (fresh = RAIDPARTS; fresh--; ) if (sockets[fresh] < 0 && fetcher[fresh].errors < 3) break;

                    if (fresh >= 0)
                    {
//                        cout << "Trying fresh channel " << fresh << "..." << endl;
                        std::cout << "[RaidReq::shiftdata] Trying fresh channel " << fresh << "..." << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                        fetcher[highest].closesocket();
                        fetcher[fresh].trigger();
                    }
                    else
                    {
                        int disconnected;
                        for (disconnected = RAIDPARTS; disconnected--; ) if (sockets[disconnected] < 0) break;

                        if (disconnected >= 0)
                        {
                            std::cout << "[RaidReq::shiftdata] Switching to slow mode on " << (int)highest << "/" << (int)disconnected << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                            setslow(highest, disconnected);
                        }
                        else std::cout << "[RaidReq::shiftdata] No disconected channel available, carrying on..." << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                    }
                 }
            }

            memset(feedlag, 0, sizeof feedlag);
            lagrounds = 0;
        }

        resumeall();

        haddata = true;
        lastdata = currtime;
    }
    std::cout << "[RaidReq::shiftdata] END (len=" << len << ") [skip="<<skip<<", rem="<<rem<<", completed="<<completed<<", haddata="<<haddata<<", lastdata="<<lastdata<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
}

void RaidReq::dispatchio(HttpReqPtr s)
{
    std::cout << "[RaidReq::dispatchio] BEGIN [s=" << s << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    // fast lookup of which PartFetcher to call from a single cache line
    // we don't check for s not being found since we know sometimes it won't be when we closed a socket to a slower/nonresponding server
    for (int i = RAIDPARTS; i--; )
    {
        if (sockets[i] == s)
        {
            int t = fetcher[i].io();

            std::cout << "[RaidReq::dispatchio] (sockets[i="<<i<<"] == s) -> int t = fetcher[i].io -> t="<<t<<" [s=" << s << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
            //if (t >= 0)
            if (t > 0)
            {
                std::cout << "[RaidReq::dispatchio] t="<<t<<" > 0 -> pool.addScheduledio(currtime(="<<currtime<<"+t="<<t<<", sockets[i="<<i<<"]="<<sockets[i]<<") [s=" << s << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                // this is a relatively infrequent ocurrence, so we tolerate the overhead of a std::set insertion/erasure
                pool.addScheduledio(currtime+t, sockets[i]);
                //pool.scheduledio.insert(std::make_pair(currtime+t, sockets[i]));            
            }
            std::cout << "[RaidReq::dispatchio] (sockets[i="<<i<<"] == s) BREAK [s=" << s << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
            break;
        }
    }
    std::cout << "[RaidReq::dispatchio] END [s=" << s << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
}

// execute cont()-triggered io()s
// must be called under lock
void RaidReq::handlependingio()
{
    while (!pendingio.empty())
    {
        auto s = pendingio.front();
        pendingio.pop_front();
        dispatchio(s);
    }
}

// watchdog: resolve stuck connections
void RaidReq::watchdog()
{
    std::cout << "[RaidReq::watchdog] BEGIN" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    if (missingsource) return;

    // check for a single fast source hanging
    int hanging = 0;
    int hangingsource = -1;
    int idlegoodsource = -1;
    int numconnected = 0;

    for (int i = RAIDPARTS; i--; )
    {
        if (sockets[i] && sockets[i]->status == REQ_INFLIGHT)
        {
            std::cout << "[RaidReq::watchdog] for (int i = RAIDPARTS; i--; ) [i="<<i<<"] fetcher[i].connected="<<fetcher[i].connected<<", fetcher[i].remfeed="<<fetcher[i].remfeed<<", currtime="<<currtime<<", fetcher[i].lastdata="<<fetcher[i].lastdata<<", currtime-fetcher[i].lastdata="<<(currtime-fetcher[i].lastdata)<<", !fetcher[i].isslow()="<<!fetcher[i].isslow()<<" [sockets[i]->status="<<sockets[i]->status<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
            if (fetcher[i].connected && fetcher[i].remfeed && currtime-fetcher[i].lastdata > 200/*20*/ && !fetcher[i].isslow())
            {
                std::cout << "[RaidReq::watchdog] for (int i = RAIDPARTS; i--; ) above is true -> hanging++, hangingsource = i [hanging="<<hanging<<", hangingsource="<<hangingsource<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                hanging++;
                hangingsource = i;
            }

            if (fetcher[i].connected) numconnected++;
            if (!fetcher[i].connected && !fetcher[i].errors) idlegoodsource = i;
            if (fetcher[i].connected) std::cout << "[RaidReq::watchdog] for (int i = RAIDPARTS; i--; ) (fetcher[i].connected) numconnected++ [numconnected="<<numconnected<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
            if (!fetcher[i].connected && !fetcher[i].errors) std::cout << "[RaidReq::watchdog] for (int i = RAIDPARTS; i--; ) (!fetcher[i].connected && !fetcher[i].errors) -> idlegoodsource = i [idlegoodsource="<<idlegoodsource<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        }
    }

    if (hanging)
    {
        std::cout << "[RaidReq::watchdog] Hanging [hanging="<<hanging<<", hangingsource="<<hangingsource<<", idlegoodsource="<<idlegoodsource<<", numconnected="<<numconnected<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        if (slow1 < 0)
        {
            // fast mode
            if (idlegoodsource >= 0)
            {
                std::cout << "[RaidReq::watchdog] Hanging fast mode && idlegoodsource >= 0 -> Attempted remedy: Switching from hangingsource=" << hangingsource << "/" << std::to_string(fetcher[hangingsource].part) << " to previously unused idlegoodsource="<<idlegoodsource<<"/" << std::to_string(fetcher[idlegoodsource].part) << " [hanging="<<hanging<<", hangingsource="<<hangingsource<<", idlegoodsource="<<idlegoodsource<<", numconnected="<<numconnected<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                // "Attempted remedy: Switching from " << hangingsource << "/" << fetcher[hangingsource].serverid << " to previously unused " << fetcher[idlegoodsource].serverid << endl;

                fetcher[hangingsource].errors++;
                fetcher[hangingsource].closesocket();
                fetcher[idlegoodsource].trigger();
                return;
            }
            //else cout << "Inactive connection potentially bad" << endl;
            else std::cout << "[RaidReq::watchdog] [Hanging] fast mode && idlegoodsource < 0 -> Inactive connection potentially bad [hanging="<<hanging<<", hangingsource="<<hangingsource<<", idlegoodsource="<<idlegoodsource<<", numconnected="<<numconnected<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        }

        // fast mode no longer tenable - enter slow mode
        int worst = -1;

        // we need another slow channel
        for (int i = RAIDPARTS; i--; )
        {
            if (i != hangingsource)
            {
                if (worst < 0 || !fetcher[i].connected || fetcher[i].remfeed > fetcher[worst].remfeed) worst = i;
            }
        }
        std::cout << "[RaidReq::watchdog] [!fast mode] No idle good source remaining - enter slow mode: Mark " << std::to_string(fetcher[hangingsource].part) << " and " << std::to_string(fetcher[worst].part) << " as slow [hangingsource="<<hangingsource<<", worst="<<worst<<", hanging="<<hanging<<", numconnected="<<numconnected<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;

//        cout << "No idle good source remaining - enter slow mode: Mark " << fetcher[hangingsource].serverid << " and " << fetcher[worst].serverid << " as slow" << endl;
        setslow(hangingsource, worst);
    }

    std::cout << "[RaidReq::watchdog] END" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
//    cout << endl;
}

std::string RaidReq::getfaildescription()
{
    char buf[300];
    const char* msg;

    switch (err_type)
    {
        case NOERR: msg = "NOERR on "; break;
        case READERR: msg = "network read from "; break;
        case WRITEERR: msg = "network write to "; break;
        case CONNECTERR: msg = "connect to "; break;
        default: msg = NULL;
    }

    sprintf(buf, "%s with errno %d %s", msg, err_errno, dataline ? " at start" : " partway");

    return std::string(buf);
}

int readdatacount;
off_t readdatatotal;
long readdatalock;
int numrrq;
int epolls, epollevents;

off_t RaidReq::readdata(byte* buf, off_t len)
{
    std::cout << "[RaidReq::readdata] BEGIN [buf=" << (void*)buf << ", len=" << len << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    off_t t;

    lock_guard<mutex> g(rr_lock);

    std::cout << "[RaidReq::readdata] call watchdog [buf=" << (void*)buf << ", len=" << len << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    watchdog();
    t = completed*RAIDLINE-skip;
    std::cout << "[RaidReq::readdata] t = completed*RAIDLINE-skip = " << (completed*RAIDLINE-skip) << "[completed="<<completed<<"] [buf=" << (void*)buf << ", len=" << len << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;

    if (t > 0)
    {
        if (t > len) t = len;

        memmove(buf, data+skip, t);

        std::cout << "[RaidReq::readdata] t > 0 (t="<<t<<") -> shiftdata(t) [buf=" << (void*)buf << ", len=" << len << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        shiftdata(t);
    }
    else
    {
        std::cout << "[RaidReq::readdata] t <= 0 (t="<<t<<") OOPS [buf=" << (void*)buf << ", len=" << len << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        if (currtime-lastdata > 1000/*100*/)
        {
            std::cout << "[RaidReq::readdata] [t<=0] (t="<<t<<") (currtime-lastdata > 100) [currtime="<<currtime<<", lastdata=" << lastdata << "] [buf=" << (void*)buf << ", len=" << len << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
            if (currtime-lastdata > 6000/*600*/)
            {
                std::cout << "[RaidReq::readdata] [t<=0] (t="<<t<<") (currtime-lastdata > 600) -> ALERT CloudRAID feed timed out -> return -1 [currtime="<<currtime<<", lastdata=" << lastdata << "] [buf=" << (void*)buf << ", len=" << len << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                LOGF("E %d CloudRAID feed timed out", 10812+haddata);
                if (!haddata) pstats.raidproxyerr++;
                return -1;
            }

            if (!reported)
            {
                reported = true;
                std::cout << "[RaidReq::readdata] [t<=0] (t="<<t<<") (currtime-lastdata > 100) !reported -> reported=true -> ALERT CloudRAID feed stuck !!!! [currtime="<<currtime<<", lastdata=" << lastdata << "] [buf=" << (void*)buf << ", len=" << len << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                LOGF("E %d CloudRAID feed stuck", 10810+haddata);
                if (!haddata) pstats.raidproxyerr++;
            }
        }
    }

    std::cout << "[RaidReq::readdata] CALL handlependingio() before return [buf=" << (void*)buf << ", len=" << len << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    handlependingio();

    std::cout << "[RaidReq::readdata] END -> return t=" << t << " [buf=" << (void*)buf << ", len=" << len << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    return t;
}

off_t RaidReq::senddata(byte* outbuf, off_t len)
{
    off_t t;
    int e;

    lock_guard<mutex> g(rr_lock);

    watchdog();
    t = completed*RAIDLINE-skip;

    if (t > 0)
    {
        if (t > len) t = len;

        //t = write(s, data+skip, t);
        memcpy(outbuf, data+skip, t);
        e = errno;

        if (t > 0) shiftdata(t);
    }
    else
    {
        if (currtime-lastdata > 300 && !reported)
        {
            reported = true;
            LOGF("E %d CloudRAID feed stuck", 10810+haddata);
            if (!haddata) pstats.raidproxyerr++;
        }

        t = -1;
        e = EAGAIN;
    }

    handlependingio();

    errno = e;
    return t;
}

bool RaidReq::isSocketConnected(size_t pos)
{
    //if (pos >= sockets.size()) return false;
    assert(pos < sockets.size());
    const auto& s = sockets[pos];
    return s != nullptr && s->httpio;
}

void RaidReq::disconnect()
{
    for (int i = 0; i < sockets.size(); i++)
    {
        sockets[i]->disconnect();
        //cloudRaid->disconnect(sockets[i]);
    }
}

bool RaidReqPool::addScheduledio(raidTime scheduledFor, const HttpReqPtr& req)
{
    std::cout << "[RaidReqPool::addScheduledio] BEGIN (GET LOCK) [scheduledFor="<<scheduledFor << ", req=" << req << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    lock_guard<recursive_mutex> g(rrp_lock);
    std::cout << "[RaidReqPool::addScheduledio] LOCK GOT [scheduledFor="<<scheduledFor << ", req=" << req << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    auto it = scheduledio.insert(std::make_pair(scheduledFor, req));
    std::cout << "[RaidReqPool::addScheduledio] END -> return " << std::to_string(it.second) << " [scheduledFor="<<scheduledFor << ", req=" << req << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    return it.second;
}

bool RaidReqPool::addDirectio(const HttpReqPtr& req)
{
    std::cout << "[RaidReqPool::addDirectio] BEGIN (GET LOCK) [req=" << req << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    lock_guard<recursive_mutex> g(rrp_lock);
    std::cout << "[RaidReqPool::addDirectio] LOCK GOT [req=" << req << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    auto it = directio.insert(req);
    std::cout << "[RaidReqPool::addDirectio] END -> return " << std::to_string(it.second) << " [req=" << req << "]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    return it.second;
}

void* RaidReqPool::raidproxyiothread()
{
    //std::cout << "[RaidReqPool::raidproxyiothread] BEGIN" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    //_Exit(1);
    //HttpReqPtr* events;
    //std::vector<HttpReqPtr> reqEvents;
    //std::map<std::pair<std::atomic<mega::reqstatus_t>&, HttpReq
    //std::deque<std::pair<HttpReqPtr, std::atomic<std::shared_ptr<RaidReq>>>> events;
    //std::vector<HttpReqPtr> events;

    int i, j, m, e;//n, e;
    RaidReq* rr;
    int epollwait = -1;

    //events = (HttpReqPtr*)calloc(MAXEPOLLEVENTS, sizeof(HttpReqPtr));
    std::deque<HttpReqPtr> events;

    //auto nCount =sss
    while (isRunning.load())
    {
        //usleep(100); // QUITAR !!!
        std::cout << "[RaidReqPool::raidproxyiothread] for (;;) BEGIN -> sleep until new Waiter::ds" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        mega::dstime timeNow = Waiter::ds;
        //int nummswaits = 0;
        while (isRunning.load() && timeNow == Waiter::ds)
        {
            //std::cout << "[RaidReqPool::raidproxyiothread] for (;;) sleep 1ms (nummswaits="<<nummswaits++<<")" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        //std::this_thread::sleep_for(std::chrono::microseconds(1000));
        //std::this_thread::sleep_for(std::chrono::milliseconds(1));
        //std::vector<
        //n = epoll_wait(efd, events, MAXEPOLLEVENTS, epollwait);

        std::cout << "[RaidReqPool::raidproxyiothread] for (;;) AFTER SLEEP" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        //if (epollwait >= 0) epollwait = -1;

        std::cout << "[RaidReqPool::raidproxyiothread] for (;;) GET LOCK" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        lock_guard<recursive_mutex> g(rrp_lock);  // this lock guarantees RaidReq will not be deleted between lookup and dispatch - locked for a while but only affects the main thread with new raidreqs being added or removed
        std::cout << "[RaidReqPool::raidproxyiothread] for (;;) LOCK GOT [events.size()="<<events.size()<<", isRunning.load()="<<isRunning.load()<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        if (!events.empty())
        {
            std::cout << "[RaidReqPool::raidproxyiothread] for (;;) PRE directio loop -> EXISTING EVENTS!" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
            for (auto& s : events) { std::cout << "[RaidReqPool::raidproxyiothread] for (;;) EXISTING EVENTS LOOP [event -> s="<<s<<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl; }
        }
        if (!isRunning.load()) std::cout << "[RaidReqPool::raidproxyiothread] for (;;) END -> !isRunning.load() -> break" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        if (!isRunning.load()) break;

        while (!directio.empty())
        {
            std::cout << "[RaidReqPool::raidproxyiothread] for (;;) (!directio.empty()) -> add events -> (s=" << *directio.begin() << ")" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
            events.emplace_front(*directio.begin());
            directio.erase(directio.begin());
        }
        //n = events.size();
        std::cout << "[RaidReqPool::raidproxyiothread] for (;;) (n=" << events.size() <<")" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;

        if (events.size() > 6) std::cout << "[RaidReqPool::raidproxyiothread] for (;;) ALERT WTF -> events.size() > 6 !!!!! (n=" << events.size() <<")" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        assert(events.size() <= 6);
        //std::cout << "[RaidReqPool::raidproxyiothread] for (;;) GET LOCK" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        //lock_guard<mutex> g(rrp_lock);  // this lock guarantees RaidReq will not be deleted between lookup and dispatch - locked for a while but only affects the main thread with new raidreqs being added or removed


        // initially process all the ones for which we can get the RaidReq lock instantly.  For any that are contended, skip and process the rest for now - then loop and retry, last loop locks rather than try_locks.
        for (j = 2; j--; )
        {
            std::cout << "[RaidReqPool::raidproxyiothread] for (;;) [j="<<j<<", n=" << events.size() <<"]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
            //m = 0;
            /*
            for (i = 0; i < events.size(); i++)
            {
                HttpReqPtr s = nullptr;
                try {
                    s = events.at(i);
                    std::cout << "[RaidReqPool::raidproxyiothread] for (;;) [j="<<j<<", i="<<i<<", n=" << events.size() <<"] -> s acquired, call (rr = socketrrs.lookup(s="<<s<<"))" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                }
                catch (std::exception& ex)
                {
                    std::cout << "[RaidReqPool::raidproxyiothread] for (;;) [j="<<j<<", i="<<i<<", n=" << events.size() <<"] -> ALERT!!!! EXCEPTION: ex=" << ex.what() << "" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                    exit(0);
                }
                //s = events[i].data.fd;
                //e = events[i].events;

                if ((rr = socketrrs.lookup(s)))  // retrieved under extremely brief lock.  RaidReqs can not be deleted until we unlock rrp_lock 
                {
                    std::cout << "[RaidReqPool::raidproxyiothread] for (;;) [j="<<j<<", i="<<i<<", n=" << events.size() <<"] -> (rr = socketrrs.lookup(s="<<s<<")) == TRUE" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                    //rr->isSocketConnected(i)
                    std::unique_lock<mutex> g(rr->rr_lock, std::defer_lock);
                    //if (j > 0 ? g.try_lock() : (g.lock(), true))
                    if (g.try_lock()) 
                    {
                        std::cout << "[RaidReqPool::raidproxyiothread] for (;;) [n=" << events.size() <<"] (rr = socketrrs.lookup(s=" << s <<") && trylock) -> rr->dispatchio(s=" << s <<")" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                        rr->dispatchio(s);
                        events.erase(events.begin() + i);
                    }
                    else
                    {
                        std::cout << "[RaidReqPool::raidproxyiothread] for (;;) [n=" << events.size() <<"] (rr = socketrrs.lookup(s=" << s <<") && !trylock) -> move it to the front" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                        // move it to the front
                        events.erase(events.begin() + i);
                        events.emplace_front(s);
                        //events[m].events = e;
                        //events[m++].data.fd = s;
                    }
                }
                else
                {
                    std::cout << "[RaidReqPool::raidproxyiothread] for (;;) [n=" << events.size() <<"] !socketrrs.lookup(s=" << s <<") -> NOTHING (delete event)" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;  
                    events.erase(events.begin() + i);
                }
            }
            */
            while (!events.empty())
            {
                const HttpReqPtr& s = *events.begin();
                std::cout << "[RaidReqPool::raidproxyiothread] for (;;) [j="<<j<<", i="<<0<<", n=" << events.size() <<"] -> s acquired, call (rr = socketrrs.lookup(s="<<s<<"))" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                //s = events[i].data.fd;
                //e = events[i].events;

                if ((rr = socketrrs.lookup(s)))  // retrieved under extremely brief lock.  RaidReqs can not be deleted until we unlock rrp_lock 
                {
                    std::cout << "[RaidReqPool::raidproxyiothread] for (;;) [j="<<j<<", i="<<0<<", n=" << events.size() <<"] -> (rr = socketrrs.lookup(s="<<s<<")) == TRUE" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                    //rr->isSocketConnected(i)
                    std::unique_lock<mutex> g(rr->rr_lock, std::defer_lock);
                    //if (j > 0 ? g.try_lock() : (g.lock(), true))
                    if (g.try_lock()) 
                    {
                        std::cout << "[RaidReqPool::raidproxyiothread] for (;;) [n=" << events.size() <<"] (rr = socketrrs.lookup(s=" << s <<") && trylock) -> rr->dispatchio(s=" << s <<")" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                        rr->dispatchio(s);
                        events.erase(events.begin());
                    }
                    else
                    {
                        std::cout << "[RaidReqPool::raidproxyiothread] for (;;) [n=" << events.size() <<"] (rr = socketrrs.lookup(s=" << s <<") && !trylock) -> sleep 1 ms and retry" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                        //std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        // move it to the front
                        events.erase(events.begin());
                        addScheduledio(currtime, s);
                        //addDirectio(s);
                        //events.emplace_front(s);
                        //events[m].events = e;
                        //events[m++].data.fd = s;
                    }
                }
                else
                {
                    std::cout << "[RaidReqPool::raidproxyiothread] for (;;) [n=" << events.size() <<"] !socketrrs.lookup(s=" << s <<") -> NOTHING (delete event)" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;  
                    events.erase(events.begin());
                }
            }

            if (j == 1)
            {
                std::cout << "[RaidReqPool::raidproxyiothread] for (;;) [j=1] -> scheduledio.size() = "<<scheduledio.size()<<"" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                while (!scheduledio.empty())
                {
                    epollwait = scheduledio.begin()->first-currtime;

                    if (epollwait <= 0)
                    {
                        //if (m == MAXEPOLLEVENTS)
                        /*
                        if (scheduledio.begin()->second->st)
                        {
                            // process remainder in the next round
                            epollwait = 0;
                            break;
                        }
                        */

                        /*
                        // a scheduled io() has come up for execution
                        events[m].events = 0;
                        events[m++].data.fd = scheduledio.begin()->second;
                        */
                        std::cout << "[RaidReqPool::raidproxyiothread] for (;;) (epollwait="<<epollwait<<" <= 0) -> a scheduled io() has come up for execution -> s="<<scheduledio.begin()->second<<"" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                        events.emplace_front(scheduledio.begin()->second);
                        scheduledio.erase(scheduledio.begin());

                        epollwait = -1;
                    }
                    else
                    {
                        std::cout << "[RaidReqPool::raidproxyiothread] for (;;) (epollwait="<<epollwait<<" > 0) -> epollwait *= 1000 " << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
                        epollwait *= 1000;
                        break;
                    }
                }
            }

            if (events.empty()) std::cout << "[RaidReqPool::raidproxyiothread] for (;;) events.empty() -> BREAK" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
            if (events.empty()) break;
            //else n = events.size();
        }
        std::cout << "[RaidReqPool::raidproxyiothread] for (;;) [END]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    }
    std::cout << "[RaidReqPool::raidproxyiothread] END" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
}

/*
void* RaidReqPool::raidproxyiothread()
{
    std::cout << "[RaidReqPool::raidproxyiothread] BEGIN" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    //_Exit(1);
    HttpReqPtr* events;
    //std::vector<HttpReqPtr> reqEvents;
    //std::map<std::pair<std::atomic<mega::reqstatus_t>&, HttpReq
    //std::deque<std::pair<HttpReqPtr, std::atomic<std::shared_ptr<RaidReq>>>> events;
    //std::vector<HttpReqPtr> events;

    int i, j, m, n, e;
    RaidReq* rr;
    int epollwait = -1;

    events = (HttpReqPtr*)calloc(MAXEPOLLEVENTS, sizeof(HttpReqPtr));
    int events_size = 0;

    //auto nCount =sss
    for (;;)
    {
        //std::vector<
        //n = epoll_wait(efd, events, MAXEPOLLEVENTS, epollwait);
        n = events_size;
        std::cout << "[RaidReqPool::raidproxyiothread] for (;;) [BEGIN] (n=" << n <<")" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;

        //if (epollwait >= 0) epollwait = -1;

        lock_guard<mutex> g(rrp_lock);  // this lock guarantees RaidReq will not be deleted between lookup and dispatch - locked for a while but only affects the main thread with new raidreqs being added or removed

        // initially process all the ones for which we can get the RaidReq lock instantly.  For any that are contended, skip and process the rest for now - then loop and retry, last loop locks rather than try_locks.
        for (j = 2; j--; )
        {
            m = 0;
            for (i = 0; i < n; i++)
            {
                auto s = events[i];
                //s = events[i].data.fd;
                //e = events[i].events;

                if ((rr = socketrrs.lookup(s)))  // retrieved under extremely brief lock.  RaidReqs can not be deleted until we unlock rrp_lock 
                {

                    //rr->isSocketConnected(s)
                    std::unique_lock<mutex> g(rr->rr_lock, std::defer_lock);
                    if (j > 0 ? g.try_lock() : (g.lock(), true)) 
                    {
                        rr->dispatchio(s);
                    }
                    else
                    {
                        // move it to the front
                        //events[m].events = e;
                        //events[m++].data.fd = s;
                    }
                }
            }

            if (j == 1)
            {
                while (!scheduledio.empty())
                {
                    epollwait = scheduledio.begin()->first-currtime;

                    if (epollwait <= 0)
                    {
                        
                        if (m == MAXEPOLLEVENTS)
                        {
                            // process remainder in the next round
                            epollwait = 0;
                            break;
                        }
                        

                        
                        // a scheduled io() has come up for execution
                        events[m].events = 0;
                        events[m++].data.fd = scheduledio.begin()->second;
                        
                        scheduledio.erase(scheduledio.begin());

                        epollwait = -1;
                    }
                    else
                    {
                        epollwait *= 1000;
                        break;
                    }
                }
            }

            std::cout << "[RaidReqPool::raidproxyiothread] for (;;) [END]" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
            if (m == 0) break;
            else n = m;
        }
    }
    std::cout << "[RaidReqPool::raidproxyiothread] END" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
}
*/

void* RaidReqPool::raidproxyiothreadstart(void* arg)
{
    return static_cast<RaidReqPool*>(arg)->raidproxyiothread();
}

RaidReqPool::RaidReqPool(RaidReqPoolArray& ar)
    : array(ar)
{
    std::cout << "[RaidReqPool::RaidReqPool] BEGIN" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    //efd = epoll_create(1);

    int err; 

    //std::thread t1(raidproxyiothreadstart, this);
    isRunning.store(true);
    if ((err = pthread_create(&rrp_thread, NULL, raidproxyiothreadstart, this)))
    {
        std::cout << "[RaidReqPool::RaidReqPool] E 10806 CloudRAID proxy thread creation failed: (err="<<err<<")" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
        LOGF("E 10806 CloudRAID proxy thread creation failed: %d", err);
        isRunning.store(false);
        exit(0);
    }
    std::cout << "[RaidReqPool::RaidReqPool] END" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
}

RaidReqPool::~RaidReqPool()
{
    std::cout << "[RaidReqPool::~RaidReqPool] BEGIN call DESTRUCTOR - GET LOCK" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    isRunning.store(false);
    {
        lock_guard<recursive_mutex> g(rrp_lock); // Let other operations end
    }
    std::cout << "[RaidReqPool::~RaidReqPool] DESTRUCTOR CALLED - LOCK GOT - joining raidproxyiothread..." << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    pthread_join(rrp_thread, NULL);
    std::cout << "[RaidReqPool::~RaidReqPool] END - DESTRUCTOR CALLED - THREAD JOINED" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
}

RaidReq* RaidReqPool::request(const mega::SCCR::RaidReq::Params& p, const std::shared_ptr<CloudRaid>& cloudRaid, int notifyfd)
{
    std::cout << "[RaidReqPool::RaidReqPool] BEGIN (GET LOCK)" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    lock_guard<recursive_mutex> g(rrp_lock);
    std::cout << "[RaidReqPool::RaidReqPool] LOCK GOT" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    auto rr = new RaidReq(p, *this, cloudRaid, notifyfd);
    rrs[rr].reset(rr);
    std::cout << "[RaidReqPool::RaidReqPool] END -> return rr="<<rr<<"" << " [thread_id=" << std::this_thread::get_id() << "]" << std::endl;
    return rr;
}

void RaidReqPool::removerequest(RaidReq* rr)
{
    // ToDo: Disconnect everything in flight
    if (rr)
    {
        lock_guard<recursive_mutex> g(rrp_lock);
    if (rr->slow1 < 0) current_fast--;
    else current_slow--;
        rr->disconnect();
        rrs.erase(rr);
    }
}

int RaidReqPool::rrcount()
{
    lock_guard<recursive_mutex> g(rrp_lock);
    return (int)rrs.size();
}

void RaidReqPoolArray::start(unsigned poolcount)
{
    for (unsigned i = poolcount; i--; ) rrps.emplace_back(new RaidReqPool(*this));
}

RaidReqPoolArray::Token RaidReqPoolArray::balancedRequest(const RaidReq::Params& params, const std::shared_ptr<CloudRaid>& cloudRaid, int notifyfd)
{
    Token t;
    int least = -1;
    for (int i = rrps.size(); i--; )
    {
        if (least == -1 || rrps[i]->rrcount() < least) 
        {
            least = rrps[i]->rrcount();
            t.poolId = i;
        }
    }

    t.rr = rrps[t.poolId]->request(params, cloudRaid, notifyfd);
    return t;
}

void RaidReqPoolArray::remove(Token& t)
{
    std::cout << "[RaidReqPoolArray::remove] BEGIN !!!! we should DISCONNECT EVERY HTTPREQ!!!!" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
    if (t.poolId >= 0 && t.poolId < rrps.size())
    {
        rrps[t.poolId]->removerequest(t.rr);
    }
    std::cout << "[RaidReqPoolArray::remove] END !!!! we should HAVE DISCONNECTED EVERY HTTPREQ!!!!" << " [thread_id = " << std::this_thread::get_id() << "]" << std::endl;
}
