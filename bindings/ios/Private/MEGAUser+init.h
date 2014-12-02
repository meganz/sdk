#import "MEGAUser.h"
#import "megaapi.h"

@interface MEGAUser (init)

- (instancetype)initWithMegaUser:(mega::MegaUser *)megaUser cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaUser *)getCPtr;

@end
