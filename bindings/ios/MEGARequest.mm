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
#import "MEGASet+init.h"
#import "MEGASetElement+init.h"
#import "MEGAPricing+init.h"
#import "MEGAAccountDetails+init.h"
#import "MEGAAchievementsDetails+init.h"
#import "MEGAFolderInfo+init.h"
#import "MEGATimeZoneDetails+init.h"
#import "MEGAStringList+init.h"
#import "MEGAPushNotificationSettings+init.h"
#import "MEGABannerList.h"
#import "MEGABannerList+init.h"
#import "MEGAHandleList+init.h"
#import "MEGACurrency+init.h"
#import "MEGARecentActionBucket+init.h"
#import "MEGABackupInfo+init.h"
#import "MEGAVPNCredentials.h"
#import "MEGAVPNCredentials+init.h"
#import "MEGAVPNRegion.h"
#import "MEGAVPNRegion+init.h"
#import "MEGANotificationList+init.h"
#import "MEGAIntegerList+init.h"

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

- (MegaRequest *)getCPtr {
    return self.megaRequest;
}

- (MEGARequestType)type {
    return (MEGARequestType) (self.megaRequest ? self.megaRequest->getType() : -1);
}

- (nullable NSString *)requestString {
    if(!self.megaRequest) return nil;
    
    return self.megaRequest->getRequestString() ? [[NSString alloc] initWithUTF8String:self.megaRequest->getRequestString()] : nil;
}

- (uint64_t)nodeHandle {
    return self.megaRequest ? self.megaRequest->getNodeHandle() : ::mega::INVALID_HANDLE;
}

- (nullable NSString *)link {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest->getLink() ? [[NSString alloc] initWithUTF8String:self.megaRequest->getLink()] : nil;
}

- (uint64_t)parentHandle {
    return self.megaRequest ? self.megaRequest->getParentHandle() : ::mega::INVALID_HANDLE;
}

- (nullable NSString *)sessionKey {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest->getSessionKey() ? [[NSString alloc] initWithUTF8String:self.megaRequest->getSessionKey()] : nil;
}

- (nullable NSString *)name {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest->getName() ? [[NSString alloc] initWithUTF8String:self.megaRequest->getName()] : nil;
}

- (nullable NSString *)email {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest->getEmail() ? [[NSString alloc] initWithUTF8String:self.megaRequest->getEmail()] : nil;
}

- (nullable NSString *)password {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest->getPassword() ? [[NSString alloc] initWithUTF8String:self.megaRequest->getPassword()] : nil;
}

- (nullable NSString *)newPassword {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest->getNewPassword() ? [[NSString alloc] initWithUTF8String:self.megaRequest->getNewPassword()] : nil;
}

- (MEGANodeAccessLevel)access {
    return (MEGANodeAccessLevel) (self.megaRequest ? self.megaRequest->getAccess() : -1);
}

- (nullable NSString *)file {
    if (!self.megaRequest) return nil;
    
    return self.megaRequest->getFile() ? [[NSString alloc] initWithUTF8String:self.megaRequest->getFile()] : nil;
    
}

- (NSInteger)numRetry {
    return self.megaRequest->getNumRetry() ? self.megaRequest->getNumRetry() : 0;
}

- (nullable MEGANode *)publicNode {
    return self.megaRequest && self.megaRequest->getPublicMegaNode() ? [[MEGANode alloc] initWithMegaNode:self.megaRequest->getPublicMegaNode() cMemoryOwn:YES] : nil;
}

- (NSInteger)paramType {
    return  self.megaRequest ? self.megaRequest->getParamType() : 0;
}

- (nullable NSString *)text {
    return self.megaRequest->getText() ? [[NSString alloc] initWithUTF8String:self.megaRequest->getText()] : nil;
}

- (long long)number {
    return self.megaRequest ? self.megaRequest->getNumber() : 0;
}

- (BOOL)flag {
    return self.megaRequest ? self.megaRequest->getFlag() : NO;
}

- (long long)transferredBytes {
    return self.megaRequest ? self.megaRequest->getTransferredBytes() : 0;
}

- (long long)totalBytes {
    return self.megaRequest ? self.megaRequest->getTotalBytes() : 0;
}

- (nullable MEGAAccountDetails *)megaAccountDetails  {
    return self.megaRequest ? [[MEGAAccountDetails alloc] initWithMegaAccountDetails:self.megaRequest->getMegaAccountDetails() cMemoryOwn:YES] : nil;
}

- (nullable MEGAPricing *)pricing {
    return self.megaRequest ? [[MEGAPricing alloc] initWithMegaPricing:self.megaRequest->getPricing() cMemoryOwn:YES] : nil;
}

- (nullable MEGACurrency *)currency {
    return self.megaRequest ? [[MEGACurrency alloc] initWithMegaCurrency:self.megaRequest->getCurrency() cMemoryOwn:YES] : nil;
}

- (nullable MEGAAchievementsDetails *)megaAchievementsDetails {
    return self.megaRequest ? [[MEGAAchievementsDetails alloc] initWithMegaAchievementsDetails:self.megaRequest->getMegaAchievementsDetails() cMemoryOwn:YES] : nil;
}

- (nullable MEGATimeZoneDetails *)megaTimeZoneDetails {
    return self.megaRequest ? [[MEGATimeZoneDetails alloc] initWithMegaTimeZoneDetails:self.megaRequest->getMegaTimeZoneDetails() cMemoryOwn:YES] : nil;
}

- (nullable MEGAFolderInfo *)megaFolderInfo {
    return self.megaRequest ? [[MEGAFolderInfo alloc] initWithMegaFolderInfo:self.megaRequest->getMegaFolderInfo()->copy() cMemoryOwn:YES] : nil;
}

- (nullable MEGAPushNotificationSettings *)megaPushNotificationSettings {
    if (!self.megaRequest) return nil;
 
    return self.megaRequest->getMegaPushNotificationSettings() ? [MEGAPushNotificationSettings.alloc initWithMegaPushNotificationSettings:self.megaRequest->getMegaPushNotificationSettings()->copy() cMemoryOwn:YES] : nil;
}

- (nullable NSDictionary<NSString *, MEGAStringList *> *)megaStringListDictionary {
    if (!self.megaRequest) {
        return nil;
    }
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

- (nullable NSDictionary<NSString *, NSString *> *)megaStringDictionary {
    if (!self.megaRequest) {
        return nil;
    }
    MegaStringMap *map = self.megaRequest->getMegaStringMap();
    
    NSMutableDictionary *dict = [NSMutableDictionary dictionaryWithCapacity:map->size()];
    MegaStringList *keyList = map->getKeys();
    
    for (int i = 0; i < keyList->size(); i++) {
        const char *key = keyList->get(i);
        dict[@(key)] = @(map->get(key));
    }
    
    delete keyList;
    return [dict copy];
}

- (nullable NSDictionary<NSString *, MEGAIntegerList *> *)megaStringIntegerDictionary {
    if (!self.megaRequest) {
        return nil;
    }
    MegaStringIntegerMap *map = self.megaRequest->getMegaStringIntegerMap();

    NSMutableDictionary *dict = [NSMutableDictionary dictionaryWithCapacity:map->size()];
    MegaStringList *keyList = map->getKeys();

    for (int i = 0; i < keyList->size(); i++) {
        const char *key = keyList->get(i);
        MegaIntegerList* errorCode = map->get(key);
        MEGAIntegerList* value = errorCode != nil ?
            [[MEGAIntegerList alloc] initWithMegaIntegerList:errorCode cMemoryOwn:YES] :
            nil;
        dict[@(key)] = value;
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

- (nullable NSArray<NSArray<NSString *> *> *)stringTableArray {
    if (!self.megaRequest) {
        return nil;
    }
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

- (nullable MEGABannerList *)bannerList {
    if (!self.megaRequest) {
        return nil;
    }
    MegaBannerList *bannerList = self.megaRequest->getMegaBannerList() -> copy();
    return [[MEGABannerList alloc] initWithMegaBannerList:bannerList cMemoryOwn:YES];
}

- (nullable NSArray<NSNumber *> *)megaHandleArray {
    if (!self.megaRequest) {
        return nil;
    }
    MEGAHandleList *handleList = [MEGAHandleList.alloc initWithMegaHandleList:self.megaRequest->getMegaHandleList()->copy() cMemoryOwn:YES];
    NSMutableArray<NSNumber *> *handleArray = [NSMutableArray.alloc initWithCapacity:handleList.size];
    for (int i = 0; i < handleList.size; i++) {
        [handleArray addObject:[NSNumber numberWithUnsignedLongLong:[handleList megaHandleAtIndex:i]]];
    }
    return handleArray.copy;
}

- (nullable NSArray *)recentActionsBuckets {
    if (!self.megaRequest) {
        return nil;
    }
    MegaRecentActionBucketList *megaRecentActionBucketList = self.megaRequest->getRecentActions();
    int count = megaRecentActionBucketList->size();
    NSMutableArray *recentActionBucketMutableArray = [NSMutableArray.alloc initWithCapacity:(NSInteger)count];
    for (int i = 0; i < count; i++) {
        MEGARecentActionBucket *recentActionBucket = [MEGARecentActionBucket.alloc initWithMegaRecentActionBucket:megaRecentActionBucketList->get(i)->copy() cMemoryOwn:YES];
        [recentActionBucketMutableArray addObject:recentActionBucket];
    }

    return recentActionBucketMutableArray;
}

- (nullable NSArray<MEGABackupInfo *> *)backupInfoList {
    if (!self.megaRequest) {
        return nil;
    }
    MegaBackupInfoList *megaBackupInfoList = self.megaRequest->getMegaBackupInfoList();
    int count = megaBackupInfoList->size();
    NSMutableArray *backupInfoMutableArray = [NSMutableArray.alloc initWithCapacity:(NSInteger)count];
    for (int i = 0; i < count; i++) {
        MEGABackupInfo *megaBackupInfo = [MEGABackupInfo.alloc initWithMegaBackupInfo:megaBackupInfoList->get(i)->copy() cMemoryOwn:YES];
        [backupInfoMutableArray addObject:megaBackupInfo];
    }

    return backupInfoMutableArray;
}

- (nullable MEGASet *)set {
    return self.megaRequest ? [[MEGASet alloc] initWithMegaSet:self.megaRequest->getMegaSet()->copy() cMemoryOwn:YES] : nil;
}

- (nullable NSArray<MEGASetElement *> *)elementsInSet {
    if (!self.megaRequest) {
        return nil;
    }
    MegaSetElementList *setElementList = self.megaRequest->getMegaSetElementList();
    int size = setElementList->size();
    
    NSMutableArray<MEGASetElement *> *setElements = [[NSMutableArray alloc] initWithCapacity:size];
    
    for (int i = 0; i < size; i++) {
        [setElements addObject:[[MEGASetElement alloc]
                                initWithMegaSetElement:setElementList->get(i)->copy()
                                            cMemoryOwn:YES]];
    }
    
    return [setElements copy];
}

- (nullable MEGAStringList *)megaStringList {
    return self.megaRequest ? [[MEGAStringList alloc] initWithMegaStringList:self.megaRequest->getMegaStringList()->copy() cMemoryOwn:YES] : nil;
}

- (nullable MEGAVPNCredentials *)megaVpnCredentials {
    return self.megaRequest ? [[MEGAVPNCredentials alloc] initWithMegaVpnCredentials:self.megaRequest->getMegaVpnCredentials()->copy() cMemoryOwn:YES] : nil;
}

- (nonnull NSArray<MEGAVPNRegion *> *)megaVpnRegions {
    if (!self.megaRequest) {
        return @[];
    }
    mega::MegaVpnRegionList *regionList = self.megaRequest->getMegaVpnRegionsDetailed();
    if (!regionList) {
        return @[];
    }
    int count = regionList->size();
    NSMutableArray<MEGAVPNRegion *> *regionsArray = [[NSMutableArray alloc] initWithCapacity:(NSUInteger)count];
    for (int i = 0; i < count; i++) {
        const mega::MegaVpnRegion *region = regionList->get(i);
        if (region) {
            MEGAVPNRegion *vpnRegion = [[MEGAVPNRegion alloc] initWithMegaVpnRegion:region->copy() cMemoryOwn:YES];
            [regionsArray addObject:vpnRegion];
        }
    }
    return regionsArray;
}

- (nullable MEGANotificationList*)megaNotifications {
    return self.megaRequest ? [[MEGANotificationList alloc] initWithMegaNotificationList:self.megaRequest->getMegaNotifications()->copy() cMemoryOwn:YES] : nil;
}

@end
