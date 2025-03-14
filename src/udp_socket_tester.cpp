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
#include <chrono>
#include <map>

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
    if (!mPendingSentMessages.empty() || !mPendingReplies.empty()) // already running
    {
        return false;
    }

    mPendingSentMessages.clear();
    mPendingSentMessages.reserve(suite.totalMessageCount());
    mTestResults.messageResults.clear();
    mTestResults.log.clear();

    mShortMessage = getShortMessage(userId);
    mLongMessage = getLongMessage(userId);
    mDnsMessage = mSocket->isIPv4() ? dns_lookup_pseudomessage::getForIPv4(userId, 1234) :
                                      dns_lookup_pseudomessage::getForIPv6(userId, 1234);

    for (uint16_t lp = 0; lp < suite.loopCount; ++lp)
    {
        // send Short messages
        for (uint16_t s = 0; s < suite.shortMessageCount; ++s)
        {
            mPendingSentMessages.emplace_back(TestSuite::MessageType::SHORT,
                                              mSocket->sendAsyncMessage(mShortMessage));
        }
        // send Long messages
        for (uint16_t l = 0; l < suite.longMessageCount; ++l)
        {
            mPendingSentMessages.emplace_back(TestSuite::MessageType::LONG,
                                              mSocket->sendAsyncMessage(mLongMessage));
        }
        // send DNS messages
        for (uint16_t d = 0; d < suite.dnsMessageCount; ++d)
        {
            mPendingSentMessages.emplace_back(TestSuite::MessageType::DNS,
                                              mSocket->sendAsyncMessage(mDnsMessage));
        }
    }
    return true;
}

UdpSocketTester::SocketResults UdpSocketTester::getSocketResults()
{
    waitForStatusOfSending();
    receiveReplies();
    waitForStatusOfReceiving();

    return mTestResults;
}

void UdpSocketTester::waitForStatusOfSending()
{
    mTestResults.messageResults.reserve(mPendingSentMessages.size());

    for (auto& pending: mPendingSentMessages)
    {
        auto& messageType = pending.first;
        auto sendError = pending.second.get();
        mTestResults.messageResults.emplace_back(MessageResult{messageType, sendError.code});
        if (sendError.code)
        {
            log(string("sending ") + static_cast<char>(messageType) + " message",
                "[" + std::to_string(sendError.code) + "] " + sendError.message);
        }
    }

    mPendingSentMessages.clear();
}

void UdpSocketTester::receiveReplies()
{
    mPendingReplies.clear();
    mPendingReplies.reserve(mTestResults.messageResults.size());

    for (auto& m: mTestResults.messageResults)
    {
        if (!m.errorCode) // no error while sending
        {
            mPendingReplies.emplace_back(mSocket->receiveAsyncMessage(TIMEOUT_SECONDS));
            m.errorCode = REPLY_NOT_RECEIVED;
        }
    }
}

void UdpSocketTester::waitForStatusOfReceiving()
{
    auto timeout = std::chrono::seconds(TIMEOUT_SECONDS);
    auto timeoutMoment = std::chrono::system_clock::now() + timeout;

    for (auto& pendingReply: mPendingReplies)
    {
        if (pendingReply.wait_for(timeout) == future_status::ready)
        {
            if (auto reply = pendingReply.get(); !reply.code) // no error
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
                log("receiving reply",
                    "Receiving: [" + std::to_string(reply.code) + "] " + reply.message);
            }
        }
        else
        {
            log("receiving reply", "Reply not received");
        }

        // reset timeout after being reached
        if (timeout.count() && std::chrono::system_clock::now() > timeoutMoment)
            timeout = std::chrono::seconds::zero();
    }
    mPendingReplies.clear();
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
