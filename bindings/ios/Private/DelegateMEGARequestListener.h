/**
 * @file DelegateMEGARequestListener.h
 * @brief Listener to reveice and send request events to the app
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
#import "MEGARequestDelegate.h"
#import "megaapi.h"
#import "MEGASdk.h"

class DelegateMEGARequestListener : public mega::MegaRequestListener {

public:
    
    DelegateMEGARequestListener(MEGASdk *megaSDK, id<MEGARequestDelegate>listener, bool singleListener = true);
    id<MEGARequestDelegate>getUserListener();
    
    void onRequestStart(mega::MegaApi *api, mega::MegaRequest *request);
    void onRequestFinish(mega::MegaApi *api, mega::MegaRequest *request, mega::MegaError *e);
    void onRequestUpdate(mega::MegaApi *api, mega::MegaRequest *request);
    void onRequestTemporaryError(mega::MegaApi *api, mega::MegaRequest *request, mega::MegaError *e);
    
private:
    MEGASdk *megaSDK;
    id<MEGARequestDelegate>listener;
    bool singleListener;
};