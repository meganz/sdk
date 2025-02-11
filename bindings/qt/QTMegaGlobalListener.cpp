#include "QTMegaGlobalListener.h"

#include "QTMegaApiManager.h"
#include "QTMegaEvent.h"

#include <QCoreApplication>

using namespace mega;

QTMegaGlobalListener::QTMegaGlobalListener(MegaApi* megaApi, MegaGlobalListener* listener):
    QObject()
{
    this->megaApi = megaApi;
    this->listener = listener;
}

QTMegaGlobalListener::~QTMegaGlobalListener()
{
    this->listener = NULL;
    if (QTMegaApiManager::isMegaApiValid(megaApi))
    {
        megaApi->removeGlobalListener(this);
    }
}

void QTMegaGlobalListener::onUsersUpdate(MegaApi *api, MegaUserList *users)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnUsersUpdate);
    event->setUsers(users ? users->copy() : NULL);
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaGlobalListener::onUserAlertsUpdate(MegaApi *api, MegaUserAlertList *alerts)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnUserAlertsUpdate);
    event->setUserAlerts(alerts ? alerts->copy() : NULL);
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaGlobalListener::onNodesUpdate(MegaApi *api, MegaNodeList *nodes)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnNodesUpdate);
    event->setNodes(nodes ? nodes->copy() : NULL);
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaGlobalListener::onAccountUpdate(MegaApi *api)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnAccountUpdate);
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaGlobalListener::onEvent(MegaApi *api, MegaEvent *e)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnEvent);
    event->setEvent(e->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaGlobalListener::onGlobalSyncStateChanged(MegaApi *api)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnGlobalSyncStateChanged);
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaGlobalListener::customEvent(QEvent *e)
{
    QTMegaEvent *event = (QTMegaEvent *)e;
    switch(QTMegaEvent::MegaType(event->type()))
    {
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
        case QTMegaEvent::OnEvent:
            if(listener) listener->onEvent(event->getMegaApi(), event->getEvent());
            break;
        case QTMegaEvent::OnGlobalSyncStateChanged:
            if(listener) listener->onGlobalSyncStateChanged(event->getMegaApi());
            break;
        default:
            break;
    }
}
