//
//  DelegateMTransferListener.h
//
//  Created by Javier Navarro on 08/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MTransferDelegate.h"
#import "megaapi.h"
#import "MegaSDK.h"

using namespace mega;

class DelegateMTransferListener : public MegaTransferListener {

public:
    
    DelegateMTransferListener(MegaSDK *megaSDK, void *listener, bool singleListener = true);
    void *getUserListener();
    
    void onTransferStart(MegaApi *api, MegaTransfer *transfer);
    void onTransferFinish(MegaApi *api, MegaTransfer *transfer, MegaError *e);
    void onTransferUpdate(MegaApi *api, MegaTransfer *transfer);
    void onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError *e);

private:
    MegaSDK *megaSDK;
    void *listener;
    bool singleListener;
};