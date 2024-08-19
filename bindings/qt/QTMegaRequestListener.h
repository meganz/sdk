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

#include <QPointer>

namespace mega
{
class QTMegaRequestListener : public QObject, public MegaRequestListener
{
    Q_OBJECT

public:
    QTMegaRequestListener(MegaApi *megaApi, MegaRequestListener *listener = NULL);
    ~QTMegaRequestListener() override;

    //Request callbacks
    void onRequestStart(MegaApi* api, MegaRequest *request) override;
    void onRequestFinish(MegaApi* api, MegaRequest *request, MegaError* e) override;
    void onRequestUpdate(MegaApi* api, MegaRequest *request) override;
    void onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError* e) override;

protected:
    virtual void customEvent(QEvent * event) override;

    MegaRequestListener *listener;
    MegaApi *megaApi;
};
}
