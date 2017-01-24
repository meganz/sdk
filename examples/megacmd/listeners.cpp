/**
 * @file examples/megacmd/listeners.cpp
 * @brief MegaCMD: Listeners
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

#include "listeners.h"
#include "configurationmanager.h"
#include "megacmdutils.h"

#define USE_VARARGS
#define PREFER_STDARG
#include <readline/readline.h>

using namespace mega;

#ifdef ENABLE_CHAT
void MegaCmdGlobalListener::onChatsUpdate(MegaApi*, MegaTextChatList*)
{

}
#endif

void MegaCmdGlobalListener::onUsersUpdate(MegaApi *api, MegaUserList *users)
{
    static bool initial = true;
    if (users)
    {
        if (users->size() == 1)
        {
            LOG_debug << " 1 user received or updated";
        }
        else
        {
            LOG_debug << users->size() << " users received or updated";
        }
    }
    else //initial update or too many changes
    {
        MegaUserList *users = api->getContacts();

        if (users && users->size())
        {
            if (users->size() == 1)
            {
                LOG_debug << " 1 user received or updated";
            }
            else
            {
                LOG_debug << users->size() << " users received or updated";
            }

            // force reshow display for a first clean prompt
            if (initial && loggerCMD->getCmdLoggerLevel()>=MegaApi::LOG_LEVEL_DEBUG)
            {
                rl_forced_update_display();
            }
            initial = false;

            delete users;
        }
    }
}

MegaCmdGlobalListener::MegaCmdGlobalListener(MegaCMDLogger *logger)
{
    this->loggerCMD = logger;
}

void MegaCmdGlobalListener::onNodesUpdate(MegaApi *api, MegaNodeList *nodes)
{
    int nfolders = 0;
    int nfiles = 0;
    int rfolders = 0;
    int rfiles = 0;
    if (nodes)
    {
        for (int i = 0; i < nodes->size(); i++)
        {
            MegaNode *n = nodes->get(i);
            if (n->getType() == MegaNode::TYPE_FOLDER)
            {
                if (n->isRemoved())
                {
                    rfolders++;
                }
                else
                {
                    nfolders++;
                }
            }
            else if (n->getType() == MegaNode::TYPE_FILE)
            {
                if (n->isRemoved())
                {
                    rfiles++;
                }
                else
                {
                    nfiles++;
                }
            }
        }
    }
    else //initial update or too many changes
    {
        if (loggerCMD->getMaxLogLevel() >= logInfo)
        {
            MegaNode * nodeRoot = api->getRootNode();
            int * nFolderFiles = getNumFolderFiles(nodeRoot, api);
            nfolders += nFolderFiles[0];
            nfiles += nFolderFiles[1];
            delete []nFolderFiles;
            delete nodeRoot;

            MegaNode * inboxNode = api->getInboxNode();
            nFolderFiles = getNumFolderFiles(inboxNode, api);
            nfolders += nFolderFiles[0];
            nfiles += nFolderFiles[1];
            delete []nFolderFiles;
            delete inboxNode;

            MegaNode * rubbishNode = api->getRubbishNode();
            nFolderFiles = getNumFolderFiles(rubbishNode, api);
            nfolders += nFolderFiles[0];
            nfiles += nFolderFiles[1];
            delete []nFolderFiles;
            delete rubbishNode;

            MegaNodeList *inshares = api->getInShares();
            if (inshares)
            {
                for (int i = 0; i < inshares->size(); i++)
                {
                    nfolders++; //add the share itself
                    nFolderFiles = getNumFolderFiles(inshares->get(i), api);
                    nfolders += nFolderFiles[0];
                    nfiles += nFolderFiles[1];
                    delete []nFolderFiles;
                }
            }
            delete inshares;
        }

        if (nfolders)
        {
            LOG_debug << nfolders << " folders " << "added or updated ";
        }
        if (nfiles)
        {
            LOG_debug << nfiles << " files " << "added or updated ";
        }
        if (rfolders)
        {
            LOG_debug << rfolders << " folders " << "removed";
        }
        if (rfiles)
        {
            LOG_debug << rfiles << " files " << "removed";
        }
    }
}

////////////////////////////////////////////
///      MegaCmdMegaListener methods     ///
////////////////////////////////////////////

void MegaCmdMegaListener::onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e)
{
    if (e && ( e->getErrorCode() == MegaError::API_ESID ))
    {
        LOG_err << "Session is no longer valid (it might have been invalidated from elsewhere) ";
        changeprompt(prompts[COMMAND]);
    }
}

MegaCmdMegaListener::MegaCmdMegaListener(MegaApi *megaApi, MegaListener *parent)
{
    this->megaApi = megaApi;
    this->listener = parent;
}

MegaCmdMegaListener::~MegaCmdMegaListener()
{
    this->listener = NULL;
    if (megaApi)
    {
        megaApi->removeListener(this);
    }
}

#ifdef ENABLE_CHAT
void MegaCmdMegaListener::onChatsUpdate(MegaApi *api, MegaTextChatList *chats)
{}
#endif

////////////////////////////////////////
///      MegaCmdListener methods     ///
////////////////////////////////////////

void MegaCmdListener::onRequestStart(MegaApi* api, MegaRequest *request)
{
    if (!request)
    {
        LOG_err << " onRequestStart for undefined request ";
        return;
    }

    LOG_verbose << "onRequestStart request->getType(): " << request->getType();
}

void MegaCmdListener::doOnRequestFinish(MegaApi* api, MegaRequest *request, MegaError* e)
{
    if (!request)
    {
        LOG_err << " onRequestFinish for undefined request ";
        return;
    }

    LOG_verbose << "onRequestFinish request->getType(): " << request->getType();

    switch (request->getType())
    {
        case MegaRequest::TYPE_FETCH_NODES:
        {
            map<string, sync_struct *>::iterator itr;
            int i = 0;
#ifdef ENABLE_SYNC

            for (itr = ConfigurationManager::configuredSyncs.begin(); itr != ConfigurationManager::configuredSyncs.end(); ++itr, i++)
            {
                sync_struct *oldsync = ((sync_struct*)( *itr ).second );
                sync_struct *thesync = new sync_struct;
                *thesync = *oldsync;

                MegaCmdListener *megaCmdListener = new MegaCmdListener(api, NULL);
                MegaNode * node = api->getNodeByHandle(thesync->handle);
                api->resumeSync(thesync->localpath.c_str(), node, thesync->fingerprint, megaCmdListener);
                megaCmdListener->wait();
                if (megaCmdListener->getError() && ( megaCmdListener->getError()->getErrorCode() == MegaError::API_OK ))
                {
                    thesync->fingerprint = megaCmdListener->getRequest()->getNumber();
                    thesync->active = true;

                    if (ConfigurationManager::loadedSyncs.find(thesync->localpath) != ConfigurationManager::loadedSyncs.end())
                    {
                        delete ConfigurationManager::loadedSyncs[thesync->localpath];
                    }
                    ConfigurationManager::loadedSyncs[thesync->localpath] = thesync;
                    char *nodepath = api->getNodePath(node);
                    LOG_info << "Loaded sync: " << thesync->localpath << " to " << nodepath;
                    delete []nodepath;
                }
                else
                {
                    delete thesync;
                }

                delete megaCmdListener;
                delete node;
            }
#endif

            break;
        }

        default:
            break;
    }
}

void MegaCmdListener::onRequestUpdate(MegaApi* api, MegaRequest *request)
{
    if (!request)
    {
        LOG_err << " onRequestUpdate for undefined request ";
        return;
    }

    LOG_verbose << "onRequestUpdate request->getType(): " << request->getType();

    switch (request->getType())
    {
        case MegaRequest::TYPE_FETCH_NODES:
        {
#if defined( RL_ISSTATE ) && defined( RL_STATE_INITIALIZED )
            int rows = 1, cols = 80;

            if (RL_ISSTATE(RL_STATE_INITIALIZED))
            {
                rl_resize_terminal();
                rl_get_screen_size(&rows, &cols);
            }
            string outputString;
            outputString.resize(cols+1);
            for (int i = 0; i < cols; i++)
            {
                outputString[i] = '.';
            }

            outputString[cols] = '\0';
            char *ptr = (char *)outputString.c_str();
            sprintf(ptr, "%s", "Fetching nodes ||");
            ptr += strlen("Fetching nodes ||");
            *ptr = '.'; //replace \0 char


            float oldpercent = percentFetchnodes;
            percentFetchnodes = request->getTransferredBytes() * 1.0 / request->getTotalBytes() * 100.0;
            if (alreadyFinished || ( ( percentFetchnodes == oldpercent ) && ( oldpercent != 0 )) )
            {
                return;
            }
            if (percentFetchnodes < 0)
            {
                percentFetchnodes = 0;
            }

            char aux[40];
            if (request->getTotalBytes() < 0)
            {
                return;                         // after a 100% this happens
            }
            if (request->getTransferredBytes() < 0.001 * request->getTotalBytes())
            {
                return;                                                            // after a 100% this happens
            }
            sprintf(aux,"||(%lld/%lld MB: %.2f %%) ", request->getTransferredBytes() / 1024 / 1024, request->getTotalBytes() / 1024 / 1024, percentFetchnodes);
            sprintf((char *)outputString.c_str() + cols - strlen(aux), "%s",                         aux);
            for (int i = 0; i <= ( cols - strlen("Fetching nodes ||") - strlen(aux)) * 1.0 * percentFetchnodes / 100.0; i++)
            {
                *ptr++ = '#';
            }


            {
                if (RL_ISSTATE(RL_STATE_INITIALIZED))
                {
                    if (percentFetchnodes == 100 && !alreadyFinished)
                    {
                        alreadyFinished = true;
                        rl_message("%s\n", outputString.c_str());
                    }
                    else
                    {
                        rl_message("%s", outputString.c_str());
                    }
                }
                else
                {
                    cout << outputString << endl; //too verbose
                }
            }

#endif
            break;
        }

        default:
            LOG_debug << "onRequestUpdate of unregistered type of request: " << request->getType();
            break;
    }
}

void MegaCmdListener::onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError* e)
{

}


MegaCmdListener::~MegaCmdListener()
{

}

MegaCmdListener::MegaCmdListener(MegaApi *megaApi, MegaRequestListener *listener)
{
    this->megaApi = megaApi;
    this->listener = listener;
    percentFetchnodes = 0.0f;
    alreadyFinished = false;
}


////////////////////////////////////////
///      MegaCmdListener methods     ///
////////////////////////////////////////

void MegaCmdTransferListener::onTransferStart(MegaApi* api, MegaTransfer *Transfer)
{
    if (!Transfer)
    {
        LOG_err << " onTransferStart for undefined Transfer ";
        return;
    }

    LOG_verbose << "onTransferStart Transfer->getType(): " << Transfer->getType();
}

void MegaCmdTransferListener::doOnTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* e)
{
    if (!transfer)
    {
        LOG_err << " onTransferFinish for undefined transfer ";
        return;
    }

    LOG_verbose << "onTransferFinish Transfer->getType(): " << transfer->getType();
}


void MegaCmdTransferListener::onTransferUpdate(MegaApi* api, MegaTransfer *transfer)
{
    if (!transfer)
    {
        LOG_err << " onTransferUpdate for undefined Transfer ";
        return;
    }

#if defined( RL_ISSTATE ) && defined( RL_STATE_INITIALIZED )
    int rows = 1, cols = 80;

    if (RL_ISSTATE(RL_STATE_INITIALIZED))
    {
        rl_resize_terminal();
        rl_get_screen_size(&rows, &cols);
    }
    string outputString;
    outputString.resize(cols + 1);
    for (int i = 0; i < cols; i++)
    {
        outputString[i] = '.';
    }

    outputString[cols] = '\0';
    char *ptr = (char *)outputString.c_str();
    sprintf(ptr, "%s", "TRANSFERING ||");
    ptr += strlen("TRANSFERING ||");
    *ptr = '.'; //replace \0 char


    float oldpercent = percentDowloaded;
    percentDowloaded = transfer->getTransferredBytes() * 1.0 / transfer->getTotalBytes() * 100.0;
    if (alreadyFinished || ( ( percentDowloaded == oldpercent ) && ( oldpercent != 0 ) ) )
    {
        return;
    }
    if (percentDowloaded < 0)
    {
        percentDowloaded = 0;
    }

    char aux[40];
    if (transfer->getTotalBytes() < 0)
    {
        return; // after a 100% this happens
    }
    if (transfer->getTransferredBytes() < 0.001 * transfer->getTotalBytes())
    {
        return; // after a 100% this happens
    }
    sprintf(aux,"||(%lld/%lld MB: %.2f %%) ", transfer->getTransferredBytes() / 1024 / 1024, transfer->getTotalBytes() / 1024 / 1024, percentDowloaded);
    sprintf((char *)outputString.c_str() + cols - strlen(aux), "%s",                         aux);
    for (int i = 0; i <= ( cols - strlen("TRANSFERING ||") - strlen(aux)) * 1.0 * percentDowloaded / 100.0; i++)
    {
        *ptr++ = '#';
    }

    {
        if (RL_ISSTATE(RL_STATE_INITIALIZED))
        {
            if (percentDowloaded == 100 && !alreadyFinished)
            {
                alreadyFinished = true;
                rl_message("%s\n", outputString.c_str());
            }
            else
            {
                rl_message("%s", outputString.c_str());
            }
        }
        else
        {
            cout << outputString << endl; //too verbose
        }
    }
#endif

    LOG_verbose << "onTransferUpdate transfer->getType(): " << transfer->getType();
}

void MegaCmdTransferListener::onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError* e)
{

}


MegaCmdTransferListener::~MegaCmdTransferListener()
{

}

MegaCmdTransferListener::MegaCmdTransferListener(MegaApi *megaApi, MegaTransferListener *listener)
{
    this->megaApi = megaApi;
    this->listener = listener;
    percentDowloaded = 0.0f;
    alreadyFinished = false;
}

bool MegaCmdTransferListener::onTransferData(MegaApi *api, MegaTransfer *transfer, char *buffer, size_t size)
{
    return true;
}

