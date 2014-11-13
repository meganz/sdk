//
//  MEGAAcountDetails.h
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>

typedef NS_ENUM (NSInteger, MEGAAccountType) {
    MEGAAccountTypeFree = 0,
    MEGAAccountTypeProI,
    MEGAAccountTypeProII,
    MEGAAccountTypeProIII
};

@interface MEGAAcountDetails : NSObject

- (NSNumber *)getUsedStorage;
- (NSNumber *)getMaxStorage;
- (NSNumber *)getOwnUsedTransfer;
- (NSNumber *)getMaxTransfer;
- (MEGAAccountType)getProLevel;

@end
