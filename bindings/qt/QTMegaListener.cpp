#include "QTMegaListener.h"
#include "QTMegaEvent.h"

#include <QCoreApplication>

namespace mega
{

using namespace std;

QTMegaListener::QTMegaListener(QObject& eventPostTarget) : mEventPostTarget(eventPostTarget)
{
}

QTMegaListener::~QTMegaListener()
{
}

void QTMegaListener::onRequestStart(MegaApi *api, MegaRequest *request)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnRequestStart);
    event->setRequest(request->copy());
    QCoreApplication::postEvent(&mEventPostTarget, event, INT_MIN);
}

void QTMegaListener::onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnRequestFinish);
    event->setRequest(request->copy());
    event->setError(e->copy());
    QCoreApplication::postEvent(&mEventPostTarget, event, INT_MIN);
}

void QTMegaListener::onRequestUpdate(MegaApi *api, MegaRequest *request)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnRequestUpdate);
    event->setRequest(request->copy());
    QCoreApplication::postEvent(&mEventPostTarget, event, INT_MIN);
}

void QTMegaListener::onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError *e)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnRequestTemporaryError);
    event->setRequest(request->copy());
    event->setError(e->copy());
    QCoreApplication::postEvent(&mEventPostTarget, event, INT_MIN);
}

void QTMegaListener::onTransferStart(MegaApi *api, MegaTransfer *transfer)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnTransferStart);
    event->setTransfer(transfer->copy());
    QCoreApplication::postEvent(&mEventPostTarget, event, INT_MIN);
}

void QTMegaListener::onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *e)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnTransferFinish);
    event->setTransfer(transfer->copy());
    event->setError(e->copy());
    QCoreApplication::postEvent(&mEventPostTarget, event, INT_MIN);
}

void QTMegaListener::onTransferUpdate(MegaApi *api, MegaTransfer *transfer)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnTransferUpdate);
    event->setTransfer(transfer->copy());
    QCoreApplication::postEvent(&mEventPostTarget, event, INT_MIN);
}

void QTMegaListener::onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError *e)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnTransferTemporaryError);
    event->setTransfer(transfer->copy());
    event->setError(e->copy());
    QCoreApplication::postEvent(&mEventPostTarget, event, INT_MIN);
}

void QTMegaListener::onUsersUpdate(MegaApi *api, MegaUserList *users)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnUsersUpdate);
    event->setUsers(users ? users->copy() : NULL);
    QCoreApplication::postEvent(&mEventPostTarget, event, INT_MIN);
}

void QTMegaListener::onUserAlertsUpdate(MegaApi *api, MegaUserAlertList *alerts)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnUserAlertsUpdate);
    event->setUserAlerts(alerts ? alerts->copy() : NULL);
    QCoreApplication::postEvent(&mEventPostTarget, event, INT_MIN);
}

void QTMegaListener::onNodesUpdate(MegaApi *api, MegaNodeList *nodes)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnNodesUpdate);
    event->setNodes(nodes ? nodes->copy() : NULL);
    QCoreApplication::postEvent(&mEventPostTarget, event, INT_MIN);
}

void QTMegaListener::onAccountUpdate(MegaApi *api)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnAccountUpdate);
    QCoreApplication::postEvent(&mEventPostTarget, event, INT_MIN);
}

void QTMegaListener::onReloadNeeded(MegaApi *api)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnReloadNeeded);
    QCoreApplication::postEvent(&mEventPostTarget, event, INT_MIN);
}

void QTMegaListener::onEvent(MegaApi *api, MegaEvent *e)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnEvent);
    event->setEvent(e->copy());
    QCoreApplication::postEvent(&mEventPostTarget, event, INT_MIN);
}

#ifdef ENABLE_SYNC
void QTMegaListener::onSyncStateChanged(MegaApi *api, MegaSync *sync)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnSyncStateChanged);
    event->setSync(sync->copy());
    QCoreApplication::postEvent(&mEventPostTarget, event, INT_MIN);
}

void QTMegaListener::onSyncFileStateChanged(MegaApi *api, MegaSync *sync, string *localPath, int newState)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnFileSyncStateChanged);
    event->setSync(sync->copy());
    event->setLocalPath(new string(*localPath));
    event->setNewState(newState);
    QCoreApplication::postEvent(&mEventPostTarget, event, INT_MIN);
}

void QTMegaListener::onSyncAdded(MegaApi *api, MegaSync *sync)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnSyncAdded);
    event->setSync(sync->copy());
    QCoreApplication::postEvent(&mEventPostTarget, event, INT_MIN);
}

void QTMegaListener::onSyncDeleted(MegaApi *api, MegaSync *sync)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnSyncDeleted);
    event->setSync(sync->copy());
    QCoreApplication::postEvent(&mEventPostTarget, event, INT_MIN);
}

void QTMegaListener::onGlobalSyncStateChanged(MegaApi *api)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnGlobalSyncStateChanged);
    QCoreApplication::postEvent(&mEventPostTarget, event, INT_MIN);
}

#endif

}

