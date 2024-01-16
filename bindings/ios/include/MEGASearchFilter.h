/**
 * @file MEGASearchFilter.h
 * @brief Class which encapsulates all data used for search nodes filtering
 *
 * (c) 2023- by Mega Limited, Auckland, New Zealand
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
#import "MEGASearchFilterTimeFrame.h"

NS_ASSUME_NONNULL_BEGIN

@interface MEGASearchFilter : NSObject

@property NSString* term;
@property MEGASearchFilterTimeFrame* _Nullable creationTimeFrame;
@property MEGASearchFilterTimeFrame* _Nullable modificationTimeFrame;
@property uint64_t parentNodeHandle;
@property int nodeType;
@property int category;
@property bool sensitivity;
@property int locationType;

- (instancetype)initWithTerm:(NSString *)term
            parentNodeHandle:(uint64_t)parentNodeHandle
                    nodeType:(int)nodeType
                    category:(int)category
                 sensitivity:(bool)sensitivity
           creationTimeFrame:(MEGASearchFilterTimeFrame* _Nullable)creationTimeFrame
       modificationTimeFrame:(MEGASearchFilterTimeFrame* _Nullable)modificationTimeFrame;

- (instancetype)initWithTerm:(NSString *)term
                    nodeType:(int)nodeType
                    category:(int)category
                 sensitivity:(bool)sensitivity
                locationType:(int)locationType
           creationTimeFrame:(MEGASearchFilterTimeFrame* _Nullable)creationTimeFrame
       modificationTimeFrame:(MEGASearchFilterTimeFrame* _Nullable)modificationTimeFrame;

- (BOOL)didSetParentNodeHandle;
- (BOOL)didSetLocationType;

@end

NS_ASSUME_NONNULL_END
