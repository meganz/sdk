#ifndef COMUNICATIONSMANAGERPORTSOCKETS_H
#define COMUNICATIONSMANAGERPORTSOCKETS_H

#include "comunicationsmanager.h"

#include <sys/types.h>
#ifdef _WIN32
#include <WinSock2.h>
#else
#include <sys/socket.h>
#endif
#define MEGACMDINITIALPORTNUMBER 12300

class CmdPetitionPortSockets: public CmdPetition
{
public:
    int outSocket;
};

std::ostream &operator<<(std::ostream &os, CmdPetitionPortSockets &p);

class ComunicationsManagerPortSockets : public ComunicationsManager
{
private:
    fd_set fds;

    // sockets and asociated variables
    int sockfd, newsockfd;
#ifdef _WIN32
    HANDLE sockfd_event_handle;
#endif
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
    ComunicationsManagerPortSockets();

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

    ~ComunicationsManagerPortSockets();
};


#endif // COMUNICATIONSMANAGERPOSIX_H
