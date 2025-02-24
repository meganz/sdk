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

#include "mega/udp_socket.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#endif

#include <cassert>

namespace mega
{

UdpSocket::UdpSocket(const std::string& remoteIP, int remotePort)
{
    if (initializeSocketSupport())
    {
        if (!createRemoteAddress(remoteIP, remotePort) || !openBlockingSocket())
        {
            cleanupSocketSupport();
        }
    }
}

UdpSocket::~UdpSocket()
{
    if (mSocket)
    {
        // Get here only if initialization has completely succeeded in constructor
        closeSocket();
        cleanupSocketSupport();
    }
}

std::future<UdpSocket::Communication> UdpSocket::sendAsyncMessage(const std::string& message)
{
    return std::async(std::launch::async,
                      [this, message]()
                      {
                          return sendSyncMessage(message);
                      });
}

UdpSocket::Communication UdpSocket::sendSyncMessage(const std::string& message)
{
    if (!mSocket)
    {
        return {-1, "Socket not properly initialized"};
    }

    // set timeout for sending to 1 second
    timeval tv{};
    tv.tv_sec = 1;
    if (setsockopt(mSocket, SOL_SOCKET, SO_SNDTIMEO, (char*)&tv, sizeof(tv)) == -1)
    {
        return getSocketError();
    }

    if (sendtoWrapper(message) == -1)
    {
        return getSocketError();
    }

    return {};
}

std::future<UdpSocket::Communication> UdpSocket::receiveAsyncMessage(int timeout)
{
    return std::async(std::launch::async,
                      [this, timeout]()
                      {
                          return receiveSyncMessage(timeout);
                      });
}

UdpSocket::Communication UdpSocket::receiveSyncMessage(int timeout)
{
    if (!mSocket)
    {
        return {-1, "Socket not properly initialized"};
    }

    if (timeout > 0)
    {
        // IPv6: never reached here
        timeval tv{};
        tv.tv_sec = timeout;
        if (setsockopt(mSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv)) == -1)
        {
            return getSocketError();
        }
    }

    char buffer[2048];
    socklen_t addrSize =
        static_cast<socklen_t>(mInetType == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));
    auto bytesReceived = recvfrom(mSocket,
                                  buffer,
                                  sizeof(buffer) - 1,
                                  0, // flags
                                  getSockaddr(),
                                  &addrSize);
    if (bytesReceived > 0)
    {
        std::string response{buffer, static_cast<size_t>(bytesReceived)};
        return {0, response};
    }

    return getSocketError();
}

bool UdpSocket::createRemoteAddress(const std::string& remoteIP, int remotePort)
{
    bool remoteAddressOK = false;
    mRemoteAddress = std::make_unique<std::variant<sockaddr_in, sockaddr_in6>>();
    mInetType = remoteIP.find(':') == std::string::npos ? AF_INET : AF_INET6;
    if (mInetType == AF_INET)
    {
        // This should work with code for AF_INET6 after prefixing the IPv4 with "::ffff:".
        // But for some reasons it only worked on Linux and MacOS,
        // while on Windows it always returned 10049 (WSAEADDRNOTAVAIL) when sending.
        // The AF_INET-specific implementation below worked everywhere.
        *mRemoteAddress = sockaddr_in{};
        sockaddr_in& addr = std::get<sockaddr_in>(*mRemoteAddress);
        addr.sin_family = static_cast<decltype(addr.sin_family)>(mInetType);
        addr.sin_port = htons(static_cast<decltype(addr.sin_port)>(remotePort));
        remoteAddressOK =
            inet_pton(mInetType, remoteIP.c_str(), &addr.sin_addr) == 1 && addr.sin_port;
    }
    else
    {
        *mRemoteAddress = sockaddr_in6{};
        sockaddr_in6& addr = std::get<sockaddr_in6>(*mRemoteAddress);
        addr.sin6_family = static_cast<decltype(addr.sin6_family)>(mInetType);
        addr.sin6_port = htons(static_cast<decltype(addr.sin6_port)>(remotePort));
        remoteAddressOK =
            inet_pton(mInetType, remoteIP.c_str(), &addr.sin6_addr) == 1 && addr.sin6_port;
    }

    return remoteAddressOK;
}

sockaddr* UdpSocket::getSockaddr()
{
    sockaddr* addr = reinterpret_cast<sockaddr*>(std::get_if<sockaddr_in>(mRemoteAddress.get()));
    if (!addr)
        addr = reinterpret_cast<sockaddr*>(std::get_if<sockaddr_in6>(mRemoteAddress.get()));
    assert(addr);
    return addr;
}

//
// OS-specific implementations
//

bool UdpSocket::initializeSocketSupport()
{
#if defined(_WIN32)
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
#else
    return true;
#endif
}

void UdpSocket::cleanupSocketSupport()
{
#if defined(_WIN32)
    WSACleanup();
#endif
}

bool UdpSocket::openBlockingSocket()
{
#if defined(_WIN32)
    // Create socket using native function, because one created with socket() is unreliable.
    // For example it will fail to receive data if SO_RCVTIMEO was set.
    mSocket = static_cast<int>(WSASocket(mInetType, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, 0));
#else
    mSocket = socket(mInetType, SOCK_DGRAM, IPPROTO_UDP);
#endif

    if (mSocket != -1)
    {
#if !defined(_WIN32)
        // Remove O_NONBLOCK flag to enable blocking mode
        int flags = fcntl(mSocket, F_GETFL, 0); // Get current flags
        if (flags == -1 || fcntl(mSocket, F_SETFL, flags & ~O_NONBLOCK) == -1)
        {
            closeSocket();
            mSocket = 0;
        }
#endif
    }
    else
    {
        mSocket = 0;
    }

    return mSocket != 0;
}

void UdpSocket::closeSocket()
{
#if defined(_WIN32)
    closesocket(mSocket);
#else
    close(mSocket);
#endif
}

intmax_t UdpSocket::sendtoWrapper(const std::string& message)
{
    socklen_t addrSize =
        static_cast<socklen_t>(mInetType == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));
    return sendto(mSocket,
                  message.c_str(),
#if defined(_WIN32)
                  static_cast<int>(message.size()),
#else
                  message.size(),
#endif
                  0, // flags
                  getSockaddr(),
                  addrSize);
}

UdpSocket::Communication UdpSocket::getSocketError()
{
#if defined(_WIN32)
    int errorCode = WSAGetLastError();
    char* error_message = nullptr;

    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                   nullptr,
                   errorCode,
                   0,
                   (LPSTR)&error_message,
                   0,
                   nullptr);
    std::string messageCopy(error_message);
    LocalFree(error_message);

    return {errorCode, messageCopy};
#else
    return {errno, strerror(errno)};
#endif
}

} // namespace mega
