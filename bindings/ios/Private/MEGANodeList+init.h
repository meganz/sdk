//
//  MEGANodeList+init.h
//
//  Created by Javier Navarro on 07/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MEGANodeList.h"
#import "megaapi.h"

@interface MEGANodeList (init)

- (instancetype)initWithNodeList:(mega::MegaNodeList *)nodelist cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::MegaNodeList *)getCPtr;

@end
