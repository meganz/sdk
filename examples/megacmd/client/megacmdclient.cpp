/**
 * @file TODO
 * @brief MegaCMD: TODO
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


#include <sys/types.h>
#ifdef _WIN32
#include <WinSock2.h>
#include <Shlwapi.h> //PathAppend
#else
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
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

void closeSocket(int socket){
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

using namespace std;

string getAbsPath(string relativelocalPath)
{
    if (!relativelocalPath.size())
    {
        return relativelocalPath;
    }


#ifdef _WIN32
    string localpath = relativelocalPath;
    string absolutepath;
    localpath.append("", 1);

   if (!PathIsRelativeW((LPCWSTR)relativelocalPath.data()))
   {
       absolutepath = relativelocalPath;
       if (memcmp(absolutepath.data(), L"\\\\?\\", 8))
       {
           absolutepath.insert(0, (const char *)L"\\\\?\\", 8);
       }
       return absolutepath;
   }

   int len = GetFullPathNameW((LPCWSTR)relativelocalPath.data(), 0, NULL, NULL);
   if (len <= 0)
   {
      return relativelocalPath;
   }

   absolutepath.resize(len * sizeof(wchar_t));
   int newlen = GetFullPathNameW((LPCWSTR)relativelocalPath.data(), len, (LPWSTR)absolutepath.data(), NULL);
   if (newlen <= 0 || newlen >= len)
   {
       cerr << " failed to get CWD" << endl;
       return relativelocalPath;
   }

   if (memcmp(absolutepath.data(), L"\\\\?\\", 8))
   {
       absolutepath.insert(0, (const char *)L"\\\\?\\", 8);
   }
   return absolutepath;

#else
    if (relativelocalPath.size() && relativelocalPath.at(0) == '/')
    {
        return relativelocalPath;
    }
    else
    {
        char cCurrentPath[PATH_MAX];
        if (!getcwd(cCurrentPath, sizeof(cCurrentPath)))
        {
            cerr << " failed to get CWD" << endl;
            return relativelocalPath;
        }

        string absolutepath = cCurrentPath;
        absolutepath.append("/");
        absolutepath.append(relativelocalPath);
        return absolutepath;
    }

    return relativelocalPath;
#endif

}

string parseArgs(int argc, char* argv[])
{
    vector<string> absolutedargs;
    int itochange = -1;
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
                    itochange = i;
                }
            }

            for (int i = 2; i < argc; i++)
            {
                if (i==itochange && totalRealArgs>=2)
                {
                    absolutedargs.push_back(getAbsPath(argv[i]));
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
                if (strlen(argv[i]) && argv[i][0] !='-' )
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
    for (u_int i=0;i < absolutedargs.size();i++)
    {
        if (absolutedargs.at(i).find(" ") != string::npos || !absolutedargs.at(i).size())
        {
            toret+="\"";
        }
        toret+=absolutedargs.at(i);
        if (absolutedargs.at(i).find(" ") != string::npos || !absolutedargs.at(i).size())
        {
            toret+="\"";
        }

        if (i!=(absolutedargs.size()-1))
        {
            toret+=" ";
        }
    }

    return toret;
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        cerr << "Too few arguments" << endl;
        return -1;
    }
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

    int thesock = socket(AF_INET, SOCK_STREAM, 0);
    if (!socketValid(thesock))
    {
        cerr << "ERROR opening socket: " << ERRNO << endl;
        return -1;
    }

    int portno=MEGACMDINITIALPORTNUMBER;

    struct sockaddr_in addr;

    memset(&addr, 0, sizeof( addr ));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
    addr.sin_port = htons(portno);

    if (::connect(thesock, (struct sockaddr*)&addr, sizeof( addr )) == SOCKET_ERROR)
    {
        cerr << "ERROR connecting to initial socket: " << ERRNO << endl;
        cerr << "Unable to connect to service" << endl;
        cerr << "Please ensure MegaCMD is running" << endl;
        return -1;
    }

    string parsedArgs = parseArgs(argc,argv);

    cout << " executing: " << parsedArgs << endl;
    int n = send(thesock,parsedArgs.data(),parsedArgs.size(), MSG_NOSIGNAL);
    if (n == SOCKET_ERROR)
    {
        cerr << "ERROR writing output Code to socket: " << ERRNO << endl;
        return -1;;
    }

    int receiveSocket = SOCKET_ERROR ;

    n = recv(thesock, (char *)&receiveSocket, sizeof(receiveSocket), MSG_NOSIGNAL);
    if (n == SOCKET_ERROR)
    {
        cerr << "ERROR reading output socket" << endl;
        return -1;;
    }

    int newsockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (!socketValid(newsockfd))
    {
        cerr << "ERROR opening output socket: " << ERRNO << endl;
        return -1;;
    }

    memset(&addr, 0, sizeof( addr ));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
    addr.sin_port = htons(MEGACMDINITIALPORTNUMBER+receiveSocket);

    if (::connect(newsockfd, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        cerr << "ERROR connecting to output socket: " << ERRNO << endl;
        return -1;;
    }

    int outcode = -1;

    n = recv(newsockfd, (char *)&outcode, sizeof(outcode), MSG_NOSIGNAL);
    if (n == SOCKET_ERROR)
    {
        cerr << "ERROR reading output code: " << ERRNO << endl;
        return -1;;
    }

    int BUFFERSIZE = 1024;
    char buffer[1025];
    do{
        n = recv(newsockfd, buffer, BUFFERSIZE, MSG_NOSIGNAL);
        if (n)
        {
            buffer[n]='\0';
            cout << buffer;
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
