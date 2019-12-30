/**
 * @file MEGANode.mm
 * @brief Represents a node (file/folder) in the MEGA account
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
    
    return self.megaNode->getName() ? [[NSString alloc] initWithUTF8String:self.megaNode->getName()] : nil;
}

- (NSString *)fingerprint {
    if(!self.megaNode) return nil;
    
    return self.megaNode->getFingerprint() ? [[NSString alloc] initWithUTF8String:self.megaNode->getFingerprint()] : nil;
}

- (NSInteger)duration {
    return self.megaNode ? self.megaNode->getDuration() : -1;
}

- (NSInteger)width {
    return self.megaNode ? self.megaNode->getWidth() : -1;
}

- (NSInteger)height {
    return self.megaNode ? self.megaNode->getHeight(): -1;
}

- (NSInteger)shortFormat {
    return self.megaNode ? self.megaNode->getShortformat() : -1;
}

- (NSInteger)videoCodecId {
    return self.megaNode ? self.megaNode->getVideocodecid(): -1;
}

- (NSNumber *)latitude {
    if (!self.megaNode) return nil;
    double latitude = self.megaNode->getLatitude();
    return latitude != MegaNode::INVALID_COORDINATE ? [NSNumber numberWithDouble:latitude] : nil;
}

- (NSNumber *)longitude {
    if (!self.megaNode) return nil;
    double longitude = self.megaNode->getLongitude();
    return longitude != MegaNode::INVALID_COORDINATE ? [NSNumber numberWithDouble:longitude] : nil;
}

- (NSString *)base64Handle {
    if (!self.megaNode) return nil;
    
    const char *val = self.megaNode->getBase64Handle();
    if (!val) return nil;
    
    NSString *ret = [[NSString alloc] initWithUTF8String:val];
    
    delete [] val;
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

- (uint64_t)restoreHandle {
    return self.megaNode ? self.megaNode->getRestoreHandle() : ::mega::INVALID_HANDLE;
}

- (uint64_t)parentHandle {
    return self.megaNode ? self.megaNode->getParentHandle() : ::mega::INVALID_HANDLE;
}

- (NSInteger)tag {
    return self.megaNode ? self.megaNode->getTag() : 0;
}

- (int64_t)expirationTime {
    return self.megaNode ? self.megaNode->getExpirationTime() : -1;
}

- (uint64_t)publicHandle {
    return self.megaNode ? self.megaNode->getPublicHandle() : mega::INVALID_HANDLE;
}

- (MEGANode *)publicNode {
    return self.megaNode ? [[MEGANode alloc] initWithMegaNode:self.megaNode->getPublicNode() cMemoryOwn:YES] : nil;
}

- (NSString *)publicLink {
    return self.megaNode ? [[NSString alloc] initWithUTF8String:self.megaNode->getPublicLink()] : nil;
}

- (uint64_t)owner {
    return self.megaNode ? self.megaNode->getOwner() : ::mega::INVALID_HANDLE;
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

- (BOOL)hasChangedType:(MEGANodeChangeType)changeType {
    return self.megaNode ? self.megaNode->hasChanged(int(changeType)) : NO;
}

- (MEGANodeChangeType)getChanges {
    return (MEGANodeChangeType) self.megaNode->getChanges();
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

- (BOOL)isShared {
    return self.megaNode ? self.megaNode->isShared() : NO;
}

- (BOOL)isOutShare {
    return self.megaNode ? self.megaNode->isOutShare() : NO;
}

- (BOOL)isInShare {
    return self.megaNode ? self.megaNode->isInShare() : NO;
}

- (BOOL)isExported {
    return self.megaNode ? self.megaNode->isExported() : NO;
}

- (BOOL)isExpired {
    return self.megaNode ? self.megaNode->isExpired() : NO;
}

- (BOOL)isTakenDown {
    return self.megaNode ? self.megaNode->isTakenDown() : NO;
}

@end
