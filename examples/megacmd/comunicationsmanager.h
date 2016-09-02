#ifndef COMUNICATIONSMANAGER_H
#define COMUNICATIONSMANAGER_H

#include "megaapi_impl.h"
using namespace mega;

struct petition_info_t {
    char * line = NULL;
    int outSocket;
};

class ComunicationsManager //TODO: do interface somewhere and move this
{
private:
    fd_set fds;

    // sockets and asociated variables
    int sockfd, newsockfd;
    socklen_t clilen;
    char buffer[1024];
    struct sockaddr_in serv_addr, cli_addr;

    // to get next socket id
    int count;
    MegaMutex *mtx;

    int get_next_outSocket_id();

    /**
     * @brief create_new_socket
     * The caller is responsible for deleting the newly created socket
     * @return
     */
    int create_new_socket(int *sockId);

public:
    ComunicationsManager();

    int initialize();

    bool receivedReadlineInput (int readline_fd);

    bool receivedPetition();

    int waitForPetitionOrReadlineInput(int readline_fd);
    int waitForPetition();

    /**
     * @brief returnAndClosePetition
     * I will clean struct and close the socket within
     */
    void returnAndClosePetition(petition_info_t *inf,std::ostringstream *s);


    /**
     * @brief getPetition
     * @return pointer to new petition_info_t. Petition returned must be properly deleted (this can be calling returnAndClosePetition)
     */
    petition_info_t *getPetition();

    ~ComunicationsManager();
};

#endif // COMUNICATIONSMANAGER_H
