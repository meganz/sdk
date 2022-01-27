#pragma once

#include <QObject>
#include "megaapi.h"

namespace mega
{
class QTMegaListener : public MegaListener
{
public:
    explicit QTMegaListener(QObject& eventPostTarget);
    virtual ~QTMegaListener();

	void onRequestStart(MegaApi* api, MegaRequest *request) override;
	void onRequestFinish(MegaApi* api, MegaRequest *request, MegaError* e) override;
    void onRequestUpdate(MegaApi* api, MegaRequest *request) override;
	void onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError* e) override;
	void onTransferStart(MegaApi *api, MegaTransfer *transfer) override;
	void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* e) override;
	void onTransferUpdate(MegaApi *api, MegaTransfer *transfer) override;
	void onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError* e) override;
    void onUsersUpdate(MegaApi* api, MegaUserList *users) override;
    void onUserAlertsUpdate(MegaApi* api, MegaUserAlertList *alerts) override;
    void onNodesUpdate(MegaApi* api, MegaNodeList *nodes) override;
    void onAccountUpdate(MegaApi* api) override;
	void onReloadNeeded(MegaApi* api) override;
    void onEvent(MegaApi* api, MegaEvent *e) override;

#ifdef ENABLE_SYNC
    void onSyncStateChanged(MegaApi *api,  MegaSync *sync) override;
    void onSyncFileStateChanged(MegaApi *api, MegaSync *sync, std::string *localPath, int newState) override;
    void onSyncAdded(MegaApi *api,  MegaSync *sync) override;
    void onSyncDeleted(MegaApi *api,  MegaSync *sync) override;
    void onGlobalSyncStateChanged(MegaApi* api) override;
#endif

protected:
    QObject& mEventPostTarget;
};
}
