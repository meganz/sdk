//
//  MShareList+init.h
//
//  Created by Javier Navarro on 07/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MShareList.h"
#import "megaapi.h"

using namespace mega;

@interface MShareList (init)

- (instancetype)initWithShareList:(ShareList *)shareList cMemoryOwn:(BOOL)cMemoryOwn;
- (ShareList *)getCPtr;

@end
