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

@property (readonly) NSNumber *storageUsed;
@property (readonly) NSNumber *storageMax;
@property (readonly) NSNumber *transferOwnUsed;
@property (readonly) NSNumber *transferMax;
@property (readonly) MEGAAccountType type;

- (NSNumber *)storageUsedWithHandle:(uint64_t)handle;
- (NSNumber *)numberFilesWithHandle:(uint64_t)handle;
- (NSNumber *)numberFoldersWithHandle:(uint64_t)handle;

@end
