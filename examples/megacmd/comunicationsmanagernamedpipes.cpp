/**
 * @file examples/megacmd/comunicationsmanagernamedPipes.cpp
 * @brief MegaCMD: Communications manager using Network NamedPipes
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
#ifdef _WIN32

#include "comunicationsmanagernamedpipes.h"
#include "megacmdutils.h"


#include <windows.h>
#include <Lmcons.h> //getusername

#define ERRNO WSAGetLastError()


using namespace mega;


bool namedPipeValid(HANDLE namedPipe)
{
    return namedPipe != INVALID_HANDLE_VALUE;
}

bool ComunicationsManagerNamedPipes::ended;

int ComunicationsManagerNamedPipes::get_next_comm_id()
{
    mtx->lock();
    ++count;
    mtx->unlock();
    return count;
}

HANDLE ComunicationsManagerNamedPipes::doCreatePipe(wstring nameOfPipe)
{
    return CreateNamedPipeW(
           nameOfPipe.c_str(), // name of the pipe
           /*PIPE_ACCESS_DUPLEX | FILE_FLAG_WRITE_THROUGH, // 2-way pipe, synchronous //TODO: use the same for the client: GENERIC_READ | GENERIC_WRITE , FILE_FLAG_WRITE_THROUGH
           PIPE_TYPE_BYTE, // send data as a byte stream
            */
                PIPE_ACCESS_DUPLEX,
                PIPE_TYPE_MESSAGE, // this treats the pipe as if FILE_FLAG_WRITE_THROUGH anyhow
           1, // only allow 1 instance of this pipe
           0, // no outbound buffer
           0, // no inbound buffer
           0, // use default wait time
           NULL // use default security attributes
       );
}

HANDLE ComunicationsManagerNamedPipes::create_new_namedPipe(int *pipeId)
{

    *pipeId = get_next_comm_id();

    HANDLE thepipe;
    wstring nameOfPipe;
    int attempts = 10;
    bool namedPipesucceded = false;
    while (--attempts && !namedPipesucceded)
    {
        wchar_t username[UNLEN+1];
        DWORD username_len = UNLEN+1;
        GetUserNameW(username, &username_len);

        //TODO: review this
        nameOfPipe += L"\\\\.\\pipe\\megacmdpipe";
        nameOfPipe += nameOfPipe;
        if (*pipeId)
        {
            nameOfPipe += std::to_wstring(*pipeId);
        }
        wcerr << " creating pipe named: " << nameOfPipe << endl; //TODO: delete

        // Create a pipe to send data
        thepipe = doCreatePipe(nameOfPipe);

        if (!namedPipeValid(thepipe))
        {
            if (errno == EMFILE) //TODO: review possible out
            {
                LOG_verbose << " Trying to reduce number of used files by sending ACK to listeners to discard disconnected ones.";
                string sack="ack";
                informStateListeners(sack);
            }
            if (attempts !=10)
            {
                LOG_fatal << "ERROR opening namedPipe ID=" << pipeId << " errno: " << errno << ". Attempts: " << attempts;
            }
            sleepMicroSeconds(500);
        }
        else
        {
            namedPipesucceded = true;
        }
    }
    if (!namedPipeValid(thepipe))
    {
        return INVALID_HANDLE_VALUE;
    }

    return thepipe;
}


ComunicationsManagerNamedPipes::ComunicationsManagerNamedPipes()
{
    count = 0;
    mtx = new MegaMutex();
    initialize();
}

int ComunicationsManagerNamedPipes::initialize()
{
    mtx->init(false);

    petitionready = false;

    wstring nameOfPipe (L"\\\\.\\pipe\\megacmdpipe");
    nameOfPipe += nameOfPipe;
    wcerr << " creating pipe named: " << nameOfPipe << endl; //TODO: delete

    pipeGeneral = doCreatePipe(nameOfPipe);

    if (!namedPipeValid(pipeGeneral))
    {
        LOG_fatal << "ERROR opening namedPipe";
        return -1;
    }

    return 0;
}

bool ComunicationsManagerNamedPipes::receivedPetition()
{
    return petitionready;
}

int ComunicationsManagerNamedPipes::waitForPetition()
{
    petitionready = false;
    if (!ConnectNamedPipe(pipeGeneral, NULL))
    {
        if (errno == EADDRINUSE) //TODO: review errors
        {
            LOG_fatal << "ERROR on connecting to namedPipe: Already in use";
        }
        else
        {
            LOG_fatal << "ERROR on connecting to namedPipe. errno: " << ERRNO;
        }
        pipeGeneral = INVALID_HANDLE_VALUE;
        return false;
    }
    petitionready = true;
    return true;
}

void ComunicationsManagerNamedPipes::stopWaiting()
{
    CloseHandle(pipeGeneral);
}

void ComunicationsManagerNamedPipes::registerStateListener(CmdPetition *inf)
{
    LOG_debug << "Registering state listener petition with namedPipe: " << ((CmdPetitionNamedPipes *) inf)->outNamedPipe;
    ComunicationsManager::registerStateListener(inf);
}

//TODO: implement unregisterStateListener, not 100% necesary, since when a state listener is not accessible it is unregistered (to deal with sudden deaths).
// also, unregistering might not be straight forward since we need to correlate the thread doing the unregistration with the one who registered.


/**
 * @brief returnAndClosePetition
 * I will clean struct and close the namedPipe within
 */
void ComunicationsManagerNamedPipes::returnAndClosePetition(CmdPetition *inf, OUTSTRINGSTREAM *s, int outCode)
{
    HANDLE outNamedPipe = ((CmdPetitionNamedPipes *)inf)->outNamedPipe;

    LOG_verbose << "Output to write in namedPipe " << outNamedPipe << ": <<" << s->str() << ">>";

    bool connectsucceeded = false;
    int attempts = 10;
    while (--attempts && !connectsucceeded)
    {
        if (!ConnectNamedPipe(outNamedPipe, NULL)) //TODO: read thepipe from inf
        {
            if (errno == EADDRINUSE)
            {
                LOG_warn << "ERROR on connecting to namedPipe " << outNamedPipe << ": Already in use. Attempts: " << attempts;
            }
            else
            {
                LOG_fatal << "ERROR on connecting to namedPipe " << outNamedPipe << ". errno: " << ERRNO << ". Attempts: " << attempts;
            }
            sleepMicroSeconds(500);
        }
        else
        {
            connectsucceeded = true;
        }
    }

    if (!connectsucceeded)
    {
        LOG_fatal << "Return and close: Unable to connect on outnamedPipe " << outNamedPipe << " error: " << ERRNO;
        delete inf;
        return;
    }

    OUTSTRING sout = s->str();

    DWORD n;
    if (!WriteFile(outNamedPipe,(const char*)&outCode, sizeof(outCode), &n, NULL))
    {
        LOG_err << "ERROR writing output Code to namedPipe: " << ERRNO;
    }

    string sutf8;
    localwtostring(&sout,&sutf8);
    if (!WriteFile(outNamedPipe,sutf8.data(), sutf8.size(), &n, NULL))
    {
        LOG_err << "ERROR writing to namedPipe: " << ERRNO;
    }
    DisconnectNamedPipe(outNamedPipe);
    CloseHandle(outNamedPipe);
    delete inf;
}

int ComunicationsManagerNamedPipes::informStateListener(CmdPetition *inf, string &s)
{
    return 1; //TODO: implement this
//    LOG_verbose << "Inform State Listener: Output to write in namedPipe " << ((CmdPetitionNamedPipes *)inf)->outNamedPipe << ": <<" << s << ">>";

//    sockaddr_in cliAddr;
//    socklen_t cliLength = sizeof( cliAddr );

//    static map<int,int> connectednamedPipes;

//    int connectednamedPipe = -1;
//    if (connectednamedPipes.find(((CmdPetitionNamedPipes *)inf)->outNamedPipe) == connectednamedPipes.end())
//    {
//        //select with timeout and accept non-blocking, so that things don't get stuck
//        fd_set set;
//        FD_ZERO(&set);
//        FD_SET(((CmdPetitionNamedPipes *)inf)->outNamedPipe, &set);

//        struct timeval timeout;
//        timeout.tv_sec = 0;
//        timeout.tv_usec = 4000000;
//        int rv = select(((CmdPetitionNamedPipes *)inf)->outNamedPipe+1, &set, NULL, NULL, &timeout);
//        if(rv == -1)
//        {
//            LOG_err << "Informing state listener: Unable to select on outnamedPipe " << ((CmdPetitionNamedPipes *)inf)->outNamedPipe << " error: " << errno;
//            return -1;
//        }
//        else if(rv == 0)
//        {
//            LOG_warn << "Informing state listener: timeout in select on outnamedPipe " << ((CmdPetitionNamedPipes *)inf)->outNamedPipe;
//        }
//        else
//        {
//#ifndef _WIN32
//            int oldfl = fcntl(sockfd, F_GETFL);
//            fcntl(((CmdPetitionNamedPipes *)inf)->outNamedPipe, F_SETFL, oldfl | O_NONBLOCK);
//#endif
//            connectednamedPipe = accept(((CmdPetitionNamedPipes *)inf)->outNamedPipe, (struct sockaddr*)&cliAddr, &cliLength);
//#ifndef _WIN32
//            fcntl(((CmdPetitionNamedPipes *)inf)->outNamedPipe, F_SETFL, oldfl);
//#endif
//        }
//        connectednamedPipes[((CmdPetitionNamedPipes *)inf)->outNamedPipe] = connectednamedPipe;
//    }
//    else
//    {
//        connectednamedPipe = connectednamedPipes[((CmdPetitionNamedPipes *)inf)->outNamedPipe];
//    }

//    if (connectednamedPipe == -1)
//    {
//        if (errno == 32) //namedPipe closed
//        {
//            LOG_debug << "Unregistering no longer listening client. Original petition: " << *inf;
//            closeNamedPipe(connectednamedPipe);
//            connectednamedPipes.erase(((CmdPetitionNamedPipes *)inf)->outNamedPipe);
//            return -1;
//        }
//        else
//        {
//            LOG_err << "Informing state listener: Unable to accept on outnamedPipe " << ((CmdPetitionNamedPipes *)inf)->outNamedPipe << " error: " << errno;
//        }
//        return 0;
//    }

//#ifdef __MACH__
//#define MSG_NOSIGNAL 0
//#endif

//    int n = send(connectednamedPipe, s.data(), s.size(), MSG_NOSIGNAL);
//    if (n < 0)
//    {
//        if (errno == 32) //namedPipe closed
//        {
//            LOG_debug << "Unregistering no longer listening client. Original petition: " << *inf;
//            connectednamedPipes.erase(((CmdPetitionNamedPipes *)inf)->outNamedPipe);
//            return -1;
//        }
//        else
//        {
//            LOG_err << "ERROR writing to namedPipe: " << errno;
//        }
//    }

//    return 0;
}

/**
 * @brief getPetition
 * @return pointer to new CmdPetitionPosix. Petition returned must be properly deleted (this can be calling returnAndClosePetition)
 */
CmdPetition * ComunicationsManagerNamedPipes::getPetition()
{
    CmdPetitionNamedPipes *inf = new CmdPetitionNamedPipes();

    wchar_t wbuffer[1024]= {};

    DWORD n;
    if (!ReadFile(pipeGeneral, wbuffer, 1023 * sizeof(wchar_t), &n, NULL ) )  //TODO: msjs > 1023? review file & port sockets
    {
        LOG_err << "Failed to read petition from named pipe. errno: L" << ERRNO;
        inf->line = strdup("ERROR");
        return inf;
    }

    string receivedutf8;
    if (n != SOCKET_ERROR)
    {
        wbuffer[n]='\0';
        localwtostring(&wstring(wbuffer),&receivedutf8);
    }
    else
    {
        LOG_warn << "Received empty command from client at getPetition: ";
    }

    int namedPipe_id = 0; // this value shouldn't matter
    inf->outNamedPipe = create_new_namedPipe(&namedPipe_id);
    if (!namedPipeValid(inf->outNamedPipe) || !namedPipe_id)
    {
        LOG_fatal << "ERROR creating output namedPipe at getPetition";
        inf->line = strdup("ERROR");
        return inf;
    }

    if(!WriteFile(pipeGeneral,(const char*)&namedPipe_id, sizeof( namedPipe_id ), &n, NULL))
    {
        LOG_fatal << "ERROR writing to namedPipe at getPetition: ERRNO = " << ERRNO;
        inf->line = strdup("ERROR");
        return inf;
    }

    if (!DisconnectNamedPipe(pipeGeneral) )
    {
        LOG_fatal << " Error disconnecting from general pip. errno: " << ERRNO;
    }

    inf->line = strdup(receivedutf8.c_str());

    return inf;
}

bool ComunicationsManagerNamedPipes::getConfirmation(CmdPetition *inf, string message)
{
    return false;
    //TODO: implement this
//    sockaddr_in cliAddr;
//    socklen_t cliLength = sizeof( cliAddr );
//    int connectednamedPipe = ((CmdPetitionNamedPipes *)inf)->acceptedOutNamedPipe;
//    if (connectednamedPipe == -1)
//        connectednamedPipe = accept(((CmdPetitionNamedPipes *)inf)->outNamedPipe, (struct sockaddr*)&cliAddr, &cliLength);
//     ((CmdPetitionNamedPipes *)inf)->acceptedOutNamedPipe = connectednamedPipe;
//    if (connectednamedPipe == -1)
//    {
//        LOG_fatal << "Getting Confirmation: Unable to accept on outnamedPipe " << ((CmdPetitionNamedPipes *)inf)->outNamedPipe << " error: " << errno;
//        delete inf;
//        return false;
//    }

//    int outCode = MCMD_REQCONFIRM;
//    int n = send(connectednamedPipe, (const char *)&outCode, sizeof( outCode ), MSG_NOSIGNAL);
//    if (n < 0)
//    {
//        LOG_err << "ERROR writing output Code to namedPipe: " << errno;
//    }
//    n = send(connectednamedPipe, message.data(), max(1,(int)message.size()), MSG_NOSIGNAL); // for some reason without the max recv never quits in the client for empty responses
//    if (n < 0)
//    {
//        LOG_err << "ERROR writing to namedPipe: " << errno;
//    }

//    bool response;
//    n = recv(connectednamedPipe,(char *)&response, sizeof(response), MSG_NOSIGNAL);
//    return response;
}

string ComunicationsManagerNamedPipes::get_petition_details(CmdPetition *inf)
{
    ostringstream os;
    os << "namedPipe output: " << ((CmdPetitionNamedPipes *)inf)->outNamedPipe;
    return os.str();
}


ComunicationsManagerNamedPipes::~ComunicationsManagerNamedPipes()
{
    delete mtx;
}
#endif
