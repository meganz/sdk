/**
 * @file examples/megacmd/comunicationsmanagerfilesockets.cpp
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

#include "comunicationsmanagerfilesockets.h"
#include "megacmdutils.h"

#ifdef __MACH__
#define MSG_NOSIGNAL 0
#endif

using namespace mega;

int ComunicationsManagerFileSockets::get_next_comm_id()
{
    mtx->lock();
    ++count;
    mtx->unlock();
    return count;
}

int ComunicationsManagerFileSockets::create_new_socket(int *sockId)
{
    int thesock;
    int attempts = 10;
    bool socketsucceded = false;
    while (--attempts && !socketsucceded)
    {
       thesock = socket(AF_UNIX, SOCK_STREAM, 0);

        if (thesock < 0)
        {

            if (errno == EMFILE)
            {
                LOG_verbose << " Trying to reduce number of used files by sending ACK to listeners to discard disconnected ones.";
                string sack="ack";
                informStateListeners(sack);
            }
            if (attempts !=10)
            {
                LOG_fatal << "ERROR opening socket ID=" << sockId << " errno: " << errno << ". Attempts: " << attempts;
            }
            sleepMicroSeconds(500);
        }
        else
        {
            socketsucceded = true;
        }
    }
    if (thesock < 0)
    {
        return -1;
    }

    char socket_path[60];
    *sockId = get_next_comm_id();
    bzero(socket_path, sizeof( socket_path ) * sizeof( *socket_path ));
    sprintf(socket_path, "/tmp/megaCMD_%d/srv_%d", getuid(), *sockId);

    struct sockaddr_un addr;
    socklen_t saddrlen = sizeof( addr );

    memset(&addr, 0, sizeof( addr ));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof( addr.sun_path ) - 1);

    unlink(socket_path);

    bool bindsucceeded = false;

    attempts = 10;
    while (--attempts && !bindsucceeded)
    {
        if (bind(thesock, (struct sockaddr*)&addr, saddrlen))
        {
            if (errno == EADDRINUSE)
            {
                LOG_warn << "ERROR on binding socket: Already in use. Attempts: " << attempts;
            }
            else
            {
                LOG_fatal << "ERROR on binding socket " << socket_path << " errno: " << errno << ". Attempts: " << attempts;
            }
            sleepMicroSeconds(500);
        }
        else
        {
            bindsucceeded = true;
        }
    }


    if (bindsucceeded)
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
        if (errno == EBADF)
        {
            LOG_fatal << "Error at select: " << errno << ". Reinitializing socket";
            initialize();
            return EBADF;
        }

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

void ComunicationsManagerFileSockets::stopWaiting()
{
#ifdef _WIN32
    shutdown(sockfd,SD_BOTH);
#else
    shutdown(sockfd,SHUT_RDWR);
#endif
}


void ComunicationsManagerFileSockets::registerStateListener(CmdPetition *inf)
{
    LOG_debug << "Registering state listener petition with socket: " << ((CmdPetitionPosixSockets *) inf)->outSocket;
    ComunicationsManager::registerStateListener(inf);
}

/**
 * @brief returnAndClosePetition
 * I will clean struct and close the socket within
 */
void ComunicationsManagerFileSockets::returnAndClosePetition(CmdPetition *inf, OUTSTRINGSTREAM *s, int outCode)
{
    LOG_verbose << "Output to write in socket " << ((CmdPetitionPosixSockets *)inf)->outSocket << ": <<" << s->str() << ">>";
    sockaddr_in cliAddr;
    socklen_t cliLength = sizeof( cliAddr );
    int connectedsocket = ((CmdPetitionPosixSockets *)inf)->acceptedOutSocket;
    if (connectedsocket == -1)
    {
        connectedsocket = accept(((CmdPetitionPosixSockets *)inf)->outSocket, (struct sockaddr*)&cliAddr, &cliLength);
        ((CmdPetitionPosixSockets *)inf)->acceptedOutSocket = connectedsocket; //So that it gets closed in destructor
    }
    if (connectedsocket == -1)
    {
        LOG_fatal << "Return and close: Unable to accept on outsocket " << ((CmdPetitionPosixSockets *)inf)->outSocket << " error: " << errno;
        delete inf;
        return;
    }

    string sout = s->str();

    int n = send(connectedsocket, (void*)&outCode, sizeof( outCode ), MSG_NOSIGNAL);
    if (n < 0)
    {
        LOG_err << "ERROR writing output Code to socket: " << errno;
    }
    n = send(connectedsocket, sout.data(), max(1,(int)sout.size()), MSG_NOSIGNAL); // for some reason without the max recv never quits in the client for empty responses
    if (n < 0)
    {
        LOG_err << "ERROR writing to socket: " << errno;
    }

    delete inf;
}

int ComunicationsManagerFileSockets::informStateListener(CmdPetition *inf, string &s)
{
    LOG_verbose << "Inform State Listener: Output to write in socket " << ((CmdPetitionPosixSockets *)inf)->outSocket << ": <<" << s << ">>";

    sockaddr_in cliAddr;
    socklen_t cliLength = sizeof( cliAddr );

    static map<int,int> connectedsockets;

    int connectedsocket = -1;
    if (connectedsockets.find(((CmdPetitionPosixSockets *)inf)->outSocket) == connectedsockets.end())
    {
        //select with timeout and accept non-blocking, so that things don't get stuck
        fd_set set;
        FD_ZERO(&set);
        FD_SET(((CmdPetitionPosixSockets *)inf)->outSocket, &set);

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 4000000;
        int rv = select(((CmdPetitionPosixSockets *)inf)->outSocket+1, &set, NULL, NULL, &timeout);
        if(rv == -1)
        {
            LOG_err << "Informing state listener: Unable to select on outsocket " << ((CmdPetitionPosixSockets *)inf)->outSocket << " error: " << errno;
            return -1;
        }
        else if(rv == 0)
        {
            LOG_warn << "Informing state listener: timeout in select on outsocket " << ((CmdPetitionPosixSockets *)inf)->outSocket;
        }
        else
        {
            int oldfl = fcntl(sockfd, F_GETFL);
            fcntl(((CmdPetitionPosixSockets *)inf)->outSocket, F_SETFL, oldfl | O_NONBLOCK);
            connectedsocket = accept(((CmdPetitionPosixSockets *)inf)->outSocket, (struct sockaddr*)&cliAddr, &cliLength);
            fcntl(((CmdPetitionPosixSockets *)inf)->outSocket, F_SETFL, oldfl);
        }
        connectedsockets[((CmdPetitionPosixSockets *)inf)->outSocket] = connectedsocket;
    }
    else
    {
        connectedsocket = connectedsockets[((CmdPetitionPosixSockets *)inf)->outSocket];
    }

    if (connectedsocket == -1)
    {
        if (errno == 32) //socket closed
        {
            LOG_debug << "Unregistering no longer listening client. Original petition: " << *inf;
            connectedsockets.erase(((CmdPetitionPosixSockets *)inf)->outSocket);
            return -1;
        }
        else
        {
            LOG_err << "Informing state listener: Unable to accept on outsocket " << ((CmdPetitionPosixSockets *)inf)->outSocket << " error: " << errno;
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
            LOG_debug << "Unregistering no longer listening client. Original petition: " << *inf;
            close(connectedsocket);
            connectedsockets.erase(((CmdPetitionPosixSockets *)inf)->outSocket);
            return -1;
        }
        else
        {
            LOG_err << "ERROR writing to socket: " << errno;
        }
    }

    return 0;
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
        if (errno == EMFILE)
        {
            LOG_fatal << "ERROR on accept at getPetition: TOO many open files.";
            //send state listeners an ACK command to see if they are responsive and close them otherwise
            string sack = "ack";
            informStateListeners(sack);
        }
        else
        {
            LOG_fatal << "ERROR on accept at getPetition: " << errno;
        }

        sleep(1);
        inf->line = strdup("ERROR");
        return inf;
    }

    bzero(buffer, 1024);
    int n = read(newsockfd, buffer, 1023); //TODO: petitions for size > 1023?
    if (n < 0)
    {
        LOG_fatal << "ERROR reading from socket at getPetition: " << errno;
        inf->line = strdup("ERROR");
        return inf;
    }

    int socket_id = 0;
    inf->outSocket = create_new_socket(&socket_id);
    if (!inf->outSocket || !socket_id)
    {
        LOG_fatal << "ERROR creating output socket at getPetition: " << errno;
        inf->line = strdup("ERROR");
        return inf;
    }

    n = write(newsockfd, &socket_id, sizeof( socket_id ));
    if (n < 0)
    {
        LOG_fatal << "ERROR writing to socket at getPetition: " << errno;
        inf->line = strdup("ERROR");
        return inf;
    }
    close(newsockfd);


    inf->line = strdup(buffer);

    return inf;
}

bool ComunicationsManagerFileSockets::getConfirmation(CmdPetition *inf, string message)
{
    sockaddr_in cliAddr;
    socklen_t cliLength = sizeof( cliAddr );
    int connectedsocket = ((CmdPetitionPosixSockets *)inf)->acceptedOutSocket;
    if (connectedsocket == -1)
        connectedsocket = accept(((CmdPetitionPosixSockets *)inf)->outSocket, (struct sockaddr*)&cliAddr, &cliLength);
     ((CmdPetitionPosixSockets *)inf)->acceptedOutSocket = connectedsocket;
    if (connectedsocket == -1)
    {
        LOG_fatal << "Getting Confirmation: Unable to accept on outsocket " << ((CmdPetitionPosixSockets *)inf)->outSocket << " error: " << errno;
        delete inf;
        return false;
    }

    int outCode = MCMD_REQCONFIRM;
    int n = send(connectedsocket, (void*)&outCode, sizeof( outCode ), MSG_NOSIGNAL);
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
    n = recv(connectedsocket,&response, sizeof(response), MSG_NOSIGNAL);

    return response;
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
