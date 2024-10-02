#include "QTMegaTransferListener.h"

#include "QTMegaApiManager.h"
#include "QTMegaEvent.h"

#include <QCoreApplication>

using namespace mega;

struct QtMegaFolderEvent : public QTMegaEvent
{
    QtMegaFolderEvent(MegaApi* megaApi, Type type) :QTMegaEvent(megaApi, type) {}
    int stage;
    uint32_t foldercount;
    uint32_t createdfoldercount;
    uint32_t filecount;
};

QTMegaTransferListener::QTMegaTransferListener(MegaApi* megaApi, MegaTransferListener* listener):
    QObject()
{
    this->megaApi = megaApi;
    this->listener = listener;
}

QTMegaTransferListener::~QTMegaTransferListener()
{
    this->listener = NULL;
    if (QTMegaApiManager::isMegaApiValid(megaApi))
    {
        megaApi->removeTransferListener(this);
    }
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

void mega::QTMegaTransferListener::onFolderTransferUpdate(mega::MegaApi* api, mega::MegaTransfer* transfer, int stage, uint32_t foldercount, uint32_t createdfoldercount, uint32_t filecount, const char*, const char*)
{
    QtMegaFolderEvent* event = new QtMegaFolderEvent(api, (QEvent::Type)QTMegaEvent::OnTransferFolderUpdate);
    event->setTransfer(transfer->copy());
    event->stage = stage;
    event->foldercount = foldercount;
    event->createdfoldercount = createdfoldercount;
    event->filecount = filecount;
    QCoreApplication::postEvent(this, event, INT_MIN);
}

void QTMegaTransferListener::customEvent(QEvent *e)
{
    QTMegaEvent *event = (QTMegaEvent *)e;
    switch(QTMegaEvent::MegaType(event->type()))
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
        case QTMegaEvent::OnTransferFolderUpdate:
            if (listener)
            {
                if (auto folderEvent = dynamic_cast<QtMegaFolderEvent*>(e))
                {
                    listener->onFolderTransferUpdate(folderEvent->getMegaApi(), folderEvent->getTransfer(), folderEvent->stage, folderEvent->foldercount, folderEvent->createdfoldercount, folderEvent->filecount, nullptr, nullptr);
                }
            }
            break;
        case QTMegaEvent::OnTransferFinish:
            if(listener) listener->onTransferFinish(event->getMegaApi(), event->getTransfer(), event->getError());
            break;
        default:
            break;
    }
}
