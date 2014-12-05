#import <Foundation/Foundation.h>
#import "MEGATransfer.h"

/**
 * @brief List of MEGATransfer objects.
 *
 * Objects of this class are immutable.
 *
 * @see [MEGASdk transfers].
 */
@interface MEGATransferList : NSObject

/**
 * @brief The number of MEGATransfer objects in the list.
 */
@property (readonly, nonatomic) NSNumber *size;

/**
 * @brief The MEGATransfer at the position index in the MEGATransferList.
 *
 * If the index is >= the size of the list, this function returns nil.
 *
 * @param index Position of the MEGATransfer that we want to get for the list.
 * @return MEGATransfer at the position index in the list.
 */
- (MEGATransfer *)transferAtIndex:(NSInteger)index;

@end
