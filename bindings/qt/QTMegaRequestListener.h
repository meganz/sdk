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
#include <functional>
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

class OnFinishOneShot : public QTMegaRequestListener
{

public:
    OnFinishOneShot(MegaApi *megaApi, QPointer<const QObject> context,
                    std::function<void(bool isContextValid, const MegaRequest&, const MegaError&)>&& onFinishedFunc)
        : QTMegaRequestListener(megaApi, &consumer)
        , consumer(this, context, std::move(onFinishedFunc))
    {
    }

    OnFinishOneShot(MegaApi *megaApi,
                    std::function<void(bool isContextValid, const MegaRequest&, const MegaError&)>&& onFinishedFunc)
        : QTMegaRequestListener(megaApi, &consumer)
        , consumer(this, nullptr, std::move(onFinishedFunc))
    {
    }

    ~OnFinishOneShot(){};

    struct QTEventConsumer : MegaRequestListener
    {
        QTEventConsumer(OnFinishOneShot* owner, QPointer<const QObject> context,
                        std::function<void(bool isContextValid, const MegaRequest&, const MegaError&)>&& fin)
            : oneShotContext(context),
              oneShotOwner(owner),
              onFinishedFunction(fin) {}

        QPointer<const QObject> oneShotContext;
        OnFinishOneShot* oneShotOwner;
        std::function<void(bool isContextValid, const MegaRequest&, const MegaError&)> onFinishedFunction;

        void onRequestStart(MegaApi*, MegaRequest*) override {}
        void onRequestUpdate(MegaApi*, MegaRequest*) override {}
        void onRequestTemporaryError(MegaApi*, MegaRequest*, MegaError*) override {}

        void onRequestFinish(MegaApi*, MegaRequest *request, MegaError* e) override {
            onFinishedFunction(oneShotContext != nullptr, *request, *e);
            delete oneShotOwner;
        }
    };

    QTEventConsumer consumer;
};

}
