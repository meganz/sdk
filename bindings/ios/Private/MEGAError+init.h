#import "MEGAError.h"
#import "megaapi.h"

@interface MEGAError (init)

- (instancetype)initWithMegaError:(mega::MegaError *)megaError cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaError *)getCPtr;

@end
