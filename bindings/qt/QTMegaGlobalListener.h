#pragma once

#include <QObject>
#include "megaapi.h"

namespace mega
{
class QTMegaGlobalListener : public QObject, public MegaGlobalListener
{
    Q_OBJECT

public:
    explicit QTMegaGlobalListener(MegaApi *megaApi, MegaGlobalListener *parent=NULL);
    ~QTMegaGlobalListener() override;

    void onUsersUpdate(MegaApi* api, MegaUserList *users) override;
    void onUserAlertsUpdate(MegaApi* api, MegaUserAlertList *alerts) override;
    void onNodesUpdate(MegaApi* api, MegaNodeList *nodes) override;
    void onAccountUpdate(MegaApi* api) override;
    void onReloadNeeded(MegaApi* api) override;
    void onEvent(MegaApi* api, MegaEvent *e) override;

#ifdef ENABLE_SYNC
    void onGlobalSyncStateChanged(MegaApi* api) override;
#endif

protected:
    void customEvent(QEvent * event) override;

    MegaApi *megaApi;
    MegaGlobalListener *listener;
};
}
