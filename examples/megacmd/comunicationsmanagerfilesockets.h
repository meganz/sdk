#ifndef COMUNICATIONSMANAGERPOSIX_H
#define COMUNICATIONSMANAGERPOSIX_H

#include "comunicationsmanager.h"

#include <sys/types.h>
#include <sys/socket.h>

class CmdPetitionPosixSockets: public CmdPetition
{
public:
    int outSocket;
};

std::ostream &operator<<(std::ostream &os, CmdPetitionPosixSockets &p);

class ComunicationsManagerFileSockets : public ComunicationsManager
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
    mega::MegaMutex *mtx;

    int get_next_outSocket_id();

    /**
     * @brief create_new_socket
     * The caller is responsible for deleting the newly created socket
     * @return
     */
    int create_new_socket(int *sockId);

public:
    ComunicationsManagerFileSockets();

    int initialize();

    bool receivedReadlineInput(int readline_fd);

    bool receivedPetition();

    int waitForPetitionOrReadlineInput(int readline_fd);
    int waitForPetition();

    /**
     * @brief returnAndClosePetition
     * I will clean struct and close the socket within
     */
    void returnAndClosePetition(CmdPetition *inf, std::ostringstream *s, int);


    /**
     * @brief getPetition
     * @return pointer to new CmdPetitionPosix. Petition returned must be properly deleted (this can be calling returnAndClosePetition)
     */
    CmdPetition *getPetition();

    /**
     * @brief get_petition_details
     * @return a string describing details of the petition
     */
    std::string get_petition_details(CmdPetition *inf); //TODO: move to CMDPetitionPosix

    ~ComunicationsManagerFileSockets();
};


#endif // COMUNICATIONSMANAGERPOSIX_H
