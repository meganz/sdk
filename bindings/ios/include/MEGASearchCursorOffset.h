/**
 * @file MEGASearchCursorOffset.h
 * @brief Cursor position for cursor-based (keyset) pagination.
 *
 * (c) 2026- by Mega Limited, Auckland, New Zealand
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

NS_ASSUME_NONNULL_BEGIN

/**
 * @brief Cursor position for cursor-based (keyset) pagination in
 *        MEGASdk listAllNodesByPage queries.
 *
 * Build this from the last node returned in the previous page.
 * The sort-key fields that must be set depend on the sort order:
 *
 *   MEGAOrderTypeDefaultAsc / MEGAOrderTypeDefaultDesc            : lastName, lastHandle
 *   MEGAOrderTypeSizeAsc    / MEGAOrderTypeSizeDesc               : lastName, lastHandle, lastSize
 *   MEGAOrderTypeModificationAsc / MEGAOrderTypeModificationDesc  : lastName, lastHandle, lastMtime
 *   MEGAOrderTypeLabelAsc   / MEGAOrderTypeLabelDesc              : lastName, lastHandle, lastLabel
 *   MEGAOrderTypeFavouriteAsc / MEGAOrderTypeFavouriteDesc        : lastName, lastHandle, lastFav
 *
 * Fields not relevant to the chosen order may be left at their defaults.
 */
@interface MEGASearchCursorOffset : NSObject

/**
 * @brief Name of the last node. Required for all sort orders. Default: nil.
 */
@property (nonatomic, copy, nullable) NSString *lastName;

/**
 * @brief Handle of the last node. Required for all sort orders.
 *        Default: INVALID_HANDLE (~0).
 */
@property (nonatomic) uint64_t lastHandle;

/**
 * @brief File size in bytes. Required for size orders. Default: -1 (unset).
 *        Any negative value is treated as unset.
 */
@property (nonatomic) int64_t lastSize;

/**
 * @brief Modification timestamp. Required for modification orders.
 *        Default: -1 (unset). Any negative value is treated as unset.
 */
@property (nonatomic) int64_t lastMtime;

/**
 * @brief Node label value. Required for label orders. Default: -1 (unset).
 *        Any negative value is treated as unset.
 */
@property (nonatomic) NSInteger lastLabel;

/**
 * @brief Favourite value (0 or 1). Required for favourite orders.
 *        Default: -1 (unset). Any negative value is treated as unset.
 */
@property (nonatomic) NSInteger lastFav;

/**
 * @brief Media capture timestamp in milliseconds, for parity with the engine's cursor.
 *        MEGASortOrderType carries no capture-time value yet, so nothing on this binding
 *        needs it. Default: -1 (unset). Any negative value is treated as unset.
 */
@property (nonatomic) int64_t lastMediaTsMs;

- (instancetype)init;

@end

NS_ASSUME_NONNULL_END
