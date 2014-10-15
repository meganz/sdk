//
//  DelegateMRequestListener.h
//
//  Created by Javier Navarro on 08/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MRequestDelegate.h"
#import "megaapi.h"
#import "MegaSDK.h"

using namespace mega;

class DelegateMRequestListener : public MegaRequestListener {

public:
    
    DelegateMRequestListener(MegaSDK *megaSDK, void *listener, bool singleListener = true);
    void *getUserListener();
    
    void onRequestStart(MegaApi *api, MegaRequest *request);
    void onRequestFinish(MegaApi *api, MegaRequest *request, MegaError *e);
    void onRequestUpdate(MegaApi *api, MegaRequest *request);
    void onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError *e);
    
private:
    MegaSDK *megaSDK;
    void *listener;
    bool singleListener;
};