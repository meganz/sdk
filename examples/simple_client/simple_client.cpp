/**
 * @file examples/simple_client/simple_client.cpp
 * @brief Example app
 *
 * (c) 2013-2025 by Mega Limited, Auckland, New Zealand
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
#include <time.h>

//ENTER YOUR CREDENTIALS HERE
#define MEGA_EMAIL "EMAIL"
#define MEGA_PASSWORD "PASSWORD"

//Get yours for free at https://mega.co.nz/#sdk
#define APP_KEY "9gETCbhB"
#define USER_AGENT "Example Win32 App"

using namespace mega;

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
				std::cout << "***** Showing files/folders in the root folder:" << std::endl;
				MegaNode *root = api->getRootNode();
				MegaNodeList *list = api->getChildren(root);

				for(int i=0; i < list->size(); i++)
				{
					MegaNode *node = list->get(i);
					if(node->isFile())
						std::cout << "*****   File:   ";
					else
						std::cout << "*****   Folder: ";

					std::cout << node->getName() << std::endl;
				}
				std::cout << "***** Done" << std::endl;

				delete list;

				std::cout << "***** Uploading the image MEGA.png" << std::endl;

				api->startUpload("MEGA.png"
				, root		/*parent*/
				, nullptr	/*filename*/
				, 0     	/*mtime*/
				, nullptr	/*appData*/
				, false		/*isSourceTemporary*/
				, false		/*startFirst*/
				, nullptr);	/*cancelToken*/

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
		std::cout << "***** Loading filesystem " <<  request->getTransferredBytes() << " / " << request->getTotalBytes() << std::endl;
	}

	virtual void onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError* error)
	{
		std::cout << "***** Temporary error in request: " << error->getErrorString() << std::endl;
	}

	virtual void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* error)
	{
		if (error->getErrorCode())
		{
			std::cout << "***** Transfer finished with error: " << error->getErrorString() << std::endl;
		}
		else
		{
			std::cout << "***** Transfer finished OK" << std::endl;
		}

		finished = true;
	}

	virtual void onTransferUpdate(MegaApi *api, MegaTransfer *transfer)
	{
		std::cout << "***** Transfer progress: " << transfer->getTransferredBytes() << "/" << transfer->getTotalBytes() << std::endl;
	}

	virtual void onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError* error)
	{
		std::cout << "***** Temporary error in transfer: " << error->getErrorString() << std::endl;
	}

	virtual void onUsersUpdate(MegaApi* api, MegaUserList *users)
	{
		if (users == NULL)
		{
			//Full account reload
			return;
		}
		std::cout << "***** There are " << users->size() << " new or updated users in your account" << std::endl;
	}

	virtual void onNodesUpdate(MegaApi* api, MegaNodeList *nodes)
	{
		if(nodes == NULL)
		{
			//Full account reload
			return;
		}

		std::cout << "***** There are " << nodes->size() << " new or updated node/s in your account" << std::endl;
	}

	virtual void onSetsUpdate(MegaApi* api, MegaSetList *sets)
	{
		if (sets)
		{
			std::cout << "***** There are " << sets->size() << " new or updated Set/s in your account" << std::endl;
		}
	}

	virtual void onSetElementsUpdate(MegaApi* api, MegaSetElementList *elements)
	{
		if (elements)
		{
			std::cout << "***** There are " << elements->size() << " new or updated Set-Element/s in your account" << std::endl;
		}
	}
};

std::string displayTime(time_t t)
{
    char timebuf[32];
    strftime(timebuf, sizeof timebuf, "%c", localtime(&t));
    return timebuf;
}


int main()
{
	//Check the documentation of MegaApi to know how to enable local caching
	MegaApi *megaApi = new MegaApi(APP_KEY, (const char *)NULL, USER_AGENT);

	//By default, logs are sent to stdout
	//You can use MegaApi::setLoggerObject to receive SDK logs in your app
	megaApi->setLogLevel(MegaApi::LOG_LEVEL_INFO);

	MyListener listener;

	//Listener to receive information about all request and transfers
	//It is also possible to register a different listener per request/transfer
	megaApi->addListener(&listener);

	if(!strcmp(MEGA_EMAIL, "EMAIL"))
	{
		std::cout << "Please enter your email/password at the top of main.cpp" << std::endl;
		std::cout << "Press any key to exit the app..." << std::endl;
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

    // Add code here to exercise MegaApi



#ifdef HAVE_LIBUV
	std::cout << "Do you want to enable the local HTTP server (y/n)?" << std::endl;
	int c = getchar();
	if (c == 'y' || c == 'Y')
	{
		megaApi->httpServerStart();
		megaApi->httpServerSetRestrictedMode(MegaApi::HTTP_SERVER_ALLOW_ALL);
		megaApi->httpServerEnableFileServer(true);
		megaApi->httpServerEnableFolderServer(true);
		std::cout << "You can browse your account now! http://127.0.0.1:4443/" << std::endl;
	}
#endif

	std::cout << "Press any key to exit the app..." << std::endl;
	getchar();
	getchar();
	return 0;
}
