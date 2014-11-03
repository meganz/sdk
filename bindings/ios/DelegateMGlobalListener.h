//
//  DelegateMGlobalListener.h
//
//  Created by Javier Navarro on 08/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "megaapi.h"
#import "MegaSDK.h"

class DelegateMGlobalListener : public mega::MegaGlobalListener {
    
public:
    
    DelegateMGlobalListener(MegaSDK *megaSDK, void *listener);
    void *getUserListener();
    
    void onUsersUpdate(mega::MegaApi* api);
    void onNodesUpdate(mega::MegaApi* api);
    void onReloadNeeded(mega::MegaApi* api);
    
private:
    MegaSDK *megaSDK;
    void *listener;
};
