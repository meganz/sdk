//
//  MEGANodeList.h
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MEGANode.h"

/**
 * @brief List of MEGANode objects
 *
 * A MEGANodeList has the ownership of the MEGANode objects that it contains, so they will be
 * only valid until the NodeList is deleted. If you want to retain a MEGAMode returned by
 * a MEGANodeList, use [MEGANode clone].
 *
 * Objects of this class are immutable.
 *
 * @see [MEGASdk childrenWithParent:], [MEGASdk inShares]
 */
@interface MEGANodeList : NSObject

/**
 * @brief The number of MEGANode objects in the list
 */
@property (readonly, nonatomic) NSNumber *size;

/**
 * @brief Returns the MEGANode at the position index in the MEGANodeList
 *
 * The MEGANodeList retains the ownership of the returned MEGANode. It will be only valid until
 * the MEGANodeList is deleted.
 *
 * If the index is >= the size of the list, this function returns nil.
 *
 * @param index Position of the MEGANode that we want to get for the list
 * @return MEGANode at the position index in the list
 */
- (MEGANode *)nodeAtIndex:(NSInteger)index;

@end
