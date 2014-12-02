#import "MEGAShareList.h"
#import "megaapi.h"

@interface MEGAShareList (init)

- (instancetype)initWithShareList:(mega::MegaShareList *)shareList cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaShareList *)getCPtr;

@end
