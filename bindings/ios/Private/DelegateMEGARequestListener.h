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