/**
 * @file MEGATreeProcessorDelegate.h
 * @brief Delegate to process node trees
 *
 * (c) 2013-2017 by Mega Limited, Auckland, New Zealand
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
 * @brief Protocol to process node trees
 *
 * An implementation of this class can be used to process a node tree passing a pointer to
 * [MEGASdk processMEGANodeTree:recursive:delegate:]
 *
 * The implementation will receive callbacks from an internal worker thread.
 *
 */
@protocol MEGATreeProcessorDelegate <NSObject>

/**
 * @brief Function that will be called for all nodes in a node tree
 * @param node MEGANode to be processed
 * @return YES to continue processing nodes, NO to stop
 */
- (BOOL)processMEGANode:(MEGANode *)node;

@end

NS_ASSUME_NONNULL_END
