//
//  MEGAShareList.h
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MEGAShare.h"

/**
 * @brief List of MEGAShare objects
 *
 * A MEGAShareList has the ownership of the MEGAShare objects that it contains, so they will be
 * only valid until the MEGAShareList is deleted. If you want to retain a MEGAShare returned by
 * a MEGAShareList, use [MEGAShare clone].
 *
 * Objects of this class are immutable.
 *
 * @see [MEGASdk outSharesWithNode:]
 */
@interface MEGAShareList : NSObject

/**
 * @brief The number of MEGAShare objects in the list
 * @return Number of MEGAShare objects in the list
 */
@property (readonly, nonatomic) NSNumber *size;

/**
 * @brief Returns the MEGAShare at the position index in the MEGAShareList
 *
 * The MEGAShareList retains the ownership of the returned MEGAShare. It will be only valid until
 * the MEGAShareList is deleted.
 *
 * If the index is >= the size of the list, this function returns nil.
 *
 * @param index Position of the MEGAShare that we want to get for the list
 * @return MEGAShare at the position index in the list
 */
- (MEGAShare *)shareAtIndex:(NSInteger)index;

@end
