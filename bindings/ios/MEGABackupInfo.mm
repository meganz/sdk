/**
 * @file MEGABackupInfo.mm
 * @brief Represents an user banner in MEGA
 *
 * (c) 2023 by Mega Limited, Auckland, New Zealand
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
#import "MEGABackupInfo.h"
#import "megaapi.h"

using namespace mega;

@interface MEGABackupInfo ()

@property MegaBackupInfo *megaBackupInfo;
@property BOOL cMemoryOwn;

@end

@implementation MEGABackupInfo

- (instancetype)initWithMegaBackupInfo:(MegaBackupInfo *)megaBackupInfo cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self != nil) {
        _megaBackupInfo = megaBackupInfo;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaBackupInfo;
    }
}

- (BOOL)isEqual:(id)object {
    if (![object isKindOfClass:[MEGABackupInfo class]]) {
        return false;
    }
    
    return self.id == ((MEGABackupInfo *)object).id;
}


- (MegaBackupInfo *)getCPtr {
    return self.megaBackupInfo;
}

- (NSUInteger)identifier {
    return (NSUInteger)self.megaBackupInfo->id();
}

- (MEGABackupType)type {
    return (MEGABackupType) (self.megaBackupInfo ? self.megaBackupInfo->type() : MEGABackupTypeInvalid);
}

- (uint64_t)root {
    return self.megaBackupInfo ? self.megaBackupInfo->root() : ::mega::INVALID_HANDLE;
}

- (NSString *)localFolder {
    if(!self.megaBackupInfo) return nil;
    
    return self.megaBackupInfo->localFolder() ? [[NSString alloc] initWithUTF8String:self.megaBackupInfo->localFolder()] : nil;
}

- (NSString *)deviceId {
    if (self.megaBackupInfo) {
        const char *val = self.megaBackupInfo->deviceId();
        if (val) {
            return [NSString stringWithUTF8String:val];
        }
    }
    return nil;
}

- (BackUpState)state {
    return (BackUpState) (self.megaBackupInfo ? self.megaBackupInfo->state() : BackUpStateUnknown);
}

- (BackUpSubState)substate {
    return (BackUpSubState) (self.megaBackupInfo ? self.megaBackupInfo->substate() : BackUpSubStateNoSyncError);
}

- (NSString *)extra {
    if(!self.megaBackupInfo) return nil;
    
    return self.megaBackupInfo->extra() ? [[NSString alloc] initWithUTF8String:self.megaBackupInfo->extra()] : nil;
}

- (NSString *)name {
    if(!self.megaBackupInfo) return nil;
    
    return self.megaBackupInfo->name() ? [[NSString alloc] initWithUTF8String:self.megaBackupInfo->name()] : nil;
}

- (NSDate *)timestamp {
    return self.megaBackupInfo ? [[NSDate alloc] initWithTimeIntervalSince1970:self.megaBackupInfo->ts()] : nil;
}

- (MEGABackupHeartbeatStatus)status {
    return (MEGABackupHeartbeatStatus) (self.megaBackupInfo ? self.megaBackupInfo->status() : MEGABackupHeartbeatStatusUnknown);
}

- (NSUInteger)progress {
    return self.megaBackupInfo ? self.megaBackupInfo->progress() : 0;
}

- (NSUInteger)uploads {
    return self.megaBackupInfo ? self.megaBackupInfo->uploads() : 0;
}

- (NSUInteger)downloads {
    return self.megaBackupInfo ? self.megaBackupInfo->downloads() : 0;
}

- (NSDate *)activityTimestamp {
    return self.megaBackupInfo ? [[NSDate alloc] initWithTimeIntervalSince1970:self.megaBackupInfo->activityTs()] : nil;
}

- (uint64_t)lastSync {
    return self.megaBackupInfo ? self.megaBackupInfo->lastSync() : ::mega::INVALID_HANDLE;
}

- (NSString *)userAgent {
    if(!self.megaBackupInfo) return nil;
    
    return self.megaBackupInfo->deviceUserAgent() ? [[NSString alloc] initWithUTF8String:self.megaBackupInfo->deviceUserAgent()] : nil;
}

@end
