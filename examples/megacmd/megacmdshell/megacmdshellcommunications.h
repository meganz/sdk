#ifndef MEGACMDSHELLCOMMUNICATIONS_H
#define MEGACMDSHELLCOMMUNICATIONS_H

#include "megacmdshell.h"

#include <string>
#include <iostream>
#include <thread>
#include <mutex>

#ifdef _WIN32
#include <WinSock2.h>
#include <Shlwapi.h> //PathAppend
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/un.h>
#endif

#ifdef _WIN32
#include <windows.h>
#define ERRNO WSAGetLastError()
#else
#define ERRNO errno
#endif

#ifndef SOCKET_ERROR
#define SOCKET_ERROR -1
#endif

#ifdef __MACH__
#define MSG_NOSIGNAL 0
#elif _WIN32
#define MSG_NOSIGNAL 0
#endif

#define MEGACMDINITIALPORTNUMBER 12300

class MegaCmdShellCommunications
{
public:
    MegaCmdShellCommunications();
    ~MegaCmdShellCommunications();

    int executeCommand(std::string command, OUTSTREAMTYPE &output = COUT);

    static int registerForStateChanges();

    void setResponseConfirmation(bool confirmation);

    static bool serverinitiatedfromshell;
    static bool registerAgainRequired;

private:
    static bool socketValid(int socket);
    static void closeSocket(int socket);
    static int listenToStateChanges(int receiveSocket);

    static bool confirmResponse;

    static bool stopListener;
    static std::thread *listenerThread;

#ifdef _WIN32
static int createSocket(int number = 0, bool net = true);
#else
static int createSocket(int number = 0, bool net = false);
#endif


};

#endif // MEGACMDSHELLCOMMUNICATIONS_H
