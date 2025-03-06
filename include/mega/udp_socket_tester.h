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

#ifndef MEGA_UDP_SOCKET_TESTER_H
#define MEGA_UDP_SOCKET_TESTER_H

#include "mega/udp_socket.h"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mega
{

/**
 * @brief Mechanism for running a network connectivity test, by sending multiple messages on a
 * single socket.
 * It includes:
 *  - encapsulate the logic for building messages;
 *  - send all messages on the required socket;
 *  - receive and validate replies;
 *  - provide the results after all communication has finished.
 */
class UdpSocketTester
{
public:
    UdpSocketTester(const std::string& ip, uint16_t port);
    ~UdpSocketTester();

    struct TestSuite
    {
        const uint16_t loopCount{};
        const uint16_t shortMessageCount{};
        const uint16_t longMessageCount{};
        const uint16_t dnsMessageCount{};

        enum class MessageType : char
        {
            SHORT = 'S',
            LONG = 'L',
            DNS = 'D',
        };

        uint16_t totalMessageCount() const
        {
            return static_cast<uint16_t>(loopCount *
                                         (shortMessageCount + longMessageCount + dnsMessageCount));
        }
    };

    bool startSuite(uint64_t userId, const TestSuite& suite);

    struct MessageResult
    {
        TestSuite::MessageType messageType;
        int errorCode;
    };

    struct SocketResults
    {
        const uint16_t port;
        std::vector<MessageResult> messageResults;

        std::map<std::string, uint16_t> log; // {log message, counter}
    };

    // Return {port, {{messageType, error}, ...}, logged messages}
    SocketResults getSocketResults(const std::chrono::high_resolution_clock::time_point& timeout);

private:
    void sendMessage(TestSuite::MessageType type, const std::string& message);
    static void sleepIfMultipleOf(uint16_t multiFactor, uint16_t factor);
    void confirmFirst(TestSuite::MessageType type);
    static std::string getShortMessage(uint64_t userId);
    static std::string getLongMessage(uint64_t userId);

    void log(std::string&& action, std::string&& error);

    std::unique_ptr<UdpSocket> mSocket;
    static constexpr int REPLY_NOT_RECEIVED = -1111;
    SocketResults mTestResults;
    bool mRunning{};

    std::string mShortMessage;
    std::string mLongMessage;
    std::string mDnsMessage;
};

} // namespace mega

#endif // MEGA_UDP_SOCKET_TESTER_H
