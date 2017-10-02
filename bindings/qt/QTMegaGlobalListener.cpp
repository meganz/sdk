#include "QTMegaGlobalListener.h"
#include "QTMegaEvent.h"

#include <QCoreApplication>

using namespace mega;

QTMegaGlobalListener::QTMegaGlobalListener(MegaApi *megaApi, MegaGlobalListener *listener) : QObject()
{
    this->megaApi = megaApi;
    this->listener = listener;
}

QTMegaGlobalListener::~QTMegaGlobalListener()
{
    this->listener = NULL;
    megaApi->removeGlobalListener(this);
}

void QTMegaGlobalListener::onUsersUpdate(MegaApi *api, MegaUserList *users)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnUsersUpdate);
    event->setUsers(users ? users->copy() : NULL);
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

void QTMegaGlobalListener::onReloadNeeded(MegaApi *api)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnReloadNeeded);
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaGlobalListener::onEvent(MegaApi *api, MegaEvent *e)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnEvent);
    event->setEvent(e->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaGlobalListener::customEvent(QEvent *e)
{
    QTMegaEvent *event = (QTMegaEvent *)e;
    switch(event->type())
    {
        case QTMegaEvent::OnUsersUpdate:
            if(listener) listener->onUsersUpdate(event->getMegaApi(), event->getUsers());
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
        default:
            break;
    }
}
