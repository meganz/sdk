/**
 * @file MEGAAchievementsDetails.mm
 * @brief Achievements that a user can unlock
 *
 * (c) 2017- by Mega Limited, Auckland, New Zealand
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

#import "MEGAAchievementsDetails.h"
#import "MEGAAchievementsDetails+init.h"
#import "MEGAStringList+init.h"

using namespace mega;

@interface MEGAAchievementsDetails ()

@property MegaAchievementsDetails *megaAchievementsDetails;
@property BOOL cMemoryOwn;

@end

@implementation MEGAAchievementsDetails

- (instancetype)initWithMegaAchievementsDetails:(mega::MegaAchievementsDetails *)megaAchievementsDetails cMemoryOwn:(BOOL)cMemoryOwn {
    self = [super init];
    
    if (self) {
        _megaAchievementsDetails = megaAchievementsDetails;
        _cMemoryOwn = cMemoryOwn;
    }

    return self;
}

- (void)dealloc {
    if (self.cMemoryOwn) {
        delete _megaAchievementsDetails;
    }
}

- (mega::MegaAchievementsDetails *)getCPtr {
    return self.megaAchievementsDetails;
}

- (long long)baseStorage {
    return self.megaAchievementsDetails ? self.megaAchievementsDetails->getBaseStorage() : -1;
}

- (long long)currentStorage {
    return self.megaAchievementsDetails ? self.megaAchievementsDetails->currentStorage() : -1;
}

- (long long)currentTransfer {
    return self.megaAchievementsDetails ? self.megaAchievementsDetails->currentTransfer() : -1;
}

- (long long)currentStorageReferrals {
    return self.megaAchievementsDetails ? self.megaAchievementsDetails->currentStorageReferrals() : -1;
}

- (long long)currentTransferReferrals {
    return self.megaAchievementsDetails ? self.megaAchievementsDetails->currentTransferReferrals() : -1;
}

- (NSUInteger)awardsCount {
    return self.megaAchievementsDetails ? self.megaAchievementsDetails->getAwardsCount() : 0;
}

- (NSInteger)rewardsCount {
    return self.megaAchievementsDetails ? self.megaAchievementsDetails->getRewardsCount() : -1;
}

- (long long)classStorageForClassId:(NSInteger)classId {
    return self.megaAchievementsDetails ? self.megaAchievementsDetails->getClassStorage((int)classId) : -1;
}

- (long long)classTransferForClassId:(NSInteger)classId {
    return self.megaAchievementsDetails ? self.megaAchievementsDetails->getClassTransfer((int)classId) : -1;
}

- (NSInteger)classExpireForClassId:(NSInteger)classId {
    return self.megaAchievementsDetails ? self.megaAchievementsDetails->getClassExpire((int)classId) : 0;
}

- (NSInteger)awardClassAtIndex:(NSUInteger)index {
    return self.megaAchievementsDetails ? self.megaAchievementsDetails->getAwardClass((unsigned int)index) : 0;
}

- (NSInteger)awardIdAtIndex:(NSUInteger)index {
    return self.megaAchievementsDetails ? self.megaAchievementsDetails->getAwardId((unsigned int)index) : 0;
}

- (NSDate *)awardTimestampAtIndex:(NSUInteger)index {
    return self.megaAchievementsDetails ? [[NSDate alloc] initWithTimeIntervalSince1970:self.megaAchievementsDetails->getAwardTimestamp((unsigned int)index)] : nil;
}

- (NSDate *)awardExpirationAtIndex:(NSUInteger)index {
    return self.megaAchievementsDetails ? [[NSDate alloc] initWithTimeIntervalSince1970:self.megaAchievementsDetails->getAwardExpirationTs((unsigned int)index)] : nil;
}

- (MEGAStringList *)awardEmailsAtIndex:(NSUInteger)index {
    return self.megaAchievementsDetails ? [[MEGAStringList alloc] initWithMegaStringList:self.megaAchievementsDetails->getAwardEmails((unsigned int)index) cMemoryOwn:YES] : nil;
}

- (NSInteger)rewardAwardIdAtIndex:(NSUInteger)index {
    return self.megaAchievementsDetails ? self.megaAchievementsDetails->getRewardAwardId((unsigned int)index) : -1;
}

- (long long)rewardStorageAtIndex:(NSUInteger)index {
    return self.megaAchievementsDetails ? self.megaAchievementsDetails->getRewardStorage((unsigned int)index) : -1;
}

- (long long)rewardTransferAtIndex:(NSUInteger)index {
    return self.megaAchievementsDetails ? self.megaAchievementsDetails->getRewardTransfer((unsigned int)index) : -1;
}

- (long long)rewardStorageByAwardId:(NSInteger)awardId {
    return self.megaAchievementsDetails ? self.megaAchievementsDetails->getRewardStorageByAwardId((int)awardId) : -1;
}

- (long long)rewardTransferByAwardId:(NSInteger)awardId {
    return self.megaAchievementsDetails ? self.megaAchievementsDetails->getRewardTransferByAwardId((int)awardId) : -1;
}

- (NSInteger)rewardExpireAtIndex:(NSUInteger)index {
    return self.megaAchievementsDetails ? self.megaAchievementsDetails->getRewardExpire((unsigned int)index) : 0;
}

@end
