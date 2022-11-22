#pragma once

#include <QObject>
#include "megaapi.h"
#include <functional>

namespace mega
{
class QTMegaRequestListener : public QObject, public MegaRequestListener
{
	Q_OBJECT

public:
    QTMegaRequestListener(MegaApi *megaApi, MegaRequestListener *listener = NULL);
    virtual ~QTMegaRequestListener();

	//Request callbacks
	void onRequestStart(MegaApi* api, MegaRequest *request) override;
	void onRequestFinish(MegaApi* api, MegaRequest *request, MegaError* e) override;
    void onRequestUpdate(MegaApi* api, MegaRequest *request) override;
	void onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError* e) override;

protected:
    virtual void customEvent(QEvent * event);

	MegaRequestListener *listener;
    MegaApi *megaApi;
};

class OnFinishOneShot : public QTMegaRequestListener
{

public:
    OnFinishOneShot(MegaApi *megaApi, std::function<void(const MegaError&)>&& onFinishedFunc)
        : QTMegaRequestListener(megaApi, &consumer)
        , consumer(this, std::move(onFinishedFunc))
    {
    }

    struct QTEventConsumer : MegaRequestListener
    {
        QTEventConsumer(OnFinishOneShot* owner, std::function<void(const MegaError&)>&& fin)
            : oneShotOwner(owner)
            , onFinishedFunction(fin) {}

        OnFinishOneShot* oneShotOwner;
        std::function<void(const MegaError&)> onFinishedFunction;

	    void onRequestStart(MegaApi* api, MegaRequest *request) override {}
        void onRequestUpdate(MegaApi* api, MegaRequest *request) override {}
	    void onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError* e) override {}

	    void onRequestFinish(MegaApi* api, MegaRequest *request, MegaError* e) override {
            onFinishedFunction(*e);
            delete oneShotOwner;
        }
    };

    QTEventConsumer consumer;
};

}
