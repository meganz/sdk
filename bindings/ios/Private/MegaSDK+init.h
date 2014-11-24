#import "MEGASdk.h"
#import "DelegateMEGARequestListener.h"
#import "DelegateMEGATransferListener.h"

@interface MEGASdk (init)

- (void)freeRequestListener:(DelegateMEGARequestListener *)delegate;
- (void)freeTransferListener:(DelegateMEGATransferListener *)delegate;

@end
