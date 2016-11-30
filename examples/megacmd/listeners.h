/**
 * @file examples/megacmd/listeners.h
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

#ifndef LISTENERS_H
#define LISTENERS_H

#include "megacmd.h"
#include "megacmdlogger.h"

class MegaCmdListener : public SynchronousRequestListener
{
private:
    float percentFetchnodes;
public:
    MegaCmdListener(MegaApi *megaApi, MegaRequestListener *listener = NULL);
    virtual ~MegaCmdListener();

    //Request callbacks
    virtual void onRequestStart(MegaApi* api, MegaRequest *request);
    virtual void doOnRequestFinish(MegaApi* api, MegaRequest *request, MegaError* error);
    virtual void onRequestUpdate(MegaApi* api, MegaRequest *request);
    virtual void onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError* e);

protected:
    //virtual void customEvent(QEvent * event);

    MegaRequestListener *listener;
};


class MegaCmdTransferListener : public SynchronousTransferListener
{
private:
    float percentFetchnodes;
public:
    MegaCmdTransferListener(MegaApi *megaApi, MegaTransferListener *listener = NULL);
    virtual ~MegaCmdTransferListener();

    //Transfer callbacks
    virtual void onTransferStart(MegaApi* api, MegaTransfer *transfer);
    virtual void doOnTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* e);
    virtual void onTransferUpdate(MegaApi* api, MegaTransfer *transfer);
    virtual void onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError* e);
    virtual bool onTransferData(MegaApi *api, MegaTransfer *transfer, char *buffer, size_t size);

protected:
    //virtual void customEvent(QEvent * event);

    MegaTransferListener *listener;
};

class MegaCmdGlobalListener : public MegaGlobalListener
{
private:
    MegaCMDLogger *loggerCMD;

public:
    MegaCmdGlobalListener(MegaCMDLogger *logger);
    void onNodesUpdate(MegaApi* api, MegaNodeList *nodes);
    void onUsersUpdate(MegaApi* api, MegaUserList *users);
#ifdef ENABLE_CHAT
    void onChatsUpdate(MegaApi*, MegaTextChatList*);
#endif
};

class MegaCmdMegaListener : public MegaListener
{

public:
    MegaCmdMegaListener(MegaApi *megaApi, MegaListener *parent=NULL);
    virtual ~MegaCmdMegaListener();

    virtual void onRequestFinish(MegaApi* api, MegaRequest *request, MegaError* e);

#ifdef ENABLE_CHAT
    void onChatsUpdate(MegaApi *api, MegaTextChatList *chats);
#endif


protected:
    MegaApi *megaApi;
    MegaListener *listener;
};


#endif // LISTENERS_H
