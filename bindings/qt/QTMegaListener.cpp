#include "QTMegaListener.h"
#include "QTMegaEvent.h"

#include <QCoreApplication>

using namespace mega;
using namespace std;

QTMegaListener::QTMegaListener(MegaApi *megaApi, MegaListener *listener) : QObject()
{
    this->megaApi = megaApi;
	this->listener = listener;
}

QTMegaListener::~QTMegaListener()
{
    this->listener = NULL;
    if (megaApi)
    {
        megaApi->removeListener(this);
    }
}

void QTMegaListener::onRequestStart(MegaApi *api, MegaRequest *request)
{
    if (request->getType() == MegaRequest::TYPE_DELETE)
    {
        megaApi = NULL;
    }

    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnRequestStart);
    event->setRequest(request->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaListener::onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnRequestFinish);
    event->setRequest(request->copy());
    event->setError(e->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaListener::onRequestUpdate(MegaApi *api, MegaRequest *request)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnRequestUpdate);
    event->setRequest(request->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaListener::onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError *e)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnRequestTemporaryError);
    event->setRequest(request->copy());
    event->setError(e->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaListener::onTransferStart(MegaApi *api, MegaTransfer *transfer)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnTransferStart);
    event->setTransfer(transfer->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaListener::onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *e)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnTransferFinish);
    event->setTransfer(transfer->copy());
    event->setError(e->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaListener::onTransferUpdate(MegaApi *api, MegaTransfer *transfer)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnTransferUpdate);
    event->setTransfer(transfer->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaListener::onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError *e)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnTransferTemporaryError);
    event->setTransfer(transfer->copy());
    event->setError(e->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaListener::onUsersUpdate(MegaApi *api, MegaUserList *users)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnUsersUpdate);
    event->setUsers(users ? users->copy() : NULL);
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaListener::onUserAlertsUpdate(MegaApi *api, MegaUserAlertList *alerts)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnUserAlertsUpdate);
    event->setUserAlerts(alerts ? alerts->copy() : NULL);
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaListener::onNodesUpdate(MegaApi *api, MegaNodeList *nodes)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnNodesUpdate);
    event->setNodes(nodes ? nodes->copy() : NULL);
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaListener::onAccountUpdate(MegaApi *api)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnAccountUpdate);
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaListener::onReloadNeeded(MegaApi *api)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnReloadNeeded);
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaListener::onEvent(MegaApi *api, MegaEvent *e)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnEvent);
    event->setEvent(e->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

#ifdef ENABLE_SYNC
void QTMegaListener::onSyncStateChanged(MegaApi *api, MegaSync *sync)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnSyncStateChanged);
    event->setSync(sync->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaListener::onSyncEvent(MegaApi *api, MegaSync *sync, MegaSyncEvent *event)
{
    QTMegaEvent *ev = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnSyncEvent);

    ev->setSync(sync->copy());
    ev->setSyncEvent(event->copy());

    QCoreApplication::postEvent(this, ev, INT_MIN);
}

void QTMegaListener::onSyncFileStateChanged(MegaApi *api, MegaSync *sync, string *localPath, int newState)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnFileSyncStateChanged);
    event->setSync(sync->copy());
    event->setLocalPath(new string(*localPath));
    event->setNewState(newState);
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaListener::onGlobalSyncStateChanged(MegaApi *api)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnGlobalSyncStateChanged);
    QCoreApplication::postEvent(this, event, INT_MIN);
}
#endif

void QTMegaListener::customEvent(QEvent *e)
{
    QTMegaEvent *event = (QTMegaEvent *)e;
    switch(event->type())
    {
        case QTMegaEvent::OnRequestStart:
            if(listener) listener->onRequestStart(event->getMegaApi(), event->getRequest());
            break;
        case QTMegaEvent::OnRequestUpdate:
            if(listener) listener->onRequestUpdate(event->getMegaApi(), event->getRequest());
            break;
        case QTMegaEvent::OnRequestFinish:
            if(listener) listener->onRequestFinish(event->getMegaApi(), event->getRequest(), event->getError());
            break;
        case QTMegaEvent::OnRequestTemporaryError:
            if(listener) listener->onRequestTemporaryError(event->getMegaApi(), event->getRequest(), event->getError());
            break;
        case QTMegaEvent::OnTransferStart:
            if(listener) listener->onTransferStart(event->getMegaApi(), event->getTransfer());
            break;
        case QTMegaEvent::OnTransferTemporaryError:
            if(listener) listener->onTransferTemporaryError(event->getMegaApi(), event->getTransfer(), event->getError());
            break;
        case QTMegaEvent::OnTransferUpdate:
            if(listener) listener->onTransferUpdate(event->getMegaApi(), event->getTransfer());
            break;
        case QTMegaEvent::OnTransferFinish:
            if(listener) listener->onTransferFinish(event->getMegaApi(), event->getTransfer(), event->getError());
            break;
        case QTMegaEvent::OnUsersUpdate:
            if(listener) listener->onUsersUpdate(event->getMegaApi(), event->getUsers());
            break;
        case QTMegaEvent::OnUserAlertsUpdate:
            if(listener) listener->onUserAlertsUpdate(event->getMegaApi(), event->getUserAlerts());
            break;
        case QTMegaEvent::OnNodesUpdate:
            if(listener) listener->onNodesUpdate(event->getMegaApi(), event->getNodes());
            break;
        case QTMegaEvent::OnAccountUpdate:
            if(listener) listener->onAccountUpdate(event->getMegaApi());
            break;
        case QTMegaEvent::OnReloadNeeded:
            if(listener) listener->onReloadNeeded(event->getMegaApi());
            break;
        case QTMegaEvent::OnEvent:
            if(listener) listener->onEvent(event->getMegaApi(), event->getEvent());
            break;
#if ENABLE_SYNC
        case QTMegaEvent::OnSyncStateChanged:
            if(listener) listener->onSyncStateChanged(event->getMegaApi(), event->getSync());
            break;
        case QTMegaEvent::OnSyncEvent:
            if (listener) listener->onSyncEvent(event->getMegaApi(), event->getSync(), event->getSyncEvent());
            break;
        case QTMegaEvent::OnFileSyncStateChanged:
            if(listener) listener->onSyncFileStateChanged(event->getMegaApi(), event->getSync(), event->getLocalPath(), event->getNewState());
            break;
        case QTMegaEvent::OnGlobalSyncStateChanged:
            if(listener) listener->onGlobalSyncStateChanged(event->getMegaApi());
            break;
#endif
        default:
            break;
    }
}
