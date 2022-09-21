/**
 * @file MEGANode.h
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

#import <Foundation/Foundation.h>

typedef NS_ENUM (NSInteger, MEGASetChangeType) {
    MEGASetChangeTypeNew      = 0x01,
    MEGASetChangeTypeName     = 0x02,
    MEGASetChangeTypeCover    = 0x04,
    MEGASetChangeTypeRemoved  = 0x08
};

NS_ASSUME_NONNULL_BEGIN

/**
 * @brief Represents a Set in MEGA
 *
 * It allows to get all data related to a Set in MEGA.
 *
 * Objects of this class aren't live, they are snapshots of the state of a Set
 * in MEGA when the object is created, they are immutable.
 *
 */
@interface MEGASet : NSObject

/**
 * @brief Returns id of current Set.
 *
 * @return Set id.
 */
@property (readonly, nonatomic) uint64_t handle;

/**
 * @brief Returns id of user that owns current Set.
 *
 * @return user id.
 */
@property (readonly, nonatomic) uint64_t userId;

/**
 * @brief Returns id of Element set as 'cover' for current Set.
 *
 * It will return INVALID_HANDLE if no cover was set or if the Element became invalid
 * (was removed) in the meantime.
 *
 * @return Element id.
 */
@property (readonly, nonatomic) uint64_t cover;

/**
 * @brief Returns timestamp of latest changes to current Set (but not to its Elements).
 *
 * @return timestamp value.
 */
@property (readonly, nonatomic) NSDate *timestamp;

/**
 * @brief Returns name of current Set.
 *
 * The MegaSet object retains the ownership of the returned string, it will be valid until
 * the MegaSet object is deleted.
 *
 * @return name of current Set.
 */
@property (readonly, nonatomic, nullable) NSString *name;

/**
 * @brief Returns true if this Set has a specific change
 *
 * This value is only useful for Sets notified by [MEGADelegate onSetsUpdate:sets:]  or
 * [MEGAGlobalDelegate onSetsUpdate:sets:] that can notify about Set modifications.
 *
 * In other cases, the return value of this function will be always false.
 *
 * @param changeType The type of change to check. It can be one of the following values:
 *
 * - MEGASetChangeTypeNew                  = 0x01
 * Check if the Set was new
 *
 * - MEGASetChangeTypeName                 = 0x02
 * Check if Set name has changed
 *
 * - MEGASetChangeTypeCover                = 0x04
 * Check if Set cover has changed
 *
 * - MEGASetChangeTypeRemoved              = 0x08
 * Check if the Set was removed
 *
 * @return true if this Set has a specific change
 */
- (BOOL)hasChangedType:(MEGASetChangeType)changeType;

/**
 * @brief Creates a copy of this MEGASet object.
 *
 * The resulting object is fully independent of the source MEGASet,
 * it contains a copy of all internal attributes, so it will be valid after
 * the original object is deleted.
 *
 * @return Copy of the MEGASet object.
 */
- (instancetype)clone;

@end

NS_ASSUME_NONNULL_END
