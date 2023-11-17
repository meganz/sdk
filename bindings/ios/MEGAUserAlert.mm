/**
 * @file MEGAUserAlert.mm
 * @brief Represents a user alert in MEGA.
 *
 * (c) 2018-Present by Mega Limited, Auckland, New Zealand
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
#import "MEGAUserAlert.h"
#import "megaapi.h"
#import "MEGAStringList+init.h"

using namespace mega;

@interface MEGAUserAlert ()

@property MegaUserAlert *megaUserAlert;
@property BOOL cMemoryOwn;

@end

@implementation MEGAUserAlert

- (instancetype)initWithMegaUserAlert:(MegaUserAlert *)megaUserAlert cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self) {
        _megaUserAlert = megaUserAlert;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaUserAlert;
    }
}

- (MegaUserAlert *)getCPtr {
    return self.megaUserAlert;
}

- (NSUInteger)identifier {
    return self.megaUserAlert ? self.megaUserAlert->getId() : 0;
}

- (BOOL)isSeen {
    return self.megaUserAlert ? self.megaUserAlert->getSeen(): NO;
}

- (BOOL)isRelevant {
    return self.megaUserAlert ? self.megaUserAlert->getRelevant() : NO;
}

- (MEGAUserAlertType)type {
    return (MEGAUserAlertType)(self.megaUserAlert ? self.megaUserAlert->getType() : 0);
}

- (nullable NSString *)typeString {
    if (!self.megaUserAlert) return nil;
    
    return self.megaUserAlert->getTypeString() ? [[NSString alloc] initWithUTF8String:self.megaUserAlert->getTypeString()] : nil;
}

- (uint64_t)userHandle {
    return self.megaUserAlert ? self.megaUserAlert->getUserHandle() : ::mega::INVALID_HANDLE;
}

- (uint64_t)nodeHandle {
    return self.megaUserAlert ? self.megaUserAlert->getNodeHandle() : ::mega::INVALID_HANDLE;
}

- (uint64_t)pendingContactRequestHandle {
    return self.megaUserAlert ? self.megaUserAlert->getPcrHandle() : ::mega::INVALID_HANDLE;
}

- (nullable NSString *)email {
    if (!self.megaUserAlert) return nil;
    
    return self.megaUserAlert->getEmail() ? [[NSString alloc] initWithUTF8String:self.megaUserAlert->getEmail()] : nil;
}

- (nullable NSString *)path {
    if (!self.megaUserAlert) return nil;
    
    return self.megaUserAlert->getPath()? [[NSString alloc] initWithUTF8String:self.megaUserAlert->getPath()] : nil;
}

- (nullable NSString *)name {
    if (!self.megaUserAlert) return nil;
    
    return self.megaUserAlert->getName() ? [[NSString alloc] initWithUTF8String:self.megaUserAlert->getName()] : nil;
}

- (nullable NSString *)heading {
    if (!self.megaUserAlert) return nil;
    
    return self.megaUserAlert->getHeading() ? [[NSString alloc] initWithUTF8String:self.megaUserAlert->getHeading()] : nil;
}

- (nullable NSString *)title {
    if (!self.megaUserAlert) return nil;
    
    return self.megaUserAlert->getTitle() ? [[NSString alloc] initWithUTF8String:self.megaUserAlert->getTitle()] : nil;
}

- (BOOL)isOwnChange {
    return self.megaUserAlert ? self.megaUserAlert->isOwnChange() : NO;
}

#ifdef ENABLE_CHAT

- (uint64_t)scheduledMeetingId {
    return self.megaUserAlert ? self.megaUserAlert->getSchedId() : ::mega::INVALID_HANDLE;
}

#endif

- (int64_t)numberAtIndex:(NSUInteger)index {
    return self.megaUserAlert ? self.megaUserAlert->getNumber((unsigned int) index) : -1;
}

- (int64_t)timestampAtIndex:(NSUInteger)index {
    return self.megaUserAlert ? self.megaUserAlert->getTimestamp((unsigned int) index) : -1;
}

- (nullable NSString *)stringAtIndex:(NSUInteger)index {
    if (!self.megaUserAlert) return nil;
    
    return self.megaUserAlert->getString((unsigned int)index) ? [[NSString alloc] initWithUTF8String:self.megaUserAlert->getString((unsigned int)index)] : nil;
}

#ifdef ENABLE_CHAT

- (BOOL)hasScheduledMeetingChangeType:(MEGAUserAlertScheduledMeetingChangeType)changeType {
    if (!self.megaUserAlert) return NO;
    
    return self.megaUserAlert->hasSchedMeetingChanged(int(changeType));
}

- (nullable MEGAStringList *)titleList {
    return self.megaUserAlert ? [MEGAStringList.alloc initWithMegaStringList:self.megaUserAlert->getUpdatedTitle() cMemoryOwn:YES] : nil;
}

- (nullable NSArray<NSDate *> *)startDateList {
    if (!self.megaUserAlert || !self.megaUserAlert->getUpdatedStartDate()) { return nil; }
    
    MegaIntegerList *integerList = self.megaUserAlert->getUpdatedStartDate()->copy();
    NSMutableArray<NSDate *> *dateArray = [NSMutableArray arrayWithCapacity:integerList->size()];

    for (int i = 0; i < integerList->size(); i++) {
        NSInteger timeInterval = integerList->get(i);
        if (timeInterval != -1) {
            NSDate *date = [NSDate dateWithTimeIntervalSince1970:integerList->get(i)];
            if (date != nil) {
                [dateArray addObject:date];
            }
        }
    }
    
    delete integerList;
    return dateArray;
}

- (nullable NSArray<NSDate *> *)endDateList {
    if (!self.megaUserAlert || !self.megaUserAlert->getUpdatedEndDate()) { return nil; }
    
    MegaIntegerList *integerList = self.megaUserAlert->getUpdatedEndDate()->copy();
    NSMutableArray<NSDate *> *dateArray = [NSMutableArray arrayWithCapacity:integerList->size()];

    for (int i = 0; i < integerList->size(); i++) {
        NSInteger timeInterval = integerList->get(i);
        if (timeInterval != -1) {
            NSDate *date = [NSDate dateWithTimeIntervalSince1970:integerList->get(i)];
            if (date != nil) {
                [dateArray addObject:date];
            }
        }
    }
    
    delete integerList;
    return dateArray;
}

#endif

@end
