/**
 * @file DelegateMEGAGlobalListener.h
 * @brief Listener to reveice and send global events to the app
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
#import "megaapi.h"
#import "MEGASdk.h"
#import "ListenerDispatch.h"

class DelegateMEGAGlobalListener : public mega::MegaGlobalListener {
    
public:
    
    DelegateMEGAGlobalListener(MEGASdk *megaSDK, id<MEGAGlobalDelegate> listener, ListenerQueueType queueType);
    id<MEGAGlobalDelegate> getUserListener();
    
    void onUsersUpdate(mega::MegaApi* api, mega::MegaUserList* userList);
    void onUserAlertsUpdate(mega::MegaApi* api, mega::MegaUserAlertList *userAlertList);
    void onNodesUpdate(mega::MegaApi* api, mega::MegaNodeList* nodeList);
    void onSetsUpdate(mega::MegaApi* api, mega::MegaSetList* setList);
    void onSetElementsUpdate(mega::MegaApi* api, mega::MegaSetElementList* setElementList);
    void onAccountUpdate(mega::MegaApi *api);
    void onContactRequestsUpdate(mega::MegaApi* api, mega::MegaContactRequestList* contactRequestList);
    void onReloadNeeded(mega::MegaApi* api);
    void onEvent(mega::MegaApi* api, mega::MegaEvent *event);
    
private:
    MEGASdk *megaSDK;
    __weak id<MEGAGlobalDelegate> listener;
    ListenerQueueType queueType;
};
