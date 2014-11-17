//
//  DelegateMEGAListener.h
//
//  Created by Javier Navarro on 08/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MEGADelegate.h"
#import "MEGATransfer.h"
#import "MEGARequest.h"
#import "MEGAError.h"
#import "megaapi.h"
#import "MEGASdk.h"

class DelegateMEGAListener : public mega::MegaListener {

public:
    
    DelegateMEGAListener(MEGASdk *megaSDK, id<MEGADelegate>listener);
    id<MEGADelegate>getUserListener();
    
    void onRequestStart(mega::MegaApi *api, mega::MegaRequest *request);
    void onRequestFinish(mega::MegaApi *api, mega::MegaRequest *request, mega::MegaError *e);
    void onRequestUpdate(mega::MegaApi *api, mega::MegaRequest *request);
    void onRequestTemporaryError(mega::MegaApi *api, mega::MegaRequest *request, mega::MegaError *e);
    void onTransferStart(mega::MegaApi *api, mega::MegaTransfer *transfer);
    void onTransferFinish(mega::MegaApi *api, mega::MegaTransfer *transfer, mega::MegaError *e);
    void onTransferUpdate(mega::MegaApi *api, mega::MegaTransfer *transfer);
    void onTransferTemporaryError(mega::MegaApi *api, mega::MegaTransfer *transfer, mega::MegaError *e);
    void onUsersUpdate(mega::MegaApi* api, mega::MegaUserList* userList);
    void onNodesUpdate(mega::MegaApi* api, mega::MegaNodeList* nodeList);
    void onReloadNeeded(mega::MegaApi *api);
    
private:
    MEGASdk *megaSDK;
    id<MEGADelegate> listener;
};
