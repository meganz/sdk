#import "MEGANode.h"
#import "megaapi.h"

using namespace mega;

@interface MEGANode()

@property MegaNode *megaNode;
@property BOOL cMemoryOwn;

@end

@implementation MEGANode

- (instancetype)initWithMegaNode:(MegaNode *)megaNode cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _megaNode = megaNode;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaNode;
    }
}

- (instancetype)clone {
    return self.megaNode ? [[MEGANode alloc] initWithMegaNode:self.megaNode->copy() cMemoryOwn:YES] : nil;
}

- (MegaNode *)getCPtr {
    return self.megaNode;
}

- (MEGANodeType)type {
    return (MEGANodeType) (self.megaNode ? self.megaNode->getType() : MegaNode::TYPE_UNKNOWN);
}

- (NSString *)name {
    if(!self.megaNode) return nil;
    
    return self.megaNode ? [[NSString alloc] initWithUTF8String:self.megaNode->getName()] : nil;
}

- (NSString *)base64Handle {
    if (!self.megaNode) return nil;
    
    const char *val = self.megaNode->getBase64Handle();
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete val;
    return ret;
}

- (NSNumber *)size {
    return self.megaNode ? [[NSNumber alloc] initWithUnsignedLongLong:self.megaNode->getSize()] : nil;
}

- (NSDate *)creationTime {
    return self.megaNode ? [[NSDate alloc] initWithTimeIntervalSince1970:self.megaNode->getCreationTime()] : nil;
}

- (NSDate *)modificationTime {
    return self.megaNode ? [[NSDate alloc] initWithTimeIntervalSince1970:self.megaNode->getModificationTime()] : nil;
}

- (uint64_t)handle {
    return self.megaNode ? self.megaNode->getHandle() : ::mega::INVALID_HANDLE;
}

- (NSInteger)tag {
    return self.megaNode ? self.megaNode->getTag() : 0;
}

- (BOOL)isFile {
    return self.megaNode ? self.megaNode->isFile() : NO;
}

- (BOOL)isFolder {
    return self.megaNode ? self.megaNode->isFolder() : NO;
}

- (BOOL)isRemoved {
    return self.megaNode ? self.megaNode->isRemoved() : NO;
}

- (BOOL)hasThumbnail {
    return self.megaNode ? self.megaNode->hasThumbnail() : NO;
}

- (BOOL)hasPreview {
    return self.megaNode ? self.megaNode->hasPreview() : NO;
}

- (BOOL)isPublic {
    return self.megaNode ? self.megaNode->isPublic() : NO;
}

@end
