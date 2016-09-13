#ifndef LISTENERS_H
#define LISTENERS_H

#include "megacmd.h"
#include "synchronousrequestlistener.h"
#include "megacmdlogger.h"


class MegaCmdListener : public SynchronousRequestListener
{
private:
    float percentFetchnodes = 0.0f;
public:
    MegaCmdListener(MegaApi *megaApi, MegaRequestListener *listener = NULL);
    virtual ~MegaCmdListener();

    //Request callbacks
    virtual void onRequestStart(MegaApi* api, MegaRequest *request);
    virtual void doOnRequestFinish(MegaApi* api, MegaRequest *request, MegaError* e);
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
    void onChatsUpdate(mega::MegaApi*, mega::MegaTextChatList*);
};


#endif // LISTENERS_H
