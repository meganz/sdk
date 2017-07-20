#ifndef MEGACMDSHELLCOMMUNICATIONSNAMEDPIPES_H
#define MEGACMDSHELLCOMMUNICATIONSNAMEDPIPES_H
#ifdef _WIN32

#include "megacmdshellcommunications.h"

#include <windows.h>
#include <Lmcons.h> //getusername

#define ERRNO WSAGetLastError()
#include <string>
#include <iostream>
#include <thread>
#include <mutex>

#include <Shlwapi.h> //PathAppend

class MegaCmdShellCommunicationsNamedPipes : public MegaCmdShellCommunications
{
public:
    MegaCmdShellCommunicationsNamedPipes();
    ~MegaCmdShellCommunicationsNamedPipes();

    int executeCommand(std::string command, bool (*readconfirmationloop)(const char *) = NULL, OUTSTREAMTYPE &output = COUT, bool interactiveshell = true);

    virtual int registerForStateChanges(void (*statechangehandle)(std::string) = NULL);

    void setResponseConfirmation(bool confirmation);

    static HANDLE doOpenPipe(std::wstring nameOfPipe);

private:
    static bool namedPipeValid(HANDLE namedPipe);
    static void closeNamedPipe(HANDLE namedPipe);
    static int listenToStateChanges(int receiveSocket, void (*statechangehandle)(std::string) = NULL);

    static bool confirmResponse;

    static bool stopListener;
    static std::thread *listenerThread;

    static HANDLE createNamedPipe(int number = 0);

};

#endif
#endif // MEGACMDSHELLCOMMUNICATIONS_H
