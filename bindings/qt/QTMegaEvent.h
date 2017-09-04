#ifndef QTMEGAEVENT_H
#define QTMEGAEVENT_H

#include <megaapi.h>
#include <QEvent>

namespace mega
{

class QTMegaEvent: public QEvent
{
public:
    enum MegaType
    {
        OnRequestStart = QEvent::User + 100,
        OnRequestUpdate,
        OnRequestFinish,
        OnRequestTemporaryError,
        OnTransferStart,
        OnTransferTemporaryError,
        OnTransferUpdate,
        OnTransferFinish,
        OnUsersUpdate,
        OnNodesUpdate,
        OnAccountUpdate,
        OnReloadNeeded,
        OnEvent
#if ENABLE_SYNC
        ,
        OnSyncStateChanged,
        OnFileSyncStateChanged,
        OnGlobalSyncStateChanged
#endif
    };

    QTMegaEvent(MegaApi *megaApi, Type type);
    ~QTMegaEvent();

    MegaApi *getMegaApi();
    MegaRequest* getRequest();
    MegaTransfer* getTransfer();
    MegaError* getError();
    MegaNodeList* getNodes();
    MegaUserList* getUsers();
    MegaEvent* getEvent();

    void setRequest(MegaRequest *request);
    void setTransfer(MegaTransfer *transfer);
    void setError(MegaError *error);
    void setNodes(MegaNodeList *nodes);
    void setUsers(MegaUserList *users);
    void setEvent(MegaEvent *event);

#ifdef ENABLE_SYNC
    MegaSync *getSync();
    void setSync(MegaSync *sync);
    std::string *getLocalPath();
    void setLocalPath(std::string *localPath);
    int getNewState();
    void setNewState(int newState);
#endif

private:
    MegaApi *megaApi;
    MegaRequest *request;
    MegaTransfer *transfer;
    MegaError *error;
    MegaNodeList *nodes;
    MegaUserList *users;
    MegaEvent *event;

#ifdef ENABLE_SYNC
    MegaSync *sync;
    std::string* localPath;
    int newState;
#endif
};

}

#endif // QTMEGAEVENT_H
