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
//#include <mega/raid.h>
#include <fstream>
#include "mega/base64.h"
//#include "mega/testhooks.h"


//ENTER YOUR CREDENTIALS HERE
#define MEGA_EMAIL "mattw@mega.co.nz" //"mattweir73+megatest1@gmail.com"//

//Get yours for free at https://mega.co.nz/#sdk
#define APP_KEY "9gETCbhB"
#define USER_AGENT "Example Win32 App"


using namespace mega;
using namespace std;

auto loglevel = MegaApi::LOG_LEVEL_WARNING;

std::unique_ptr<MegaNode> bigFileNode = NULL;

class MyMegaRequestListener : public MegaRequestListener
{
public:
    bool finished = false;
    virtual void onRequestStart(MegaApi* api, MegaRequest *request)
    {
        cout << "MyMegaRequestListener::onRequestStart" << endl;
    }
    virtual void onRequestFinish(MegaApi* api, MegaRequest *request, MegaError* e)
    {
        cout << "MyMegaRequestListener::onRequestFinish" << endl;
        finished = true;
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
MyMegaTransferListener myTL;

bool paused = false;
time_t pauseTime = 0;
size_t pauseByteCount = 1000000000;
bool onetime = false;

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
                api->enableTransferResumption();

				cout << "***** Showing files/folders in the root folder:" << endl;
				MegaNode *root = api->getRootNode();
				MegaNodeList *list = api->getChildren(root);
			
                bool megaPNGPresent = false;
				for(int i=0; i < list->size(); i++)
				{
					MegaNode *node = list->get(i);
					if(node->isFile())
						cout << "*****   File:   ";
					else
						cout << "*****   Folder: ";
				
                    std::string name(node->getName());
                    cout << name << endl;

                    if (name == "MEGA.png")
                        megaPNGPresent = true;

#if 0
                    if (name) == "kimorg-20110421-021857+0600.tif")  
                        bigFileNode.reset(node->copy());
#elif 0
    if (name == "nonRaidVersion.tst") //"out.dat.original")
        bigFileNode.reset(node->copy());
#else
    if (name == "IMG_7112.MOV") //"out.dat.original")
        bigFileNode.reset(node->copy());
#endif

    if (name.size() > 5 && name.substr(0,5) == "small") 
        api->startDownload(node->copy(), ("c:\\tmp\\" + name).c_str());


				}
				cout << "***** Done" << endl;
				delete list;

                if (megaPNGPresent)
                    finished = true;
                else
                {
                    cout << "***** Uploading the image MEGA.png" << endl;
                    api->startUpload("MEGA.png", root);
                }
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
            //g_fileinputs.clear();

            if (bigFileNode)
            {
                std::experimental::filesystem::remove("c:\\tmp\\test_mega_download");
                //api->startDownload(bigFileNode.get(), "c:\\tmp\\test_mega_download", &myTL);

                paused = false;
                pauseByteCount = 7000000;
                onetime = false;
            }

		}

		finished = true;
	}
	
	virtual void onTransferUpdate(MegaApi *api, MegaTransfer *transfer)
	{
		LOG_info << "***** Transfer progress: " << transfer->getTransferredBytes() << "/" << transfer->getTotalBytes(); 


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





void continueTransfersForAWhile(int nSeconds, std::string sessionString)
{
    MegaApi megaApi(APP_KEY, "C:\\tmp\\MegaCache", USER_AGENT);
    megaApi.setLogLevel(loglevel);

    MyListener listener;
    megaApi.addListener(&listener);

    MyMegaRequestListener fastLogonListener;
    megaApi.fastLogin(sessionString.c_str(), &fastLogonListener);
    while (!fastLogonListener.finished)
        Sleep(100);
    
    paused = false;
    cout << "fast logon complete" << endl;
    auto t = time(NULL);
    while (time(NULL) - t < nSeconds && !paused)
        Sleep(100);
}

std::unique_ptr<byte[]> compareEncryptedData;
std::unique_ptr<byte[]> compareDecryptedData;


namespace mega
{
    class DebugTestHook
    {
    public:
        static int countdownToOverquota;
        static int countdownTo404;
        static int countdownTo403;
        static int countdownToTimeout;
        static bool isRaid;
        static bool isRaidKnown;

/*        static void onSetIsRaid_morechunks(::mega::TransferBufferManager* tbm)
        {

            unsigned oldvalue = tbm->raidLinesPerChunk;
            tbm->raidLinesPerChunk /= 4;
            LOG_info << "adjusted raidlinesPerChunk from " << oldvalue << " to " << tbm->raidLinesPerChunk;
        };

        static bool  onHttpReqPost509(HttpReq* req)
        {
            if (req->type == REQ_BINARY)
            {
                if (countdownToOverquota-- == 0) {
                    req->httpstatus = 509;
                    req->timeleft = 30;  // in seconds
                    req->status = REQ_FAILURE;

                    LOG_info << "SIMULATING HTTP GET 509 OVERQUOTA";
                    return true;
                }
            }
            return false;
        };

        static bool  onHttpReqPost404Or403(HttpReq* req)
        {
            if (req->type == REQ_BINARY)
            {
                if (countdownTo404-- == 0) {
                    req->httpstatus = 404;
                    req->status = REQ_FAILURE;

                    LOG_info << "SIMULATING HTTP GET 404";
                    return true;
                }
                if (countdownTo403-- == 0) {
                    req->httpstatus = 403;
                    req->status = REQ_FAILURE;

                    LOG_info << "SIMULATING HTTP GET 403";
                    return true;
                }
            }
            return false;
        };


        static bool  onHttpReqPostTimeout(HttpReq* req)
        {
            if (req->type == REQ_BINARY)
            {
                if (countdownToTimeout-- == 0) {
                    req->lastdata = Waiter::ds;
                    req->status = REQ_INFLIGHT;

                    LOG_info << "SIMULATING HTTP TIMEOUT (timeout period begins now)-------------------------------------------------------------------------------------------------------------------------------------------";
                    return true;
                }
            }
            return false;
        };

        static void onSetIsRaid(::mega::TransferBufferManager* tbm)
        {
            isRaid = tbm->isRaid();
            isRaidKnown = true;
        };

        static bool resetForTests()
        {
#ifdef DEBUG_TEST_HOOKS_ENABLED
            globalMegaTestHooks = MegaTestHooks(); // remove any callbacks set in other tests
            countdownToOverquota = 3;
            countdownTo404 = 5;
            countdownTo403 = 10;
            countdownToTimeout = 15;
            isRaid = false;
            isRaidKnown = false;
            return true;
#else
            return false;
#endif
        }*/
    };

    int DebugTestHook::countdownToOverquota = 3;
    bool DebugTestHook::isRaid = false;
    bool DebugTestHook::isRaidKnown = false;
    int DebugTestHook::countdownTo404 = 5;
    int DebugTestHook::countdownTo403 = 10;
    int DebugTestHook::countdownToTimeout = 15;

};


int main()
{
	//Check the documentation of MegaApi to know how to enable local caching
	MegaApi *megaApi = new MegaApi(APP_KEY, "C:\\tmp\\MegaCache", USER_AGENT);

	//By default, logs are sent to stdout
	//You can use MegaApi::setLoggerObject to receive SDK logs in your app
	megaApi->setLogLevel(loglevel);
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

    {
        auto filesize = std::experimental::filesystem::file_size(R"(C:\Users\MATTW\source\repos\cloudraidproxy\cloudraidproxy\out.dat.original)");
        std::ifstream compareEncryptedFile(R"(C:\Users\MATTW\source\repos\cloudraidproxy\cloudraidproxy\out.dat.original)", ios::binary);
        compareEncryptedData.reset(new byte[filesize]);
        compareEncryptedFile.read((char*)compareEncryptedData.get(), filesize);
        std::ifstream compareDecryptedFile(R"("C:\Users\MATTW\Downloads\kimorg-20110421-021857+0600copy.tif")", ios::binary);
        compareDecryptedData.reset(new byte[filesize]);
        compareDecryptedFile.read((char*)compareDecryptedData.get(), filesize);
    }


    std::string sessionString;

    // try to re-establish the prior session 
    bool fastLogonSucceeded = false;
    {
        std::ifstream sessionFile("C:\\tmp\\MegaSession.txt");
        sessionFile >> sessionString;
        if (!sessionString.empty())
        { 
            MyMegaRequestListener fastLogonListener;
            megaApi->fastLogin(sessionString.c_str(), &fastLogonListener);
            while (!fastLogonListener.finished)
                Sleep(1000);
            fastLogonSucceeded = megaApi->isLoggedIn();
        }
    }

	//Login. You can get the result in the onRequestFinish callback of your listener.  
    if (!megaApi->isLoggedIn())
    {
        std::string password;
        cout << "Enter password for account " << MEGA_EMAIL << ": ";
        cin >> password;
        megaApi->login(MEGA_EMAIL, password.c_str());

        //You can use the main thread to show a GUI or anything else. MegaApi runs in a background thread.
        while (!listener.finished)
        {
            Sleep(1000);
        }
    }

    std::unique_ptr<char[]> sessionKeyString(megaApi->dumpSession());
    if (sessionKeyString)
    {
        sessionString = std::string(sessionKeyString.get());
        std::ofstream sessionFile("C:\\tmp\\MegaSession.txt");
        sessionFile << sessionKeyString.get();
    }



    DebugTestHook::countdownToTimeout = 15;
#ifdef DEBUG_TEST_HOOKS_ENABLED
    globalMegaTestHooks.onHttpReqPost = DebugTestHook::onHttpReqPostTimeout;
    globalMegaTestHooks.onSetIsRaid = DebugTestHook::onSetIsRaid_morechunks;
#endif

    if (bigFileNode)
    {
        std::experimental::filesystem::remove("c:\\tmp\\test_mega_download");
        //megaApi->startDownload(bigFileNode.get(), "c:\\tmp\\test_mega_download", &myTL);




        pauseByteCount = 1500000000;
        for (int i = 0; !paused; ++i)
        {
            Sleep(1000);
            if (DebugTestHook::countdownToTimeout < -1)
                DebugTestHook::countdownToTimeout = 17;
        }

    }

    cout << "delete api object!!!" << endl;
    delete megaApi;
    cout << "sleep a bit!!!" << endl;
    Sleep(3);
    for (int i = 0; i < 100; ++i)
    {
        pauseByteCount += 10000000;
        paused = false;
        cout << "start api for a while!!!" << endl;
        continueTransfersForAWhile(20, sessionString);
        cout << "api gone, sleep for a while!!!" << endl;
        Sleep(1000);
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

    delete megaApi;
	return 0;
}


void checkMac(const std::string& from, m_off_t pos, ChunkMAC& chunkmac)
{
    std::string b;
    Base64::btoa(std::string((char*)chunkmac.mac, sizeof(chunkmac.mac)), b);
    LOG_info << from << " mac at " << pos << " is " << b << " finished " << chunkmac.finished;

    std::string correctmac = "";
    switch (pos)
    {
    case 0: correctmac = "S6dzVBx-EGU-MS0l_xYwHg"; break;
    case 131072: correctmac = "oe6wVm3IDhXir-ve3Fj86A"; break;
    case 393216: correctmac = "yDrj7Z2vZ0RwAx5XsOnEdw"; break;
    case 786432: correctmac = "Z3yHg9LVeQNpTYNY0TBb-Q"; break;
    case 1310720: correctmac = "F-soRH_IvtXKgS7l94kqgQ"; break;
    case 1966080: correctmac = "Y85asy4N-nikj_J2C3Tp8A"; break;
    case 2752512: correctmac = "_AzmzMPuW2TbTgQVNkojIQ"; break;
    case 3670016: correctmac = "zCobz3MW6psEd8dFZRbv6A"; break;
    case 4718592: correctmac = "lHQXkecxw9tnxVu8lcZtmA"; break;
    case 5767168: correctmac = "iG-HKhch52blDmjAe6E29w"; break;
    case 6815744: correctmac = "EhQu0wraLgRmMP78uaxNTg"; break;
    case 7864320: correctmac = "JZvPJrUwiNjwIui_wYUqoQ"; break;
    case 8912896: correctmac = "aqeZIzTxIb9Euedw19NA_g"; break;
    case 9961472: correctmac = "ZO-UXbJgLnhiL-nphV3gAA"; break;
    case 11010048: correctmac = "1GVe3JR2Ud-CkPA-eRBCDA"; break;
    case 12058624: correctmac = "LY8Sdw_yFXnSMSnxb-eZSA"; break;
    case 13107200: correctmac = "arxk4pVG620Vb8FHhn_EEA"; break;
    case 14155776: correctmac = "8EO24yq4IMRsGA9u1c1bqw"; break;
    case 15204352: correctmac = "HKOHmWFeTejhPvny9asr2g"; break;
    case 16252928: correctmac = "MOoGuAtCxEdnQAbOI2dzRw"; break;
    case 17301504: correctmac = "4bhtezmxrHMZN8R8P6Y-8g"; break;
    case 18350080: correctmac = "rmUeC97xt8xrvqVoMmVMSg"; break;
    case 19398656: correctmac = "2ldwM-5FPIUoHK-COdIHIg"; break;
    case 20447232: correctmac = "UkUmefQ1n3yHO1f0K79IkQ"; break;
    case 21495808: correctmac = "6sVS3rm6ns_hdVCNe_WD8Q"; break;
    case 22544384: correctmac = "XnJatg3TgveeOkE3BXd49g"; break;
    case 23592960: correctmac = "Iy1hGqI7XKv7VgNE_8AMww"; break;
    case 24641536: correctmac = "6SR0uXr6Mf3shMM_8908rg"; break;
    case 25690112: correctmac = "Kf7sOD3QLy43rGGSDj2KJw"; break;
    case 26738688: correctmac = "FeJdcAxtUN8Py-vcwnfs4Q"; break;
    case 27787264: correctmac = "CxwZnPY9KzTUzz6iZOTKng"; break;
    case 28835840: correctmac = "Soon7ZDbcx2b4Ur5PpjRuQ"; break;
    case 29884416: correctmac = "1he2UvzCHru866eOQxVW0A"; break;
    case 30932992: correctmac = "MnGNL78oqq6Bh9Pm55g1ug"; break;
    case 31981568: correctmac = "eHMcY8nC18fxcbSkzp9CQA"; break;
    }
    assert(b == correctmac || b == "zc3Nzc3Nzc3Nzc3Nzc3NzQ" );
}


//void checkEncryptedFilePiece(TransferBufferManager::FilePiece& r)
//{
//    // call from finalize()
//    LOG_info << "processing output from " << r.pos << " to " << r.pos + r.buf.datalen();
//    extern std::unique_ptr<byte[]> compareEncryptedData;
//    assert(0 == memcmp(compareEncryptedData.get() + r.pos, r.buf.datastart(), r.buf.datalen()));
//}

// test code for transferslot.cpp
//if (reqs[i] && (reqs[i]->status == REQ_PREPARED))
//{
//    // todo: remove test code
//    extern bool onetime;
//    static bool secondtime = true;
//    unsigned failcon = (transfer->progresscompleted * 2 * 6 / transfer->size) % RAIDPARTS;
//    if ((i == failcon || i + 1 == failcon) && (transfer->progresscompleted > 5'000'000 && !onetime || transfer->progresscompleted > 10'000'000 /*&& !secondtime*/))
//    {
//        LOG_info << "SIMULATING HTTP GET FAIL ON " << i;
//        reqs[i]->status = REQ_FAILURE;  // simulate one failing connection
//        reqs[i]->httpstatus = 403;
//        secondtime = onetime;
//        onetime = true;
//    }
//    else
//        reqs[i]->post(client);
//}

// test code for raid.cpp
//// todo: remove testing code
//useOnlyFiveRaidConnections = false;
//unusedRaidConnection = rand() % RAIDPARTS;
//raidLinesPerChunk = 16 * 1024;
//LOG_debug << (useOnlyFiveRaidConnections ? "starting with 5 connection raid" : "using 6 Connection raid");
