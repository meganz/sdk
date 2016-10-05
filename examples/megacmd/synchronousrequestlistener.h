/**
 * @file examples/megacmd/synchronousrequestlistener.h
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

#ifndef SYNCHRONOUSREQUESTLISTENER_H
#define SYNCHRONOUSREQUESTLISTENER_H

//#include "megacmd.h"
//#include "megaapi.h"
#include "megaapi_impl.h"

using namespace mega;

/**
 * @brief This abstract class extendes the functionality of MegaRequestListener
 * allowing a synchronous beheviour
 * A virtual method is declared and should be implemented: doOnRequestFinish
 * when onRequestFinish is called by the SDK.
 * A client for this listener may wait() until the request is finished and doOnRequestFinish is completed.
 *
 * @see MegaRequestListener
 */
class SynchronousRequestListener : public MegaRequestListener //TODO: move to somewhere else within the sdk
{
private:
    MegaSemaphore* semaphore;
protected:
    MegaRequestListener *listener = NULL;
    MegaApi *megaApi = NULL;
    MegaRequest *megaRequest = NULL;
    MegaError *megaError = NULL;

public:
    SynchronousRequestListener();
    virtual ~SynchronousRequestListener();
    virtual void doOnRequestFinish(MegaApi *api, MegaRequest *request, MegaError *error) = 0;

    void onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *error);

    void wait();

    int trywait(int milliseconds);

    MegaError *getError() const;
    MegaRequest *getRequest() const;
    MegaApi *getApi() const;
};


/**
 * TODO
 * @see MegaTransferListener
 */
class SynchronousTransferListener : public MegaTransferListener //TODO: move to somewhere else within the sdk
{
private:
    MegaSemaphore* semaphore;
protected:
    MegaTransferListener *listener = NULL;
    MegaApi *megaApi = NULL;
    MegaTransfer *megaTransfer = NULL;
    MegaError *megaError = NULL;

public:
    SynchronousTransferListener();
    virtual ~SynchronousTransferListener();
    virtual void doOnTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *error) = 0;

    void onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *error);

    void wait();

    int trywait(int milliseconds);

    MegaError *getError() const;
    MegaTransfer *getTransfer() const;
    MegaApi *getApi() const;
};

#endif // SYNCHRONOUSREQUESTLISTENER_H
