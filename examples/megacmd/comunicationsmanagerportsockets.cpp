/**
 * @file examples/megacmd/comunicationsmanagerportsockets.cpp
 * @brief MegaCMD: Communications manager
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

using namespace mega;

int ComunicationsManagerPortSockets::get_next_outSocket_id()
{
    mtx->lock();
    ++count;
    mtx->unlock();
    return count;
}

int ComunicationsManagerPortSockets::create_new_socket(int *sockId)
{
    int thesock = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);

    if (thesock < 0)
    {
        LOG_fatal << "ERROR opening socket";
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

    if (bind(thesock, (struct sockaddr*)&addr, saddrlength))
    {
        if (errno == EADDRINUSE)
        {
            LOG_warn << "ERROR on binding socket: Already in use.";
        }
        else
        {
            LOG_fatal << "ERROR on binding socket: " << errno;
            thesock = 0;
        }
    }
    else
    {
        if (thesock)
        {
            int returned = listen(thesock, 150);
            if (returned)
            {
                LOG_fatal << "ERROR on listen socket: " << errno;
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

int ComunicationsManagerPortSockets::initialize()
{
    mtx->init(false);

    sockfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);

    if (sockfd < 0)
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

    if (bind(sockfd, (struct sockaddr*)&addr, saddrlength))
    {
        if (errno == EADDRINUSE)
        {
            LOG_fatal << "ERROR on binding socket at port: " << portno << ": Already in use.";
        }
        else
        {
            LOG_fatal << "ERROR on binding socket at port: " << portno << ": " << errno;
        }
        sockfd = -1;

    }
    else
    {
        int returned = listen(sockfd, 150);
        if (returned)
        {
            LOG_fatal << "ERROR on listen socket initializing communications manager  at port: " << portno << ": " << errno;
            return errno;
        }
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
    FD_SET(readline_fd, &fds);
    if (sockfd > 0)
    {
        FD_SET(sockfd, &fds);
    }
    int rc = select(FD_SETSIZE, &fds, NULL, NULL, NULL);
    if (rc < 0)
    {
        if (errno != EINTR)  //syscall
        {
            LOG_fatal << "Error at select: " << errno;
            return errno;
        }
    }
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
    if (rc < 0)
    {
        if (errno != EINTR)  //syscall
        {
            LOG_fatal << "Error at select: " << errno;
            return errno;
        }
    }
    return 0;
}

/**
 * @brief returnAndClosePetition
 * I will clean struct and close the socket within
 */
void ComunicationsManagerPortSockets::returnAndClosePetition(CmdPetition *inf, std::ostringstream *s, int outCode)
{
    LOG_verbose << "Output to write in socket " << ((CmdPetitionPortSockets *)inf)->outSocket << ": <<" << s->str() << ">>";
    sockaddr_in cliAddr;
    socklen_t cliLength = sizeof( cliAddr );
    int connectedsocket = accept(((CmdPetitionPortSockets *)inf)->outSocket, (struct sockaddr*)&cliAddr, &cliLength);
    if (connectedsocket == -1)
    {
        LOG_fatal << "Unable to accept on outsocket " << ((CmdPetitionPortSockets *)inf)->outSocket << " error: " << errno;
        delete inf;
        return;
    }
    string sout = s->str();
#ifdef __MACH__
#define MSG_NOSIGNAL 0
#endif
    int n = send(connectedsocket, (void*)&outCode, sizeof( outCode ), MSG_NOSIGNAL);
    if (n < 0)
    {
        LOG_err << "ERROR writing output Code to socket: " << errno;
    }
    n = send(connectedsocket, sout.data(), sout.size(), MSG_NOSIGNAL);
    if (n < 0)
    {
        LOG_err << "ERROR writing to socket: " << errno;
    }
    close(connectedsocket);
    close(((CmdPetitionPortSockets *)inf)->outSocket);
    delete inf;
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

    if (newsockfd < 0)
    {
        LOG_fatal << "ERROR on accept";
        sleep(1);
        inf->line = strdup("ERROR");
        return inf;
    }

    bzero(buffer, 1024);
    int n = read(newsockfd, buffer, 1023);
    if (n < 0)
    {
        LOG_fatal << "ERROR reading from socket";
        inf->line = strdup("ERROR");
        return inf;
    }

    int socket_id = 0;
    inf->outSocket = create_new_socket(&socket_id);
    if (!inf->outSocket || !socket_id)
    {
        LOG_fatal << "ERROR creating output socket";
        inf->line = strdup("ERROR");
        return inf;
    }

    n = write(newsockfd, &socket_id, sizeof( socket_id ));
    if (n < 0)
    {
        LOG_fatal << "ERROR writing to socket: errno = " << errno;
        inf->line = strdup("ERROR");
        return inf;
    }
    close(newsockfd);


    inf->line = strdup(buffer);

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
    delete mtx;
}
