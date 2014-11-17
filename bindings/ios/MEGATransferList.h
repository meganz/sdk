//
//  MEGATransferList.h
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>
#import "MEGATransfer.h"

/**
 * @brief List of MEGATransfer objects
 *
 * A MEGATransferList has the ownership of the MEGATransfer objects that it contains, so they will be
 * only valid until the MEGATransferList is deleted. If you want to retain a MEGATransfer returned by
 * a MEGATransferList, use [MEGATransfer clone].
 *
 * Objects of this class are immutable.
 *
 * @see [MEGASdk transfers]
 */
@interface MEGATransferList : NSObject

/**
 * @brief The number of MEGATransfer objects in the list
 * @return Number of MEGATransfer objects in the list
 */
@property (readonly, nonatomic) NSNumber *size;

/**
 * @brief The MEGATransfer at the position index in the MEGATransferList
 *
 * The MEGATransferList retains the ownership of the returned MEGATransfer. It will be only valid until
 * the MEGATransferList is deleted.
 *
 * If the index is >= the size of the list, this function returns nil.
 *
 * @param index Position of the MEGATransfer that we want to get for the list
 * @return MEGATransfer at the position index in the list
 */
- (MEGATransfer *)transferAtIndex:(NSInteger)index;

@end
