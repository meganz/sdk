/**
 * @file MEGANodeList.h
 * @brief List of MEGANode objects
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
#import "MEGANode.h"

NS_ASSUME_NONNULL_BEGIN

/**
 * @brief List of MEGANode objects.
 *
 * Objects of this class are immutable.
 *
 * @see [MEGASdk childrenForParent:], [MEGASdk inShares:].
 */
@interface MEGANodeList : NSObject

/**
 * @brief The number of MEGANode objects in the list.
 */
@property (readonly, nonatomic) NSInteger size;

/**
 * @brief Add new node to nodeList
 * @param node to be added. The node inserted is a copy from 'node'
 */
- (void)addNode:(MEGANode *)node;

/**
 * @brief Returns the MEGANode at the position index in the MEGANodeList.
 *
 * If the index is >= the size of the list, this function returns nil.
 *
 * @param index Position of the MEGANode that we want to get for the list.
 * @return MEGANode at the position index in the list.
 */
- (nullable MEGANode *)nodeAtIndex:(NSInteger)index;

NS_ASSUME_NONNULL_END

@end
