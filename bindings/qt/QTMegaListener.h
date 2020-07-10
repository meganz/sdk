#pragma once

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
    virtual void onUsersUpdate(MegaApi* api, MegaUserList *users);
    virtual void onUserAlertsUpdate(MegaApi* api, MegaUserAlertList *alerts);
    virtual void onNodesUpdate(MegaApi* api, MegaNodeList *nodes);
    virtual void onAccountUpdate(MegaApi* api);
	virtual void onReloadNeeded(MegaApi* api);
    virtual void onEvent(MegaApi* api, MegaEvent *e);

#ifdef ENABLE_SYNC
    virtual void onSyncStateChanged(MegaApi *api,  MegaSync *sync);
    virtual void onSyncEvent(MegaApi *api, MegaSync *sync, MegaSyncEvent *event);
    virtual void onSyncFileStateChanged(MegaApi *api, MegaSync *sync, std::string *localPath, int newState);
    virtual void onGlobalSyncStateChanged(MegaApi* api);
#endif

protected:
    virtual void customEvent(QEvent * event);

    MegaApi *megaApi;
	MegaListener *listener;
};
}
