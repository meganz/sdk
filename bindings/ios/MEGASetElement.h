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

typedef NS_ENUM (NSInteger, MEGASetElementChangeType) {
    MEGASetElementChangeTypeNew   = 0x01,
    MEGASetElementChangeTypeName  = 0x02,
    MEGASetElementChangeTypeOrder = 0x04,
    MEGASetElementChangeTypeSize  = 0x08
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
@property (readonly, nonatomic) NSDate *timestamp;

/**
 * @brief Returns name of current Element.
 *
 * @return name of current Element.
 */
@property (readonly, nonatomic, nullable) NSString *name;

/**
 * @brief Returns true if this SetElement has a specific change
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
 * - MEGASetElementChangeTypeName         = 0x02
 * Check if Set name has changed
 *
 * - MEGASetElementChangeTypeOrder        = 0x04
 * Check if Set cover has changed
 *
 * - MEGASetElementChangeTypeSize         = 0x08
 * Check if the Set was removed
 *
 * @return true if this SetElement has a specific change
 */
- (BOOL)hasChangedType:(MEGASetElementChangeType)changeType;

/**
 * @brief Creates a copy of this MEGASetElement object.
 *
 * The resulting object is fully independent of the source MEGASetElement,
 * it contains a copy of all internal attributes, so it will be valid after
 * the original object is deleted.
 *
 * @return Copy of the MEGASetElement object.
 */
- (instancetype)clone;

@end

NS_ASSUME_NONNULL_END
