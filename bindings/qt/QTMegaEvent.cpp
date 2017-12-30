#include "QTMegaEvent.h"

using namespace mega;
using namespace std;

QTMegaEvent::QTMegaEvent(MegaApi *megaApi, Type type) : QEvent(type)
{
    this->megaApi = megaApi;
    request = NULL;
    transfer = NULL;
    error = NULL;
    nodes = NULL;
    users = NULL;
    event  = NULL;

#ifdef ENABLE_SYNC
    sync = NULL;
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
    delete event;

#ifdef ENABLE_SYNC
    delete sync;
    delete localPath;
#endif
}

MegaApi *QTMegaEvent::getMegaApi()
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

void QTMegaEvent::setLocalPath(string *localPath)
{
    this->localPath = localPath;
}

void QTMegaEvent::setNewState(int newState)
{
    this->newState = newState;
}
#endif
