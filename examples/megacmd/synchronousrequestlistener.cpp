/**
 * @file examples/megacmd/megacmd.cpp
 * @brief MegaCMD: synchronous request listener
 *
 * (c) 2013-2016 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#include "synchronousrequestlistener.h"

SynchronousRequestListener::SynchronousRequestListener()
{
    semaphore = new MegaSemaphore();
}
SynchronousRequestListener::~SynchronousRequestListener()
{
    delete semaphore;
    if (megaRequest)
    {
        delete megaRequest;
    }
    if (megaError)
    {
        delete megaError;
    }
}

void SynchronousRequestListener::onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *error)
{
    this->megaApi = api;
    if (megaRequest)
    {
        delete megaRequest;              //in case of reused listener
    }
    this->megaRequest = request->copy();
    if (megaError)
    {
        delete megaError;            //in case of reused listener
    }
    this->megaError = error->copy();

    doOnRequestFinish(api, request, error);
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
    if (megaTransfer)
    {
        delete megaTransfer;
    }
    if (megaError)
    {
        delete megaError;
    }
}

void SynchronousTransferListener::onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *error)
{
    this->megaApi = api;
    if (megaTransfer)
    {
        delete megaTransfer;               //in case of reused listener
    }
    this->megaTransfer = transfer->copy();
    if (megaError)
    {
        delete megaError;            //in case of reused listener
    }
    this->megaError = error->copy();

    doOnTransferFinish(api, transfer, error);
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
