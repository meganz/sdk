//
//  DelegateMRequestListener.h
//
//  Created by Javier Navarro on 08/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MRequestDelegate.h"
#import "megaapi.h"
#import "MegaSDK.h"

class DelegateMRequestListener : public mega::MegaRequestListener {

public:
    
    DelegateMRequestListener(MegaSDK *megaSDK, id<MRequestDelegate>listener, bool singleListener = true);
    id<MRequestDelegate>getUserListener();
    
    void onRequestStart(mega::MegaApi *api, mega::MegaRequest *request);
    void onRequestFinish(mega::MegaApi *api, mega::MegaRequest *request, mega::MegaError *e);
    void onRequestUpdate(mega::MegaApi *api, mega::MegaRequest *request);
    void onRequestTemporaryError(mega::MegaApi *api, mega::MegaRequest *request, mega::MegaError *e);
    
private:
    MegaSDK *megaSDK;
    id<MRequestDelegate>listener;
    bool singleListener;
};