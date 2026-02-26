#pragma once

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#include <QObject>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include "megaapi.h"
#include "QTMegaEvent.h"

namespace mega
{

class QTMegaListener : public QObject, public MegaListener
{
	Q_OBJECT

public:
    explicit QTMegaListener(MegaApi* megaApi, MegaListener* parent = NULL);
    ~QTMegaListener() override;

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
    void onReloadNeeded(MegaApi* api);
    void onEvent(MegaApi* api, MegaEvent *e) override;

#ifdef ENABLE_SYNC
    void onSyncStateChanged(MegaApi *api,  MegaSync *sync) override;
    void onSyncStatsUpdated(MegaApi *api,  MegaSyncStats *stats) override;
    void onSyncFileStateChanged(MegaApi *api, MegaSync *sync, std::string *localPath, int newState) override;
    void onSyncAdded(MegaApi *api,  MegaSync *sync) override;
    void onSyncDeleted(MegaApi *api,  MegaSync *sync) override;
    void onGlobalSyncStateChanged(MegaApi* api) override;
    void onSyncRemoteRootChanged(MegaApi* api, MegaSync* sync) override;
#endif

    void onMountAdded(MegaApi* api, const char* path, int result) override;
    void onMountChanged(MegaApi* api, const char* path, int result) override;
    void onMountDisabled(MegaApi* api, const char* path, int result) override;
    void onMountEnabled(MegaApi* api, const char* path, int result) override;
    void onMountRemoved(MegaApi* api, const char* path, int result) override;

protected:
    void customEvent(QEvent * event) override;

    using FuseEventHandler =
      void (MegaListener::*)(MegaApi*, const char*, int);

    void onMountEvent(FuseEventHandler handler, const QTMegaEvent& event);

    void postMountEvent(QTMegaEvent::MegaType eventType,
                        MegaApi *api,
                        const std::string& path,
                        int result);

    MegaApi* megaApi;
    MegaListener *listener;
};
}
