/**
 * @file DelegateMEGAListener.h
 * @brief Listener to reveice and send events to the app
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
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
    void onUserAlertsUpdate(mega::MegaApi* api, mega::MegaUserAlertList *userAlertList);
    void onNodesUpdate(mega::MegaApi* api, mega::MegaNodeList* nodeList);
    void onSetsUpdate(mega::MegaApi* api, mega::MegaSetList* nodeList);
    void onSetElementsUpdate(mega::MegaApi* api, mega::MegaSetElementList* nodeList);
    void onAccountUpdate(mega::MegaApi *api);
    void onContactRequestsUpdate(mega::MegaApi* api, mega::MegaContactRequestList* contactRequestList);
    void onReloadNeeded(mega::MegaApi *api);
    void onEvent(mega::MegaApi* api, mega::MegaEvent *event);
    
private:
    MEGASdk *megaSDK;
    id<MEGADelegate> listener;
};
