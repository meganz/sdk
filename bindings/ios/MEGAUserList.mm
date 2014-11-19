//
//  MEGAUserList.m
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

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

- (instancetype)clone {
    return self.userList ? [[MEGAUserList alloc] initWithUserList:self.userList->copy() cMemoryOwn:YES] : nil;
}

- (MegaUserList *)getCPtr {
    return self.userList;
}

- (MEGAUser *)userAtIndex:(NSInteger)index {
    return self.userList ? [[MEGAUser alloc] initWithMegaUser:self.userList->get((int)index)->copy() cMemoryOwn:YES] : nil;
}

- (NSNumber *)size {
    return self.userList ? [[NSNumber alloc] initWithInt:self.userList->size()] : nil;
}

@end
