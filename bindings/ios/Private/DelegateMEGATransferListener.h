#import "MEGATransferDelegate.h"
#import "megaapi.h"
#import "MEGASdk.h"

class DelegateMEGATransferListener : public mega::MegaTransferListener {

public:
    
    DelegateMEGATransferListener(MEGASdk *megaSDK, id<MEGATransferDelegate>listener, bool singleListener = true);
    id<MEGATransferDelegate>getUserListener();
    
    void onTransferStart(mega::MegaApi *api, mega::MegaTransfer *transfer);
    void onTransferFinish(mega::MegaApi *api, mega::MegaTransfer *transfer, mega::MegaError *e);
    void onTransferUpdate(mega::MegaApi *api, mega::MegaTransfer *transfer);
    void onTransferTemporaryError(mega::MegaApi *api, mega::MegaTransfer *transfer, mega::MegaError *e);
    bool onTransferData(mega::MegaApi *api, mega::MegaTransfer *transfer, char *buffer, size_t size);

private:
    MEGASdk *megaSDK;
    id<MEGATransferDelegate>listener;
    bool singleListener;
};