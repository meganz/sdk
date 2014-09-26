#ifndef QTMEGALISTENER_H
#define QTMEGALISTENER_H

#include <QObject>
#include "megaapi.h"

namespace mega
{
class QTMegaListener : public QObject, public MegaListener
{
	Q_OBJECT

public:
    explicit QTMegaListener(MegaApi *megaApi, MegaListener *parent=NULL);
    virtual ~QTMegaListener();

	virtual void onRequestStart(MegaApi* api, MegaRequest *request);
	virtual void onRequestFinish(MegaApi* api, MegaRequest *request, MegaError* e);
    virtual void onRequestUpdate(MegaApi* api, MegaRequest *request);
	virtual void onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError* e);
	virtual void onTransferStart(MegaApi *api, MegaTransfer *transfer);
	virtual void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* e);
	virtual void onTransferUpdate(MegaApi *api, MegaTransfer *transfer);
	virtual void onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError* e);
	virtual void onUsersUpdate(MegaApi* api, UserList *users);
	virtual void onNodesUpdate(MegaApi* api, NodeList *nodes);
	virtual void onReloadNeeded(MegaApi* api);
    virtual void onSyncStateChanged(MegaApi* api);
    virtual void onSyncFileStateChanged(MegaApi *api, const char *filePath, int newState);

protected:
    virtual void customEvent(QEvent * event);

    MegaApi *megaApi;
	MegaListener *listener;
};
}

#endif // QTMEGALISTENER_H
