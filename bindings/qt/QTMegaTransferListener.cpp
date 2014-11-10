#include "QTMegaTransferListener.h"
#include <QCoreApplication>
#include "QTMegaEvent.h"

using namespace mega;

QTMegaTransferListener::QTMegaTransferListener(MegaApi *megaApi, MegaTransferListener *listener) : QObject()
{
    this->megaApi = megaApi;
    this->listener = listener;
}

QTMegaTransferListener::~QTMegaTransferListener()
{
    this->listener = NULL;
    megaApi->removeTransferListener(this);
}


void QTMegaTransferListener::onTransferStart(MegaApi *api, MegaTransfer *transfer)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnTransferStart);
    event->setTransfer(transfer->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaTransferListener::onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *e)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnTransferFinish);
    event->setTransfer(transfer->copy());
    event->setError(e->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaTransferListener::onTransferUpdate(MegaApi *api, MegaTransfer *transfer)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnTransferUpdate);
    event->setTransfer(transfer->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaTransferListener::onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError *e)
{
    QTMegaEvent *event = new QTMegaEvent(api, (QEvent::Type)QTMegaEvent::OnTransferTemporaryError);
    event->setTransfer(transfer->copy());
    event->setError(e->copy());
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaTransferListener::customEvent(QEvent *e)
{
    QTMegaEvent *event = (QTMegaEvent *)e;
    switch(event->type())
    {
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
        default:
            break;
    }
}
