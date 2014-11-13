//
//  MEGANodeList.m
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import "MEGANodeList.h"
#import "MEGANode+init.h"

using namespace mega;

@interface MEGANodeList ()

@property NodeList *nodeList;
@property BOOL cMemoryOwn;

@end

@implementation MEGANodeList

- (instancetype)initWithNodeList:(NodeList *)nodelist cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _nodeList = nodelist;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _nodeList;
    }
}

- (NodeList *)getCPtr {
    return self.nodeList;
}

- (MEGANode *)getNodeAtPosition:(NSInteger)position {
    return  self.nodeList ? [[MEGANode alloc] initWithMegaNode:self.nodeList->get((int)position)->copy() cMemoryOwn:YES] : nil;
}

- (NSNumber *)size {
    return self.nodeList ? [[NSNumber alloc] initWithInt:self.nodeList->size()] : nil;
}

@end
