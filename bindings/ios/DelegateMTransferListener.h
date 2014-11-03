//
//  DelegateMTransferListener.h
//
//  Created by Javier Navarro on 08/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MTransferDelegate.h"
#import "megaapi.h"
#import "MegaSDK.h"

class DelegateMTransferListener : public mega::MegaTransferListener {

public:
    
    DelegateMTransferListener(MegaSDK *megaSDK, void *listener, bool singleListener = true);
    void *getUserListener();
    
    void onTransferStart(mega::MegaApi *api, mega::MegaTransfer *transfer);
    void onTransferFinish(mega::MegaApi *api, mega::MegaTransfer *transfer, mega::MegaError *e);
    void onTransferUpdate(mega::MegaApi *api, mega::MegaTransfer *transfer);
    void onTransferTemporaryError(mega::MegaApi *api, mega::MegaTransfer *transfer, mega::MegaError *e);

private:
    MegaSDK *megaSDK;
    void *listener;
    bool singleListener;
};