#pragma once

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
        OnUserAlertsUpdate,
        OnNodesUpdate,
        OnAccountUpdate,
        OnReloadNeeded,
        OnEvent
#if ENABLE_SYNC
        ,
        OnSyncStateChanged,
        OnSyncEvent,
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
    MegaUserAlertList* getUserAlerts();
    MegaEvent* getEvent();

    void setRequest(MegaRequest *request);
    void setTransfer(MegaTransfer *transfer);
    void setError(MegaError *error);
    void setNodes(MegaNodeList *nodes);
    void setUsers(MegaUserList *users);
    void setUserAlerts(MegaUserAlertList *userAlerts);
    void setEvent(MegaEvent *event);

#ifdef ENABLE_SYNC
    MegaSyncEvent *getSyncEvent();
    void setSyncEvent(MegaSyncEvent *event);

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
    MegaUserAlertList *userAlerts;
    MegaEvent *event;

#ifdef ENABLE_SYNC
    MegaSync *sync;
    std::string* localPath;
    int newState;
#endif
};

}
