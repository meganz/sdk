/**
 * @file MEGAAchievementsDetails.h
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

#import <Foundation/Foundation.h>
#import "MEGAStringList.h"

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSUInteger, MEGAAchievement) {
    MEGAAchievementWelcome = 1,
    MEGAAchievementInvite = 3,
    MEGAAchievementDesktopInstall = 4,
    MEGAAchievementMobileInstall = 5,
    MEGAAchievementAddPhone = 9,
    MEGAAchievementPassFreeTrial = 10,
    MEGAAchievementVPNFreeTrial = 11
};

/**
 * @brief The MEGAAchievementsDetails class
 *
 * There are several MEGA Achievements that a user can unlock, resulting in a
 * temporary extension of the storage and/or transfer quota during a period of
 * time.
 *
 * Currently there are 4 different classes of MEGA Achievements:
 *
 *  - Welcome: Create your free account and get 35 GB of complimentary storage space,
 *      valid for 30 days.
 *
 *  - Invite: Invite as many friends or coworkers as you want. For every signup under the
 *      invited email address, you will receive 10 GB of complimentary storage plus 20 GB
 *      of transfer quota, both valid for 365 days, provided that the new user installs
 *      either MEGAsync or a mobile app and starts using MEGA.
 *
 *  - Desktop install: When you install MEGAsync you get 20 GB of complimentary storage
 *      space plus 40 GB of transfer quota, both valid for 180 days.
 *
 *  - Mobile install: When you install our mobile app you get 15 GB of complimentary
 *      storage space plus 30 GB transfer quota, both valid for 180 days.
 *
 * When the user unlocks one of the achievements above, it unlocks an "Award". The award
 * includes a timestamps to indicate when it was unlocked, plus an expiration timestamp.
 * Afterwards, the award will not be active. Additionally, each award results in a "Reward".
 * The reward is linked to the corresponding award and includes the storage and transfer
 * quota obtained thanks to the unlocked award.
 *
 * @note It may take 2-3 days for achievements to show on the account after they have been completed.
 */

@interface MEGAAchievementsDetails : NSObject

/**
 * @brief The base storage value for this account, in bytes
 */
@property (nonatomic, readonly) long long baseStorage;

/**
 * @brief Returns the actual storage achieved by this account
 *
 * This function considers the base storage (permanent) plus all the
 * storage granted to the logged in account as result of the unlocked
 * achievements. It does not consider the expired achievements.
 *
 * @return The achieved storage for this account
 */
@property (nonatomic, readonly) long long currentStorage;

/**
 * @brief Returns the actual transfer quota achieved by this account
 *
 * This function considers all the transfer quota granted to the logged
 * in account as result of the unlocked achievements. It does not consider
 * the expired achievements.
 *
 * @return The achieved transfer quota for this account
 */
@property (nonatomic, readonly) long long currentTransfer;

/**
 * @brief Returns the actual achieved storage due to referrals
 *
 * This function considers all the storage granted to the logged in account
 * as result of the successful invitations (referrals). It does not consider
 * the expired achievements.
 *
 * @return The achieved storage by this account as result of referrals
 */
@property (nonatomic, readonly) long long currentStorageReferrals;

/**
 * @brief Returns the actual achieved transfer quota due to referrals
 *
 * This function considers all the transfer quota granted to the logged
 * in account as result of the successful invitations (referrals). It
 * does not consider the expired achievements.
 *
 * @return The transfer achieved quota by this account as result of referrals
 */
@property (nonatomic, readonly) long long currentTransferReferrals;


/**
 * @brief The number of unlocked awards for this account
 */
@property (nonatomic, readonly) NSUInteger awardsCount;

/**
 * @brief The number of active rewards for this account
 */
@property (nonatomic, readonly) NSInteger rewardsCount;

/**
 * @brief Checks if the corresponding achievement is valid
 *
 * Some achievements are valid only for some users.
 *
 * The following classes are valid:
 *  - MEGAAchievementWelcome = 1
 *  - MEGAAchievementInvite = 3
 *  - MEGAAchievementDesktopInstall = 4
 *  - MEGAAchievementMobileInstall = 5
 *  - MEGAAchievementAddPhone = 9
 *  - MEGAAchievementPassFreeTrial = 10
 *  - MEGAAchievementVPNFreeTrial = 11
 *
 * @param classId Id of the achievement.
 * @return True if it is valid, false otherwise
 */
- (bool)isValidClass:(NSInteger)classId;

/**
 * @brief Get the storage granted by a MEGA achievement class
 *
 * The following classes are valid:
 *  - MEGAAchievementWelcome = 1
 *  - MEGAAchievementInvite = 3
 *  - MEGAAchievementDesktopInstall = 4
 *  - MEGAAchievementMobileInstall = 5
 *  - MEGAAchievementAddPhone = 9
 *  - MEGAAchievementPassFreeTrial = 10
 *  - MEGAAchievementVPNFreeTrial = 11
 *
 * @param classId Id of the MEGA achievement
 * @return Storage granted by this MEGA achievement class, in bytes
 */
- (long long)classStorageForClassId:(NSInteger)classId;

/**
 * @brief Get the transfer quota granted by a MEGA achievement class
 *
 * The following classes are valid:
 *  - MEGAAchievementWelcome = 1
 *  - MEGAAchievementInvite = 3
 *  - MEGAAchievementDesktopInstall = 4
 *  - MEGAAchievementMobileInstall = 5
 *  - MEGAAchievementAddPhone = 9
 *  - MEGAAchievementPassFreeTrial = 10
 *  - MEGAAchievementVPNFreeTrial = 11
 *
 * @param classId Id of the MEGA achievement
 * @return Transfer quota granted by this MEGA achievement class, in bytes
 */
- (long long)classTransferForClassId:(NSInteger)classId;

/**
 * @brief Get the duration of storage/transfer quota granted by a MEGA achievement class
 *
 * The following classes are valid:
 *  - MEGAAchievementWelcome = 1
 *  - MEGAAchievementInvite = 3
 *  - MEGAAchievementDesktopInstall = 4
 *  - MEGAAchievementMobileInstall = 5
 *  - MEGAAchievementAddPhone = 9
 *  - MEGAAchievementPassFreeTrial = 10
 *  - MEGAAchievementVPNFreeTrial = 11
 *
 * The storage and transfer quota resulting from a MEGA achievement may expire after
 * certain number of days. In example, the "Welcome" reward lasts for 30 days and afterwards
 * the granted storage and transfer quota is revoked.
 *
 * @param classId Id of the MEGA achievement
 * @return Number of days for the storage/transfer quota granted by this MEGA achievement class
 */
- (NSInteger)classExpireForClassId:(NSInteger)classId;

/**
 * @brief Get the MEGA achievement class of the award
 * @param index Position of the award in the list of unlocked awards
 * @return The achievement class associated to the award in position \c index
 */
- (NSInteger)awardClassAtIndex:(NSUInteger)index;

/**
 * @brief Get the id of the award
 * @param index Position of the award in the list of unlocked awards
 * @return The id of the award in position \c index
 */
- (NSInteger)awardIdAtIndex:(NSUInteger)index;

/**
 * @brief Get the timestamp of the award (when it was unlocked)
 * @param index Position of the award in the list of unlocked awards
 * @return The timestamp of the award (when it was unlocked) in position \c index
 */
- (nullable NSDate *)awardTimestampAtIndex:(NSUInteger)index;

/**
 * @brief Get the expiration timestamp of the award
 *
 * After this moment, the storage and transfer quota granted as result of the award
 * will not be valid anymore.
 *
 * @note The expiration time may not be the \c awardTimestamp plus the number of days
 * returned by \c classExpireForClassId, since the award can be unlocked but not yet granted. It
 * typically takes 2 days from unlocking the award until the user is actually rewarded.
 *
 * @param index Position of the award in the list of unlocked awards
 * @return The expiration timestamp of the award in position \c index
 */
- (nullable NSDate *)awardExpirationAtIndex:(NSUInteger)index;

/**
 * @brief Get the list of referred emails for the award
 *
 * This function is specific for the achievements of class MEGAAchievementInvite.
 *
 * @param index Position of the award in the list of unlocked awards
 * @return The list of invited emails for the award in position \c index
 */
- (nullable MEGAStringList *)awardEmailsAtIndex:(NSUInteger)index;

/**
 * @brief Get the id of the award associated with the reward
 * @param index Position of the reward in the list of active rewards
 * @return The id of the award associated with the reward
 */
- (NSInteger)rewardAwardIdAtIndex:(NSUInteger)index;

/**
 * @brief Get the storage rewarded by the award
 * @param index Position of the reward in the list of active rewards
 * @return The storage rewarded by the award
 */
- (long long)rewardStorageAtIndex:(NSUInteger)index;

/**
 * @brief Get the transfer quota rewarded by the award
 * @param index Position of the reward in the list of active rewards
 * @return The transfer quota rewarded by the award
 */
- (long long)rewardTransferAtIndex:(NSUInteger)index;

/**
 * @brief Get the storage rewarded by the awardId
 * @param awardId The id of the award
 * @return The storage rewarded by the awardId
 */
- (long long)rewardStorageByAwardId:(NSInteger)awardId;

/**
 * @brief Get the transfer rewarded by the awardId
 * @param awardId The id of the award
 * @return The transfer rewarded by the awardId
 */
- (long long)rewardTransferByAwardId:(NSInteger)awardId;

/**
 * @brief Get the duration of the reward
 * @param index Position of the reward in the list of active rewards
 * @return The duration of the reward, in days
 */
- (NSInteger)rewardExpireAtIndex:(NSUInteger)index;

@end

NS_ASSUME_NONNULL_END
