#ifdef ENABLE_SYNC

#include "QTMegaSyncListener.h"
#include "QTMegaEvent.h"

#include <QCoreApplication>

using namespace mega;
using namespace std;

QTMegaSyncListener::QTMegaSyncListener(MegaApi *megaApi, MegaSyncListener *listener) : QObject()
{
    this->megaApi = megaApi;
	this->listener = listener;
}

QTMegaSyncListener::~QTMegaSyncListener()
{
    this->listener = NULL;
    megaApi->removeSyncListener(this);
}

void QTMegaSyncListener::onSyncStateChanged(MegaApi *api, MegaSync *sync)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnSyncStateChanged);
    event->setSync(sync->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaSyncListener::onSyncFileStateChanged(MegaApi *api, MegaSync *sync, string *localPath, int newState)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnFileSyncStateChanged);
    event->setSync(sync->copy());
    event->setLocalPath(new string(*localPath));
    event->setNewState(newState);
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaSyncListener::onSyncAdded(MegaApi *api, MegaSync *sync, int additionState)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnSyncAdded);
    event->setSync(sync->copy());
    event->setNewState(additionState);
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaSyncListener::onSyncDisabled(MegaApi *api, MegaSync *sync)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnSyncDisabled);
    event->setSync(sync->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaSyncListener::onSyncEnabled(MegaApi *api, MegaSync *sync)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnSyncEnabled);
    event->setSync(sync->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaSyncListener::onSyncDeleted(MegaApi *api, MegaSync *sync)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnSyncDeleted);
    event->setSync(sync->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaSyncListener::customEvent(QEvent *e)
{
    QTMegaEvent *event = (QTMegaEvent *)e;
    switch(event->type())
    {
        case QTMegaEvent::OnSyncStateChanged:
            if(listener) listener->onSyncStateChanged(event->getMegaApi(), event->getSync());
            break;
        case QTMegaEvent::OnFileSyncStateChanged:
            if(listener) listener->onSyncFileStateChanged(event->getMegaApi(), event->getSync(), event->getLocalPath(), event->getNewState());
            break;
        default:
            break;
    }
}

#endif
