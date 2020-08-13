/**
 * @file tcprelay.cpp
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

#include "tcprelay.h"
#include <functional>
#include <regex>
#include <iostream>
#include <sys/timeb.h>

using namespace std;

std::ostream* logstream = nullptr;

bool g_showreplyheaders = false;
bool g_showrequest = true;
uint64_t g_overallspeed = 1000000000;

atomic<unsigned> TcpRelay::Side::s_activesenders(0);
BucketCountArray<30> TcpRelay::s_send_rate_all_buckets;

void DelayAndDo(std::chrono::steady_clock::duration delayTime, std::function<void()>&& action, asio::io_service& as)
{
    auto t = std::make_shared<DelayAndDoRecord>(as, action);
    t->timer.expires_from_now(delayTime);
    t->timer.async_wait([t](const asio::error_code& ec) {  DelayAndDoHandler(ec, t); });
}

void DelayAndDoHandler(const asio::error_code& ec, std::shared_ptr<DelayAndDoRecord> t)
{
    if (ec)
    {
        cout << "delay-and-do timer failed: " << ec.message() << endl;
    }
    else
    {
        if (t)
            t->action();
    }
}


TcpRelay::TcpRelay(asio::io_service& as, const std::string& name, asio::ip::tcp::endpoint connect_endpoint)
    : reporting_name(name)
    , stopped(false)
    , paused(false)
    , restInProgress(false)
    , expected_incoming(0)
    , original_expected_incoming(0)
    , asio_service(as)
    , connect_address(connect_endpoint)
    , send_rate_timer(as)
    , accept_to_connect_circular_buf(new CircularBuffer<BufSize>)
    , connect_to_accept_circular_buf(new CircularBuffer<BufSize>)
    , acceptor_side(as)
    , connect_side(as)
    , forwardingDirection("forwarding", acceptor_side, connect_side, *accept_to_connect_circular_buf)
    , replyDirection("replying", connect_side, acceptor_side, *connect_to_accept_circular_buf)
{
    acceptor_side.asio_socket = asio::ip::tcp::socket(asio_service);//.open(asio::ip::tcp::v6());
    connect_side.asio_socket = asio::ip::tcp::socket(asio_service);// .open(asio::ip::tcp::v6());

    acceptor_side.Reset();
    connect_side.Reset();

    QueueRateTimer();
}

void TcpRelay::SetBytesPerSecond(size_t n)
{
    forwardingDirection.outgoing.target_bytes_per_second = n;  // variable is atomic so ok to assign from another thread
    replyDirection.outgoing.target_bytes_per_second = n;  // variable is atomic so ok to assign from another thread
    //connect_side.asio_socket.set_option(asio::ip::tcp::no_delay(true));
    //acceptor_side.asio_socket.set_option(asio::ip::tcp::no_delay(true));
}

void TcpRelay::Stop()
{
    if (stopped) return;
    asio_service.post([this]() {
        acceptor_side.asio_socket.close();
        connect_side.asio_socket.close();
        stopped = true;
    });
}


void TcpRelay::OutputDebugState(std::stringstream& s)
{
    s << "buf " << connect_to_accept_circular_buf->StoredByteCount() << " rate " << acceptor_side.send_rate_buckets.CalculatRate() << " ";
}


void TcpRelay::QueueRateTimer()
{
    if (stopped) return;
    send_rate_timer.expires_from_now(std::chrono::milliseconds(MillisecPerBucket));
    send_rate_timer.async_wait([this](const asio::error_code& ec) { RateTimerHandler(ec); });
}

void TcpRelay::RateTimerHandler(const asio::error_code& ec)
{
    if (stopped) return;
    RollBucket(forwardingDirection);
    RollBucket(replyDirection);
    QueueRateTimer();
}

void TcpRelay::RollBucket(Direction& d)
{
    if (stopped) return;
    if (d.circular_buf.StoredByteCount() > 0)
        d.outgoing.send_rate_buckets.AddToCurrentBucket(0);  // the bucket counts for averaging if we are sending but didn't actually get a callback in that period
    d.outgoing.send_rate_buckets.RollBucket();
    if (!d.outgoing.send_in_progress)
        StartSending(d);   // restart sending if we had to back off due to high rate
}

void TcpRelay::StopNow()
{
    if (stopped) return;
    cout << reporting_name << " Stopping, total relayed " << acceptor_side.totalbytes << " " << connect_side.totalbytes << endl;
    acceptor_side.asio_socket.close();
    connect_side.asio_socket.close();
    stopped = true;
}

void TcpRelay::StartConnecting()
{
    if (stopped) return;
    //LOGF("%s attempting connect to %s~%d", reporting_name.c_str(), connect_address.address().to_string().c_str(), (int)connect_address.port());
    connect_side.asio_socket.async_connect(connect_address, [this](const asio::error_code& ec) { ConnectHandler(ec); });
}

void TcpRelay::ConnectHandler(const asio::error_code& ec)
{
    if (stopped) return;
    if (ec)
    {
        cout << reporting_name << " connect failed: " << ec.message() << endl;
        StopNow();
    }
    else
    {
        cout << reporting_name << " connect success" << endl;
        //connect_side.asio_socket.set_option(asio::ip::tcp::no_delay(true));
        StartReceiving(forwardingDirection);
        StartReceiving(replyDirection);
    }
}

void TcpRelay::StartReceiving(Direction& d)
{
    if (stopped) return;
    assert(!d.incoming.receive_in_progress);
    auto range = d.circular_buf.PeekAheadBytes(ReadSize);
    if (range.len > 0)
    {
        d.incoming.receive_in_progress = true;
        d.incoming.asio_socket.async_receive(asio::buffer(range.start_pos, range.len), [this, &d](const asio::error_code& ec, std::size_t n) { ReceiveHandler(d, ec, n); });
    }
}

void TcpRelay::ReceiveHandler(Direction& d, const asio::error_code& ec, std::size_t bytes_received)
{
    if (stopped) return;
    assert(d.incoming.receive_in_progress);
    d.incoming.receive_in_progress = false;
    if (ec)
    {
        cout << reporting_name << " " << d.directionName << " error receiving: " << ec.message() << endl;
        StopNow();
    }
    else
    {
        if (paused)
        {
            d.incoming.receive_in_progress = true;
            return DelayAndDo(std::chrono::milliseconds(100), [this, &d, ec, bytes_received]() { ReceiveHandler(d, ec, bytes_received); }, asio_service);
        }

        d.incoming.totalbytes += bytes_received;
        if (d.directionName == "forwarding")
        {
            if (!restInProgress)
            {
                auto r = d.circular_buf.PeekAheadBytes(bytes_received).tostring();
                auto pos = r.find_first_of("\r\n");
                if (pos != string::npos) r.erase(pos);

                if (g_showrequest)
                {
                    cout << reporting_name << " " << bytes_received << " byte request: " << r << endl;
                    if (logstream) *logstream << this << " " << reporting_name << " " << bytes_received << " byte request: " << r << "\n";
                }

                std::regex re("([0-9]+)-([0-9]+)");
                std::smatch m;
                std::regex_search(r, m, re);
                if (m.size() == 3)
                {
                    //LOGF("from %u to %u", atoi(m[1].str().c_str()), atoi(m[2].str().c_str()));
                    expected_incoming = atoi(m[2].str().c_str()) - atoi(m[1].str().c_str());
                    original_expected_incoming = expected_incoming;
                }

                ++TcpRelay::Side::s_activesenders;
                restInProgress = true;
            }
        }
        else
        {
            if (original_expected_incoming == expected_incoming)
            {
                if (g_showreplyheaders)
                {
                    auto r = d.circular_buf.PeekAheadBytes(bytes_received).tostring();
                    auto pos = r.find("\r\n\r\n");
                    if (pos == string::npos) pos = r.size();
                    r.erase(pos);
                    cout << reporting_name << " " << bytes_received << " reply headers: " << r << endl;
                    if (logstream) *logstream << this << " " << reporting_name << " " << bytes_received << " reply headers: " << r << "\n";
                }
            }

            if (expected_incoming > 0)
            {
                expected_incoming -= (int)bytes_received;
                if (expected_incoming <= 0)
                {
                    cout << reporting_name << " " << d.directionName << " all data received: " << expected_incoming << endl;
                    if (restInProgress)
                    {
                        restInProgress = false;
                        --TcpRelay::Side::s_activesenders;
                    }
                }
            }
            //LOGF("received %d bytes, passing them on", (int)bytes_received);
        }
        d.circular_buf.CommitNewHeadBytes(bytes_received);
        if (!d.outgoing.send_in_progress)
            StartSending(d);
        StartReceiving(d);  
    }
}

void TcpRelay::RestartSending(Direction& d, const asio::error_code& ec)
{
    //--Side::s_activesenders;
    if (stopped) return;
    // we were sending too fast, now check again
    if (!ec)
    {
        //LOGF("%s %s restart send timer handler", reporting_name.c_str(), d.directionName.c_str());
        if (!d.outgoing.send_in_progress)
        {
            StartSending(d, true);
        }
    }
}

void TcpRelay::StartSending(Direction& d, bool restarted)
{
    if (stopped) return;
    assert(!d.outgoing.send_in_progress);

    //++Side::s_activesenders;

    auto rate1 = d.outgoing.send_rate_buckets.CalculatRate();
    auto rate2 = d.outgoing.send_rate_buckets.RateThisBucket();
    auto rate3 = s_send_rate_all_buckets.CalculatRate();
    auto rate4 = s_send_rate_all_buckets.RateThisBucket();
    if (rate1 >= d.outgoing.target_bytes_per_second || rate2 >= d.outgoing.target_bytes_per_second ||
        rate3 >= g_overallspeed || rate4 >= g_overallspeed)
    {
        d.outgoing.send_timer.expires_from_now(std::chrono::milliseconds(100));
        d.outgoing.send_timer.async_wait([this, &d](const asio::error_code& ec) { RestartSending(d, ec); });
        return;   // rate is too high, give up sending for a little.  The timer will restart us when the rate falls enough.
    }

    unsigned sendrate = (unsigned)d.outgoing.target_bytes_per_second;

    if (Side::s_activesenders)
    {
        sendrate = min<unsigned>(sendrate, unsigned(g_overallspeed / Side::s_activesenders));
    }

    auto range = d.circular_buf.PeekTailBytes(sendrate / 5 /*ReadSize*/);   // 10 shots per second so we can catch up when needed, on average we send / skip / send / skip

    if (range.len > 0)
    {
        static int call_id = 0;
        auto id = ++call_id;
        //LOGF("%s %s sending data %d id %d %p %s", reporting_name.c_str(), d.directionName.c_str(), (int)range.len, (int)id, range.start_pos, (restarted?"(restarted)" : ""));
        d.outgoing.send_in_progress = true;
        d.outgoing.asio_socket.async_write_some(asio::buffer(range.start_pos, range.len), [this, &d, id](const asio::error_code& ec, std::size_t n) { SendHandler(d, ec, n, id); });
        if (logstream)
        {
        #if _WIN32
            struct _timeb timebuffer;
            _ftime(&timebuffer); 
        #else /* _WIN32 */
            struct timeb timebuffer;
            ftime(&timebuffer);
        #endif /* ! _WIN32 */
            struct tm * timeinfo = localtime(&timebuffer.time);
            char stringtime[100];
            strftime(stringtime, 80, "%T.", timeinfo);
            *logstream << stringtime << timebuffer.millitm << " " << this << " wrote " << range.len << '\n';
        }
    }
    else
    {
        //LOGF("%s %s no more to send at this time", reporting_name.c_str(), d.directionName.c_str());
        //--Side::s_activesenders;
    }
}

void TcpRelay::SendHandler(Direction& d, const asio::error_code& ec, std::size_t bytes_sent, int id)
{
    if (stopped) return;
    assert(d.outgoing.send_in_progress);
    d.outgoing.send_in_progress = false;
    //--Side::s_activesenders;

    if (ec)
    {
        cout << reporting_name << " " << d.directionName << " error sending (id " << id << "): " << ec.message() << ".  only sent " << bytes_sent << " bytes" << endl;
        StopNow();
    }
    else
    {
        //LOGF("%s %s sent data %d callback id %d", reporting_name.c_str(), d.directionName.c_str(), (int)bytes_sent, (int)id);
        d.outgoing.send_rate_buckets.AddToCurrentBucket(bytes_sent);
        s_send_rate_all_buckets.AddToCurrentBucket(bytes_sent);

        d.circular_buf.RecycleTailBytes(bytes_sent);
        StartSending(d);  // if any more data has arrived in the meantime, send it now

        if (!d.incoming.receive_in_progress)
            StartReceiving(d);  // restart receiving if we needed to back off for a bit
    }
}

void TcpRelay::Pause(bool b)
{
    paused = b;
}

TcpRelayAcceptor::TcpRelayAcceptor(asio::io_service& as, const std::string& name, uint16_t port, asio::ip::tcp::endpoint connect_endpoint, onAcceptedFn f)
    : reporting_name(name)
    , listen_port(port)
    , asio_service(as)
    , connect_address(connect_endpoint)
    , asio_acceptor(as, asio::ip::tcp::endpoint(asio::ip::tcp::v6(), port))
    , nextRelay()
    , relayCount(0)
    , stopped(false)
    , onAccepted(f)
    , bytespersec(0)
{
    nextRelay.reset(new TcpRelay(asio_service, reporting_name + "-" + to_string(++relayCount), connect_address));
}

void TcpRelayAcceptor::SetBytesPerSecond(size_t n)
{
    bytespersec = n;
}

void TcpRelayAcceptor::Stop()
{
    asio_service.post([this]() {
        asio_acceptor.close();
        stopped = true;
    });
}

void TcpRelayAcceptor::Start()
{
    using asio::ip::tcp;

    if (stopped)
    {
        asio_acceptor =
          tcp::acceptor(asio_service, tcp::endpoint(tcp::v6(), listen_port));
    }

    stopped = false;
    StartAccepting();
}

void TcpRelayAcceptor::StartAccepting()
{
    if (stopped)
        return;

    //LOGF("%s accept starts", reporting_name.c_str());
    asio_acceptor.async_accept(nextRelay->acceptor_side.asio_socket, [this](const asio::error_code& ec) { AcceptHandler(ec); });
}

void TcpRelayAcceptor::AcceptHandler(const asio::error_code& ec)
{
    if (ec)
    {
        cout << reporting_name << " accept failed: " << ec.message() << endl;
        DelayAndDo(std::chrono::seconds(3), [this]() { StartAccepting();  }, asio_service);
    }
    else
    {
        // we have received an incoming socket connection.  So now make the corresponding connection to the remote side that we will forward all data to
        //LOGF("%s accepted a connection: %s", reporting_name.c_str(), nextRelay->reporting_name.c_str());
        if (bytespersec > 0) nextRelay->SetBytesPerSecond(bytespersec);
        nextRelay->StartConnecting();
        onAccepted(move(nextRelay));
        nextRelay.reset(new TcpRelay(asio_service, reporting_name + "-" + to_string(++relayCount), connect_address));
        StartAccepting();
    }
}



