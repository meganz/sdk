/**
 * @file MEGADownloadItemList.h
 * @brief List of nodes to download in one batch, each with its own options
 *
 * (c) 2026 by Mega Limited, Auckland, New Zealand
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
 * @brief The roots of a batch download, each with its own destination and options
 *
 * Build one of these and hand it to
 * [MEGASdk startDownloadBatchWithItems:cancelToken:collisionCheck:collisionResolution:delegate:],
 * which downloads the whole selection as a single MEGATransfer.
 */
@interface MEGADownloadItemList : NSObject

/**
 * @brief The number of items in the list
 */
@property (readonly, nonatomic) NSInteger size;

/**
 * @brief Add a node to download, with its destination and per-node options
 *
 * @param node Node to download. The node inserted is a copy of it
 * @param localPath Destination for this node, following the same rule as
 * [MEGASdk startDownloadNode:localPath:]: a path ending in '/' is a folder the node is stored
 * inside under its own name, any other path names the destination itself. Pass nil to store it
 * in the current folder. Items may each name a different destination
 * @param customName Local name to store this node under, or nil for the node's own name
 * @param appData Custom app data for the MEGATransfer objects this item produces, or nil
 * @param startFirst Put this item ahead of whatever is already queued. Ignored for a folder
 * @param undelete Recover a node that has been completely deleted. Only valid for a file
 */
- (void)addNode:(MEGANode *)node
      localPath:(nullable NSString *)localPath
     customName:(nullable NSString *)customName
        appData:(nullable NSString *)appData
     startFirst:(BOOL)startFirst
       undelete:(BOOL)undelete;

/**
 * @brief Returns the node of the item at the position index, or nil when out of range
 */
- (nullable MEGANode *)nodeAtIndex:(NSInteger)index;

/**
 * @brief Returns the destination of the item at the position index, or nil when none was given
 */
- (nullable NSString *)localPathAtIndex:(NSInteger)index;

/**
 * @brief Returns the custom local name of the item at the position index, or nil when none
 */
- (nullable NSString *)customNameAtIndex:(NSInteger)index;

/**
 * @brief Returns the custom app data of the item at the position index, or nil when none
 */
- (nullable NSString *)appDataAtIndex:(NSInteger)index;

/**
 * @brief Returns whether the item at the position index is queued ahead of existing work
 */
- (BOOL)startFirstAtIndex:(NSInteger)index;

/**
 * @brief Returns whether the item at the position index is being recovered
 */
- (BOOL)undeleteAtIndex:(NSInteger)index;

@end

NS_ASSUME_NONNULL_END
