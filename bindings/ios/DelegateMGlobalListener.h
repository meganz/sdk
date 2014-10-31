//
//  DelegateMGlobalListener.h
//
//  Created by Javier Navarro on 08/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "megaapi.h"
#import "MegaSDK.h"

using namespace mega;

class DelegateMGlobalListener : public MegaGlobalListener {
    
public:
    
    DelegateMGlobalListener(MegaSDK *megaSDK, void *listener);
    void *getUserListener();
    
    void onUsersUpdate(MegaApi* api);
    void onNodesUpdate(MegaApi* api);
    void onReloadNeeded(MegaApi* api);
    
private:
    MegaSDK *megaSDK;
    void *listener;
};
