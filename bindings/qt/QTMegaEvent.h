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
        OnReloadNeeded,
        OnSyncStateChanged,
        OnSyncFileStateChanged
    };

    QTMegaEvent(MegaApi *megaApi, Type type);
    ~QTMegaEvent();

    MegaApi *getMegaApi();
    MegaRequest* getRequest();
    MegaTransfer* getTransfer();
    MegaError* getError();
    MegaNodeList* getNodes();
    MegaUserList* getUsers();
    const char* getFilePath();
    int getNewState();

    void setRequest(MegaRequest *request);
    void setTransfer(MegaTransfer *transfer);
    void setError(MegaError *error);
    void setNodes(MegaNodeList *nodes);
    void setUsers(MegaUserList *users);
    void setFilePath(const char* filePath);
    void setNewState(int newState);

private:
    MegaApi *megaApi;
    MegaRequest *request;
    MegaTransfer *transfer;
    MegaError *error;
    MegaNodeList *nodes;
    MegaUserList *users;
    const char* filePath;
    int newState;
};

}

#endif // QTMEGAEVENT_H
