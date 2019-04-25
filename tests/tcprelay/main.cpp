/**
 * @file main.cpp
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

#include <asio.hpp>
#include <fstream>
#include <thread>
#include <chrono>
#include <deque>
#include <vector>
#include <mutex>
#include "TcpRelay.h"
#include <windns.h>
#include <codecvt>
#include <mega.h>
#include <mega/logging.h>
#include <windns.h>
#include <iomanip>
#include <regex>

using namespace std;
using namespace ::mega;
namespace ac = ::mega::autocomplete;

void AsioThreadRunFunction(asio::io_service& asio_service, const std::string& name)
{
    try
    {
        asio_service.run();
    }
    catch (std::exception& e)
    {
        cout << "Asio service '" << name << "' exception: " << e.what() << endl;
    }
    catch (...)
    {
        cout << "Asio service '" << name << "' unknown exception." << endl;
    }
    cout << "Asio service '" << name << "' finished." << endl;
}

struct RelayRunner
{
    asio::io_service asio_service;											// a single service object so all socket callbacks are serviced by just one thread
    std::unique_ptr<asio::io_service::work> asio_dont_exit;
    asio::steady_timer logTimer;
    bool stopped = false;
    asio::steady_timer send_rate_timer;

    mutex relaycollectionmutex;
    vector<unique_ptr<TcpRelayAcceptor>> relayacceptors;
    vector<unique_ptr<TcpRelay>> acceptedrelays;

    RelayRunner()
        : asio_dont_exit(std::make_unique<asio::io_service::work>(asio_service))
        , logTimer(asio_service)
        , send_rate_timer(asio_service)
    {
        StartLogTimer();
        QueueRateTimer();
    }

    void AddAcceptor(const string& name, uint16_t port, asio::ip::address_v6 targetAddress, bool start)
    {
        lock_guard g(relaycollectionmutex);
        relayacceptors.emplace_back(new TcpRelayAcceptor(asio_service, name, port, asio::ip::tcp::endpoint(targetAddress, 80),
            [this](unique_ptr<TcpRelay> p)
        {
            lock_guard g(relaycollectionmutex);
            acceptedrelays.emplace_back(move(p));
            cout << acceptedrelays.back()->reporting_name << " acceptor is #" << (acceptedrelays.size() - 1) << endl;
        }));

        cout << "Acceptor active on " << port << ", relaying to " << name << endl;

        if (start)
        {
            relayacceptors.back()->Start();
        }
    }

    void RunRelays()
    {
        // keep these on their own thread & io_service so app glitches don't slow them down
        AsioThreadRunFunction(asio_service, "Relays");
    }

    void Stop()
    {
        asio_dont_exit.reset();  // remove the last work item so the service's run() will return
        asio_service.stop();  // interrupt asio's current wait for something to do
    }

    void StartLogTimer()
    {
        logTimer.expires_from_now(std::chrono::seconds(1));
        logTimer.async_wait([this](const asio::error_code&) { Log(); });
    }

    void Log()
    {
        lock_guard g(relaycollectionmutex);
        size_t eversent = 0, everreceived = 0;
        size_t sendRate = 0, receiveRate = 0;
        int senders = 0, receivers = 0, active = 0;
        for (size_t i = acceptedrelays.size(); i--; )
        {
            if (!acceptedrelays[i]->stopped)
            {
                size_t s = acceptedrelays[i]->connect_side.send_rate_buckets.CalculatRate();
                size_t r = acceptedrelays[i]->acceptor_side.send_rate_buckets.CalculatRate();
                if (s) senders += 1;
                if (r) receivers += 1;
                sendRate += s;
                receiveRate += r;
                active += 1;
            }
            eversent += acceptedrelays[i]->acceptor_side.totalbytes;
            everreceived += acceptedrelays[i]->connect_side.totalbytes;
        }
        cout << "active: " << active << " senders: " << senders << " rate " << sendRate << " receivers: " << receivers << " rate " << receiveRate << " totals: " << eversent << " " << everreceived << " 3sec-rate: " << TcpRelay::s_send_rate_all_buckets.CalculatRate() << endl;
        //TcpRelay::s_send_rate_all_buckets.show();
        StartLogTimer();
    }

    void report()
    {
        lock_guard g(relaycollectionmutex);
        for (auto& r : acceptedrelays)
        {
            cout << " " << r->reporting_name << ": " << r->acceptor_side.totalbytes << " " << r->connect_side.totalbytes << " " << (r->stopped ? "stopped" : "active") << (r->paused ? " (paused)" : "") << endl;
        }
    }

    void QueueRateTimer()
    {
        if (stopped) return;
        send_rate_timer.expires_from_now(std::chrono::milliseconds(TcpRelay::MillisecPerBucket));
        send_rate_timer.async_wait([this](const asio::error_code& ec) { RateTimerHandler(ec); });
    }

    void RateTimerHandler(const asio::error_code& ec)
    {
        if (stopped) return;
        TcpRelay::s_send_rate_all_buckets.RollBucket();
        TcpRelay::s_send_rate_all_buckets.AddToCurrentBucket(0); // all buckets valid
        QueueRateTimer();
    }

};

RelayRunner g_relays;


void addRelay(const string& server, uint16_t port)
{
    DNS_RECORD* pDnsrec;
    DNS_STATUS dnss = DnsQuery_A(server.c_str(), DNS_TYPE_A, DNS_QUERY_STANDARD, NULL,  &pDnsrec, NULL);
    
    if (dnss || !pDnsrec)
    {
        cout << "dns error" << endl;
        return;
    }
    
    asio::ip::address_v6::bytes_type bytes;
    for (auto& b : bytes) b = 0;
    bytes[10] = 0xff;
    bytes[11] = 0xff;
    bytes[12] = *(reinterpret_cast<unsigned char*>(&pDnsrec->Data.A.IpAddress) + 0);
    bytes[13] = *(reinterpret_cast<unsigned char*>(&pDnsrec->Data.A.IpAddress) + 1);
    bytes[14] = *(reinterpret_cast<unsigned char*>(&pDnsrec->Data.A.IpAddress) + 2);
    bytes[15] = *(reinterpret_cast<unsigned char*>(&pDnsrec->Data.A.IpAddress) + 3);
    
    asio::ip::address_v6 targetAddress(bytes);
    
    g_relays.AddAcceptor(string(server.c_str()), port, targetAddress, true);
    
}


uint16_t g_nextPort = 3677;

void exec_nextport(ac::ACState& ac)
{
    if (ac.words.size() == 2)
    {
        g_nextPort = (uint16_t)atoi(ac.words[1].s.c_str());
    }
    cout << "Next Port: " << g_nextPort << endl;
}


void exec_addrelay(ac::ACState& ac)
{
    addRelay(ac.words[1].s, g_nextPort++);
}

void exec_adddefaultrelays(ac::ACState& ac)
{
    addRelay("gfs262n300.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n118.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n108.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n108.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n221.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n108.userstorage.mega.co.nz", g_nextPort++);

    addRelay("gfs270n212.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n211.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n210.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n209.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n208.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n207.userstorage.mega.co.nz", g_nextPort++);

    addRelay("gfs302n117.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n117.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n127.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n117.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n309.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n128.userstorage.mega.co.nz", g_nextPort++);

}


void exec_addbulkrelays(ac::ACState& ac)
{
    addRelay("gfs262n300.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n110.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n111.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n113.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n114.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n115.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n116.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n117.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n118.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n119.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n120.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n121.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n122.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n123.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n124.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n125.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n126.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n127.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n128.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n129.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n130.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n131.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n132.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n100.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n101.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n103.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n104.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n105.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n106.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n107.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n108.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n109.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n110.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n111.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n112.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n113.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n114.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n115.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n116.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n117.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n118.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n119.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n120.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n121.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n122.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n100.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n101.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n103.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n104.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n105.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n106.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n107.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n108.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n109.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n110.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n111.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n112.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n113.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n114.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n115.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n116.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n117.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n118.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n119.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n120.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n121.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n122.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n143.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n145.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n146.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n147.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n151.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n153.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n165.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n167.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n168.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n169.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n173.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n174.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n176.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n182.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n184.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n186.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n187.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n189.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n300.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n301.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n302.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n303.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n304.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n305.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n306.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n307.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n308.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n309.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n310.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n311.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n312.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n313.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs262n316.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n111.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n112.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n113.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n114.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n115.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n116.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n117.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n119.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n120.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n121.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n122.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n124.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n125.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n126.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n127.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n128.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n165.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n166.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n167.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n170.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n171.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n172.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n173.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n174.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n175.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n176.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n221.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n404.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n405.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n406.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n407.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n408.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n100.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n101.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n103.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n104.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n105.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n106.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n107.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n108.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n109.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n110.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n111.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n112.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n113.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n114.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n115.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n116.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n117.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n118.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n119.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n120.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n121.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n123.userstorage.mega.co.nz", g_nextPort++);
}

void exec_getjavascript(ac::ACState& ac)
{
    lock_guard g(g_relays.relaycollectionmutex);
    for (auto& ra : g_relays.relayacceptors)
    {
        cout << "pieceUrl = pieceUrl.replace(\"" << ra->reporting_name << "\", \"localhost:" << ra->listen_port << "\");" << endl;
    }
    cout << "if (pieceUrl.includes(\"localhost\")) pieceUrl = pieceUrl.replace(\"https:\", \"http:\");" << endl;
}

void exec_getcpp(ac::ACState& ac)
{
    lock_guard g(g_relays.relaycollectionmutex);
    cout << "size_t pos;" << endl;
    for (auto& ra : g_relays.relayacceptors)
    {
        cout << "if (string::npos != (pos = posturl.find(\"" << ra->reporting_name << "\"))) posturl.replace(pos, " << ra->reporting_name.size() << ", \"localhost:" << ra->listen_port << "\");" << endl;
    }
    cout << "if (string::npos != (pos = posturl.find(\"https://\"))) posturl.replace(pos, 8, \"http://\");" << endl; 
}

void exec_closeacceptor(ac::ACState& ac)
{
    bool all = ac.words[1].s == "all";
    regex specific_re(ac.words[1].s);
    lock_guard g(g_relays.relaycollectionmutex);
    for (auto& r : g_relays.relayacceptors)
    {
        if (all || std::regex_match(r->reporting_name, specific_re))
        {
            r->Stop();
            cout << "closed " << r->reporting_name << endl;
        }
    }
}

void exec_closerelay(ac::ACState& ac)
{
    bool all = ac.words[1].s == "all";
    regex specific_re(ac.words[1].s);
    lock_guard g(g_relays.relaycollectionmutex);
    for (auto& r : g_relays.acceptedrelays)
    {
        if (all || std::regex_match(r->reporting_name, specific_re))
        {
            if (!r->stopped)
            {
                r->StopNow();
                cout << "closed " << r->reporting_name << endl;
            }
        }
    }
}

void exec_pauserelay(ac::ACState& ac)
{
    bool all = ac.words[1].s == "all";
    regex specific_re(ac.words[1].s);
    bool pause = 2 >= ac.words.size() || 0 != atoi(ac.words[2].s.c_str());
    lock_guard g(g_relays.relaycollectionmutex); 
    for (auto& r : g_relays.acceptedrelays)
    {
        if (all || std::regex_match(r->reporting_name, specific_re))
        {
            if (!r->stopped && r->paused != pause)
            {
                r->Pause(pause);
                cout << (pause ? "paused " : "unpaused ") << r->reporting_name << endl;
            }
        }
    }
}

void randompause(int periodsec, int pausesec)
{
    lock_guard g(g_relays.relaycollectionmutex);
    vector<TcpRelay*> tr;
    for (auto& r : g_relays.acceptedrelays)
    {
        if (!r->stopped && !r->paused) tr.push_back(r.get());
    }
    if (!tr.empty())
    {
        int n = rand() % tr.size();
        tr[n]->Pause(true);
        cout << "paused " << tr[n]->reporting_name << endl;
        DelayAndDo(chrono::seconds(pausesec), [=, p = tr[n]]() { 
            p->Pause(false);
            cout << "unpaused " << tr[n]->reporting_name << endl;
        }, g_relays.asio_service);
    }
    DelayAndDo(chrono::seconds(periodsec), [=]() { randompause(periodsec, pausesec); }, g_relays.asio_service);
}


void exec_randompauses(ac::ACState& ac)
{
    int periodsec = atoi(ac.words[1].s.c_str());
    int pausesec = atoi(ac.words[2].s.c_str());
    randompause(periodsec, pausesec);
}

void randomclose(int periodsec)
{
    lock_guard g(g_relays.relaycollectionmutex);
    vector<TcpRelay*> tr;
    for (auto& r : g_relays.acceptedrelays)
    {
        if (!r->stopped && !r->paused) tr.push_back(r.get());
    }
    if (!tr.empty())
    {
        int n = rand() % tr.size();
        tr[n]->StopNow();
        cout << "random closed " << tr[n]->reporting_name << endl;
    }
    DelayAndDo(chrono::seconds(periodsec), [=]() { randomclose(periodsec); }, g_relays.asio_service);
}

void exec_randomcloses(ac::ACState& ac)
{
    int periodsec = atoi(ac.words[1].s.c_str());
    randomclose(periodsec);
}

void exec_relayspeed(ac::ACState& ac)
{
    bool all = ac.words[1].s == "all";
    regex specific_re(ac.words[1].s);
    int speed = atoi(ac.words[2].s.c_str());
    lock_guard g(g_relays.relaycollectionmutex); 
    for (auto& r : g_relays.acceptedrelays)
    {
        if (all || std::regex_match(r->reporting_name, specific_re))
        {
            r->SetBytesPerSecond(speed);
        }
    }
}

void exec_acceptorspeed(ac::ACState& ac)
{
    bool all = ac.words[1].s == "all";
    regex specific_re(ac.words[1].s);
    int speed = atoi(ac.words[2].s.c_str());
    lock_guard g(g_relays.relaycollectionmutex);
    for (auto& r : g_relays.relayacceptors)
    {
        if (all || std::regex_match(r->reporting_name, specific_re))
        {
            r->SetBytesPerSecond(speed);
        }
    }
}


void exec_report(ac::ACState& ac)
{
    g_relays.report();
}

bool g_exitprogram = false;

void exec_exit(ac::ACState&)
{
    g_exitprogram = true;
}


ac::ACN autocompleteTemplate;

void exec_help(ac::ACState&)
{
    cout << *autocompleteTemplate << flush;
}

void exec_showrequest(ac::ACState& s)
{
    g_showreplyheaders = s.words.size() < 2 || s.words[1].s == "on";
}

void exec_showreply(ac::ACState& s)
{
    g_showrequest = s.words.size() < 2 || s.words[1].s == "on";
}

void exec_speed(ac::ACState& s)
{
    g_overallspeed = unsigned(atoi(s.words[1].s.c_str()));
}

ac::ACN autocompleteSyntax()
{
    using namespace autocomplete;
    std::unique_ptr<Either> p(new Either("      "));

    p->Add(exec_nextport, sequence(text("nextport"), opt(param("port"))));
    p->Add(exec_addrelay, sequence(text("addrelay"), param("server")));
    p->Add(exec_adddefaultrelays, sequence(text("adddefaultrelays")));
    p->Add(exec_addbulkrelays, sequence(text("addbulkrelays")));
    
    p->Add(exec_acceptorspeed, sequence(text("acceptorspeed"), either(text("all"), param("id")), param("bytespersec")));
    p->Add(exec_getjavascript, sequence(text("getjavascript")));
    p->Add(exec_getcpp, sequence(text("getc++")));

    p->Add(exec_relayspeed, sequence(text("relayspeed"), either(text("all"), param("id")), param("bytespersec")));
    p->Add(exec_pauserelay, sequence(text("pauserelay"), either(text("all"), param("id")), opt(either(text("1"), text("0")))));
    p->Add(exec_closerelay, sequence(text("closerelay"), either(text("all"), param("id"))));
    p->Add(exec_closeacceptor, sequence(text("closeacceptor"), either(text("all"), param("id"))));
    p->Add(exec_randomcloses, sequence(text("randomcloses"), param("period-sec")));
    p->Add(exec_randompauses, sequence(text("randompauses"), param("period-sec"), param("paused-sec")));
    p->Add(exec_showrequest, sequence(text("showrequest"), opt(either(text("on"), text("off")))));
    p->Add(exec_showreply, sequence(text("showreply"), opt(either(text("on"), text("off")))));
    p->Add(exec_speed, sequence(text("speed"), param("bytespersec")));

    p->Add(exec_report, sequence(text("report")));
    p->Add(exec_help, sequence(either(text("help"), text("?"))));
    p->Add(exec_exit, sequence(either(text("exit"))));
    
    return autocompleteTemplate = std::move(p);
}

class MegaCLILogger : public ::mega::Logger {
public:
    virtual void log(const char * /*time*/, int loglevel, const char * /*source*/, const char *message)
    {
#ifdef _WIN32
        OutputDebugStringA(message);
        OutputDebugStringA("\r\n");
#endif

        if (loglevel <= logWarning)
        {
            std::cout << message << std::endl;
        }
    }
};

MegaCLILogger logger;

// local console
Console* console;


int main()
{
    ofstream mylog("c:\\tmp\\tcprelaylog.txt");
    logstream = &mylog;

#ifdef _WIN32
    SimpleLogger::setLogLevel(logMax);  // warning and stronger to console; info and weaker to VS output window
    SimpleLogger::setOutputClass(&logger);
#else
    SimpleLogger::setAllOutputs(&std::cout);
#endif

    console = new CONSOLE_CLASS;

#ifdef HAVE_AUTOCOMPLETE
    ac::ACN acs = autocompleteSyntax();
#endif
#if defined(WIN32) && defined(NO_READLINE) && defined(HAVE_AUTOCOMPLETE)
    static_cast<WinConsole*>(console)->setAutocompleteSyntax((acs));
#endif


#ifndef NO_READLINE
    char *saved_line = NULL;
    int saved_point = 0;
#ifdef HAVE_AUTOCOMPLETE
    rl_attempted_completion_function = my_rl_completion;
#endif

    rl_save_prompt();

#elif defined(WIN32) && defined(NO_READLINE)
    static_cast<WinConsole*>(console)->setShellConsole(CP_UTF8, GetConsoleOutputCP());
#else
#error non-windows platforms must use the readline library
#endif

    std::thread relayRunnerThread([&]() { g_relays.RunRelays(); });

    while (!g_exitprogram)
    {
#if defined(WIN32) && defined(NO_READLINE)
        static_cast<WinConsole*>(console)->updateInputPrompt("TCPRELAY>");
#else
        rl_callback_handler_install(*dynamicprompt ? dynamicprompt : prompts[COMMAND], store_line);

        // display prompt
        if (saved_line)
        {
            rl_replace_line(saved_line, 0);
            free(saved_line);
        }

        rl_point = saved_point;
        rl_redisplay();
#endif
        char* line = nullptr;
        // command editing loop - exits when a line is submitted or the engine requires the CPU
        for (;;)
        {
            //int w = client->wait();
            Sleep(100);

            //if (w & Waiter::HAVESTDIN)
            {
#if defined(WIN32) && defined(NO_READLINE)
                line = static_cast<WinConsole*>(console)->checkForCompletedInputLine();
#else
                if (prompt == COMMAND)
                {
                    rl_callback_read_char();
                }
                else
                {
                    console->readpwchar(pw_buf, sizeof pw_buf, &pw_buf_pos, &line);
                }
#endif
            }

            //if (w & Waiter::NEEDEXEC || line)
            if (line)
            {
                break;
            }
        }

#ifndef NO_READLINE
        // save line
        saved_point = rl_point;
        saved_line = rl_copy_text(0, rl_end);

        // remove prompt
        rl_save_prompt();
        rl_replace_line("", 0);
        rl_redisplay();
#endif

        if (line)
        {
            // execute user command
            if (*line)
            {
                string consoleOutput;
                if (autoExec(line, strlen(line), autocompleteTemplate, false, consoleOutput, true))
                {
                    if (!consoleOutput.empty())
                    {
                        cout << consoleOutput << endl;
                    }
                }
            }
            free(line);
            line = NULL;

            if (!cerr)
            {
                cerr.clear();
                cerr << "Console error output failed, perhaps on a font related utf8 error or on NULL.  It is now reset." << endl;
            }
            if (!cout)
            {
                cout.clear();
                cerr << "Console output failed, perhaps on a font related utf8 error or on NULL.  It is now reset." << endl;
            }
        }
    }

    g_relays.Stop();
    relayRunnerThread.join();

    return 0;
}

