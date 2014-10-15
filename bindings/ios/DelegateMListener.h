//
//  DelegateMListener.h
//
//  Created by Javier Navarro on 08/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MListenerDelegate.h"
#import "MTransfer.h"
#import "MRequest.h"
#import "MError.h"
#import "megaapi.h"
#import "MegaSDK.h"

using namespace mega;

class DelegateMListener : public MegaListener {

public:
    
    DelegateMListener(MegaSDK *megaSDK, void *listener);
    void *getUserListener();
    
    void onRequestStart(MegaApi *api, MegaRequest *request);
    void onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e);
    void onRequestUpdate(MegaApi *api, MegaRequest *request);
    void onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError *e);
    void onTransferStart(MegaApi *api, MegaTransfer *transfer);
    void onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *e);
    void onTransferUpdate(MegaApi *api, MegaTransfer *transfer);
    void onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError *e);
    void onUsersUpdate(MegaApi *api);
    void onNodesUpdate(MegaApi *api);
    void onReloadNeeded(MegaApi *api);
    
private:
    MegaSDK *megaSDK;
    void *listener;
};
