/**
 * @file MEGAPushNotificationSettings.mm
 * @brief Push Notification related MEGASdk methods.
 *
 * (c) 2019- by Mega Limited, Auckland, New Zealand
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

#import "MEGAPushNotificationSettings.h"
#import "megaapi.h"
#import "MEGAPushNotificationSettings+init.h"

using namespace mega;

@interface MEGAPushNotificationSettings ()

@property MegaPushNotificationSettings *megaPushNotificationSettings;
@property BOOL cMemoryOwn;

@end

@implementation MEGAPushNotificationSettings

#pragma mark - Initializer.

- (instancetype)init {
    self = [super init];
    
    if (self != nil) {
        _megaPushNotificationSettings = MegaPushNotificationSettings::createInstance();
        _cMemoryOwn = YES;
    }
    
    return self;
}

#pragma mark - Private methods.

- (instancetype)initWithMegaPushNotificationSettings:(MegaPushNotificationSettings *)megaPushNotificationSettings cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self) {
        _megaPushNotificationSettings = megaPushNotificationSettings;
        _cMemoryOwn = cMemoryOwn;
    }
    
    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaPushNotificationSettings;
    }
}

- (MegaPushNotificationSettings *)getCPtr {
    return self.megaPushNotificationSettings;
}

#pragma mark - Property getter and setter methods.

- (int64_t)globalChatsDNDTimestamp {
    return self.megaPushNotificationSettings->getGlobalChatsDnd();
}

- (void)setGlobalChatsDNDTimestamp:(int64_t)globalChatsDNDTimestamp {
    self.megaPushNotificationSettings->setGlobalChatsDnd(globalChatsDNDTimestamp);
}

- (BOOL)globalChatsDndEnabled {
    return self.megaPushNotificationSettings->isGlobalChatsDndEnabled();
}

- (void)setGlobalChatsDndEnabled:(BOOL)globalChatsDndEnabled {
    self.megaPushNotificationSettings->enableChats(!globalChatsDndEnabled);
}

#pragma mark - Interface methods.

- (BOOL)isChatDndEnabledForChatId:(uint64_t)chatId {
    return self.megaPushNotificationSettings->isChatDndEnabled(chatId);
}

- (void)setChatEnabled:(BOOL)enabled forChatId:(uint64_t)chatId {
    self.megaPushNotificationSettings->enableChat(chatId, enabled);
}

- (int64_t)timestampForChatId:(uint64_t)chatId {
    return self.megaPushNotificationSettings->getChatDnd(chatId);
}

- (void)setChatDndForChatId:(uint64_t)chatId untilTimestamp:(int64_t)timestamp {
    self.megaPushNotificationSettings->setChatDnd(chatId, timestamp);
}

@end
