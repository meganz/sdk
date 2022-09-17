/**
 * @file DelegateMEGATransferListener.h
 * @brief Listener to reveice and send transfer events to the app
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
#import "MEGATransferDelegate.h"
#import "megaapi.h"
#import "MEGASdk.h"
#import "ListenerDispatch.h"

class DelegateMEGATransferListener : public mega::MegaTransferListener {

public:
    
    DelegateMEGATransferListener(MEGASdk *megaSDK, id<MEGATransferDelegate>listener, bool singleListener, ListenerQueueType queueType);
    id<MEGATransferDelegate>getUserListener();
    
    void onTransferStart(mega::MegaApi *api, mega::MegaTransfer *transfer);
    void onTransferFinish(mega::MegaApi *api, mega::MegaTransfer *transfer, mega::MegaError *e);
    void onTransferUpdate(mega::MegaApi *api, mega::MegaTransfer *transfer);
    void onFolderTransferUpdate(mega::MegaApi *api, mega::MegaTransfer *transfer, int stage, uint32_t foldercount, uint32_t createdfoldercount, uint32_t filecount, const char *currentFolder, const char *currentFileLeafname);
    void onTransferTemporaryError(mega::MegaApi *api, mega::MegaTransfer *transfer, mega::MegaError *e);
    bool onTransferData(mega::MegaApi *api, mega::MegaTransfer *transfer, char *buffer, size_t size);

private:
    MEGASdk *megaSDK;
    id<MEGATransferDelegate>listener;
    bool singleListener;
    ListenerQueueType queueType;
};
