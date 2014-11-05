//
//  MShareList+init.h
//
//  Created by Javier Navarro on 07/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MShareList.h"
#import "megaapi.h"

@interface MShareList (init)

- (instancetype)initWithShareList:(mega::ShareList *)shareList cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::ShareList *)getCPtr;

@end
