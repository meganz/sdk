#include "comunicationsmanager.h"

void destroy_thread_info_t(petition_info_t *t)
{
    if (t && t->line !=NULL)
        free(t->line);
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
    *sockId=get_next_outSocket_id();
    bzero(socket_path,sizeof(socket_path)*sizeof(*socket_path));
    sprintf(socket_path, "/tmp/megaCMD/srv_%d", *sockId);

    struct sockaddr_un addr;
    socklen_t saddrlen = sizeof(addr);

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);

    unlink(socket_path);

    if ( bind(thesock, (struct sockaddr*)&addr, saddrlen) )
    {
        if (errno == EADDRINUSE)
        {
            LOG_warn << "ERROR on binding socket: Already in use.";

        }
        else
        {
            LOG_fatal << "ERROR on binding socket: " << errno;
            thesock=0; //TODO: potentian issue: if no stdin/stdout, 0 is valid Id
        }
    }
    else
    {
        if (thesock)
        {
            int returned = listen(thesock,150); //TODO: check errors?
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
    count=0;
    mtx=new MegaMutex();
    initialize();
}

void ComunicationsManager::initialize(){
    mtx->init(false);

    MegaFileSystemAccess *fsAccess = new MegaFileSystemAccess();
    string socketsFolder = "/tmp/megaCMD";
    int oldPermissions = fsAccess->getdefaultfolderpermissions();
    fsAccess->setdefaultfolderpermissions(0700);
    fsAccess->rmdirlocal(&socketsFolder);
    if ( !fsAccess->mkdirlocal(&socketsFolder,false))
    {
        LOG_fatal << "ERROR CREATING sockets folder";
    }
    fsAccess->setdefaultfolderpermissions(oldPermissions);


//        sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
//        sockfd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);

    if (sockfd < 0)
    {
        LOG_fatal << "ERROR opening socket";
    }

    //        portno=12347;         //TODO: read port from somewhere
    //        bzero((char *) &serv_addr, sizeof(serv_addr));
    //        serv_addr.sin_family = AF_INET;
    //        serv_addr.sin_addr.s_addr = INADDR_ANY;
    //        serv_addr.sin_port = htons(poFrtno);
    //        if (bind(sockfd, (struct sockaddr *) &serv_addr,
    //                 sizeof(serv_addr)) < 0)


    struct sockaddr_un addr;
    socklen_t saddrlen = sizeof(addr);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    const char * socketPath = "/tmp/megaCMD/srv";
    strncpy(addr.sun_path, socketPath, sizeof(addr.sun_path)-1);


    unlink(socketPath);

    if ( bind(sockfd, (struct sockaddr*)&addr, saddrlen) )
    {
        if (errno == EADDRINUSE)
        {
            LOG_warn << "ERROR on binding socket: Already in use.";
//                exit(1);
//                close(sockfd);

//                sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
//                struct sockaddr saddr = {AF_UNIX, socketPath};

////                bzero((char *) &saddr, sizeof(saddr));
////                saddr.sa_family = AF_UNIX;
////                saddr.sa_data = socketPath;

//                socklen_t saddrlen = sizeof(struct sockaddr) + 6;

//                int yes=1;
//                if (setsockopt(sockfd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(int)) == -1) {
//                    LOG_fatal << "ERROR on setsockopt socket: " << errno;
//                    exit(1);
//                }

//                if ( bind(sockfd, &saddr, saddrlen) )
//                {
//                    LOG_fatal << "ERROR on binding socket: " << errno;
//                    sockfd=NULL;
//                }

        }
        else
        {
            LOG_fatal << "ERROR on binding socket: " << errno;
            sockfd=-1;
        }

    }
    else
    {
       listen(sockfd,150);
    }
}

bool ComunicationsManager::receivedReadlineInput (int readline_fd){
    return FD_ISSET(readline_fd, &fds);
}

bool ComunicationsManager::receivedPetition()
{
    return FD_ISSET(sockfd, &fds);
}

void ComunicationsManager::waitForPetitionOrReadlineInput(int readline_fd)
{
    FD_ZERO(&fds);
    FD_SET(readline_fd, &fds);
    if (sockfd)
        FD_SET(sockfd, &fds);
    int rc = select(FD_SETSIZE,&fds,NULL,NULL,NULL);
    if (rc < 0)
    {
        if (errno  != EINTR) //syscall
        {
            LOG_fatal << "Error at select: " << errno;
            //TODO: return?
        }
    }
}

/**
 * @brief returnAndClosePetition
 * I will clean struct and close the socket within
 */
void ComunicationsManager::returnAndClosePetition(petition_info_t *inf,std::ostringstream *s){

    sockaddr_in cliAddr;
    socklen_t cliLength = sizeof(cliAddr);
    int connectedsocket = accept(inf->outSocket,(struct sockaddr *) &cliAddr,&cliLength); //TODO: check errors

    //TODO use mutex, and free after accept

    if (connectedsocket == -1) {
        LOG_fatal << "Unable to accept on outsocket " << inf->outSocket << " error: " << errno;
        destroy_thread_info_t(inf);
        delete inf;
        return;
    }
    string sout = s->str();

    int n = send(connectedsocket,sout.data(),sout.size(),MSG_NOSIGNAL);
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

    clilen = sizeof(cli_addr);

    newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);

    if (newsockfd < 0)
    {
        LOG_fatal << "ERROR on accept";
        sleep (1);
        inf->line=strdup("ERROR");
        return inf;
    }

    bzero(buffer,1024);
    int n = read(newsockfd,buffer,1023);
    if (n < 0) {
        LOG_fatal << "ERROR reading from socket";
        inf->line=strdup("ERROR");
        return inf;
    }

    int socket_id = 0;
    inf->outSocket = create_new_socket(&socket_id);
    if (!inf->outSocket || !socket_id)
    {
        LOG_fatal << "ERROR creating output socket";
        inf->line=strdup("ERROR");
        return inf;
    }

    //TODO: investigate possible failure in case client disconects!
    n = write(newsockfd,&socket_id,sizeof(socket_id));
    if (n < 0){
        LOG_fatal << "ERROR writing to socket: errno = " << errno;
        inf->line=strdup("ERROR");
        return inf;
    }
    close (newsockfd);


    inf->line=strdup(buffer);

    return inf;

}

ComunicationsManager::~ComunicationsManager()
{
    delete mtx;
}
