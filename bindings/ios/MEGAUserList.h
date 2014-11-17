//
//  MEGAUserList.h
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MEGAUser.h"

/**
 * @brief List of MEGAUser objects
 *
 * A MEGAUserList has the ownership of the MEGAUser objects that it contains, so they will be
 * only valid until the MEGAUserList is deleted. If you want to retain a MEGAUser returned by
 * a MEGAUserList, use [MEGAUser clone].
 *
 * Objects of this class are immutable.
 *
 * @see [MEGASdk contacts]
 *
 */
@interface MEGAUserList : NSObject

/**
 * @brief The number of MEGAUser objects in the list
 * @return Number of MEGAUser objects in the list
 */
@property (readonly, nonatomic) NSNumber *size;

/**
 * @brief Returns the MEGAUser at the position index in the MEGAUserList
 *
 * The MEGAUserList retains the ownership of the returned MEGAUser. It will be only valid until
 * the MEGAUserList is deleted.
 *
 * If the index is >= the size of the list, this function returns nil.
 *
 * @param index Position of the MEGAUser that we want to get for the list
 * @return MEGAUser at the position index in the list
 */
- (MEGAUser *)userAtIndex:(NSInteger)index;

@end
