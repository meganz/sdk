#ifndef QTMEGATRANSFERLISTENER_H
#define QTMEGATRANSFERLISTENER_H

#include <QObject>
#include <megaapi.h>

namespace mega
{
class QTMegaTransferListener : public QObject, public MegaTransferListener
{
	Q_OBJECT

public:
    QTMegaTransferListener(MegaApi *megaApi,MegaTransferListener *listener);
    virtual ~QTMegaTransferListener();

public:
	virtual void onTransferStart(MegaApi *api, MegaTransfer *transfer);
	virtual void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* e);
	virtual void onTransferUpdate(MegaApi *api, MegaTransfer *transfer);
	virtual void onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError* e);

protected:
    virtual void customEvent(QEvent * event);

    MegaApi *megaApi;
	MegaTransferListener *listener;
};
}

#endif // QTMEGATRANSFERLISTENER_H
