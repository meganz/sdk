#include "QTMegaRequestListener.h"

#include "QTMegaApiManager.h"
#include "QTMegaEvent.h"

#include <QCoreApplication>

using namespace mega;

QTMegaRequestListener::QTMegaRequestListener(MegaApi* megaApi, MegaRequestListener* listener):
    QObject()
{
    this->megaApi = megaApi;
	this->listener = listener;
}

QTMegaRequestListener::~QTMegaRequestListener()
{
    this->listener = NULL;
    if (QTMegaApiManager::isMegaApiValid(megaApi))
    {
        megaApi->removeRequestListener(this);
    }
}

void QTMegaRequestListener::onRequestStart(MegaApi *api, MegaRequest *request)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnRequestStart);
    event->setRequest(request->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaRequestListener::onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnRequestFinish);
    event->setRequest(request->copy());
    event->setError(e->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaRequestListener::onRequestUpdate(MegaApi *api, MegaRequest *request)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnRequestUpdate);
    event->setRequest(request->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaRequestListener::onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError *e)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnRequestTemporaryError);
    event->setRequest(request->copy());
    event->setError(e->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaRequestListener::customEvent(QEvent *e)
{
    QTMegaEvent *event = (QTMegaEvent *)e;
    switch(QTMegaEvent::MegaType(event->type()))
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
        default:
            break;
    }
}
