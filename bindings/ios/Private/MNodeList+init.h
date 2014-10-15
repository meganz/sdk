//
//  MNodeList+init.h
//
//  Created by Javier Navarro on 07/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MNodeList.h"
#import "megaapi.h"

using namespace mega;

@interface MNodeList (init)

- (instancetype)initWithNodeList:(NodeList *)nodelist cMemoryOwn:(BOOL)cMemoryOwn;
- (NodeList *)getCPtr;

@end
