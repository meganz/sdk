/**
 * @file MEGANodeList.mm
 * @brief List of MEGANode objects
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */
#import "MEGANodeList.h"
#import "MEGANode+init.h"

using namespace mega;

@interface MEGANodeList ()

@property MegaNodeList *nodeList;
@property BOOL cMemoryOwn;

@end

@implementation MEGANodeList

- (instancetype)init {
    self = [super init];
    
    if (self != nil) {
        _nodeList = self.nodeList->createInstance();
        _cMemoryOwn = YES;
    }
    
    return self;
}

- (instancetype)initWithNodeList:(MegaNodeList *)nodelist cMemoryOwn:(BOOL)cMemoryOwn {
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

- (instancetype)clone {
    return self.nodeList ? [[MEGANodeList alloc] initWithNodeList:self.nodeList->copy() cMemoryOwn:YES] : nil;
}

- (MegaNodeList *)getCPtr {
    return self.nodeList;
}

- (void)addNode:(MEGANode *)node {
    if (node == nil) return;
    
    self.nodeList->addNode([node getCPtr]);
}

- (MEGANode *)nodeAtIndex:(NSInteger)index {
    return  self.nodeList ? [[MEGANode alloc] initWithMegaNode:self.nodeList->get((int)index)->copy() cMemoryOwn:YES] : nil;
}

- (NSNumber *)size {
    return self.nodeList ? [[NSNumber alloc] initWithInt:self.nodeList->size()] : nil;
}

@end
