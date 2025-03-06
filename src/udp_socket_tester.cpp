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

bool UdpSocketTester::startSuite(uint64_t userId, const TestSuite& suite)
{
    if (mRunning)
    {
        return false;
    }

    mRunning = true;
    mTestResults.log.clear();
    mTestResults.messageResults.clear();
    mTestResults.messageResults.reserve(suite.totalMessageCount());

    mShortMessage = getShortMessage(userId);
    mLongMessage = getLongMessage(userId);
    static constexpr uint16_t randomDnsMessageID = 1234;
    mDnsMessage = mSocket->isIPv4() ?
                      dns_lookup_pseudomessage::getForIPv4(userId, randomDnsMessageID) :
                      dns_lookup_pseudomessage::getForIPv6(userId, randomDnsMessageID);

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
                log("receiving reply", "Reply could not be matched with an original message");
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

string UdpSocketTester::getShortMessage(uint64_t userId)
{
    static constexpr char magic = '\x33'; // ASCII 51
    return magic + userIdToHex(userId);
}

string UdpSocketTester::getLongMessage(uint64_t userId)
{
    static constexpr char magic = '\x51'; // ASCII 85
    string prefix = magic + userIdToHex(userId);
    static constexpr size_t MAX_MESSAGE_LENGTH = 1400u;
    return prefix + string(MAX_MESSAGE_LENGTH - prefix.size(), 'P');
}

void UdpSocketTester::log(string&& action, string&& error)
{
    auto& counter =
        mTestResults.log["Error " + std::move(action) + ", IPv" + (mSocket->isIPv4() ? '4' : '6') +
                         ", port " + std::to_string(mTestResults.port) + ": " + std::move(error)];
    ++counter;
}

} // namespace mega
