#include "mega/sccloudraid/mega.h"
#include <algorithm>
#include <map>
#include <sstream>

// FIXME: add connection re-use between RaidReqs
// DONE: allow multiple CloudRAID threads with RaidReq-level mutexes
// FIXME: read local files directly
// FIXME: use predictive HTTP GET request pipelining to avoid RTTs
// DONE: use all connections
// FIXME: switch back to five connections when beneficial (less reconnection overhead)

#define MAXEPOLLEVENTS 1024

bool PartFetcher::updateGlobalBytesReceived;
atomic<uint64_t> PartFetcher::globalBytesReceived;

static atomic<int> current_readahead, highest_readahead;
static atomic<int> current_fast, current_slow;
static atomic<int> connest, connerr, readerr;
static atomic<int> bytesreceived;

void proxylog()
{
    static mtime_t lasttime;
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

    delayuntil = 0;

    memset(&target, 0, sizeof(target));
    target.sin6_family = AF_INET6;
    target.sin6_port = ntohs(80);   // we don't use SSL for inter-server encrypted file transfer
}

PartFetcher::~PartFetcher()
{
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
}

bool PartFetcher::setsource(short cserverid, byte* chash, RaidReq* crr, int cpart)
{
    serverid = cserverid;
    part = cpart;
    rr = crr;
    memcpy(hash, chash, sizeof hash);

    char buf[32];

    sprintf(buf, "storage.%d", serverid);

    if (!config.getallips(buf, &target.sin6_addr, 1))
    {
        LOGF("E 10801 Unknown CloudRAID target server: %d", serverid);
        errors++;
        return false;
    }

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
    remfeed = min(rem, (off_t)NUMLINES*RAIDSECTOR);
    if (sourcesize-pos < remfeed) remfeed = sourcesize-pos; // we only read to the physical end of the part
}

bool RaidReq::allconnected()
{
    for (int i = RAIDPARTS; i--; ) if (!fetcher[i].connected) return false;

    return true;
}

// close socket
void PartFetcher::closesocket()
{
    if (skip_setposrem)
    {
        skip_setposrem = false;
    }
    else
    {
        rem = 0;
        remfeed = 0;    // need to clear remfeed so that the disconnected channel does not corrupt feedlag
    }

    int& s = rr->sockets[part];

    if (s >= 0)
    {
       if (close(s) < 0) perror("Error closing socket");

        rr->pool.socketrrs.del(s);
        s = -1;

        connected = false;
    }
}

// (re)create, set up socket and start (optionally delayed) io on it
int PartFetcher::trigger(int delay)
{
    assert(serverid);

    closesocket();

    if (!rem)
    {
        assert(pos <= rr->paddedpartsize);
        if (pos == rr->paddedpartsize) return -1;
    }

    int& s = rr->sockets[part];

    if ((s = socket(AF_INET6, SOCK_STREAM, 0)) < 0)
    {
        LOGF("E 10800 Can't get CloudRAID socket (%d)", errno);
        exit(0);
    }

    TcpServer::makenblock(s);

    rr->pool.socketrrs.set(s, rr);  // set up for event to be handled immediately on the wait thread 

    epoll_event event;
    event.data.fd = s;
    event.events = EPOLLRDHUP | EPOLLHUP | EPOLLIN | EPOLLOUT | EPOLLET;

    int t = epoll_ctl(rr->pool.efd, EPOLL_CTL_ADD, s, &event);

    if (t < 0)
    {
        perror("CloudRAID EPOLL_CTL_ADD failed");
        exit(0);
    }

    if (delay) delayuntil = currtime+delay;

    return delay;
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
    int t, s = rr->sockets[part];

    assert(s >= 0);

    // FIXME: retrigger io() at retrytime
    if (!connected)
    {
        // prevent spurious epoll events from triggering a delayed reconnect early
        if (currtime < delayuntil) return -1;

        lastconnect = currtime;
        t = connect(s, (const sockaddr*)&target, sizeof target);

        if (t < 0 && errno != EINPROGRESS && errno != EALREADY)
        {
            int save_errno = errno;

            if (save_errno == EADDRNOTAVAIL)
            {
                LOGF("E 10809 CloudRAID proxy subsystem ran out of local ports");
                exit(0);    // we ran out of local ports: restart
            }

            static time_t lastlog;
            if (currtime > lastlog)
            {
                LOGF("E 10802 CloudRAID connection to %d failed (%d)", serverid, save_errno);
                lastlog = currtime;
            }
connerr++;
            errors++;

            if (consecutive_errors > MAXRETRIES)
            {
                // we are in slow mode and have ten connection failures in a row on this
                // server - switch back to fast mode
                if (rr->slow1 >= 0)
                {
                    closesocket();
                    rr->setfast();
                    for (int i = RAIDPARTS; i--; ) if (i != part && rr->sockets[i] < 0)
                    {
                        rr->fetcher[i].trigger();
                    }
                }

                return -1;
            }
            else
            {
                consecutive_errors++;
                for (int i = RAIDPARTS; i--; ) if (i != part && rr->sockets[i] < 0)
                {
                    rr->fetcher[i].trigger();
                }

                return trigger(save_errno == ETIMEDOUT ? -1 : 3);
            }
        }

        if (!t)
        {
            connected = true;
connest++;
            if (rr->slow1 < 0 && rr->allconnected())
            {
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
                closesocket();
                rr->resumeall();
                return -1;
            }

            DirectTicket dt;

            memcpy(dt.hash, hash, sizeof dt.hash);
            dt.size = sourcesize;
            dt.pos = pos;
            dt.rem = rem;
            dt.timestamp = rr->tickettime;
            dt.partshard = rr->shard | (part << 10);

            strcpy(outbuf, "GET /dl/");
            strcpy(outbuf+8+Base64::btoa((const byte*)&dt, sizeof dt, outbuf+8), " HTTP/0.9\r\n\r\n");
        }
    }

    if (connected)
    {
        while (*outbuf)
        {
            t = write(s, outbuf, strlen(outbuf));

            if (t <= 0)
            {
                if (!t || errno != EAGAIN)
                {
                    //int save_errno = errno;
                    LOGF("E 10803 CloudRAID request write to %d failed (%d)", serverid, errno);
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

        // feed from network
        char inbuf[NUMLINES*RAIDSECTOR];

        while (remfeed)
        {
            t = sizeof inbuf;
            if (remfeed < (unsigned)t) t = remfeed;

            t = read(s, inbuf, t);

            if (t <= 0)
            {
                if (!t || errno != EAGAIN)
                {
                    errors++;
                    int save_errno = t ? errno : 0;
                    LOGF("E 10804 CloudRAID data read from %d failed (%d)", serverid, save_errno);
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
                    return trigger(3);
                }

                break;
            }
            else
            {
                if (updateGlobalBytesReceived) globalBytesReceived += t;  // atomically added
bytesreceived += t;
                remfeed -= t;
                rem -= t;

                // completed a read: reset consecutive_errors
                if (!rem && consecutive_errors) consecutive_errors = 0;

                rr->procdata(part, inbuf, pos, t);

                if (!connected) break;

                pos += t;
            }
        }

        lastdata = currtime;

        if (!remfeed && pos == sourcesize && sourcesize < rr->paddedpartsize)
        {
            // we have reached the end of a part requires padding
            static char nulpad[RAIDSECTOR];

            rr->procdata(part, nulpad, pos, rr->paddedpartsize-sourcesize);
            rem = 0;
            pos = rr->paddedpartsize;
        }

        rr->procreadahead();

        if (!rem)
        {
            if (readahead.empty() && pos == rr->paddedpartsize)
            {
                closesocket();
                rr->resumeall();
            }
            else
            {
                trigger();
            }

            return -1;
        }
    }

    return -1;
}

// request a further chunk of data from the open connection
// (we cannot call io() directly due procdata() being non-reentrant)
void PartFetcher::cont(int numbytes)
{
    if (connected && pos < rr->paddedpartsize)
    {
        remfeed = numbytes;
        if (rem < remfeed) remfeed = rem;
        if (sourcesize-pos < remfeed) remfeed = sourcesize-pos; // we only read to the physical end of the part

        int s = rr->sockets[part];
assert(s >= 0);
        rr->pendingio.push_back(s);
    }
}

RaidReq::RaidReq(const Params& p, RaidReqPool& rrp, int notifyfd)
    : pool(rrp)
    , notifyeventfd(notifyfd)
{
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
    shard = p.shard;

    lastdata = currtime;
    haddata = false;
    reported = false;
    missingsource = false;

    for (int i = RAIDPARTS; i--; ) 
    {
        sockets[i] = -1;

        int serverid = i ? p.p1to5[i-1].serverid : p.serverid0;

        if (serverid)
        {
            // we don't trigger I/O on unknown source servers (which shouldn't exist in normal ops anyway)
            if (fetcher[i].setsource(serverid, i ? p.p1to5[i-1].hash : p.hash0, this, i))
            {
                // this kicks off I/O on that source
                fetcher[i].trigger();
            }
            else cout << "Unknown source server: " << serverid << ", please update config" << endl;
        }
        else
        {
            if (missingsource) cout << "Can't operate with more than one missing source server" << endl;
            missingsource = true;
        }
    }
}

// resume fetching on a parked source that has become eligible again
void PartFetcher::resume()
{
    // no readahead or end of readahead within READAHEAD?
    if (rr && rr->sockets[part] < 0 && pos < rr->paddedpartsize)
    {
        setposrem();

        if (rem)
        {
            skip_setposrem = true;
            trigger();
        }
    }
}

// try to resume fetching on all sources
void RaidReq::resumeall()
{
    if (slow1 >= 0) for (int i = RAIDPARTS; i--; ) fetcher[i].resume();
}

// feed suitable readahead data
bool PartFetcher::feedreadahead()
{
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
        char* d = it->second.first;
        unsigned l = it->second.second;
        readahead.erase(it);
current_readahead--;

        rr->procdata(part, d, p, l);
        free(d);

        remaining--;
    }

    return total && total != remaining;
}

// feed relevant read-ahead data to procdata
// returns true if any data was processed
void RaidReq::procreadahead()
{
    bool fed;

    do {
        fed = false;

        for (int i = RAIDPARTS; i--; )
        {
            if (fetcher[i].feedreadahead()) fed = true;
        }
    } while (fed);
}

// procdata() handles input in any order/size and will push excess data to readahead
// data is assumed to be 0-padded to paddedpartsize at EOF
void RaidReq::procdata(int part, char* ptr, off_t pos, int len)
{
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
        char* p = (char*)malloc(ahead_len);
        memcpy(p, ahead_ptr, ahead_len);
        fetcher[part].readahead[ahead_pos] = pair<char*, unsigned>(p, ahead_len);
// FIXME: race condition below
if (++current_readahead > highest_readahead) highest_readahead = (int)current_readahead;
        // if this is a pure readahead, we're done
        if (!consecutive) return;

        len = ahead_pos-pos;
    }

    // non-readahead data must flow contiguously
    assert(pos == partpos[part]+dataline*RAIDSECTOR);

    //int eof = partpos[part]+len == paddedpartsize-dataline*RAIDSECTOR;

    partpos[part] += len;

    unsigned t = pos-dataline*RAIDSECTOR;

    // ascertain absence of overflow (also covers parity part)
    assert(t+len <= sizeof data/(RAIDPARTS-1));

    // set valid bit for every block that's been received in full
    char partmask = 1 << part;
    int until = (t+len)/RAIDSECTOR;
    for (int i = t/RAIDSECTOR; i < until; i++)
    {
        assert(invalid[i] & partmask);
        invalid[i] -= partmask;
    }

    // copy (partial) blocks to data or parity buf
    if (part)
    {
        part--;

        char* ptr2 = ptr;
        int len2 = len;
        char* target = data + part * RAIDSECTOR + (t / RAIDSECTOR) * RAIDLINE;
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
    for (; completed < until; completed++)
    {
        unsigned char mask = invalid[completed];
        auto bitsSet = __builtin_popcount(mask);   // machine instruction for number of bits set

        assert(bitsSet);
        if (bitsSet > 1)
        {
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
    }

    if (completed > old_completed && notifyeventfd >= 0)
    {
        uint64_t n = 1;
        write(notifyeventfd, &n, sizeof(n)); // signal eventfd to wake up writer thread's epoll_wait
    }
}

void RaidReq::shiftdata(off_t len)
{
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
                    }
                    else
                    {
//                        cout << "Switching " << highest << " to slow" << endl;
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
                        fetcher[highest].closesocket();
                        fetcher[fresh].trigger();
                    }
                    else
                    {
                        int disconnected;
                        for (disconnected = RAIDPARTS; disconnected--; ) if (sockets[disconnected] < 0) break;

                        if (disconnected >= 0)
                        {
                            cout << "Switching to slow mode on " << (int)highest << "/" << (int)disconnected << endl;
                            setslow(highest, disconnected);
                        }
                        else cout << "No disconected channel available, carrying on..." << endl;
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
}

void RaidReq::dispatchio(int s)
{
    // fast lookup of which PartFetcher to call from a single cache line
    // we don't check for s not being found since we know sometimes it won't be when we closed a socket to a slower/nonresponding server
    for (int i = RAIDPARTS; i--; )
    {
        if (sockets[i] == s)
        {
            int t = fetcher[i].io();

            if (t >= 0)
            {
                // this is a relatively infrequent ocurrence, so we tolerate the overhead of a std::set insertion/erasure
                pool.scheduledio.insert(pair<mtime_t, int>(currtime+t, sockets[i]));            
            }

            break;
        }
    }
}

// execute cont()-triggered io()s
// must be called under lock
void RaidReq::handlependingio()
{
    while (!pendingio.empty())
    {
        int s = pendingio.front();
        pendingio.pop_front();
        dispatchio(s);
    }
}

// watchdog: resolve stuck connections
void RaidReq::watchdog()
{
    if (missingsource) return;

    // check for a single fast source hanging
    int hanging = 0;
    int hangingsource = -1;
    int idlegoodsource = -1;
    int numconnected = 0;

    for (int i = RAIDPARTS; i--; )
    {
        if (fetcher[i].connected && fetcher[i].remfeed && currtime-fetcher[i].lastdata > 2 && !fetcher[i].isslow())
        {
            hanging++;
            hangingsource = i;
        }

        if (fetcher[i].connected) numconnected++;
        if (!fetcher[i].connected && !fetcher[i].errors) idlegoodsource = i;
    }

    if (hanging)
    {
        if (slow1 < 0)
        {
            // fast mode
            if (idlegoodsource >= 0)
            {
                // "Attempted remedy: Switching from " << hangingsource << "/" << fetcher[hangingsource].serverid << " to previously unused " << fetcher[idlegoodsource].serverid << endl;

                fetcher[hangingsource].errors++;
                fetcher[hangingsource].closesocket();
                fetcher[idlegoodsource].trigger();
                return;
            }
            //else cout << "Inactive connection potentially bad" << endl;
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

//        cout << "No idle good source remaining - enter slow mode: Mark " << fetcher[hangingsource].serverid << " and " << fetcher[worst].serverid << " as slow" << endl;
        setslow(hangingsource, worst);
    }

//    cout << endl;
}

string RaidReq::getfaildescription()
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

    return string(buf);
}

int readdatacount;
off_t readdatatotal;
long readdatalock;
int numrrq;
int epolls, epollevents;

off_t RaidReq::readdata(char* buf, off_t len)
{
    off_t t;

    lock_guard<mutex> g(rr_lock);

    watchdog();
    t = completed*RAIDLINE-skip;

    if (t > 0)
    {
        if (t > len) t = len;

        memmove(buf, data+skip, t);

        shiftdata(t);
    }
    else
    {
        if (currtime-lastdata > 10)
        {
            if (currtime-lastdata > 60)
            {
                LOGF("E %d CloudRAID feed timed out", 10812+haddata);
                if (!haddata) pstats.raidproxyerr++;
                return -1;
            }

            if (!reported)
            {
                reported = true;

                LOGF("E %d CloudRAID feed stuck", 10810+haddata);
                if (!haddata) pstats.raidproxyerr++;
            }
        }
    }

    handlependingio();

    return t;
}

off_t RaidReq::senddata(int s, off_t len)
{
    off_t t;
    int e;

    lock_guard<mutex> g(rr_lock);

    watchdog();
    t = completed*RAIDLINE-skip;

    if (t > 0)
    {
        if (t > len) t = len;

        t = write(s, data+skip, t);
        e = errno;

        if (t > 0) shiftdata(t);
    }
    else
    {
        if (currtime-lastdata > 30 && !reported)
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

void* RaidReqPool::raidproxyiothread()
{
    epoll_event* events;
    int i, j, m, n, s, e;
    RaidReq* rr;
    int epollwait = -1;

    events = (epoll_event*)calloc(MAXEPOLLEVENTS, sizeof(epoll_event));

    for (;;)
    {
        n = epoll_wait(efd, events, MAXEPOLLEVENTS, epollwait);
        if (epollwait >= 0) epollwait = -1;

        lock_guard<mutex> g(rrp_lock);  // this lock guarantees RaidReq will not be deleted between lookup and dispatch - locked for a while but only affects the main thread with new raidreqs being added or removed

        // initially process all the ones for which we can get the RaidReq lock instantly.  For any that are contended, skip and process the rest for now - then loop and retry, last loop locks rather than try_locks.
        for (j = 2; j--; )
        {
            m = 0;
            for (i = 0; i < n; i++)
            {
                s = events[i].data.fd;
                e = events[i].events;

                if (s >= 0 && (rr = socketrrs.lookup(s)))  // retrieved under extremely brief lock.  RaidReqs can not be deleted until we unlock rrp_lock 
                {
                    unique_lock<mutex> g(rr->rr_lock, defer_lock);
                    if (j > 0 ? g.try_lock() : (g.lock(), true)) 
                    {
                        rr->dispatchio(s);
                    }
                    else
                    {
                        // move it to the front
                        events[m].events = e;
                        events[m++].data.fd = s;
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

            if (m == 0) break;
            else n = m;
        }
    }
}

void* RaidReqPool::raidproxyiothreadstart(void* arg)
{
    return static_cast<RaidReqPool*>(arg)->raidproxyiothread();
}

RaidReqPool::RaidReqPool(RaidReqPoolArray& ar)
    : array(ar)
{
    efd = epoll_create(1);

    pthread_t dummy;
    int err;

    if ((err = pthread_create(&dummy, NULL, raidproxyiothreadstart, this)))
    {
        LOGF("E 10806 CloudRAID proxy thread creation failed: %d", err);
        exit(0);
    }
}

RaidReq* RaidReqPool::request(const RaidReq::Params& p, int notifyfd)
{
    lock_guard<mutex> g(rrp_lock);
    auto rr = new RaidReq(p, *this, notifyfd);
    rrs[rr].reset(rr);
    return rr;
}

void RaidReqPool::removerequest(RaidReq* rr)
{
    lock_guard<mutex> g(rrp_lock);
if (rr->slow1 < 0) current_fast--;
else current_slow--;
    rrs.erase(rr);
}

int RaidReqPool::rrcount()
{
    lock_guard<mutex> g(rrp_lock);
    return (int)rrs.size();
}

void RaidReqPoolArray::start(unsigned poolcount)
{
    for (unsigned i = poolcount; i--; ) rrps.emplace_back(new RaidReqPool(*this));
}

RaidReqPoolArray::Token RaidReqPoolArray::balancedRequest(const RaidReq::Params& params, int notifyfd)
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

    t.rr = rrps[t.poolId]->request(params, notifyfd);
    return t;
}

void RaidReqPoolArray::remove(Token& t)
{
    rrps[t.poolId]->removerequest(t.rr);
}
