#import <Foundation/Foundation.h>
#import "MEGAShare.h"

/**
 * @brief List of MEGAShare objects.
 *
 * Objects of this class are immutable.
 *
 * @see [MEGASdk outSharesForNode:]
 */
@interface MEGAShareList : NSObject

/**
 * @brief Number of MEGAShare objects in the list.
 */
@property (readonly, nonatomic) NSNumber *size;

/**
 * @brief Returns the MEGAShare at the position index in the MEGAShareList.
 *
 * If the index is >= the size of the list, this function returns nil.
 *
 * @param index Position of the MEGAShare that we want to get for the list.
 * @return MEGAShare at the position index in the list.
 */
- (MEGAShare *)shareAtIndex:(NSInteger)index;

@end
