/**
 * @file MEGARequest.mm
 * @brief Provides information about an asynchronous request
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
#import "MEGARequest.h"
#import "MEGANode+init.h"
#import "MEGAPricing+init.h"
#import "MEGAAccountDetails+init.h"
#import "MEGAAchievementsDetails+init.h"
#import "MEGAFolderInfo+init.h"
#import "MEGATimeZoneDetails+init.h"
#import "MEGAStringList+init.h"
#import "MEGAPushNotificationSettings+init.h"

using namespace mega;

@interface MEGARequest()

@property MegaRequest *megaRequest;
@property BOOL cMemoryOwn;

@end

@implementation MEGARequest

- (instancetype)initWithMegaRequest:(MegaRequest *)megaRequest cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _megaRequest = megaRequest;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn){
        delete _megaRequest;
    }
}

- (instancetype)clone {
    return  self.megaRequest ? [[MEGARequest alloc] initWithMegaRequest:self.megaRequest->copy() cMemoryOwn:YES] : nil;
}

- (MegaRequest *)getCPtr {
    return self.megaRequest;
}

- (MEGARequestType)type {
    return (MEGARequestType) (self.megaRequest ? self.megaRequest->getType() : -1);
}

- (NSString *)requestString {
    if(!self.megaRequest) return nil;
    
    return self.megaRequest->getRequestString() ? [[NSString alloc] initWithUTF8String:self.megaRequest->getRequestString()] : nil;
}

- (uint64_t)nodeHandle {
    return self.megaRequest ? self.megaRequest->getNodeHandle() : ::mega::INVALID_HANDLE;
}

- (NSString *)link {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest->getLink() ? [[NSString alloc] initWithUTF8String:self.megaRequest->getLink()] : nil;
}

- (uint64_t)parentHandle {
    return self.megaRequest ? self.megaRequest->getParentHandle() : ::mega::INVALID_HANDLE;
}

- (NSString *)sessionKey {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest->getSessionKey() ? [[NSString alloc] initWithUTF8String:self.megaRequest->getSessionKey()] : nil;
}

- (NSString *)name {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest->getName() ? [[NSString alloc] initWithUTF8String:self.megaRequest->getName()] : nil;
}

- (NSString *)email {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest->getEmail() ? [[NSString alloc] initWithUTF8String:self.megaRequest->getEmail()] : nil;
}

- (NSString *)password {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest->getPassword() ? [[NSString alloc] initWithUTF8String:self.megaRequest->getPassword()] : nil;
}

- (NSString *)newPassword {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest->getNewPassword() ? [[NSString alloc] initWithUTF8String:self.megaRequest->getNewPassword()] : nil;
}

- (NSString *)privateKey {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest->getPrivateKey() ? [[NSString alloc] initWithUTF8String:self.megaRequest->getPrivateKey()] : nil;
}

- (MEGANodeAccessLevel)access {
    return (MEGANodeAccessLevel) (self.megaRequest ? self.megaRequest->getAccess() : -1);
}

- (NSString *)file {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest->getFile() ? [[NSString alloc] initWithUTF8String:self.megaRequest->getFile()] : nil;
    
}

- (NSInteger)numRetry {
    return self.megaRequest->getNumRetry() ? self.megaRequest->getNumRetry() : 0;
}

- (MEGANode *)publicNode {
    return self.megaRequest && self.megaRequest->getPublicMegaNode() ? [[MEGANode alloc] initWithMegaNode:self.megaRequest->getPublicMegaNode() cMemoryOwn:YES] : nil;
}

- (NSInteger)paramType {
    return  self.megaRequest ? self.megaRequest->getParamType() : 0;
}

- (NSString *)text {
    return self.megaRequest->getText() ? [[NSString alloc] initWithUTF8String:self.megaRequest->getText()] : nil;
}

- (NSNumber *)number {
    return self.megaRequest ? [[NSNumber alloc] initWithLongLong:self.megaRequest->getNumber()] : nil;
}

- (BOOL)flag {
    return self.megaRequest ? self.megaRequest->getFlag() : NO;
}

- (NSNumber *)transferredBytes {
    return self.megaRequest ? [[NSNumber alloc] initWithLongLong:self.megaRequest->getTransferredBytes()] : nil;
}

- (NSNumber *)totalBytes {
    return self.megaRequest ? [[NSNumber alloc] initWithLongLong:self.megaRequest->getTotalBytes()] : nil;
}

- (MEGAAccountDetails *)megaAccountDetails  {
    return self.megaRequest ? [[MEGAAccountDetails alloc] initWithMegaAccountDetails:self.megaRequest->getMegaAccountDetails() cMemoryOwn:YES] : nil;
}

- (MEGAPricing *)pricing {
    return self.megaRequest ? [[MEGAPricing alloc] initWithMegaPricing:self.megaRequest->getPricing() cMemoryOwn:YES] : nil;
}

- (MEGAAchievementsDetails *)megaAchievementsDetails {
    return self.megaRequest ? [[MEGAAchievementsDetails alloc] initWithMegaAchievementsDetails:self.megaRequest->getMegaAchievementsDetails() cMemoryOwn:YES] : nil;
}

- (MEGATimeZoneDetails *)megaTimeZoneDetails {
    return self.megaRequest ? [[MEGATimeZoneDetails alloc] initWithMegaTimeZoneDetails:self.megaRequest->getMegaTimeZoneDetails() cMemoryOwn:YES] : nil;
}

- (MEGAFolderInfo *)megaFolderInfo {
    return self.megaRequest ? [[MEGAFolderInfo alloc] initWithMegaFolderInfo:self.megaRequest->getMegaFolderInfo()->copy() cMemoryOwn:YES] : nil;
}

- (MEGAPushNotificationSettings *)megaPushNotificationSettings {
    if (!self.megaRequest) return nil;
 
    return self.megaRequest->getMegaPushNotificationSettings() ? [MEGAPushNotificationSettings.alloc initWithMegaPushNotificationSettings:self.megaRequest->getMegaPushNotificationSettings()->copy() cMemoryOwn:YES] : nil;
}

- (NSDictionary<NSString *, MEGAStringList *> *)megaStringListDictionary {
    MegaStringListMap *map = self.megaRequest->getMegaStringListMap();
    NSMutableDictionary *dict = [NSMutableDictionary dictionaryWithCapacity:map->size()];
    MegaStringList *keyList = map->getKeys();
    for (int i = 0; i < keyList->size(); i++) {
        const char *key = keyList->get(i);
        dict[@(key)] = [[MEGAStringList alloc] initWithMegaStringList:(MegaStringList *)map->get(key)->copy() cMemoryOwn:YES];
    }
    
    delete keyList;
    
    return [dict copy];
}

- (NSInteger)transferTag {
    return self.megaRequest ? self.megaRequest->getTransferTag() : 0;
}

- (NSInteger)numDetails {
    return self.megaRequest ? self.megaRequest->getNumDetails() : 0;
}

- (NSArray<NSArray<NSString *> *> *)stringTableArray {
    MegaStringTable *table = self.megaRequest->getMegaStringTable();
    NSMutableArray<NSArray <NSString *> *> *stringTableArray = [NSMutableArray.alloc initWithCapacity:table->size()];
    for (int i = 0; i < table->size(); i++) {
        const MegaStringList *stringList = table->get(i);
        NSMutableArray<NSString *> *stringsArray = [NSMutableArray.alloc initWithCapacity:stringList->size()];
        for (int j = 0; j < stringList->size(); j++) {
            [stringsArray addObject:[NSString stringWithUTF8String:stringList->get(j)]];
        }
        [stringTableArray addObject:stringsArray.copy];
    }
    
    return stringTableArray.copy;
}

@end
