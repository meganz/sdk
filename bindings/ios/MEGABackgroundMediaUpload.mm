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
    return [self initWithBackgroundMediaUpload:mega::MegaBackgroundMediaUpload::createInstance(sdk.getCPtr)];
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
    std::string suffix = self.mediaUpload->encryptFile(inputFilePath.UTF8String, start, length, outputFilePath.UTF8String, adjustsSizeOnly);
    return @(suffix.c_str());
}

- (NSString *)uploadURLString {
    return @(self.mediaUpload->getUploadURL().c_str());
}

- (void)setCoordinatesWithLatitude:(NSNumber *)latitude longitude:(NSNumber *)longitude isUnshareable:(BOOL)unshareable {
    self.mediaUpload->setCoordinates(latitude ? latitude.doubleValue : mega::MegaNode::INVALID_COORDINATE,
                                     longitude ? longitude.doubleValue : mega::MegaNode::INVALID_COORDINATE,
                                     unshareable);
}

- (NSData *)serialize {
    std::string binary = self.mediaUpload->serialize();
    return [NSData dataWithBytes:binary.data() length:binary.size()];
}

+ (instancetype)unserializByData:(NSData *)data MEGASdk:(MEGASdk *)sdk {
    mega::MegaBackgroundMediaUpload *mediaUpload = mega::MegaBackgroundMediaUpload::unserialize((const char *)data.bytes, sdk.getCPtr);
    return [[self alloc] initWithBackgroundMediaUpload:mediaUpload];
}

@end
