/**
 * @file examples/megacmd/comunicationsmanagerfilesockets.h
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
#ifndef COMUNICATIONSMANAGERFILESOCKETS_H
#define COMUNICATIONSMANAGERFILESOCKETS_H

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
