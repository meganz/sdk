/**
 * @file MEGAAccountType.h
 * @brief Details about a MEGA account type enum
 *
 * (c) 2024 by Mega Limited, Auckland, New Zealand
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

typedef NS_ENUM (NSInteger, MEGAAccountType) {
    MEGAAccountTypeUnknown = -1,
    MEGAAccountTypeFree = 0,
    MEGAAccountTypeProI = 1,
    MEGAAccountTypeProII = 2,
    MEGAAccountTypeProIII = 3,
    MEGAAccountTypeLite = 4,
    MEGAAccountTypeStarter = 11,
    MEGAAccountTypeBasic = 12,
    MEGAAccountTypeEssential = 13,
    MEGAAccountTypeBusiness = 100,
    MEGAAccountTypeProFlexi = 101,
    MEGAAccountTypeFeature = 99999
};
