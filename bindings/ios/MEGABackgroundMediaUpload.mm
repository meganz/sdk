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

- (void)dealloc {
    delete _mediaUpload;
}

- (mega::MegaBackgroundMediaUpload *)getCPtr {
    return self.mediaUpload;
}

- (BOOL)encryptFileAtPath:(NSString *)inputFilePath startPosition:(int64_t)start length:(unsigned *)length outputFilePath:(NSString *)outputFilePath urlSuffix:(NSString **)urlSuffix adjustsSizeOnly:(BOOL)adjustsSizeOnly {
    std::string suffix;
    BOOL succeed = self.mediaUpload->encryptFile(inputFilePath.UTF8String, start, length, outputFilePath.UTF8String, &suffix, adjustsSizeOnly);
    if (urlSuffix != NULL) {
        *urlSuffix = @(suffix.c_str());
    }
    
    return succeed;
}

- (NSString *)uploadURLString {
    std::string urlString;
    self.mediaUpload->getUploadURL(&urlString);
    return @(urlString.c_str());
}

- (NSData *)serialize {
    std::string binary = self.mediaUpload->serialize();
    return [NSData dataWithBytes:binary.data() length:binary.size()];
}

@end
