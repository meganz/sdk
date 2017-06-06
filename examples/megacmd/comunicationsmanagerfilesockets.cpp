/**
 * @file examples/megacmd/comunicationsmanagerfilesockets.cpp
 * @brief MEGAcmd: Communications manager using Network Sockets
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

#include "comunicationsmanagerfilesockets.h"

using namespace mega;

int ComunicationsManagerFileSockets::get_next_outSocket_id()
{
    mtx->lock();
    ++count;
    mtx->unlock();
    return count;
}

int ComunicationsManagerFileSockets::create_new_socket(int *sockId)
{
    int thesock = socket(AF_UNIX, SOCK_STREAM, 0);

    if (thesock < 0)
    {
        LOG_fatal << "ERROR opening socket";
    }

    char socket_path[60];
    *sockId = get_next_outSocket_id();
    bzero(socket_path, sizeof( socket_path ) * sizeof( *socket_path ));
    sprintf(socket_path, "/tmp/megaCMD_%d/srv_%d", getuid(), *sockId);

    struct sockaddr_un addr;
    socklen_t saddrlen = sizeof( addr );

    memset(&addr, 0, sizeof( addr ));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof( addr.sun_path ) - 1);

    unlink(socket_path);

    if (bind(thesock, (struct sockaddr*)&addr, saddrlen))
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


ComunicationsManagerFileSockets::ComunicationsManagerFileSockets()
{
    count = 0;
    mtx = new MegaMutex();
    initialize();
}

int ComunicationsManagerFileSockets::initialize()
{
    mtx->init(false);

    MegaFileSystemAccess *fsAccess = new MegaFileSystemAccess();
    char csocketsFolder[19]; // enough to hold all numbers up to 64-bits
    sprintf(csocketsFolder, "/tmp/megaCMD_%d", getuid());
    string socketsFolder = csocketsFolder;

    fsAccess->setdefaultfolderpermissions(0700);
    fsAccess->rmdirlocal(&socketsFolder);
    LOG_debug << "CREATING sockets folder: " << socketsFolder << "!!!";
    if (!fsAccess->mkdirlocal(&socketsFolder, false))
    {
        LOG_fatal << "ERROR CREATING sockets folder: " << socketsFolder << ": " << errno;
    }
    delete fsAccess;

    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (sockfd < 0)
    {
        LOG_fatal << "ERROR opening socket";
    }

    struct sockaddr_un addr;
    socklen_t saddrlen = sizeof( addr );
    memset(&addr, 0, sizeof( addr ));
    addr.sun_family = AF_UNIX;

    char socketPath[60];
    bzero(socketPath, sizeof( socketPath ) * sizeof( *socketPath ));
    sprintf(socketPath, "/tmp/megaCMD_%d/srv", getuid());

    strncpy(addr.sun_path, socketPath, sizeof( addr.sun_path ) - 1);

    unlink(socketPath);

    if (bind(sockfd, (struct sockaddr*)&addr, saddrlen))
    {
        if (errno == EADDRINUSE)
        {
            LOG_warn << "ERROR on binding socket: " << socketPath << ": Already in use.";
        }
        else
        {
            LOG_fatal << "ERROR on binding socket: " << socketPath << ": " << errno;
            sockfd = -1;
        }
    }
    else
    {
        int returned = listen(sockfd, 150);
        if (returned)
        {
            LOG_fatal << "ERROR on listen socket initializing communications manager: " << socketPath << ": " << errno;
            return errno;
        }
    }
    return 0;
}

bool ComunicationsManagerFileSockets::receivedReadlineInput(int readline_fd)
{
    return FD_ISSET(readline_fd, &fds);
}

bool ComunicationsManagerFileSockets::receivedPetition()
{
    return FD_ISSET(sockfd, &fds);
}

int ComunicationsManagerFileSockets::waitForPetitionOrReadlineInput(int readline_fd)
{
    FD_ZERO(&fds);
    FD_SET(readline_fd, &fds);
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


int ComunicationsManagerFileSockets::waitForPetition()
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
void ComunicationsManagerFileSockets::returnAndClosePetition(CmdPetition *inf, std::ostringstream *s, int outCode)
{

    LOG_verbose << "Output to write in socket " << ((CmdPetitionPosixSockets *)inf)->outSocket << ": <<" << s->str() << ">>";
    sockaddr_in cliAddr;
    socklen_t cliLength = sizeof( cliAddr );
    int connectedsocket = accept(((CmdPetitionPosixSockets *)inf)->outSocket, (struct sockaddr*)&cliAddr, &cliLength);
    if (connectedsocket == -1)
    {
        LOG_fatal << "Unable to accept on outsocket " << ((CmdPetitionPosixSockets *)inf)->outSocket << " error: " << errno;
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
    close(((CmdPetitionPosixSockets *)inf)->outSocket);
    delete inf;
}

/**
 * @brief getPetition
 * @return pointer to new CmdPetitionPosix. Petition returned must be properly deleted (this can be calling returnAndClosePetition)
 */
CmdPetition * ComunicationsManagerFileSockets::getPetition()
{
    CmdPetitionPosixSockets *inf = new CmdPetitionPosixSockets();

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

string ComunicationsManagerFileSockets::get_petition_details(CmdPetition *inf)
{
    ostringstream os;
    os << "socket output: " << ((CmdPetitionPosixSockets *)inf)->outSocket;
    return os.str();
}


ComunicationsManagerFileSockets::~ComunicationsManagerFileSockets()
{
    delete mtx;
}
