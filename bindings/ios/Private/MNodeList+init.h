//
//  MNodeList+init.h
//
//  Created by Javier Navarro on 07/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MNodeList.h"
#import "megaapi.h"

@interface MNodeList (init)

- (instancetype)initWithNodeList:(mega::NodeList *)nodelist cMemoryOwn:(BOOL)cMemoryOwn;
- (mega::NodeList *)getCPtr;

@end
