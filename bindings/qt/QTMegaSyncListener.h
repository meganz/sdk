#ifndef QTMEGALISTENER_H
#define QTMEGALISTENER_H

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

protected:
    virtual void customEvent(QEvent * event);

    MegaApi *megaApi;
    MegaSyncListener *listener;
};
}

#endif //ENABLE_SYNC

#endif // QTMEGALISTENER_H
