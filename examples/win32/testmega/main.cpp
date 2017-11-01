/**
 * @file examples/win32/testmega/main.cpp
 * @brief Example app for Windows
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
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

#include <megaapi.h>
#include <Windows.h>
#include <iostream>
#include <memory>
#include <filesystem>
#include <mega/logging.h>

//ENTER YOUR CREDENTIALS HERE
#define MEGA_EMAIL "EMAIL"
#define MEGA_PASSWORD "PASSWORD"

//Get yours for free at https://mega.co.nz/#sdk
#define APP_KEY "9gETCbhB"
#define USER_AGENT "Example Win32 App"

using namespace mega;
using namespace std;

std::unique_ptr<MegaNode> bigFileNode = nullptr;

class MyMegaRequestListener : public MegaRequestListener
{
public:
    virtual void onRequestStart(MegaApi* api, MegaRequest *request)
    {
        cout << "MyMegaRequestListener::onRequestStart" << endl;
    }
    virtual void onRequestFinish(MegaApi* api, MegaRequest *request, MegaError* e)
    {
        cout << "MyMegaRequestListener::onRequestFinish" << endl;
    }
    virtual void onRequestUpdate(MegaApi*api, MegaRequest *request)
    {
        cout << "MyMegaRequestListener::onRequestUpdate" << endl;
    }
    virtual void onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError* error)
    {
        cout << "MyMegaRequestListener::onRequestTemporaryError" << endl;
    }
};
MyMegaRequestListener myMyMegaRequestListener;

bool paused = false;
time_t pauseTime = 0;
size_t pauseByteCount = 10000000;

class MyListener: public MegaListener
{
public:
	bool finished;

	MyListener()
	{
		finished = false;
	}

	virtual void onRequestFinish(MegaApi* api, MegaRequest *request, MegaError* e)
	{
		if(e->getErrorCode() != MegaError::API_OK)
		{
			finished = true;
			return;
		}

		switch(request->getType())
		{
			case MegaRequest::TYPE_LOGIN:
			{
				api->fetchNodes();
				break;
			}
			case MegaRequest::TYPE_FETCH_NODES:
			{
				cout << "***** Showing files/folders in the root folder:" << endl;
				MegaNode *root = api->getRootNode();
				MegaNodeList *list = api->getChildren(root);
			
				for(int i=0; i < list->size(); i++)
				{
					MegaNode *node = list->get(i);
					if(node->isFile())
						cout << "*****   File:   ";
					else
						cout << "*****   Folder: ";
				
					cout << node->getName() << endl;

#if 0
                    if (std::string(node->getName()) == "kimorg-20110421-021857+0600.tif")  //"nonRaidVersion.tst") //"out.dat.original")
                        bigFileNode.reset(node->copy());
#else
                    if (std::string(node->getName()) == "nonRaidVersion.tst") //"out.dat.original")
                        bigFileNode.reset(node->copy());
#endif

				}
				cout << "***** Done" << endl;

				delete list;

				cout << "***** Uploading the image MEGA.png" << endl;
				api->startUpload("MEGA.png", root);
				delete root;

				break;
			}
			default:
				break;
		}
	}

	//Currently, this callback is only valid for the request fetchNodes()
	virtual void onRequestUpdate(MegaApi*api, MegaRequest *request)
	{
		cout << "***** Loading filesystem " <<  request->getTransferredBytes() << " / " << request->getTotalBytes() << endl;
	}

	virtual void onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError* error)
	{
		cout << "***** Temporary error in request: " << error->getErrorString() << endl;
	}

	virtual void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* error)
	{
		if (error->getErrorCode())
		{
			cout << "***** Transfer finished with error: " << error->getErrorString() << endl;
		}
		else
		{
			cout << "***** Transfer finished OK" << endl;
		}

		finished = true;
	}
	
	virtual void onTransferUpdate(MegaApi *api, MegaTransfer *transfer)
	{
		cout << "***** Transfer progress: " << transfer->getTransferredBytes() << "/" << transfer->getTotalBytes() << endl; 


        if (transfer->getTransferredBytes() > pauseByteCount)
        {
            cout << "*** requesting pause! *****" << endl;

            api->pauseTransfers(true, &myMyMegaRequestListener);
            paused = true;
            pauseTime = time(NULL);
        }

        

	}

	virtual void onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError* error)
	{
		cout << "***** Temporary error in transfer: " << error->getErrorString() << endl;
	}

	virtual void onUsersUpdate(MegaApi* api, MegaUserList *users)
	{
		if (users == NULL)
		{
			//Full account reload
			return;
		}
		cout << "***** There are " << users->size() << " new or updated users in your account" << endl;
	}

	virtual void onNodesUpdate(MegaApi* api, MegaNodeList *nodes)
	{
		if(nodes == NULL)
		{
			//Full account reload
			return;
		}

		cout << "***** There are " << nodes->size() << " new or updated node/s in your account" << endl;
	}
};



class MyMegaTransferListener : public MegaTransferListener
{
    void onTransferStart(MegaApi *api, MegaTransfer *transfer) override
    {
        cout << "onTransferStart" << endl;
    }
    void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* error) override
    {
        cout << "onTransferFinish" << endl;
    }
    void onTransferUpdate(MegaApi *api, MegaTransfer *transfer) override
    {
        cout << "onTransferUpdate" << endl;
    }
    void onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError* error) override
    {
        cout << "onTransferTemporaryError" << endl;
    }
    bool onTransferData(MegaApi *api, MegaTransfer *transfer, char *buffer, size_t size) override
    {
        cout << "onTransferData " << size << endl;
        return true;
    }
};

int main()
{
	//Check the documentation of MegaApi to know how to enable local caching
	MegaApi *megaApi = new MegaApi(APP_KEY, (const char *)NULL, USER_AGENT);

	//By default, logs are sent to stdout
	//You can use MegaApi::setLoggerObject to receive SDK logs in your app
	//megaApi->setLogLevel(MegaApi::LOG_LEVEL_INFO);
    megaApi->setLogLevel(MegaApi::LOG_LEVEL_DEBUG);
    SimpleLogger::setAllOutputs(&cout);

	MyListener listener;

	//Listener to receive information about all request and transfers
	//It is also possible to register a different listener per request/transfer
	megaApi->addListener(&listener);

	if(!strcmp(MEGA_EMAIL, "EMAIL"))
	{
		cout << "Please enter your email/password at the top of main.cpp" << endl;
		cout << "Press any key to exit the app..." << endl;
		getchar();
		exit(0);
	}

	//Login. You can get the result in the onRequestFinish callback of your listener
	megaApi->login(MEGA_EMAIL, MEGA_PASSWORD);
	
	//You can use the main thread to show a GUI or anything else. MegaApi runs in a background thread.
	while(!listener.finished)
	{
		Sleep(1000);
	}


    if (bigFileNode)
    {
        std::experimental::filesystem::remove("c:\\tmp\\test_mega_download");
        MyMegaTransferListener myTL;
        megaApi->startDownload(bigFileNode.get(), "c:\\tmp\\test_mega_download", &myTL);
    }


    while (1)
    {
        Sleep(1000);

        if (paused && (time(NULL) - pauseTime > 5))
        {
            cout << "*** requesting resume! *****" << endl;
            megaApi->pauseTransfers(false, &myMyMegaRequestListener);
            paused = false;
            pauseByteCount += 10000000;
        }

    }


#ifdef HAVE_LIBUV
	cout << "Do you want to enable the local HTTP server (y/n)?" << endl;
	char c = getchar();
	if (c == 'y' || c == 'Y')
	{
		megaApi->httpServerStart();
		megaApi->httpServerSetRestrictedMode(MegaApi::HTTP_SERVER_ALLOW_ALL);
		megaApi->httpServerEnableFileServer(true);
		megaApi->httpServerEnableFolderServer(true);
		cout << "You can browse your account now! http://127.0.0.1:4443/" << endl;
	}
#endif

	cout << "Press any key to exit the app..." << endl;
	getchar();
	getchar();
	return 0;
}
