/**
 * @file examples/megacmd/comunicationsmanagerportsockets.cpp
 * @brief MegaCMD: Communications manager using Network Sockets
 *
 * (c) 2013-2016 by Mega Limited, Auckland, New Zealand
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

#include "comunicationsmanagerportsockets.h"

#ifdef _WIN32
#include <windows.h>
#define ERRNO WSAGetLastError()
#else
#define ERRNO errno
#endif

#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif

#ifndef EADDRINUSE
#define EADDRINUSE WSAEADDRINUSE
#endif

using namespace mega;


bool socketValid(int socket)
{
#ifdef _WIN32
    return socket != INVALID_SOCKET;
#else
    return socket >= 0;
#endif
}

void closeSocket(int socket){
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

#ifdef _WIN32
    HANDLE ComunicationsManagerPortSockets::readlinefd_event_handle;
    bool ComunicationsManagerPortSockets::ended;
#endif


int ComunicationsManagerPortSockets::get_next_comm_id()
{
    mtx->lock();
    ++count;
    mtx->unlock();
    return count;
}

int ComunicationsManagerPortSockets::create_new_socket(int *sockId)
{
    int thesock = socket(AF_INET, SOCK_STREAM, 0);

    if (!socketValid(thesock))
    {
        LOG_fatal << "ERROR opening socket: " << ERRNO ;
    }

    int portno=MEGACMDINITIALPORTNUMBER;

    *sockId = get_next_comm_id();
    portno += *sockId;

    struct sockaddr_in addr;

    memset(&addr, 0, sizeof( addr ));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
    addr.sin_port = htons(portno);

    socklen_t saddrlength = sizeof( addr );

    if (::bind(thesock, (struct sockaddr*)&addr, saddrlength) == SOCKET_ERROR)
    {
        if (ERRNO == EADDRINUSE)
        {
            LOG_warn << "ERROR on binding socket: Already in use.";
        }
        else
        {
            LOG_fatal << "ERROR on binding socket: " << ERRNO;
        }
    }
    else
    {
        if (thesock)
        {
            int returned = listen(thesock, 150);
            if (returned == SOCKET_ERROR)
            {
                LOG_fatal << "ERROR on listen socket: " << ERRNO;
            }
        }
        return thesock;
    }

    return 0;
}


ComunicationsManagerPortSockets::ComunicationsManagerPortSockets()
{
    count = 0;
    mtx = new MegaMutex();
    initialize();
}

#ifdef _WIN32
void * ComunicationsManagerPortSockets::watchReadlineFd(void *vfd)
{
    fd_set fds2;
    int fd = *(int *)vfd;

    while (!ended)
    {
        FD_ZERO(&fds2);
        FD_SET(fd, &fds2);

        int rc = select(FD_SETSIZE, &fds2, NULL, NULL, NULL);
        if (rc < 0)
        {
            if (errno != EINTR)  //syscall
            {
                if (errno != ENOENT) // unexpectedly enters here, although works fine TODO: review this
                {
                    //LOG_fatal << "Error at select: " << errno;
                    Sleep(20);
                    if (_kbhit()) //check if a key has been pressed
                    {
                        SetEvent(readlinefd_event_handle);
                    }
                    continue;
                }
                continue;
            }
        }
        LOG_info << "signaling readline event";

        SetEvent(readlinefd_event_handle);
    }
    return 0;
}
#endif
int ComunicationsManagerPortSockets::initialize()
{
    mtx->init(false);
#if _WIN32
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;

    /* Use the MAKEWORD(lowbyte, highbyte) macro declared in Windef.h */
    wVersionRequested = MAKEWORD(2, 2);

    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) {
        LOG_fatal << "ERROR initializing WSA";
    }
#endif

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (!socketValid(sockfd))
    {
        LOG_fatal << "ERROR opening socket";
    }

    int portno=MEGACMDINITIALPORTNUMBER;

    struct sockaddr_in addr;

    memset(&addr, 0, sizeof( addr ));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
    addr.sin_port = htons(portno);

    socklen_t saddrlength = sizeof( addr );

#ifdef _WIN32
    DWORD reuse = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) != 0) {
         printf("SO_REUSEADDR setsockopt failed with: %d\n", WSAGetLastError());
    }
#endif

    if (::bind(sockfd, (struct sockaddr*)&addr, saddrlength) == SOCKET_ERROR)
    {
        if (ERRNO == EADDRINUSE)
        {
            LOG_fatal << "ERROR on binding socket at port: " << portno << ": Already in use.";
        }
        else
        {
            LOG_fatal << "ERROR on binding socket at port: " << portno << ": " << ERRNO;
        }
        sockfd = -1;

    }
    else
    {
        int returned = listen(sockfd, 150);
        if (returned == SOCKET_ERROR)
        {
            LOG_fatal << "ERROR on listen socket initializing communications manager  at port: " << portno << ": " << ERRNO;
            return ERRNO;
        }
#if _WIN32
    sockfd_event_handle = WSACreateEvent();
    if (WSAEventSelect( sockfd, sockfd_event_handle, FD_ACCEPT) == SOCKET_ERROR )
    {
        LOG_fatal << "Error at WSAEventSelect: " << ERRNO;
    }
    WSAResetEvent(sockfd_event_handle);


    ended=false;
    readlinefd_event_handle = NULL;

#endif
    }

    return 0;
}

bool ComunicationsManagerPortSockets::receivedReadlineInput(int readline_fd)
{
    return FD_ISSET(readline_fd, &fds);
}

bool ComunicationsManagerPortSockets::receivedPetition()
{
    return FD_ISSET(sockfd, &fds);
}

int ComunicationsManagerPortSockets::waitForPetitionOrReadlineInput(int readline_fd)
{

    FD_ZERO(&fds);

#ifdef _WIN32
    if (readlinefd_event_handle == NULL)
    {
        readlinefd_event_handle = WSACreateEvent();
        WSAResetEvent(readlinefd_event_handle);
        MegaThread *readlineWatcherThread = new MegaThread();
        readlineWatcherThread->start(&ComunicationsManagerPortSockets::watchReadlineFd,(void *)&readline_fd);
    }
    HANDLE handles[2];

    handles[0] = sockfd_event_handle;
    handles[1] = readlinefd_event_handle;

    DWORD result = WSAWaitForMultipleEvents(2, handles, false, WSA_INFINITE, false);

    WSAResetEvent(handles[result - WSA_WAIT_EVENT_0]);

    switch(result) {
    case WSA_WAIT_TIMEOUT:
        break;

    case WSA_WAIT_EVENT_0 + 0:
        FD_SET(sockfd, &fds);
        break;

    case WSA_WAIT_EVENT_0 + 1:
        FD_SET(readline_fd, &fds);
        break;

    default: // handle the other possible conditions
        if (GetLastError() == ERROR_INVALID_HANDLE)
        {
            LOG_fatal << "Error at WaitForMultipleObjects: Port might be in use. Close any other instances";
        }
        else
        {
            LOG_fatal << "Error at WaitForMultipleObjects: " << GetLastError();
        }

        Sleep(2900);
        break;
    }

#else
    FD_SET(readline_fd, &fds);

    if (socketValid(sockfd))
    {
        FD_SET(sockfd, &fds);
    }
    else
    {
        LOG_warn << "invalid socket to select: " << sockfd  << " readline_fd="  << readline_fd;
    }

    int rc = select(FD_SETSIZE, &fds, NULL, NULL, NULL);
    if (rc == SOCKET_ERROR)
    {
        if (ERRNO != EINTR)  //syscall
        {
            LOG_fatal << "Error at select: " << ERRNO;
            return ERRNO;
        }
    }
#endif

    return 0;
}


int ComunicationsManagerPortSockets::waitForPetition()
{
    FD_ZERO(&fds);
    if (sockfd)
    {
        FD_SET(sockfd, &fds);
    }
    int rc = select(FD_SETSIZE, &fds, NULL, NULL, NULL);
    if (rc == SOCKET_ERROR)
    {
        if (ERRNO != EINTR)  //syscall
        {
            LOG_fatal << "Error at select: " << ERRNO;
            return ERRNO;
        }
    }
    return 0;
}

void ComunicationsManagerPortSockets::stopWaiting()
{
#ifdef _WIN32
    shutdown(sockfd,SD_BOTH);
#else
    shutdown(sockfd,SHUT_RDWR);
#endif
}

void ComunicationsManagerPortSockets::registerStateListener(CmdPetition *inf)
{
    LOG_debug << "Registering state listener petition with socket: " << ((CmdPetitionPortSockets *) inf)->outSocket;
    ComunicationsManager::registerStateListener(inf);
}

/**
 * @brief returnAndClosePetition
 * I will clean struct and close the socket within
 */
void ComunicationsManagerPortSockets::returnAndClosePetition(CmdPetition *inf, OUTSTRINGSTREAM *s, int outCode)
{
    LOG_verbose << "Output to write in socket " << ((CmdPetitionPortSockets *)inf)->outSocket << ": <<" << s->str() << ">>";
    sockaddr_in cliAddr;
    socklen_t cliLength = sizeof( cliAddr );
    int connectedsocket = ((CmdPetitionPortSockets *)inf)->acceptedOutSocket;
    if (connectedsocket == SOCKET_ERROR)
    {
        connectedsocket = accept(((CmdPetitionPortSockets *)inf)->outSocket, (struct sockaddr*)&cliAddr, &cliLength);
    }
    if (connectedsocket == SOCKET_ERROR)
    {
        LOG_fatal << "Unable to accept on outsocket " << ((CmdPetitionPortSockets *)inf)->outSocket << " error: " << ERRNO;
        delete inf;
        return;
    }

    OUTSTRING sout = s->str();
#ifdef __MACH__
#define MSG_NOSIGNAL 0
#elif _WIN32
#define MSG_NOSIGNAL 0
#endif
    int n = send(connectedsocket, (const char*)&outCode, sizeof( outCode ), MSG_NOSIGNAL);
    if (n == SOCKET_ERROR)
    {
        LOG_err << "ERROR writing output Code to socket: " << ERRNO;
    }

#ifdef _WIN32
   string sutf8;
   localwtostring(&sout,&sutf8);
   n = send(connectedsocket, sutf8.data(), sutf8.size(), MSG_NOSIGNAL);
#else
   n = send(connectedsocket, sout.data(), max(1,(int)sout.size()), MSG_NOSIGNAL); //TODO: test this max and do it for windows
#endif

    if (n == SOCKET_ERROR)
    {
        LOG_err << "ERROR writing to socket: " << ERRNO;
    }
    closeSocket(connectedsocket);
    closeSocket(((CmdPetitionPortSockets *)inf)->outSocket);
    delete inf;
}

int ComunicationsManagerPortSockets::informStateListener(CmdPetition *inf, string &s)
{
    LOG_verbose << "Inform State Listener: Output to write in socket " << ((CmdPetitionPortSockets *)inf)->outSocket << ": <<" << s << ">>";

    sockaddr_in cliAddr;
    socklen_t cliLength = sizeof( cliAddr );

    static map<int,int> connectedsockets;

    int connectedsocket = -1;
    if (connectedsockets.find(((CmdPetitionPortSockets *)inf)->outSocket) == connectedsockets.end())
    {
        connectedsocket = accept(((CmdPetitionPortSockets *)inf)->outSocket, (struct sockaddr*)&cliAddr, &cliLength); //this will be done only once??
        connectedsockets[((CmdPetitionPortSockets *)inf)->outSocket] = connectedsocket;
    }
    else
    {
        connectedsocket = connectedsockets[((CmdPetitionPortSockets *)inf)->outSocket];
    }

    if (connectedsocket == -1)
    {
        if (errno == 32) //socket closed
        {
            LOG_debug << "Unregistering no longer listening client. Original petition: " << *inf;
            connectedsockets.erase(((CmdPetitionPortSockets *)inf)->outSocket);
            return -1;
        }
        else
        {
            LOG_err << "Unable to accept on outsocket " << ((CmdPetitionPortSockets *)inf)->outSocket << " error: " << errno;
        }
        return 0;
    }

#ifdef __MACH__
#define MSG_NOSIGNAL 0
#endif

    int n = send(connectedsocket, s.data(), s.size(), MSG_NOSIGNAL);
    if (n < 0)
    {
        if (errno == 32) //socket closed
        {
            LOG_debug << "Unregistering no longer listening client. Original petition " << *inf;
            connectedsockets.erase(((CmdPetitionPortSockets *)inf)->outSocket);
            return -1;
        }
        else
        {
            LOG_err << "ERROR writing to socket: " << errno;
        }
    }

    //TODO: this two should be cleaned somewhere
//    close(connectedsocket);
//    close(((CmdPetitionPortSockets *)inf)->outSocket);
//    delete inf; //TODO: when should inf be deleted? (upon destruction I believe)
    return 0;
}

/**
 * @brief getPetition
 * @return pointer to new CmdPetitionPosix. Petition returned must be properly deleted (this can be calling returnAndClosePetition)
 */
CmdPetition * ComunicationsManagerPortSockets::getPetition()
{
    CmdPetitionPortSockets *inf = new CmdPetitionPortSockets();

    clilen = sizeof( cli_addr );

    newsockfd = accept(sockfd, (struct sockaddr*)&cli_addr, &clilen);

    if (!socketValid(newsockfd))
    {
        LOG_fatal << "ERROR on accept";
#if _WIN32
        Sleep(1000);
#else
        sleep(1);
#endif
        inf->line = strdup("ERROR");
        return inf;
    }

    memset(buffer, 0, 1024);

#ifdef _WIN32
    wchar_t wbuffer[1024]= {};
    int n = recv(newsockfd, (char *)wbuffer, 1023, MSG_NOSIGNAL);
    //TODO: control n is ok
    string receivedutf8;
    if (n != SOCKET_ERROR)
    {
        wbuffer[n]='\0';
        localwtostring(&wstring(wbuffer),&receivedutf8);
    }
    else
    {
        LOG_warn << "Received empty command from client";
    }

#else
    int n = recv(newsockfd, buffer, 1023, MSG_NOSIGNAL);
#endif

    if (n == SOCKET_ERROR)
    {
        LOG_fatal << "ERROR reading from socket errno: " << errno;
        inf->line = strdup("ERROR");
        return inf;
    }

    int socket_id = 0;
    inf->outSocket = create_new_socket(&socket_id);
    if (!socketValid(inf->outSocket) || !socket_id)
    {
        LOG_fatal << "ERROR creating output socket";
        inf->line = strdup("ERROR");
        return inf;
    }

    n = send(newsockfd, (const char*)&socket_id, sizeof( socket_id ), MSG_NOSIGNAL);

    if (n == SOCKET_ERROR)
    {
        LOG_fatal << "ERROR writing to socket: ERRNO = " << ERRNO;
        inf->line = strdup("ERROR");
        return inf;
    }
    closeSocket(newsockfd);
#if _WIN32
    inf->line = strdup(receivedutf8.c_str());
#else
    inf->line = strdup(buffer);
#endif
    return inf;
}

bool ComunicationsManagerPortSockets::getConfirmation(CmdPetition *inf, string message)
{
    sockaddr_in cliAddr;
    socklen_t cliLength = sizeof( cliAddr );
    int connectedsocket = ((CmdPetitionPortSockets *)inf)->acceptedOutSocket;
    if (connectedsocket == -1)
        connectedsocket = accept(((CmdPetitionPortSockets *)inf)->outSocket, (struct sockaddr*)&cliAddr, &cliLength);
     ((CmdPetitionPortSockets *)inf)->acceptedOutSocket = connectedsocket;
    if (connectedsocket == -1)
    {
        LOG_fatal << "Unable to accept on outsocket " << ((CmdPetitionPortSockets *)inf)->outSocket << " error: " << errno;
        delete inf;
        return false;
    }

    int outCode = MCMD_REQCONFIRM;
    int n = send(connectedsocket, (const char *)&outCode, sizeof( outCode ), MSG_NOSIGNAL);
    if (n < 0)
    {
        LOG_err << "ERROR writing output Code to socket: " << errno;
    }
    n = send(connectedsocket, message.data(), max(1,(int)message.size()), MSG_NOSIGNAL); // for some reason without the max recv never quits in the client for empty responses
    if (n < 0)
    {
        LOG_err << "ERROR writing to socket: " << errno;
    }

    bool response;
    n = recv(connectedsocket,(char *)&response, sizeof(response), MSG_NOSIGNAL);
    return response;
}

string ComunicationsManagerPortSockets::get_petition_details(CmdPetition *inf)
{
    ostringstream os;
    os << "socket output: " << ((CmdPetitionPortSockets *)inf)->outSocket;
    return os.str();
}


ComunicationsManagerPortSockets::~ComunicationsManagerPortSockets()
{
#if _WIN32
   WSACleanup();
   ended = true;
#endif
    delete mtx;
}
