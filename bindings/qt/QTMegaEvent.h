#pragma once

#include <megaapi.h>

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#include <QEvent>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

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
        OnTransferFolderUpdate,
        OnTransferFinish,
        OnUsersUpdate,
        OnUserAlertsUpdate,
        OnNodesUpdate,
        OnAccountUpdate,
        OnReloadNeeded,
        OnEvent,
#if ENABLE_SYNC
        OnSyncStateChanged,
        OnSyncStatsUpdated,
        OnFileSyncStateChanged,
        OnSyncAdded,
        OnSyncDeleted,
        OnGlobalSyncStateChanged,
#endif
        OnMountAdded,
        OnMountChanged,
        OnMountDisabled,
        OnMountEnabled,
        OnMountRemoved
    };

    QTMegaEvent(MegaApi* api, Type type);
    ~QTMegaEvent() override;

    MegaApi *getMegaApi() const;
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
    MegaSync *getSync();
    void setSync(MegaSync *sync);
    void setSyncStats(MegaSyncStats *stats);
    MegaSyncStats *getSyncStats();
    std::string *getLocalPath();
    void setLocalPath(std::string *localPath);
    int getNewState();
    void setNewState(int newState);
#endif

    const std::string& getMountPath() const;
    int getMountResult() const;

    void setMountPath(const std::string& path);
    void setMountResult(int result);

private:
    MegaApi* megaApi;
    MegaRequest *request;
    MegaTransfer *transfer;
    MegaError *error;
    MegaNodeList *nodes;
    MegaUserList *users;
    MegaUserAlertList *userAlerts;
    MegaEvent *event;

#ifdef ENABLE_SYNC
    MegaSync *sync;
    MegaSyncStats *syncStats = nullptr;
    std::string* localPath;
    int newState;
#endif

    std::string mMountPath;
    int mMountResult;
};

}
