#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#import "MEGASdk.h"

#define imagesSet       [[NSSet alloc] initWithObjects:@"gif", @"jpg", @"tif", @"jpeg", @"bmp", @"png",@"nef", nil]
#define videoSet        [[NSSet alloc] initWithObjects:/*@"mkv",*/ @"avi", @"mp4", @"m4v", @"mpg", @"mpeg", @"mov", @"3gp",/*@"aaf",*/ nil]

#define isImage(n)        [imagesSet containsObject:n]
#define isVideo(n)        [videoSet containsObject:n]

#define kMEGANode @"kMEGANode"
#define kIndex @"kIndex"

@interface Helper : NSObject

+ (UIImage *)imageForNode:(MEGANode *)node;

+ (NSString *)pathForNode:(MEGANode *)node searchPath:(NSSearchPathDirectory)path directory:(NSString *)directory;

+ (NSString *)pathForNode:(MEGANode *)node searchPath:(NSSearchPathDirectory)path;

+ (NSString *)pathForUser:(MEGAUser *)user searchPath:(NSSearchPathDirectory)path directory:(NSString *)directory;

@end
