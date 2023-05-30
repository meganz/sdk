#pragma once

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#include <QObject>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include <megaapi.h>

namespace mega
{
class QTMegaTransferListener : public QObject, public MegaTransferListener
{
	Q_OBJECT

public:
    QTMegaTransferListener(MegaApi *megaApi,MegaTransferListener *listener);
    ~QTMegaTransferListener() override;

public:
    void onTransferStart(MegaApi *api, MegaTransfer *transfer) override;
    void onTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* e) override;
    void onTransferUpdate(MegaApi *api, MegaTransfer *transfer) override;
    void onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError* e) override;

protected:
    void customEvent(QEvent * event) override;

    MegaApi *megaApi;
	MegaTransferListener *listener;
};
}
