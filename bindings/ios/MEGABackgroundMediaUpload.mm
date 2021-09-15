/**
 * @file MEGABackgroundMediaUpload.mm
 * @brief Background media upload
 *
 * (c) 2018 - by Mega Limited, Auckland, New Zealand
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

#import "MEGABackgroundMediaUpload.h"
#import "megaapi.h"
#import "MEGABackgroundMediaUpload+init.h"
#import "MEGASdk+init.h"

@interface MEGABackgroundMediaUpload ()

@property (nonatomic) mega::MegaBackgroundMediaUpload *mediaUpload;

@end

@implementation MEGABackgroundMediaUpload

- (instancetype)initWithBackgroundMediaUpload:(mega::MegaBackgroundMediaUpload *)mediaUpload {
    self = [super init];
    if (self) {
        _mediaUpload = mediaUpload;
    }
    
    return self;
}

- (instancetype)initWithMEGASdk:(MEGASdk *)sdk {
    mega::MegaApi *api = sdk.getCPtr;
    if (api) {
        return [self initWithBackgroundMediaUpload:mega::MegaBackgroundMediaUpload::createInstance(api)];
    } else {
        return nil;
    }
}

- (void)dealloc {
    delete _mediaUpload;
}

- (mega::MegaBackgroundMediaUpload *)getCPtr {
    return self.mediaUpload;
}

- (BOOL)analyseMediaInfoForFileAtPath:(NSString *)inputFilepath {
    return self.mediaUpload->analyseMediaInfo(inputFilepath.UTF8String);
}

- (NSString *)encryptFileAtPath:(NSString *)inputFilePath startPosition:(int64_t)start length:(int64_t *)length outputFilePath:(NSString *)outputFilePath adjustsSizeOnly:(BOOL)adjustsSizeOnly {
    const char *val = self.mediaUpload->encryptFile(inputFilePath.UTF8String, start, length, outputFilePath.UTF8String, adjustsSizeOnly);
    NSString *suffix = val == NULL ? nil : @(val);
    
    delete [] val;
    
    return suffix;
}

- (NSString *)uploadURLString {
    const char *val = self.mediaUpload->getUploadURL();
    NSString *urlString = val == NULL ? nil : @(val);
    
    delete [] val;
    
    return urlString;
}

- (void)setCoordinatesWithLatitude:(double)latitude longitude:(double)longitude isUnshareable:(BOOL)unshareable {
    self.mediaUpload->setCoordinates(latitude, longitude, unshareable);
}

- (NSData *)serialize {
    const char *binary = self.mediaUpload->serialize();
    NSData *data = binary == NULL ? nil : [NSData dataWithBytes:binary length:strlen(binary)];
    
    delete [] binary;
    
    return data;
}

+ (instancetype)unserializByData:(NSData *)data MEGASdk:(MEGASdk *)sdk {
    mega::MegaApi *api = sdk.getCPtr;
    if (api) {
        mega::MegaBackgroundMediaUpload *mediaUpload = mega::MegaBackgroundMediaUpload::unserialize((const char *)data.bytes, api);
        return [[self alloc] initWithBackgroundMediaUpload:mediaUpload];
    } else {
        return nil;
    }
}

@end
