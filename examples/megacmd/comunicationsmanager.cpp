#include "comunicationsmanager.h"

void destroy_thread_info_t(petition_info_t *t)
{
    if (t && ( t->line != NULL ))
    {
        free(t->line);
    }
}

int ComunicationsManager::get_next_outSocket_id()
{
    mtx->lock();
    ++count;
    mtx->unlock();
    return count;
}

int ComunicationsManager::create_new_socket(int *sockId){
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


ComunicationsManager::ComunicationsManager(){
    count = 0;
    mtx = new MegaMutex();
    initialize();
}

int ComunicationsManager::initialize(){
    mtx->init(false);

    MegaFileSystemAccess *fsAccess = new MegaFileSystemAccess();
    char csocketsFolder[19]; // enough to hold all numbers up to 64-bits
    sprintf(csocketsFolder, "/tmp/megaCMD_%d", getuid());
    string socketsFolder = csocketsFolder;

    int oldPermissions = fsAccess->getdefaultfolderpermissions();
    fsAccess->setdefaultfolderpermissions(0700);
    fsAccess->rmdirlocal(&socketsFolder);
    LOG_debug << "CREATING sockets folder: " << socketsFolder << "!!!";

    if (!fsAccess->mkdirlocal(&socketsFolder, false))
    {
        LOG_fatal << "ERROR CREATING sockets folder: " << socketsFolder << ": " << errno;
    }
    fsAccess->setdefaultfolderpermissions(oldPermissions);
    delete fsAccess;


//        sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
//        sockfd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
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

bool ComunicationsManager::receivedReadlineInput(int readline_fd){
    return FD_ISSET(readline_fd, &fds);
}

bool ComunicationsManager::receivedPetition()
{
    return FD_ISSET(sockfd, &fds);
}

int ComunicationsManager::waitForPetitionOrReadlineInput(int readline_fd)
{
    FD_ZERO(&fds);
        FD_SET(readline_fd, &fds);
    if (sockfd)
    {
        FD_SET(sockfd,      &fds);
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


int ComunicationsManager::waitForPetition()
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
void ComunicationsManager::returnAndClosePetition(petition_info_t *inf, std::ostringstream *s, int outCode){
    sockaddr_in cliAddr;
    socklen_t cliLength = sizeof( cliAddr );
    int connectedsocket = accept(inf->outSocket, (struct sockaddr*)&cliAddr, &cliLength);
    if (connectedsocket == -1)
    {
        LOG_fatal << "Unable to accept on outsocket " << inf->outSocket << " error: " << errno;
        destroy_thread_info_t(inf);
        delete inf;
        return;
    }
    string sout = s->str();

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
    close(inf->outSocket);
    destroy_thread_info_t(inf);
    delete inf;
}


/**
 * @brief getPetition
 * @return pointer to new petition_info_t. Petition returned must be properly deleted (this can be calling returnAndClosePetition)
 */
petition_info_t * ComunicationsManager::getPetition(){
    petition_info_t *inf = new petition_info_t();

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

ComunicationsManager::~ComunicationsManager()
{
    delete mtx;
}
