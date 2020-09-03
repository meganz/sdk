#pragma once

#ifdef ENABLE_SYNC

#include <QObject>
#include "megaapi.h"

namespace mega
{
class QTMegaSyncListener : public QObject, public MegaSyncListener
{
	Q_OBJECT

public:
    explicit QTMegaSyncListener(MegaApi *megaApi, MegaSyncListener *parent=NULL);
    virtual ~QTMegaSyncListener();

    virtual void onSyncStateChanged(MegaApi *api,  MegaSync *sync);
    virtual void onSyncFileStateChanged(MegaApi *api, MegaSync *sync, std::string *localPath, int newState);
    virtual void onSyncAdded(MegaApi *api,  MegaSync *sync, int additionState);
    virtual void onSyncDisabled(MegaApi *api,  MegaSync *sync);
    virtual void onSyncEnabled(MegaApi *api,  MegaSync *sync);
    virtual void onSyncDeleted(MegaApi *api,  MegaSync *sync);

protected:
    virtual void customEvent(QEvent * event);

    MegaApi *megaApi;
    MegaSyncListener *listener;
};
}

#endif //ENABLE_SYNC
