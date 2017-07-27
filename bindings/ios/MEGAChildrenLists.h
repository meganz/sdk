/**
 * @file MEGAChildrenLists.h
 * @brief Provides information about node's children organize 
 * them into two list (files and folders)
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
#import "MEGANodeList.h"

/**
 * @brief Lists of file and folder children MEGANode objects
 *
 * A MEGAChildrenLists object has the ownership of the MEGANodeList objects that it contains,
 * so they will be only valid until the MEGAChildrenLists is deleted. If you want to retain
 * a MEGANodeList returned by a MEGAChildrenLists, use [MEGANodeList clone].
 *
 * Objects of this class are immutable.
 */
@interface MEGAChildrenLists : NSObject

/**
 * @brief List of MEGANode folders
 */
@property (nonatomic, readonly) MEGANodeList *folderList;

/**
 * @brief List of MEGANode files
 */
@property (nonatomic, readonly) MEGANodeList *fileList;

- (instancetype)clone;

@end
