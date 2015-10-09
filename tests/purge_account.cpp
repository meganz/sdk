/**
 * @file tests/sdktests.cpp
 * @brief A helper tool to completely wipe all data on a given account
 *
 * (c) 2015 by Mega Limited, Wellsford, New Zealand
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
#include "mega.h"
#include "../include/megaapi.h"
#include <string.h>

using namespace mega;

static const string APP_KEY = "V8ZGDDBA";
static const unsigned int pollingT      = 500000;   // (microseconds) to check if response from server is received
static const unsigned int maxTimeout    = 300;      // Maximum time (seconds) to wait for response from server

class PurgeAcc: public MegaListener, MegaRequestListener {

public:
    MegaApi *megaApi = NULL;
    int lastError;
    string email;
    string pwd;

    bool loggingReceived;
    bool fetchnodesReceived;
    bool logoutReceived;
    bool responseReceived;

    PurgeAcc(const char *cemail, const char *cpwd);
    void purge();
    void login();
    void logout();
protected:
    void onRequestStart(MegaApi *api, MegaRequest *request) {}
    void onRequestUpdate(MegaApi*api, MegaRequest *request) {}
    void onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e);
private:
    void waitForResponse(bool *responseReceived, int timeout = maxTimeout);
    void purgeTree(MegaNode *p);
    void fetchnodes();
};

PurgeAcc::PurgeAcc(const char *cemail, const char *cpwd)
{
    megaApi = new MegaApi(APP_KEY.c_str());
    email.assign(cemail);
    pwd.assign(cpwd);
    megaApi->addListener(this);
}

void PurgeAcc::login()
{
    loggingReceived = false;
    megaApi->login(email.data(), pwd.data());
    waitForResponse(&loggingReceived);
    if (lastError != MegaError::API_OK || !megaApi->isLoggedIn()) {
        std::cout << "Failed to login!" << std::endl;
        exit(1);
    }
    fetchnodes();
}

void PurgeAcc::fetchnodes()
{
    fetchnodesReceived = false;
    megaApi->fetchNodes();
    waitForResponse(&fetchnodesReceived);
    if (lastError != MegaError::API_OK || !megaApi->isLoggedIn()) {
        std::cout << "Failed to fetchnodes!" << std::endl;
        exit(1);
    }
}

void PurgeAcc::purgeTree(MegaNode *p)
{
    MegaNodeList *children;
    children = megaApi->getChildren(p);

    for (int i = 0; i < children->size(); i++)
    {
        MegaNode *n = children->get(i);
        if (n->isFolder())
            purgeTree(n);

        megaApi->remove(n);
    }
}

void PurgeAcc::purge()
{
    // remove files / folders
    purgeTree(megaApi->getRootNode());
    // clean Rubbish Bin
    megaApi->cleanRubbishBin();

    // clean contacts
    MegaUserList *ul = megaApi->getContacts();
    for (int i = 0; i < ul->size(); i++)
    {
        MegaUser *u = ul->get(i);
        if (u->getEmail() != email) // Trying to remove your own user throws API_EARGS
            megaApi->removeContact(u);
    }

    // Remove pending contact requests
    MegaContactRequestList *crl = megaApi->getOutgoingContactRequests();
    for (int i = 0; i < crl->size(); i++)
    {
        MegaContactRequest *cr = crl->get(i);
        megaApi->inviteContact(cr->getTargetEmail(), "Removing you", MegaContactRequest::INVITE_ACTION_DELETE);
    }

    // Remove incoming contact requests
    crl = megaApi->getIncomingContactRequests();
    for (int i = 0; i < crl->size(); i++)
    {
        MegaContactRequest *cr = crl->get(i);
        megaApi->replyContactRequest(cr, MegaContactRequest::REPLY_ACTION_DENY);
    }
}

void PurgeAcc::logout()
{
    logoutReceived = false;
    megaApi->logout(this);
    waitForResponse(&logoutReceived);
}

void PurgeAcc::onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e)
{
    lastError = e->getErrorCode();

    switch(request->getType())
    {
    case MegaRequest::TYPE_LOGIN:
        loggingReceived = true;
        break;

    case MegaRequest::TYPE_FETCH_NODES:
        fetchnodesReceived = true;
        break;

    case MegaRequest::TYPE_LOGOUT:
        logoutReceived = true;
        break;

    case MegaRequest::TYPE_REMOVE:
        responseReceived = true;
        break;

    case MegaRequest::TYPE_REMOVE_CONTACT:
        responseReceived = true;
        break;

    case MegaRequest::TYPE_SHARE:
        responseReceived = true;
        break;

    case MegaRequest::TYPE_CLEAN_RUBBISH_BIN:
        responseReceived = true;
        break;
    }
}

void PurgeAcc::waitForResponse(bool *responseReceived, int timeout)
{
    timeout *= 1000000; // convert to micro-seconds
    int tWaited = 0;    // microseconds
    while(!(*responseReceived))
    {
        usleep(pollingT);

        if (timeout)
        {
            tWaited += pollingT;
            if (tWaited >= timeout)
            {
                break;
            }
        }
    }
}

static void display_help(const char *app)
{
    std::cout << "Please make sure that MEGA_EMAIL and MEGA_PWD environment variables are set." << std::endl;
    std::cout << "Please specify `-y` flag to completely wipe all data (files, folders, contacts) on a given account." << std::endl;
}

int main(int argc, char *argv[])
{
    PurgeAcc *pa;

    if (argc < 2) {
        display_help(argv[0]);
        return 1;
    }

    if (strncmp(argv[1], "-y", 2) != 0) {
        display_help(argv[0]);
        return 1;
    }

    if (!getenv("MEGA_EMAIL") || !getenv("MEGA_PWD")) {
        display_help(argv[0]);
        return 1;
    }

    pa = new PurgeAcc(getenv("MEGA_EMAIL"), getenv("MEGA_PWD"));
    pa->login();
    pa->purge();
    pa->logout();
}
