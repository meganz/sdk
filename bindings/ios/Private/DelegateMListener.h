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

class DelegateMListener : public mega::MegaListener {

public:
    
    DelegateMListener(MegaSDK *megaSDK, id<MListenerDelegate>listener);
    id<MListenerDelegate>getUserListener();
    
    void onRequestStart(mega::MegaApi *api, mega::MegaRequest *request);
    void onRequestFinish(mega::MegaApi *api, mega::MegaRequest *request, mega::MegaError *e);
    void onRequestUpdate(mega::MegaApi *api, mega::MegaRequest *request);
    void onRequestTemporaryError(mega::MegaApi *api, mega::MegaRequest *request, mega::MegaError *e);
    void onTransferStart(mega::MegaApi *api, mega::MegaTransfer *transfer);
    void onTransferFinish(mega::MegaApi *api, mega::MegaTransfer *transfer, mega::MegaError *e);
    void onTransferUpdate(mega::MegaApi *api, mega::MegaTransfer *transfer);
    void onTransferTemporaryError(mega::MegaApi *api, mega::MegaTransfer *transfer, mega::MegaError *e);
    void onUsersUpdate(mega::MegaApi *api);
    void onNodesUpdate(mega::MegaApi *api);
    void onReloadNeeded(mega::MegaApi *api);
    
private:
    MegaSDK *megaSDK;
    id<MListenerDelegate> listener;
};
