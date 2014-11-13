//
//  MEGAUserList+init.h
//
//  Created by Javier Navarro on 07/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MEGAUserList.h"
#import "megaapi.h"

@interface MEGAUserList (init)

- (instancetype)initWithUserList:(mega::UserList *)userList cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::UserList *)getCPtr;

@end
