#ifndef QTMEGAGLOBALLISTENER_H
#define QTMEGAGLOBALLISTENER_H

#include <QObject>
#include "megaapi.h"

namespace mega
{
class QTMegaGlobalListener : public QObject, public MegaGlobalListener
{
    Q_OBJECT

public:
    explicit QTMegaGlobalListener(MegaApi *megaApi, MegaGlobalListener *parent=NULL);
    virtual ~QTMegaGlobalListener();

    virtual void onUsersUpdate(MegaApi* api, MegaUserList *users);
    virtual void onNodesUpdate(MegaApi* api, MegaNodeList *nodes);
    virtual void onAccountUpdate(MegaApi* api);
    virtual void onReloadNeeded(MegaApi* api);
    virtual void onEvent(MegaApi* api, MegaEvent *e);

protected:
    virtual void customEvent(QEvent * event);

    MegaApi *megaApi;
    MegaGlobalListener *listener;
};
}

#endif // QTMEGAGLOBALLISTENER_H
