/**
 * @file MEGASetElement.h
 * @brief Represents an MEGASetElement of a MEGASet in MEGA
 *
 * It allows to get all data related to an Element of a Set in MEGA.
 *
 * (c) 2022- by Mega Limited, Auckland, New Zealand
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

typedef NS_ENUM(NSUInteger, MEGASetElementChangeType) {
    MEGASetElementChangeTypeNew         = 0x01,
    MEGASetElementChangeTypeName        = 0x02,
    MEGASetElementChangeTypeOrder       = 0x04,
    MEGASetElementChangeTypeRemoved     = 0x08
};

NS_ASSUME_NONNULL_BEGIN

/**
 * @brief Represents an Element of a Set in MEGA
 *
 * It allows to get all data related to an Element of a Set in MEGA.
 *
 * Objects of this class aren't live, they are snapshots of the state of an Element of a Set
 * in MEGA when the object is created, they are immutable.
 *
 */
@interface MEGASetElement : NSObject

/**
 * @brief Returns id of current Element.
 *
 * @return Element id.
 */
@property (readonly, nonatomic) uint64_t handle;

/**
 * @brief Returns id of MegaSet current MegaSetElement belongs to.
 *
 * @return MegaSet id.
 */
@property (readonly, nonatomic) uint64_t ownerId;

/**
 * @brief Returns order of current Element.
 *
 * If not set explicitly, the API will typically set it to multiples of 1000.
 *
 * @return order of current Element.
 */
@property (readonly, nonatomic) uint64_t order;

/**
 * @brief Returns handle of file-node represented by current Element.
 *
 * @return file-node handle.
 */
@property (readonly, nonatomic) uint64_t nodeId;

/**
 * @brief Returns timestamp of latest changes to current Element.
 *
 * @return timestamp value.
 */
@property (readonly, nonatomic, nullable) NSDate *timestamp;

/**
 * @brief Returns name of current Element.
 *
 * @return name of current Element.
 */
@property (readonly, nonatomic, nullable) NSString *name;

/**
 * @brief Returns YES if this SetElement has a specific change
 *
 * This value is only useful for Sets notified by [MEGADelegate onSetElementsUpdate:sets:]  or
 * [MEGAGlobalDelegate onSetElementsUpdate:sets:] that can notify about Set modifications.
 *
 * In other cases, the return value of this function will be always false.
 *
 * @param changeType The type of change to check. It can be one of the following values:
 *
 * - MEGASetElementChangeTypeNew          = 0x01
 * Check if the Set was new
 *
 * - MEGASetElementChangeTypeName        = 0x02
 * Check if Set name has changed
 *
 * - MEGASetElementChangeTypeOrder        = 0x04
 * Check if Set cover has changed
 *
 * - MEGASetElementChangeTypeSize          = 0x08
 * Check if the Set was removed
 *
 * @return YES if this SetElement has a specific change
 */
- (BOOL)hasChangedType:(MEGASetElementChangeType)changeType;

/**
 * @brief Returns changes  for MEGASetElement
 *
 * Note that the position of each bit matches the MEGASetElementChangeType value
 *
 * @return combination of changes in
 */
- (MEGASetElementChangeType)changes;

@end

NS_ASSUME_NONNULL_END
