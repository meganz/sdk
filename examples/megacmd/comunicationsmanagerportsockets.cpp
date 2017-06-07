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


int ComunicationsManagerPortSockets::get_next_outSocket_id()
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

    *sockId = get_next_outSocket_id();
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
        LOG_fatal << "Error at WaitForMultipleObjects: " << GetLastError();
        Sleep(300);
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
    //TODO: implement
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
    int connectedsocket = accept(((CmdPetitionPortSockets *)inf)->outSocket, (struct sockaddr*)&cliAddr, &cliLength);
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
   // determine the required buffer size
   size_t buffer_size;
   wcstombs_s(&buffer_size, NULL, 0, sout.c_str(), _TRUNCATE);

   // do the actual conversion
   char *buffer = (char*) malloc(buffer_size);
   wcstombs_s(&buffer_size, buffer, buffer_size, sout.c_str(), _TRUNCATE);

   n = send(connectedsocket, buffer, buffer_size, MSG_NOSIGNAL);
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
    //TODO: implement
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

    int n = recv(newsockfd, buffer, 1023, MSG_NOSIGNAL);
#ifdef _WIN32
    // convert the UTF16 string to widechar
    size_t wbuffer_size;
    mbstowcs_s(&wbuffer_size, NULL, 0, buffer, _TRUNCATE);
    wchar_t *wbuffer = new wchar_t[wbuffer_size];
    mbstowcs_s(&wbuffer_size, wbuffer, wbuffer_size, buffer, _TRUNCATE);

    // convert the UTF16 widechar to UTF8 string
    string receivedutf8;
    MegaApi::utf16ToUtf8(wbuffer, wbuffer_size,&receivedutf8);
#endif

    if (n == SOCKET_ERROR)
    {
        LOG_fatal << "ERROR reading from socket";
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
