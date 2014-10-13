#include "QTMegaEvent.h"

using namespace mega;

QTMegaEvent::QTMegaEvent(MegaApi *megaApi, Type type) : QEvent(type)
{
    this->megaApi = megaApi;
    request = NULL;
    transfer = NULL;
    error = NULL;
    filePath = NULL;
    newState = 0;
}

QTMegaEvent::~QTMegaEvent()
{
    delete request;
    delete transfer;
    delete error;
    delete filePath;
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

NodeList *QTMegaEvent::getNodes()
{
    return nodes;
}

UserList *QTMegaEvent::getUsers()
{
    return users;
}

const char *QTMegaEvent::getFilePath()
{
    return filePath;
}

int QTMegaEvent::getNewState()
{
    return newState;
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

void QTMegaEvent::setNodes(NodeList *nodes)
{
    this->nodes = nodes;
}

void QTMegaEvent::setUsers(UserList *users)
{
    this->users = users;
}

void QTMegaEvent::setFilePath(const char *filePath)
{
    this->filePath = filePath;
}

void QTMegaEvent::setNewState(int newState)
{
    this->newState = newState;
}

