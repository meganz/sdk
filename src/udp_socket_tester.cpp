/**
 * (c) 2025 by Mega Limited, New Zealand
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

#include "mega/udp_socket_tester.h"

#include "mega/dns_lookup_pseudomessage.h"
#include "mega/udp_socket.h"
#include "mega/utils.h"

#include <algorithm>
#include <thread>

using namespace std;

namespace mega
{

UdpSocketTester::UdpSocketTester(const string& ip, uint16_t port):
    mTestResults{port, {}, {}}
{
    mSocket = std::make_unique<UdpSocket>(ip, port);
}

UdpSocketTester::~UdpSocketTester() = default;

bool UdpSocketTester::startSuite(const TestSuite& suite)
{
    if (mRunning)
    {
        return false;
    }

    mRunning = true;
    mTestResults.log.clear();
    mTestResults.messageResults.clear();
    mTestResults.messageResults.reserve(suite.totalMessageCount());

    mShortMessage = suite.getShortMessage();
    mLongMessage = suite.getLongMessage();
    mDnsMessage = mSocket->isIPv4() ? suite.getDnsIPv4Message() : suite.getDnsIPv6Message();

    for (uint16_t lp = 0, current = 0; lp < suite.loopCount; ++lp)
    {
        // send Short messages
        for (uint16_t s = 0; s < suite.shortMessageCount; ++s)
        {
            sendMessage(TestSuite::MessageType::SHORT, mShortMessage);
            sleepIfMultipleOf(++current, 10);
        }
        // send Long messages
        for (uint16_t l = 0; l < suite.longMessageCount; ++l)
        {
            sendMessage(TestSuite::MessageType::LONG, mLongMessage);
            sleepIfMultipleOf(++current, 10);
        }
        // send DNS messages
        for (uint16_t d = 0; d < suite.dnsMessageCount; ++d)
        {
            sendMessage(TestSuite::MessageType::DNS, mDnsMessage);
            sleepIfMultipleOf(++current, 10);
        }
    }
    return true;
}

void UdpSocketTester::sendMessage(UdpSocketTester::TestSuite::MessageType type,
                                  const string& message)
{
    auto&& sendResult{mSocket->sendSyncMessage(message)};

    mTestResults.messageResults.emplace_back(MessageResult{type, sendResult.code});

    if (sendResult.code)
    {
        log(string("sending ") + static_cast<char>(type) + " message",
            "[" + std::to_string(sendResult.code) + "] " + std::move(sendResult.message));
    }
}

void UdpSocketTester::sleepIfMultipleOf(uint16_t multiFactor, uint16_t factor)
{
    if (!(multiFactor % factor))
    {
        std::this_thread::sleep_for(1ms);
    }
}

UdpSocketTester::SocketResults
    UdpSocketTester::getSocketResults(const chrono::high_resolution_clock::time_point& timeout)
{
    // count expected replies
    int expectedReplyCount = 0;
    for (auto it = mTestResults.messageResults.begin(); it != mTestResults.messageResults.end();
         ++it)
    {
        if (!it->errorCode)
        {
            it->errorCode = REPLY_NOT_RECEIVED;
            ++expectedReplyCount;
        }
    }

    for (int i = 0; i < expectedReplyCount; ++i)
    {
        auto&& reply{mSocket->receiveSyncMessage(timeout)};
        if (!reply.code)
        {
            if (reply.message == mShortMessage)
            {
                confirmFirst(TestSuite::MessageType::SHORT);
            }
            else if (reply.message == mLongMessage)
            {
                confirmFirst(TestSuite::MessageType::LONG);
            }
            else if (reply.message == mDnsMessage)
            {
                confirmFirst(TestSuite::MessageType::DNS);
            }
            else
            {
                // log this but don't count it
                log("receiving reply",
                    "Invalid message (hex): " + Utils::stringToHex(reply.message, true));
                --i;
            }
        }
        else
        {
            log("receiving reply", "[" + std::to_string(reply.code) + "] " + reply.message);
        }
    }

    mRunning = false;

    return mTestResults;
}

void UdpSocketTester::confirmFirst(TestSuite::MessageType type)
{
    auto it = std::find_if(mTestResults.messageResults.begin(),
                           mTestResults.messageResults.end(),
                           [type](const MessageResult& r)
                           {
                               return r.messageType == type && r.errorCode == REPLY_NOT_RECEIVED;
                           });
    if (it != mTestResults.messageResults.end())
    {
        it->errorCode = 0;
    }
}

void UdpSocketTester::log(string&& action, string&& error)
{
    auto& counter =
        mTestResults.log["Error " + std::move(action) + ", IPv" + (mSocket->isIPv4() ? '4' : '6') +
                         ", port " + std::to_string(mTestResults.port) + ": " + std::move(error)];
    ++counter;
}

UdpSocketTester::TestSuite::TestSuite(uint16_t loops,
                                      uint16_t shorts,
                                      uint16_t longs,
                                      uint16_t dnss,
                                      uint64_t userId):
    loopCount{loops},
    shortMessageCount{shorts},
    longMessageCount{longs},
    dnsMessageCount{dnss}
{
    // short test message
    static constexpr char magicS = '\x33'; // ASCII 51
    mShortMessage = magicS + userIdToHex(userId);

    // long test message
    static constexpr char magicL = '\x51'; // ASCII 85
    string prefix = magicL + userIdToHex(userId);
    static constexpr size_t MAX_MESSAGE_LENGTH = 1400u;
    static constexpr char RANDOM_PADDING_CHAR = 'P';
    mLongMessage = prefix + string(MAX_MESSAGE_LENGTH - prefix.size(), RANDOM_PADDING_CHAR);

    // DNS test message for IPv4
    static constexpr uint16_t RANDOM_DNS_MESSAGE_ID = 1234;
    mDnsIPv4Message = dns_lookup_pseudomessage::getForIPv4(userId, RANDOM_DNS_MESSAGE_ID);
    mDnsIPv6Message = dns_lookup_pseudomessage::getForIPv6(userId, RANDOM_DNS_MESSAGE_ID);
}

} // namespace mega
