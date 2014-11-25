#import "MEGANodeList.h"
#import "MEGANode+init.h"

using namespace mega;

@interface MEGANodeList ()

@property MegaNodeList *nodeList;
@property BOOL cMemoryOwn;

@end

@implementation MEGANodeList

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

- (MEGANode *)nodeAtIndex:(NSInteger)index {
    return  self.nodeList ? [[MEGANode alloc] initWithMegaNode:self.nodeList->get((int)index)->copy() cMemoryOwn:YES] : nil;
}

- (NSNumber *)size {
    return self.nodeList ? [[NSNumber alloc] initWithInt:self.nodeList->size()] : nil;
}

@end
