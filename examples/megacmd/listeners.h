/**
 * @file examples/megacmd/listeners.h
 * @brief MEGAcmd: Listeners
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

#ifndef LISTENERS_H
#define LISTENERS_H

#include "megacmd.h"
#include "megacmdlogger.h"

class MegaCmdListener : public mega::SynchronousRequestListener
{
private:
    float percentFetchnodes;
    bool alreadyFinished;
public:
    MegaCmdListener(mega::MegaApi *megaApi, mega::MegaRequestListener *listener = NULL);
    virtual ~MegaCmdListener();

    //Request callbacks
    virtual void onRequestStart(mega::MegaApi* api, mega::MegaRequest *request);
    virtual void doOnRequestFinish(mega::MegaApi* api, mega::MegaRequest *request, mega::MegaError* error);
    virtual void onRequestUpdate(mega::MegaApi* api, mega::MegaRequest *request);
    virtual void onRequestTemporaryError(mega::MegaApi *api, mega::MegaRequest *request, mega::MegaError* e);

protected:
    mega::MegaRequestListener *listener;
};


class MegaCmdTransferListener : public mega::SynchronousTransferListener
{
private:
    float percentDowloaded;
    bool alreadyFinished;
public:
    MegaCmdTransferListener(mega::MegaApi *megaApi, mega::MegaTransferListener *listener = NULL);
    virtual ~MegaCmdTransferListener();

    //Transfer callbacks
    virtual void onTransferStart(mega::MegaApi* api, mega::MegaTransfer *transfer);
    virtual void doOnTransferFinish(mega::MegaApi* api, mega::MegaTransfer *transfer, mega::MegaError* e);
    virtual void onTransferUpdate(mega::MegaApi* api, mega::MegaTransfer *transfer);
    virtual void onTransferTemporaryError(mega::MegaApi *api, mega::MegaTransfer *transfer, mega::MegaError* e);
    virtual bool onTransferData(mega::MegaApi *api, mega::MegaTransfer *transfer, char *buffer, size_t size);

protected:
    mega::MegaTransferListener *listener;
};

class MegaCmdGlobalListener : public mega::MegaGlobalListener
{
private:
    MegaCMDLogger *loggerCMD;

public:
    MegaCmdGlobalListener(MegaCMDLogger *logger);
    void onNodesUpdate(mega::MegaApi* api, mega::MegaNodeList *nodes);
    void onUsersUpdate(mega::MegaApi* api, mega::MegaUserList *users);
#ifdef ENABLE_CHAT
    void onChatsUpdate(mega::MegaApi*, mega::MegaTextChatList*);
#endif
};

class MegaCmdMegaListener : public mega::MegaListener
{

public:
    MegaCmdMegaListener(mega::MegaApi *megaApi, mega::MegaListener *parent=NULL);
    virtual ~MegaCmdMegaListener();

    virtual void onRequestFinish(mega::MegaApi* api, mega::MegaRequest *request, mega::MegaError* e);

#ifdef ENABLE_CHAT
    void onChatsUpdate(mega::MegaApi *api, mega::MegaTextChatList *chats);
#endif


protected:
    mega::MegaApi *megaApi;
    mega::MegaListener *listener;
};


#endif // LISTENERS_H
