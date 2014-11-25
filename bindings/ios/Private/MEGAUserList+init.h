#import "MEGAUserList.h"
#import "megaapi.h"

@interface MEGAUserList (init)

- (instancetype)initWithUserList:(mega::MegaUserList *)userList cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaUserList *)getCPtr;

@end
