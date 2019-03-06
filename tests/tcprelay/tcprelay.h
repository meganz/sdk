/**
 * @file tcprelay.h
 * @brief assists with testing MEGA CloudRAID
 *
 * (c) 2013-2019 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#pragma once
#include <asio.hpp>
#include <deque>
#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <iostream>

extern std::ostream* logstream;

template <unsigned BucketCount>
class BucketCountArray
{
    struct Bucket
    {
        size_t bytes = 0;
        size_t millisec = 0;
        bool valid = false;  // true if any download was in progress during this bucket; then the bucket counts towards average even if no data actually arrived during that timeslot
    };

    std::array<Bucket, BucketCount> buckets;
    const size_t Count = BucketCount;
    
    std::chrono::steady_clock::time_point current_bucket_start_time;
    bool started = false;

public:
    BucketCountArray()
    {
        Reset();
    }
    void Reset()
    {
        memset(&buckets[0], 0, sizeof buckets);
        started = false;
    }
    void RollBucket()
    {
        auto now =  std::chrono::steady_clock::now();
        buckets[BucketCount - 1].millisec = size_t(std::chrono::duration_cast<std::chrono::milliseconds>(now - current_bucket_start_time).count());
        current_bucket_start_time = now;

        // todo: make this circular for more efficiency later
        memmove(&buckets[0], &buckets[1], (BucketCount - 1) * sizeof(buckets[0]));
        buckets[BucketCount - 1] = Bucket{ 0, 0, false };
    }
    void AddToCurrentBucket(size_t bytes_sent)
    {
        if (!started)
        {
            started = true;
            current_bucket_start_time = std::chrono::steady_clock::now();
        }

        auto& bucket = buckets[BucketCount - 1];
        bucket.bytes += bytes_sent;
        bucket.valid = true;
    }
    size_t CalculatRate()
    {
        size_t bytessum = 0;
        size_t millisecsum = 0;
        for (int i = 0; i < BucketCount; ++i)
        {
            auto& bucket = buckets[i];
            if (bucket.valid)
            {
                bytessum += bucket.bytes;
                if (i == BucketCount - 1)
                    millisecsum += (size_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - current_bucket_start_time).count();
                else
                    millisecsum += bucket.millisec;
            }
        }
        return millisecsum == 0 ? 0 : bytessum * 1000 / millisecsum;
    }
    size_t RateThisBucket()
    {
        auto now = std::chrono::steady_clock::now();
        auto millisec = std::chrono::duration_cast<std::chrono::milliseconds>(now - current_bucket_start_time).count();
        if (millisec < 100)
            millisec = 100;
        return size_t(buckets[BucketCount - 1].bytes * 1000 / millisec);
    }
};

struct DataRange
{
    size_t start_pos = 0;
    size_t len = 0;
    bool valid() { return start_pos != 0 || len != 0; }
    DataRange() { }
    DataRange(size_t s, size_t n) { start_pos = s; len = n; }
};

struct BufferRange
{
    unsigned char* start_pos = nullptr;
    size_t len = 0;
    bool valid() { return start_pos != nullptr || len != 0; }
    BufferRange() { }
    BufferRange(unsigned char* s, size_t n) { start_pos = s; len = n; }
    std::string tostring() { return std::string((char*)start_pos, len); }
};


template <size_t Size>
class CircularBuffer
{
    unsigned char buf[Size];
    size_t tail = 0;
    size_t stored = 0;
public:

    BufferRange PeekAheadBytes(size_t up_to_n)
    {
        auto head = tail + stored;
        BufferRange r(head < Size ? buf + head : buf + head - Size,
            head < Size ? Size - head : Size - stored);
        if (r.len > up_to_n)
            r.len = up_to_n;
        //LOGF("%p: ahead %d %d (%d %d)", this, (int)(r.start_pos - buf), (int)r.len, (int)tail, (int)stored);
        return r;
    }
    void CommitNewHeadBytes(size_t exactly_n)
    {
        stored += exactly_n;
        assert(stored <= Size);
        //LOGF("%p: commit %d (%d %d)", this, (int)exactly_n, (int)tail, (int)stored);
    }
    BufferRange PeekTailBytes(size_t up_to_n)
    {
        BufferRange r(buf + tail, tail + stored <= Size ? stored : Size - tail);
        if (r.len > up_to_n)
            r.len = up_to_n;
        //LOGF("%p: gettail %d %d (%d %d)", this, (int)(r.start_pos - buf), (int)r.len, (int)tail, (int)stored);
        return r;
    }
    void RecycleTailBytes(size_t exactly_n)
    {
        assert(exactly_n <= stored);
        stored -= exactly_n;
        tail += exactly_n;
        if (tail >= Size)
            tail -= Size;
        //LOGF("%p: recycle %d (%d %d)", this, (int)exactly_n, (int)(tail), (int)stored);
    }
    void Reset()
    {
        size_t tail = 0;
        size_t stored = 0;
    }
    size_t StoredByteCount()
    {
        return stored;
    }
};

struct DelayAndDoRecord {
    asio::steady_timer timer;
    std::function<void()> action;
    DelayAndDoRecord(asio::io_service& as, std::function<void()> a) : timer(as), action(a) {}
};

void DelayAndDo(std::chrono::steady_clock::duration delayTime, std::function<void()>&& action, asio::io_service& as);
void DelayAndDoHandler(const asio::error_code& ec, std::shared_ptr<DelayAndDoRecord> t);


class TcpRelay
{
public:
    // This class is to assist manual testing, auto testing, and debugging. 
    // It opens a receiver socket for one incoming connection (from the program under test).  
    // When it is connected to, it makes an corresponding outgoing connection (to the server the progran would usually connect to).
    // Anything received from one side is forwarded to the other.
    // The connection can then simulate various conditions such as  disconnection, reconnection, controlling the max data throughput, or stopping data arriving

    TcpRelay(asio::io_service& as, const std::string& name, asio::ip::tcp::endpoint connect_endpoint);

    void SetBytesPerSecond(size_t);
    void Stop();

    void OutputDebugState(std::stringstream& s);

    std::string reporting_name;
    bool stopped = false;
    bool paused = false;

    int expected_incoming = 0;

private:
    enum { BufSize = 150 * 1024 };
    enum { ReadSize = 16 * 1024 };
    enum { MillisecPerBucket = 100 };

    asio::io_service& asio_service;
    asio::ip::tcp::endpoint connect_address;
    asio::steady_timer send_rate_timer;

    std::unique_ptr<CircularBuffer<BufSize>> accept_to_connect_circular_buf;  // keep this on the heap as it may be large
    std::unique_ptr<CircularBuffer<BufSize>> connect_to_accept_circular_buf;  // keep this on the heap as it may be large

    struct Side {
        asio::ip::tcp::socket asio_socket;
        asio::steady_timer send_timer;
        bool receive_in_progress = false;
        bool send_in_progress = false;
        std::atomic<size_t> target_bytes_per_second = 1024 * 1024;
        BucketCountArray<30> send_rate_buckets;
        size_t totalbytes = 0;

        Side(asio::io_service& as) 
            : asio_socket(as) 
            , send_timer(as)
        { 
            Reset();
        }
        void Reset() 
        { 
            assert(!receive_in_progress);
            assert(!send_in_progress);
            receive_in_progress = false; 
            send_in_progress = false;
            send_rate_buckets.Reset();
        }
    };

public:
    Side acceptor_side;
    Side connect_side;

    struct Direction
    {
        std::string directionName;
        Side& incoming;
        Side& outgoing;
        CircularBuffer<BufSize>& circular_buf;
        Direction(const std::string& n, Side& a, Side& b, CircularBuffer<BufSize>& buf) : directionName(n), incoming(a), outgoing(b), circular_buf(buf) {}
    };

    Direction forwardingDirection, replyDirection;

    void QueueRateTimer();
    void RateTimerHandler(const asio::error_code& ec);
    void RollBucket(Direction& d);
    void StopNow();
    void StartConnecting();
    void ConnectHandler(const asio::error_code& ec);
    void StartReceiving(Direction& d);
    void ReceiveHandler(Direction& d, const asio::error_code& ec, std::size_t bytes_received);
    void RestartSending(Direction& d, const asio::error_code& ec);
    void StartSending(Direction& d, bool restarted = false);
    void SendHandler(Direction& d, const asio::error_code& ec, std::size_t bytes_sent, int id);
    void Pause(bool b);
};

class TcpRelayAcceptor
{
    // listens on a port, and spawns a TcpRelay for each accepted connection
public:
    std::string reporting_name;
    uint16_t listen_port;

private:
    asio::io_service& asio_service;
    asio::ip::tcp::endpoint connect_address;

    asio::ip::tcp::acceptor asio_acceptor;

    std::unique_ptr<TcpRelay> nextRelay;
    int relayCount = 0;

    bool stopped = false;

    typedef std::function<void(std::unique_ptr<TcpRelay>&&)> onAcceptedFn;
    onAcceptedFn onAccepted;

    size_t bytespersec = 0;

public:

    TcpRelayAcceptor(asio::io_service& as, const std::string& name, uint16_t port, asio::ip::tcp::endpoint connect_endpoint, onAcceptedFn f);

    void SetBytesPerSecond(size_t);
    void Stop();
    void Start();

    void StartAccepting();
    void AcceptHandler(const asio::error_code& ec);
};


