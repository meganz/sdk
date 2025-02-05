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

#ifndef MEGA_UDP_SOCKET_H
#define MEGA_UDP_SOCKET_H

#include <future>
#include <stdint.h>
#include <string>
#include <variant>

struct sockaddr_in;
struct sockaddr_in6;
struct sockaddr;

namespace mega
{

class UdpSocket final
{
public:
    UdpSocket(const std::string& remoteIP, int remotePort);
    ~UdpSocket();
    std::future<std::pair<int, std::string>> sendAsyncMessage(const std::string& message);
    std::future<std::pair<int, std::string>> receiveAsyncMessage(int timeout);

private:
    std::pair<int, std::string> sendSyncMessage(const std::string& message);
    std::pair<int, std::string> receiveSyncMessage(int timeout);
    bool createRemoteAddress(const std::string& remoteIP, int remotePort);
    sockaddr* getSockaddr();

    int mInetType{};
    std::unique_ptr<std::variant<sockaddr_in, sockaddr_in6>> mRemoteAddress;
    int mSocket{};

    //
    // OS-specific implementations
    //
    bool initializeSocketSupport();
    void cleanupSocketSupport();
    bool openBlockingSocket();
    intmax_t sendtoWrapper(const std::string& message);
    void closeSocket();
    static std::pair<int, std::string> getSocketError();
};

} // namespace mega

#endif // MEGA_UDP_SOCKET_H
