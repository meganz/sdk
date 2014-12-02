#import "MEGAShare.h"
#import "megaapi.h"
@interface MEGAShare (init)

- (instancetype)initWithMegaShare:(mega::MegaShare *)megaShare cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaShare *) getCPtr;

@end
