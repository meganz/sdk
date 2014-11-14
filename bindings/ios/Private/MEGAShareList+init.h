//
//  MEGAShareList+init.h
//
//  Created by Javier Navarro on 07/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MEGAShareList.h"
#import "megaapi.h"

@interface MEGAShareList (init)

- (instancetype)initWithShareList:(mega::MegaShareList *)shareList cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaShareList *)getCPtr;

@end
