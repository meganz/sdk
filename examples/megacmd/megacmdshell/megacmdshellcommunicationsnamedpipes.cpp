#ifdef _WIN32
//#include "megacmdshell.h"
#include "megacmdshellcommunicationsnamedpipes.h"

#include <iostream>
#include <thread>
#include <sstream>

#include <shlobj.h> //SHGetFolderPath
#include <Shlwapi.h> //PathAppend

#include <fcntl.h>
#include <io.h>

using namespace std;

enum
{
    MCMD_OK = 0,              ///< Everything OK

    MCMD_EARGS = -51,         ///< Wrong arguments
    MCMD_INVALIDEMAIL = -52,  ///< Invalid email
    MCMD_NOTFOUND = -53,      ///< Resource not found
    MCMD_INVALIDSTATE = -54,  ///< Invalid state
    MCMD_INVALIDTYPE = -55,   ///< Invalid type
    MCMD_NOTPERMITTED = -56,  ///< Operation not allowed
    MCMD_NOTLOGGEDIN = -57,   ///< Needs loging in
    MCMD_NOFETCH = -58,       ///< Nodes not fetched
    MCMD_EUNEXPECTED = -59,   ///< Unexpected failure

    MCMD_REQCONFIRM = -60,     ///< Confirmation required

};

bool MegaCmdShellCommunicationsNamedPipes::confirmResponse; //TODO: do all this only in parent class
bool MegaCmdShellCommunicationsNamedPipes::stopListener;
std::thread *MegaCmdShellCommunicationsNamedPipes::listenerThread;

bool MegaCmdShellCommunicationsNamedPipes::namedPipeValid(HANDLE namedPipe)
{
    return namedPipe != INVALID_HANDLE_VALUE;
}

void MegaCmdShellCommunicationsNamedPipes::closeNamedPipe(HANDLE namedPipe){
    CloseHandle(namedPipe);
}

HANDLE MegaCmdShellCommunicationsNamedPipes::doOpenPipe(wstring nameOfPipe)
{
    wchar_t username[UNLEN+1];
    DWORD username_len = UNLEN+1;
    GetUserNameW(username, &username_len);
    wstring serverPipeName(L"\\\\.\\pipe\\megacmdpipe");
    serverPipeName += L"_";
    serverPipeName += username;

    if (nameOfPipe != serverPipeName)
    {
        if (!WaitNamedPipeW(nameOfPipe.c_str(),8000)) //TODO: use a real time and report timeout error.
        {
            OUTSTREAM << "ERROR WaitNamedPipe: " << nameOfPipe << " . errno: " << ERRNO << endl; //TODO: delete?
        }
    }

    HANDLE theNamedPipe = CreateFileW(
                nameOfPipe.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,/*FILE_FLAG_WRITE_THROUGH,*/
                NULL
                );

    return theNamedPipe;
}

HANDLE MegaCmdShellCommunicationsNamedPipes::createNamedPipe(int number)
{
    wstring nameOfPipe;

    wchar_t username[UNLEN+1];
    DWORD username_len = UNLEN+1;
    GetUserNameW(username, &username_len);

    nameOfPipe += L"\\\\.\\pipe\\megacmdpipe";
    nameOfPipe += L"_";
    nameOfPipe += username;

    if (number)
    {
        nameOfPipe += std::to_wstring(number);
    }

    // Open the named pipe
    HANDLE theNamedPipe = doOpenPipe(nameOfPipe);

    if (!namedPipeValid(theNamedPipe))
    {
        if (!number)
        {
            //launch server
            OUTSTREAM << "Server not running. Initiating in the background." << endl;
            STARTUPINFO si;
            PROCESS_INFORMATION pi;
            ZeroMemory( &si, sizeof(si) );
            ZeroMemory( &pi, sizeof(pi) );

            //TODO: This created the file but no log was flushed
            //                string pathtolog = createAndRetrieveConfigFolder()+"/megacmdserver.log";
            //                OUTSTREAM << " The output will logged to " << pathtolog << endl;
            //                //TODO: use pathtolog
            //                HANDLE h = CreateFile(TEXT("megacmdserver.log"), GENERIC_READ| GENERIC_WRITE,FILE_SHARE_READ,
            //                                      NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

            //                if(h != INVALID_HANDLE_VALUE)
            //                {
            //                    SetFilePointer (h, 0L, NULL, FILE_END); //TODO: review this
            //                    si.dwFlags |= STARTF_USESTDHANDLES;
            //                    si.hStdOutput = h;
            //                    si.hStdError = h;
            //                }
            //                else
            //                {
            //                    cerr << " Could not create log file: " << endl;
            //                }


#ifndef NDEBUG //TODO: check in release version (all windows???)
            //LPCWSTR t = TEXT("C:\\Users\\MEGA\\AppData\\Local\\MEGAcmd\\MEGAcmd.exe");//TODO: get appData/Local folder programatically
            LPCWSTR t = TEXT("..\\MEGAcmdServer\\debug\\MEGAcmd.exe");
#else
            LPCWSTR t = TEXT("..\\MEGAcmdServer\\release\\MEGAcmd.exe");
#endif

            LPWSTR t2 = (LPWSTR) t;
            si.cb = sizeof(si);
            if (!CreateProcess( t,t2,NULL,NULL,TRUE,
                                CREATE_NEW_CONSOLE,
                                NULL,NULL,
                                &si,&pi) )
            {
                COUT << "Unable to execute: " << t; //TODO: improve error printing //ERRNO=2 (not found) might happen
            }

            Sleep(2000); // Give it a initial while to start.

            //try again:
            int attempts = 10;
            int waitimet = 1500;
            theNamedPipe = INVALID_HANDLE_VALUE;
            while ( attempts && !namedPipeValid(theNamedPipe))
            {
                Sleep(waitimet/1000);
                waitimet=waitimet*2;
                attempts--;
                theNamedPipe = doOpenPipe(nameOfPipe);
            }
            if (attempts < 0) //TODO: check this whenever attempts is > 0
            {
                cerr << "Unable to connect to " << (number?("response namedPipe N "+number):"server") << ": error=" << ERRNO << endl;

                cerr << "Please ensure MegaCMD is running" << endl;
                return INVALID_HANDLE_VALUE;
            }
            else
            {
                serverinitiatedfromshell = true;
                registerAgainRequired = true;
            }
        }
        else
        {
            OUTSTREAM << "ERROR opening namedPipe: " << nameOfPipe << ": " << ERRNO << endl;
        }
        return theNamedPipe;
    }
    return theNamedPipe;
}

MegaCmdShellCommunicationsNamedPipes::MegaCmdShellCommunicationsNamedPipes()
{
#ifdef _WIN32
    setlocale(LC_ALL, ""); // en_US.utf8 could do?
#endif

    serverinitiatedfromshell = false;
    registerAgainRequired = false;

    stopListener = false;
    listenerThread = NULL;
}

int MegaCmdShellCommunicationsNamedPipes::executeCommandW(wstring wcommand, bool (*readconfirmationloop)(const char *), OUTSTREAMTYPE &output, bool interactiveshell)
{
    return executeCommand("", readconfirmationloop, output, interactiveshell, wcommand);
}

int MegaCmdShellCommunicationsNamedPipes::executeCommand(string command, bool (*readconfirmationloop)(const char *), OUTSTREAMTYPE &output, bool interactiveshell, wstring wcommand)
{
    HANDLE theNamedPipe = createNamedPipe();
    if (!namedPipeValid(theNamedPipe))
    {
        return -1;
    }

    if (interactiveshell)
    {
        command="X"+command;
    }

//    //unescape \uXXXX sequences
//    command=unescapeutf16escapedseqs(command.c_str());

    //get local wide chars string (utf8 -> utf16)
    if (!wcommand.size())
    {
        stringtolocalw(command.c_str(),&wcommand);
    }
    else if (interactiveshell)
    {
        wcommand=L"X"+wcommand;
    }

    DWORD n;
    if (!WriteFile(theNamedPipe,(char *)wcommand.data(),wcslen(wcommand.c_str())*sizeof(wchar_t), &n, NULL))
    {
        cerr << "ERROR writing command to namedPipe: " << ERRNO << endl;
        return -1;
    }

    int receiveNamedPipeNum = -1 ;
    if (!ReadFile(theNamedPipe, (char *)&receiveNamedPipeNum, sizeof(receiveNamedPipeNum), &n, NULL) )
    {
        cerr << "ERROR reading output namedPipe" << endl;
        return -1;
    }

    HANDLE newNamedPipe = createNamedPipe(receiveNamedPipeNum);
    if (!namedPipeValid(newNamedPipe))
    {
        return -1;
    }

    int outcode = -1;

    if (!ReadFile(newNamedPipe, (char *)&outcode, sizeof(outcode),&n, NULL))
    {
        cerr << "ERROR reading output code: " << ERRNO << endl;
        return -1;
    }

    while (outcode == MCMD_REQCONFIRM)
    {
        int BUFFERSIZE = 1024;
        char confirmQuestion[1025];
        memset(confirmQuestion,'\0',1025);
        bool readok;
        do{
            readok = ReadFile(newNamedPipe, confirmQuestion, BUFFERSIZE, &n, NULL);
        } while(n == BUFFERSIZE && readok);

        if (!readok)
        {
            cerr << "ERROR reading confirm question: " << ERRNO << endl;
        }

        bool response = false;

        if (readconfirmationloop != NULL)
        {
            response = readconfirmationloop(confirmQuestion);
        }

        if (!WriteFile(newNamedPipe, (const char *) &response, sizeof(response), &n, NULL))
        {
            cerr << "ERROR writing confirm response to namedPipe: " << ERRNO << endl;
            return -1;
        }

        if (!ReadFile(newNamedPipe, (char *)&outcode, sizeof(outcode),&n, NULL))
        {
            cerr << "ERROR reading output code: " << ERRNO << endl;
            return -1;
        }
    }

    int BUFFERSIZE = 1024;
    char buffer[1025];
    BOOL readok;
    do{
        readok = ReadFile(newNamedPipe, buffer, BUFFERSIZE,&n,NULL);
        if (readok)
        {
            buffer[n]='\0';
            wstring wbuffer;
            stringtolocalw((const char*)&buffer,&wbuffer);
            int oldmode = _setmode(fileno(stdout), _O_U16TEXT);
            output << wbuffer;
            _setmode(fileno(stdout), oldmode);
        }
    } while(n == BUFFERSIZE && readok);

    if (!readok)
    {
        cerr << "ERROR reading output: " << ERRNO << endl;
        return -1;
    }

    closeNamedPipe(newNamedPipe);
    closeNamedPipe(theNamedPipe);
    return outcode;
}

int MegaCmdShellCommunicationsNamedPipes::listenToStateChanges(int receiveNamedPipeNum, void (*statechangehandle)(string))
{
    HANDLE newNamedPipe = createNamedPipe(receiveNamedPipeNum);
    if (!namedPipeValid(newNamedPipe))
    {
        return -1;
    }

    int timeout_notified_server_might_be_down = 0;
    while (!stopListener)
    {
        if (!namedPipeValid(newNamedPipe))
        {
            return -1;
        }

        string newstate;

        int BUFFERSIZE = 1024;
        char buffer[1025];
        DWORD n;
        bool readok;
        do{
            readok = ReadFile(newNamedPipe, buffer, BUFFERSIZE, &n, NULL);
            if (readok)
            {
                buffer[n]='\0';
                newstate += buffer;
            }
        } while(n == BUFFERSIZE && readok);

        if (!readok)
        {
            cerr << "ERROR reading output (state change): " << ERRNO << endl;
            closeNamedPipe(newNamedPipe);
            return -1;
        }

        if (!n)
        {
            if (!timeout_notified_server_might_be_down)
            {
                timeout_notified_server_might_be_down = 30;
                cerr << endl << "Server is probably down. Executing anything will try to respawn or reconnect to it";
            }
            timeout_notified_server_might_be_down--;
            if (!timeout_notified_server_might_be_down)
            {
                registerAgainRequired = true;
                closeNamedPipe(newNamedPipe);
                return -1;
            }
            Sleep(1000);
            continue;
        }

        if (statechangehandle != NULL)
        {
            statechangehandle(newstate);
        }
    }

    closeNamedPipe(newNamedPipe);
    return 0;
}

int MegaCmdShellCommunicationsNamedPipes::registerForStateChanges(void (*statechangehandle)(string))
{
    if (statechangehandle == NULL)
    {
        registerAgainRequired = false;
        return 0; //Do nth
    }
    HANDLE theNamedPipe = createNamedPipe();
    if (!namedPipeValid(theNamedPipe))
    {
        return -1;
    }

    wstring wcommand=L"registerstatelistener";

    DWORD n;
    if (!WriteFile(theNamedPipe,(char *)wcommand.data(),wcslen(wcommand.c_str())*sizeof(wchar_t), &n, NULL))
    {
        cerr << "ERROR writing command to namedPipe: " << ERRNO << endl;
        return -1;
    }

    int receiveNamedPipeNum = -1;

    if (!ReadFile(theNamedPipe, (char *)&receiveNamedPipeNum, sizeof(receiveNamedPipeNum), &n, NULL) )
    {
        cerr << "ERROR reading output namedPipe" << endl;
        return -1;
    }

    if (listenerThread != NULL)
    {
        stopListener = true;
        listenerThread->join();
    }

    stopListener = false;

    listenerThread = new std::thread(listenToStateChanges,receiveNamedPipeNum,statechangehandle);

    registerAgainRequired = false;

    closeNamedPipe(theNamedPipe);
    return 0;
}

void MegaCmdShellCommunicationsNamedPipes::setResponseConfirmation(bool confirmation)
{
    confirmResponse = confirmation;
}

MegaCmdShellCommunicationsNamedPipes::~MegaCmdShellCommunicationsNamedPipes()
{
    if (listenerThread != NULL) //TODO: use heritage for whatever we can
    {
        stopListener = true;
        listenerThread->join();
    }
}
#endif
