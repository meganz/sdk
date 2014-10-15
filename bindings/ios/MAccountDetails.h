//
//  MAccountDetails.h
//
//  Created by Javier Navarro on 02/10/14.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#import <Foundation/Foundation.h>

typedef NS_ENUM (NSInteger, MAccountType) {
    MAccountTypeFree = 0,
    MAccountTypeProI,
    MAccountTypeProII,
    MAccountTypeProIII
};

@interface MAccountDetails : NSObject

- (NSNumber *)getUsedStorage;
- (NSNumber *)getMaxStorage;
- (NSNumber *)getOwnUsedTransfer;
- (NSNumber *)getMaxTransfer;
- (MAccountType)getProLevel;

@end
