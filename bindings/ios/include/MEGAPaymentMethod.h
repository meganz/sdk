/**
 * @file MEGAPaymentMethod.h
 * @brief Payment methods
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
typedef NS_ENUM(NSInteger, MEGAPaymentMethod) {
    MEGAPaymentMethodBalance = 0,
    MEGAPaymentMethodPaypal = 1,
    MEGAPaymentMethodItunes = 2,
    MEGAPaymentMethodGoogleWallet = 3,
    MEGAPaymentMethodBitcoin = 4,
    MEGAPaymentMethodUnionPay = 5,
    MEGAPaymentMethodFortumo = 6,
    MEGAPaymentMethodStripe = 7,
    MEGAPaymentMethodCreditCard = 8,
    MEGAPaymentMethodCentili = 9,
    MEGAPaymentMethodPaysafeCard = 10,
    MEGAPaymentMethodAstropay = 11,
    MEGAPaymentMethodReserved = 12,
    MEGAPaymentMethodWindowsStore = 13,
    MEGAPaymentMethodTpay = 14,
    MEGAPaymentMethodDirectReseller = 15,
    MEGAPaymentMethodECP = 16,
    MEGAPaymentMethodSabadell = 17,
    MEGAPaymentMethodHuaweiWallet = 18,
    MEGAPaymentMethodStripe2 = 19,
    MEGAPaymentMethodWireTransfer = 999
};
