#include "synchronousrequestlistener.h"

SynchronousRequestListener::SynchronousRequestListener()
{
    semaphore = new MegaSemaphore();
}
SynchronousRequestListener::~SynchronousRequestListener()
{
    delete semaphore;
    if (megaRequest) delete megaRequest;
    if (megaError) delete megaError;
}

void SynchronousRequestListener::onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *error)
{
    this->megaApi = api;
    if (megaRequest) delete megaRequest; //in case of reused listener
    this->megaRequest = request->copy();
    if (megaError) delete megaError; //in case of reused listener
    this->megaError = error->copy();

    doOnRequestFinish(api,request,error);
    semaphore->release();
}

void SynchronousRequestListener::wait()
{
    semaphore->wait();
}

int SynchronousRequestListener::trywait(int milliseconds)
{
    return semaphore->timedwait(milliseconds);
}

MegaRequest *SynchronousRequestListener::getRequest() const
{
    return megaRequest;
}

MegaApi *SynchronousRequestListener::getApi() const
{
    return megaApi;
}

MegaError *SynchronousRequestListener::getError() const
{
    return megaError;
}



SynchronousTransferListener::SynchronousTransferListener()
{
    semaphore = new MegaSemaphore();
}
SynchronousTransferListener::~SynchronousTransferListener()
{
    delete semaphore;
    if (megaTransfer) delete megaTransfer;
    if (megaError) delete megaError;
}

void SynchronousTransferListener::onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *error)
{
    this->megaApi = api;
    if (megaTransfer) delete megaTransfer; //in case of reused listener
    this->megaTransfer = transfer->copy();
    if (megaError) delete megaError; //in case of reused listener
    this->megaError = error->copy();

    doOnTransferFinish(api,transfer,error);
    semaphore->release();
}

void SynchronousTransferListener::wait()
{
    semaphore->wait();
}

int SynchronousTransferListener::trywait(int milliseconds)
{
    return semaphore->timedwait(milliseconds);
}

MegaTransfer *SynchronousTransferListener::getTransfer() const
{
    return megaTransfer;
}

MegaApi *SynchronousTransferListener::getApi() const
{
    return megaApi;
}

MegaError *SynchronousTransferListener::getError() const
{
    return megaError;
}
