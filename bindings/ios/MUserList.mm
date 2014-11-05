//
//  MUserList.m
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MUserList.h"
#import "MUser+init.h"

using namespace mega;

@interface MUserList ()

@property UserList *userList;
@property BOOL cMemoryOwn;

@end

@implementation MUserList

- (instancetype)initWithUserList:(UserList *)userList cMemoryOwn:(BOOL)cMemoryOwn {
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

- (UserList *)getCPtr {
    return self.userList;
}

- (MUser *)getUserAtPosition:(NSInteger)position {
    return self.userList ? [[MUser alloc] initWithMegaUser:self.userList->get((int)position)->copy() cMemoryOwn:YES] : nil;
}

- (NSNumber *)size {
    return self.userList ? [[NSNumber alloc] initWithInt:self.userList->size()] : nil;
}

@end
