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
#import "MEGANode.h"
#import "MEGASearchFilterTimeFrame.h"

typedef NS_ENUM (NSInteger, MEGASearchFilterSensitiveOption) {
    MEGASearchFilterSensitiveOptionDisabled = 0,
    MEGASearchFilterSensitiveOptionNonSensitiveOnly = 1,
    MEGASearchFilterSensitiveOptionSensitiveOnly = 2
};

typedef NS_ENUM (NSInteger, MEGASearchFilterFavouriteOption) {
    MEGASearchFilterFavouriteOptionDisabled = 0,
    MEGASearchFilterFavouriteOptionFavouritesOnly = 1,
    MEGASearchFilterFavouriteOptionNonFavouritesOnly = 2
};

NS_ASSUME_NONNULL_BEGIN

@interface MEGASearchFilter : NSObject

@property NSString * term;
@property NSString * _Nullable searchDescription;
@property MEGASearchFilterTimeFrame * _Nullable creationTimeFrame;
@property MEGASearchFilterTimeFrame * _Nullable modificationTimeFrame;
@property uint64_t parentNodeHandle;

/**
 * @brief Set option for filtering by predefined node types.
 *
 * Valid values for this parameter are (invalid values will be ignored):
 * - MEGANodeTypeUnknown = -1  --> all types
 * - MEGANodeTypeFile = 0 --> Returns file nodes only
 * - MEGANodeTypeFolder = 1 --> Returns folder nodes only
 */
@property (readonly, nonatomic) MEGANodeType nodeType;

/**
 * @brief Set option for filtering by predefined file categories.
 *
 * Category of files requested in the search
 * Valid values for this parameter are (invalid values will be ignored):
 * - MEGANodeFormatTypeUnknown = 0  --> no particular category, include folders too
 * - MEGANodeFormatTypePhoto  = 1
 * - MEGANodeFormatTypeAudio = 2
 * - MEGANodeFormatTypeVideo = 3
 * - MEGANodeFormatTypeDocument = 4
 * - MEGANodeFormatTypePdf = 5
 * - MEGANodeFormatTypePresentation = 6
 * - MEGANodeFormatTypeArchive = 7
 * - MEGANodeFormatTypeProgram = 8
 * - MEGANodeFormatTypeMisc = 9
 * - MEGANodeFormatTypeSpreadsheet = 10
 * - MEGANodeFormatTypeAllDocs = 11  --> any of {DOCUMENT, PDF, PRESENTATION, SPREADSHEET}
 * - MEGANodeFormatTypeOthers = 12
 */
@property (readonly, nonatomic) MEGANodeFormatType category;

/**
 * @brief Option for filtering out sensitive nodes.
 * 
 * Option to determine node inclusion based on sensitive criteria.
 *
 * Valid values for this parameter are (invalid values will be ignored):
 * - MEGASearchFilterSensitiveOptionDisabled = 0 --> All nodes are taken in consideration, no
 * filter is applied
 * - MEGASearchFilterSensitiveOptionNonSensitiveOnly = 1 --> Returns nodes not marked as sensitive (nodes
 * with property set or if any of their ancestors have it are considered sensitive)
 * - MEGASearchFilterSensitiveOptionSensitiveOnly = 2 --> Returns nodes with property set to true
 * (regardless of their children)
 */
@property (readonly, nonatomic) MEGASearchFilterSensitiveOption sensitiveFilter;

/**
 * @brief Set option for filtering out nodes based on isFavourite.
 *
 * Valid values for this parameter are (invalid values will be ignored):
 * - MEGASearchFilterFavouriteOptionDisabled = 0 --> Both favourites and non favourites are considered
 * - MEGASearchFilterFavouriteOptionFavouritesOnly = 1 --> Only favourites
 * - MEGASearchFilterFavouriteOptionNonFavouritesOnly = 2 --> Only non favourites
 */
@property (readonly, nonatomic) MEGASearchFilterFavouriteOption favouriteFilter;

@property int locationType;
@property BOOL useAndForTextQuery;

// TODO: This is a temporary proxy to support existing higher modules calls without having to modify their code. To be removed before Search By Description feature is fully implemented.
- (instancetype)initWithTerm:(NSString *)term
            parentNodeHandle:(uint64_t)parentNodeHandle
                    nodeType:(MEGANodeType)nodeType
                    category:(MEGANodeFormatType)category
             sensitiveFilter:(MEGASearchFilterSensitiveOption)sensitiveFilter
             favouriteFilter:(MEGASearchFilterFavouriteOption)favouriteFilter
           creationTimeFrame:(MEGASearchFilterTimeFrame* _Nullable)creationTimeFrame
       modificationTimeFrame:(MEGASearchFilterTimeFrame* _Nullable)modificationTimeFrame;

// TODO: This is a temporary proxy to support existing higher modules calls without having to modify their code. To be removed before Search By Description feature is fully implemented.
- (instancetype)initWithTerm:(NSString *)term
                    nodeType:(MEGANodeType)nodeType
                    category:(MEGANodeFormatType)category
             sensitiveFilter:(MEGASearchFilterSensitiveOption)sensitiveFilter
             favouriteFilter:(MEGASearchFilterFavouriteOption)favouriteFilter
                locationType:(int)locationType
           creationTimeFrame:(MEGASearchFilterTimeFrame* _Nullable)creationTimeFrame
       modificationTimeFrame:(MEGASearchFilterTimeFrame* _Nullable)modificationTimeFrame;

- (instancetype)initWithTerm:(NSString *)term
                 description:(NSString * _Nullable)description
            parentNodeHandle:(uint64_t)parentNodeHandle
                    nodeType:(MEGANodeType)nodeType
                    category:(MEGANodeFormatType)category
             sensitiveFilter:(MEGASearchFilterSensitiveOption)sensitiveFilter
             favouriteFilter:(MEGASearchFilterFavouriteOption)favouriteFilter
           creationTimeFrame:(MEGASearchFilterTimeFrame * _Nullable)creationTimeFrame
       modificationTimeFrame:(MEGASearchFilterTimeFrame * _Nullable)modificationTimeFrame
          useAndForTextQuery:(BOOL)useAndForTextQuery;

- (instancetype)initWithTerm:(NSString *)term
                 description:(NSString * _Nullable)description
                    nodeType:(MEGANodeType)nodeType
                    category:(MEGANodeFormatType)category
             sensitiveFilter:(MEGASearchFilterSensitiveOption)sensitiveFilter
             favouriteFilter:(MEGASearchFilterFavouriteOption)favouriteFilter
                locationType:(int)locationType
           creationTimeFrame:(MEGASearchFilterTimeFrame * _Nullable)creationTimeFrame
       modificationTimeFrame:(MEGASearchFilterTimeFrame * _Nullable)modificationTimeFrame
          useAndForTextQuery:(BOOL)useAndForTextQuery;

- (BOOL)didSetParentNodeHandle;
- (BOOL)didSetLocationType;

@end

NS_ASSUME_NONNULL_END
