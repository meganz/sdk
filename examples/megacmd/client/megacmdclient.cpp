/**
 * @file examples/megacmd/client/megacmdclient.cpp
 * @brief MegaCMDClient: Client application of MEGAcmd
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

#include <stdio.h>
#include <iostream>
#include <errno.h>
#include <string>
#include <vector>
#include <memory.h>
#include <limits.h>

#ifdef _WIN32
#include <WinSock2.h>
#include <Shlwapi.h> //PathAppend

#include <fcntl.h>
#include <io.h>
#else
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/un.h>
#endif
#define MEGACMDINITIALPORTNUMBER 12300


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


bool socketValid(int socket)
{
#ifdef _WIN32
    return socket != INVALID_SOCKET;
#else
    return socket >= 0;
#endif
}

#ifndef INVALID_SOCKET
#define INVALID_SOCKET -1
#endif

void closeSocket(int socket){
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

using namespace std;

#ifdef _WIN32
// convert UTF-8 to Windows Unicode
void path2local(string* path, string* local)
{
    // make space for the worst case
    local->resize((path->size() + 1) * sizeof(wchar_t));

    int len = MultiByteToWideChar(CP_UTF8, 0,
                                  path->c_str(),
                                  -1,
                                  (wchar_t*)local->data(),
                                  local->size() / sizeof(wchar_t) + 1);
    if (len)
    {
        // resize to actual result
        local->resize(sizeof(wchar_t) * (len - 1));
    }
    else
    {
        local->clear();
    }
}

// convert to Windows Unicode Utf8
void local2path(string* local, string* path)
{
    path->resize((local->size() + 1) * 4 / sizeof(wchar_t));

    path->resize(WideCharToMultiByte(CP_UTF8, 0, (wchar_t*)local->data(),
                                     local->size() / sizeof(wchar_t),
                                     (char*)path->data(),
                                     path->size() + 1,
                                     NULL, NULL));
    //normalize(path);
}
//TODO: delete the 2 former??

// convert UTF-8 to Windows Unicode wstring
void stringtolocalw(const char* path, std::wstring* local)
{
    // make space for the worst case
    local->resize((strlen(path) + 1) * sizeof(wchar_t));

    int wchars_num = MultiByteToWideChar(CP_UTF8, 0, path,-1, NULL,0);
    local->resize(wchars_num);

    int len = MultiByteToWideChar(CP_UTF8, 0, path,-1, (wchar_t*)local->data(), wchars_num);

    if (len)
    {
        local->resize(len-1);
    }
    else
    {
        local->clear();
    }
}

//widechar to utf8 string
void localwtostring(const std::wstring* wide, std::string *multibyte)
{
    if( !wide->empty() )
    {
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wide->data(), (int)wide->size(), NULL, 0, NULL, NULL);
        multibyte->resize(size_needed);
        WideCharToMultiByte(CP_UTF8, 0, wide->data(), (int)wide->size(), (char*)multibyte->data(), size_needed, NULL, NULL);
    }
}


#endif
string getAbsPath(string relativePath)
{
    if (!relativePath.size())
    {
        return relativePath;
    }

#ifdef _WIN32
    string utf8absolutepath;
    string localpath;
    path2local(&relativePath, &localpath);

    string absolutelocalpath;
    localpath.append("", 1);

   if (!PathIsRelativeW((LPCWSTR)localpath.data()))
   {
       utf8absolutepath = relativePath;
       if (utf8absolutepath.find("\\\\?\\") != 0)
       {
           utf8absolutepath.insert(0, "\\\\?\\");
       }
       return utf8absolutepath;
   }

   int len = GetFullPathNameW((LPCWSTR)localpath.data(), 0, NULL, NULL);
   if (len <= 0)
   {
      return relativePath;
   }

   absolutelocalpath.resize(len * sizeof(wchar_t));
   int newlen = GetFullPathNameW((LPCWSTR)localpath.data(), len, (LPWSTR)absolutelocalpath.data(), NULL);
   if (newlen <= 0 || newlen >= len)
   {
       cerr << " failed to get CWD" << endl;
       return relativePath;
   }
   absolutelocalpath.resize(newlen* sizeof(wchar_t));

   local2path(&absolutelocalpath, &utf8absolutepath);

   if (utf8absolutepath.find("\\\\?\\") != 0)
   {
       utf8absolutepath.insert(0, "\\\\?\\");
   }

   return utf8absolutepath;

#else
    if (relativePath.size() && relativePath.at(0) == '/')
    {
        return relativePath;
    }
    else
    {
        char cCurrentPath[PATH_MAX];
        if (!getcwd(cCurrentPath, sizeof(cCurrentPath)))
        {
            cerr << " failed to get CWD" << endl;
            return relativePath;
        }

        string absolutepath = cCurrentPath;
        absolutepath.append("/");
        absolutepath.append(relativePath);
        return absolutepath;
    }

    return relativePath;
#endif

}

string parseArgs(int argc, char* argv[])
{
    vector<string> absolutedargs;
    int totalRealArgs = 0;
    if (argc>1)
    {
        absolutedargs.push_back(argv[1]);

        if (!strcmp(argv[1],"sync"))
        {
            for (int i = 2; i < argc; i++)
            {
                if (strlen(argv[i]) && argv[i][0] !='-' )
                {
                    totalRealArgs++;
                }
            }
            bool firstrealArg = true;
            for (int i = 2; i < argc; i++)
            {
                if (strlen(argv[i]) && argv[i][0] !='-' )
                {
                    if (totalRealArgs >=2 && firstrealArg)
                    {
                        absolutedargs.push_back(getAbsPath(argv[i]));
                        firstrealArg=false;
                    }
                    else
                    {
                        absolutedargs.push_back(argv[i]);
                    }
                }
                else
                {
                    absolutedargs.push_back(argv[i]);
                }
            }
        }
        else if (!strcmp(argv[1],"lcd")) //localpath args
        {
            for (int i = 2; i < argc; i++)
            {
                if (strlen(argv[i]) && argv[i][0] !='-' )
                {
                    absolutedargs.push_back(getAbsPath(argv[i]));
                }
                else
                {
                    absolutedargs.push_back(argv[i]);
                }
            }
        }
        else if (!strcmp(argv[1],"get") || !strcmp(argv[1],"preview") || !strcmp(argv[1],"thumbnail"))
        {
            for (int i = 2; i < argc; i++)
            {
                if (strlen(argv[i]) && argv[i][0] != '-' )
                {
                    totalRealArgs++;
                    if (totalRealArgs>1)
                    {
                        absolutedargs.push_back(getAbsPath(argv[i]));
                    }
                    else
                    {
                        absolutedargs.push_back(argv[i]);
                    }
                }
                else
                {
                    absolutedargs.push_back(argv[i]);
                }
            }
            if (totalRealArgs == 1)
            {
                absolutedargs.push_back(getAbsPath("."));

            }
        }
        else if (!strcmp(argv[1],"put"))
        {
            int lastRealArg = 0;
            for (int i = 2; i < argc; i++)
            {
                if (strlen(argv[i]) && argv[i][0] !='-' )
                {
                    lastRealArg = i;
                }
            }
            bool firstRealArg = true;
            for  (int i = 2; i < argc; i++)
            {
                if (strlen(argv[i]) && argv[i][0] !='-')
                {
                    if (firstRealArg || i <lastRealArg)
                    {
                        absolutedargs.push_back(getAbsPath(argv[i]));
                        firstRealArg = false;
                    }
                    else
                    {
                        absolutedargs.push_back(argv[i]);
                    }
                }
                else
                {
                    absolutedargs.push_back(argv[i]);
                }
            }
        }
        else
        {
            for (int i = 2; i < argc; i++)
            {
                absolutedargs.push_back(argv[i]);
            }
        }
    }

    string toret="";
    for (u_int i=0; i < absolutedargs.size(); i++)
    {
        if (absolutedargs.at(i).find(" ") != string::npos || !absolutedargs.at(i).size())
        {
            toret += "\"";
        }
        toret+=absolutedargs.at(i);
        if (absolutedargs.at(i).find(" ") != string::npos || !absolutedargs.at(i).size())
        {
            toret += "\"";
        }

        if (i != (absolutedargs.size()-1))
        {
            toret += " ";
        }
    }

    return toret;
}

#ifdef _WIN32
int createSocket(int number = 0, bool net = true)
#else
int createSocket(int number = 0, bool net = false)
#endif
{
    if (net)
    {
        int thesock = socket(AF_INET, SOCK_STREAM, 0);
        if (!socketValid(thesock))
        {
            cerr << "ERROR opening socket: " << ERRNO << endl;
            return INVALID_SOCKET;
        }
        int portno=MEGACMDINITIALPORTNUMBER+number;

        struct sockaddr_in addr;

        memset(&addr, 0, sizeof( addr ));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
        addr.sin_port = htons(portno);

        if (::connect(thesock, (struct sockaddr*)&addr, sizeof( addr )) == SOCKET_ERROR)
        {
            cerr << "Unable to connect to " << (number?("response socket N "+number):"service") << ": error=" << ERRNO << endl;
            if (!number)
            {
#ifdef __linux__
                cerr << "Please ensure mega-cmd is running" << endl;
#else
                cerr << "Please ensure MegaCMD is running" << endl;
#endif

            }
            return INVALID_SOCKET;
        }
        return thesock;
    }

#ifndef _WIN32
    else
    {
        int thesock = socket(AF_UNIX, SOCK_STREAM, 0);
        char socket_path[60];
        if (!socketValid(thesock))
        {
            cerr << "ERROR opening socket: " << ERRNO << endl;
            return INVALID_SOCKET;
        }

        bzero(socket_path, sizeof( socket_path ) * sizeof( *socket_path ));
        if (number)
        {
            sprintf(socket_path, "/tmp/megaCMD_%d/srv_%d", getuid(), number);
        }
        else
        {
            sprintf(socket_path, "/tmp/megaCMD_%d/srv", getuid() );
        }

        struct sockaddr_un addr;

        memset(&addr, 0, sizeof( addr ));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, socket_path, sizeof( addr.sun_path ) - 1);

        if (::connect(thesock, (struct sockaddr*)&addr, sizeof( addr )) == SOCKET_ERROR)
        {
            cerr << "Unable to connect to " << (number?("response socket N "+number):"service") << ": error=" << ERRNO << endl;
            if (!number)
            {
#ifdef __linux__
                cerr << "Please ensure mega-cmd is running" << endl;
#else
                cerr << "Please ensure MegaCMD is running" << endl;
#endif
            }
            return INVALID_SOCKET;
        }

        return thesock;
    }
    return INVALID_SOCKET;

#endif

    return INVALID_SOCKET;
}

int main(int argc, char* argv[])
{
#ifdef _WIN32
    setlocale(LC_ALL, ""); // en_US.utf8 could do?
#endif

    if (argc < 2)
    {
        cerr << "Too few arguments" << endl;
        return -1;
    }
    string parsedArgs = parseArgs(argc,argv);

#if _WIN32
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;

    /* Use the MAKEWORD(lowbyte, highbyte) macro declared in Windef.h */
    wVersionRequested = MAKEWORD(2, 2);

    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) {
        cerr << "ERROR initializing WSA" << endl;
    }
#endif

    int thesock = createSocket();
    if (thesock == INVALID_SOCKET)
    {
        return INVALID_SOCKET;
    }
#ifdef _WIN32

    // get local wide chars string (utf8 -> utf16)
    wstring wcommand;
    stringtolocalw(parsedArgs.c_str(),&wcommand);

    int n = send(thesock,(char *)wcommand.data(),wcslen(wcommand.c_str())*sizeof(wchar_t), MSG_NOSIGNAL);
#else
    int n = send(thesock,parsedArgs.data(),parsedArgs.size(), MSG_NOSIGNAL);
#endif
    if (n == SOCKET_ERROR)
    {
        cerr << "ERROR writing output Code to socket: " << ERRNO << endl;
        return -1;
    }

    int receiveSocket = SOCKET_ERROR ;

    n = recv(thesock, (char *)&receiveSocket, sizeof(receiveSocket), MSG_NOSIGNAL);
    if (n == SOCKET_ERROR)
    {
        cerr << "ERROR reading output socket" << endl;
        return -1;
    }

    int newsockfd = createSocket(receiveSocket);
    if (newsockfd == INVALID_SOCKET)
        return INVALID_SOCKET;

    int outcode = -1;

    n = recv(newsockfd, (char *)&outcode, sizeof(outcode), MSG_NOSIGNAL);
    if (n == SOCKET_ERROR)
    {
        cerr << "ERROR reading output code: " << ERRNO << endl;
        return -1;
    }

    int BUFFERSIZE = 1024;
    char buffer[1025];
    do{
        n = recv(newsockfd, buffer, BUFFERSIZE, MSG_NOSIGNAL);
        if (n)
        {
#ifdef _WIN32
            buffer[n]='\0';

            wstring wbuffer;
            stringtolocalw((const char*)&buffer,&wbuffer);
            int oldmode = _setmode(fileno(stdout), _O_U16TEXT);
            wcout << wbuffer;
            _setmode(fileno(stdout), oldmode);
#else
            buffer[n]='\0';
            cout << buffer;
#endif
        }
    } while(n == BUFFERSIZE && n !=SOCKET_ERROR);

    if (n == SOCKET_ERROR)
    {
        cerr << "ERROR reading output: " << ERRNO << endl;
        return -1;;
    }

    closeSocket(thesock);
    closeSocket(newsockfd);
#if _WIN32
    WSACleanup();
#endif
    return outcode;
}
