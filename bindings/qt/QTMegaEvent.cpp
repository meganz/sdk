#include "QTMegaEvent.h"

using namespace mega;
using namespace std;

QTMegaEvent::QTMegaEvent(MegaApi* api, Type type):
    QEvent(type)
{
    megaApi = api;
    request = NULL;
    transfer = NULL;
    error = NULL;
    nodes = NULL;
    users = NULL;
    userAlerts = NULL;
    event  = NULL;

#ifdef ENABLE_SYNC
    sync = NULL;
    syncStats = NULL;
    localPath = NULL;
    newState = 0;
#endif
}

QTMegaEvent::~QTMegaEvent()
{
    delete request;
    delete transfer;
    delete error;
    delete nodes;
    delete users;
    delete userAlerts;
    delete event;

#ifdef ENABLE_SYNC
    delete sync;
    delete syncStats;
    delete localPath;
#endif
}

MegaApi *QTMegaEvent::getMegaApi() const
{
    return megaApi;
}

MegaRequest *QTMegaEvent::getRequest()
{
    return request;
}

MegaTransfer *QTMegaEvent::getTransfer()
{
    return transfer;
}

MegaError *QTMegaEvent::getError()
{
    return error;
}

MegaNodeList *QTMegaEvent::getNodes()
{
    return nodes;
}

MegaUserList *QTMegaEvent::getUsers()
{
    return users;
}

MegaUserAlertList *QTMegaEvent::getUserAlerts()
{
    return userAlerts;
}

void QTMegaEvent::setRequest(MegaRequest *request)
{
    this->request = request;
}

void QTMegaEvent::setTransfer(MegaTransfer *transfer)
{
    this->transfer = transfer;
}

void QTMegaEvent::setError(MegaError *error)
{
    this->error = error;
}

void QTMegaEvent::setNodes(MegaNodeList *nodes)
{
    this->nodes = nodes;
}

void QTMegaEvent::setUsers(MegaUserList *users)
{
    this->users = users;
}

void QTMegaEvent::setUserAlerts(MegaUserAlertList *userAlerts)
{
    this->userAlerts = userAlerts;
}

MegaEvent *QTMegaEvent::getEvent()
{
    return event;
}

void QTMegaEvent::setEvent(MegaEvent* event)
{
    this->event = event;
}

#ifdef ENABLE_SYNC
MegaSync *QTMegaEvent::getSync()
{
    return sync;
}

MegaSyncStats *QTMegaEvent::getSyncStats()
{
    return syncStats;
}

string *QTMegaEvent::getLocalPath()
{
    return localPath;
}

int QTMegaEvent::getNewState()
{
    return newState;
}

void QTMegaEvent::setSync(MegaSync *sync)
{
    this->sync = sync;
}

void QTMegaEvent::setSyncStats(MegaSyncStats *stats)
{
    this->syncStats = stats;
}

void QTMegaEvent::setLocalPath(string *localPath)
{
    this->localPath = localPath;
}

void QTMegaEvent::setNewState(int newState)
{
    this->newState = newState;
}
#endif

const std::string& QTMegaEvent::getMountPath() const
{
    return mMountPath;
}

int QTMegaEvent::getMountResult() const
{
    return mMountResult;
}

void QTMegaEvent::setMountPath(const std::string& path)
{
    mMountPath = path;
}

void QTMegaEvent::setMountResult(int result)
{
    mMountResult = result;
}
