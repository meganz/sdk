/**
 * @file MEGACurrency.h
 * @brief Details about currencies
 *
 * (c) 2021- by Mega Limited, Auckland, New Zealand
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

@interface MEGACurrency : NSObject

/// @brief The currency symbol of prices, ie. €
@property (readonly, nonatomic, nullable) NSString *currencySymbol;

/// @brief The currency name of prices, ie. EUR
@property (readonly, nonatomic, nullable) NSString *currencyName;

/// @brief The currency symbol of local prices, ie. $
@property (readonly, nonatomic, nullable) NSString *localCurrencySymbol;

/// @brief The currency name of local prices, ie. NZD
@property (readonly, nonatomic, nullable) NSString *localCurrencyName;

@end

NS_ASSUME_NONNULL_END
