/**
 * @file MEGAUserList.mm
 * @brief List of MEGAUser objects
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
#import "MEGAUserList.h"
#import "MEGAUser+init.h"

using namespace mega;

@interface MEGAUserList ()

@property MegaUserList *userList;
@property BOOL cMemoryOwn;

@end

@implementation MEGAUserList

- (instancetype)initWithUserList:(MegaUserList *)userList cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _userList = userList;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _userList;
    }
}

- (MegaUserList *)getCPtr {
    return self.userList;
}

- (nullable MEGAUser *)userAtIndex:(NSInteger)index {
    return self.userList ? [[MEGAUser alloc] initWithMegaUser:self.userList->get((int)index)->copy() cMemoryOwn:YES] : nil;
}

- (NSInteger)size {
    return self.userList ? self.userList->size() : 0;
}

@end
