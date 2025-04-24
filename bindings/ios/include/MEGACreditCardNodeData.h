/**
 * @file MEGACreditCardNodeData.h
 * @brief Object Data for Password Node attributes
 *
 * (c) 2025 by Mega Limited, Auckland, New Zealand
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

@interface MEGACreditCardNodeData : NSObject

/**
 * @brief Get cardNumber attribute value.
 *
 * @return A string with the cardNumber value or nil if not set
 */
@property (readonly, nonatomic, nullable) NSString *cardNumber;

/**
 * @brief Get notes attribute value.
 *
 * @return A string with the notes value or nil if not set
 */
@property (readonly, nonatomic, nullable) NSString *notes;

/**
 * @brief Get cardHolderName attribute value.
 *
 * @return A string with the cardHolderName value or nil if not set
 */
@property (readonly, nonatomic, nullable) NSString *cardHolderName;

/**
 * @brief Get cvv attribute value.
 *
 * @return A string with the cvv value or nil if not set
 */
@property (readonly, nonatomic, nullable) NSString *cvv;

/**
 * @brief Get expiration date attribute value (in MM/YY format).
 *
 * @return A string with the expirationDate value or nil if not set
 */
@property (readonly, nonatomic, nullable) NSString *expirationDate;

/**
 * @brief Creates a new instance of MEGACreditCardNodeData.
 *
 * @param cardNumber Number of the card (All characters must be digits). This field
 * cannot be null nor empty when creating a new Credit card Node
 * @param notes Notes to attach to the Credit card node
 * @param cardHolderName Name of holder of Credit Card
 * @param cvv card verification Value of the Credit Card (All characters must be digits)
 * @param expirationDate expiration date of Credit card (Expected format `MM/YY` with MM
 * and YY digits)
 *
 * @note: nil can be used to specify that a field is not to be updated when calling
 * [MEGASdk updateCreditCardNodeWithHandle:newData:delegate:].
 *
 * @return A newly created MEGACreditCardNodeData object.
 */
-(instancetype)initWithCardNumber:(NSString *)cardNumber
                            notes:(nullable NSString *)notes
                   cardHolderName:(nullable NSString *)cardHolderName
                              cvv:(nullable NSString *)cvv
                   expirationDate:(nullable NSString *)expirationDate;

@end

NS_ASSUME_NONNULL_END
