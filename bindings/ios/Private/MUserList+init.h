//
//  MUserList+init.h
//
//  Created by Javier Navarro on 07/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MUserList.h"
#import "megaapi.h"

using namespace mega;

@interface MUserList (init)

- (instancetype)initWithUserList:(UserList *)userList cMemoryOwn:(BOOL)cMemoryOwn;
- (UserList *)getCPtr;

@end
