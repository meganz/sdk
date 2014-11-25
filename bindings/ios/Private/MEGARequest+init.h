#import "MEGARequest.h"
#import "megaapi.h"

@interface MEGARequest (init)

- (instancetype)initWithMegaRequest:(mega::MegaRequest *)megaNode cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaRequest *)getCPtr;

@end
