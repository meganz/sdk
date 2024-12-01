/**
 * @file GfxProviderCG.mm
 * @brief Graphics layer using Cocoa Touch
 *
 * (c) 2013-2015 by Mega Limited, Auckland, New Zealand
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

#include "mega.h"
#import <AVFoundation/AVFoundation.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIImage.h>
#else
#import <AppKit/NSImage.h>
#endif
#import <QuickLookThumbnailing/QuickLookThumbnailing.h>

const CGFloat COMPRESSION_QUALITY = 0.8f;
const int THUMBNAIL_MIN_SIZE = 200;
const int64_t WAIT_60_SECONDS = 60;

using namespace mega;

#ifndef USE_FREEIMAGE

GfxProviderCG::GfxProviderCG(): sourceURL(NULL) {
    w = h = 0;
    semaphore = dispatch_semaphore_create(0);
}

GfxProviderCG::~GfxProviderCG() {
    freebitmap();
    if (sourceURL) {
        CFRelease(sourceURL);
        sourceURL = NULL;
    }
}

const char* GfxProviderCG::supportedformats() {
    return ".bmp.cr2.crw.cur.dng.gif.heic.ico.j2c.jp2.jpf.jpeg.jpg.nef.orf.pbm.pdf.pgm.png.pnm.ppm.psd.raf.rw2.rwl.tga.tif.tiff.3g2.3gp.avi.m4v.mov.mp4.mqv.qt.webp.jxl.avif.";
}

const char* GfxProviderCG::supportedvideoformats() {
    return NULL;
}

bool GfxProviderCG::readbitmap(const LocalPath& path, int size) {
    // Convenience.
    using mega::detail::AdjustBasePathResult;
    using mega::detail::adjustBasePath;

    // Ensure provided path is absolute.
    AdjustBasePathResult absolutePath = adjustBasePath(path);

    // Make absolute path usable to Cocoa.
    NSString* sourcePath =
      [NSString stringWithCString: absolutePath.c_str()
                encoding:NSUTF8StringEncoding];

    // Couldn't create a Cocoa-friendly path.
    if (sourcePath == nil) {
        return false;
    }
    
    sourceURL = (CFURLRef)CFBridgingRetain([NSURL fileURLWithPath:sourcePath isDirectory:NO]);
    if (sourceURL == NULL) {
        return false;
    }

    w = h = 0;

    NSString *fileExtension = [sourcePath pathExtension];
    if (fileExtension) {
        UTType *type = [UTType typeWithFilenameExtension:fileExtension];
        
        if ([type conformsToType:UTTypeMovie]) {
            AVAsset *asset = [AVAsset assetWithURL:(__bridge NSURL *)sourceURL];
            AVAssetTrack *videoTrack = [[asset tracksWithMediaType:AVMediaTypeVideo] firstObject];
            CGSize naturalSize = videoTrack.naturalSize;
            w = naturalSize.width;
            h = naturalSize.height;
        } else if ([type conformsToType:UTTypeImage]) {
            CFMutableDictionaryRef imageOptions = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            CFDictionaryAddValue(imageOptions, kCGImageSourceShouldCache, kCFBooleanFalse);
            CGImageSourceRef imageSource = CGImageSourceCreateWithURL(sourceURL, imageOptions);
            if (imageSource) {
                CFDictionaryRef imageProperties = CGImageSourceCopyPropertiesAtIndex(imageSource, 0, imageOptions);
                if (imageProperties) {
                    CFNumberRef width = (CFNumberRef)CFDictionaryGetValue(imageProperties, kCGImagePropertyPixelWidth);
                    CFNumberRef height = (CFNumberRef)CFDictionaryGetValue(imageProperties, kCGImagePropertyPixelHeight);
                    if (width && height) {
                        CGFloat value;
                        if (CFNumberGetValue(width, kCFNumberCGFloatType, &value)) {
                            w = value;
                        }
                        if (CFNumberGetValue(height, kCFNumberCGFloatType, &value)) {
                            h = value;
                        }
                    }
                    CFRelease(imageProperties);
                }
                CFRelease(imageSource);
            }
            
            if (imageOptions) {
                CFRelease(imageOptions);
            }
        }
    }

    if (!(w && h)) {
        w = h = size;
    }
    return w && h;
}

static inline CGRect tileRect(size_t w, size_t h)
{
    CGRect res;
    // square rw*rw crop thumbnail
    res.size.width = res.size.height = std::min(w, h);
    if (w < h)
    {
        res.origin.x = 0;
        res.origin.y = (h - w) / 2;
    }
    else
    {
        res.origin.x = (w - h) / 2;
        res.origin.y = 0;
    }
    return res;
}

#if !(TARGET_OS_IPHONE)
static inline NSData* dataForImage(CGImageRef image) {
    NSBitmapImageRep *bitmap = [[NSBitmapImageRep alloc] initWithCGImage:image];
    NSDictionary *properties = @{NSImageCompressionFactor : @(COMPRESSION_QUALITY)};
    return [bitmap representationUsingType:NSBitmapImageFileTypeJPEG properties:properties];
}
#endif

bool GfxProviderCG::resizebitmap(int rw, int rh, string* jpegout) {
    jpegout->clear();
    
    bool isThumbnail = !rh;
    
    if (isThumbnail) {
        if (w > h) {
            rh = THUMBNAIL_MIN_SIZE;
            rw = THUMBNAIL_MIN_SIZE * w / h;
        } else if (h > w) {
            rh = THUMBNAIL_MIN_SIZE * h / w;
            rw = THUMBNAIL_MIN_SIZE;
        } else {
            rw = rh = THUMBNAIL_MIN_SIZE;
        }
    }

    CGSize size = CGSizeMake(rw, rh);
    __block NSData *data;

    QLThumbnailGenerationRequest *request = [[QLThumbnailGenerationRequest alloc] initWithFileAtURL:(__bridge NSURL *)sourceURL size:size scale:1.0 representationTypes:QLThumbnailGenerationRequestRepresentationTypeThumbnail];

    [QLThumbnailGenerator.sharedGenerator generateBestRepresentationForRequest:request completionHandler:^(QLThumbnailRepresentation * _Nullable thumbnail, NSError * _Nullable error) {
        if (error) {
            LOG_err << "Error generating best representation for a request: " << error.localizedDescription;
        } else {
            if (isThumbnail) {
                CGImageRef newImage = CGImageCreateWithImageInRect(thumbnail.CGImage, tileRect(CGImageGetWidth(thumbnail.CGImage), CGImageGetHeight(thumbnail.CGImage)));
#if TARGET_OS_IPHONE
                data = UIImageJPEGRepresentation([UIImage imageWithCGImage:newImage], COMPRESSION_QUALITY);
                if (newImage) {
                    CFRelease(newImage);
                }
#else
                data = dataForImage(newImage);
#endif
            } else {
#if TARGET_OS_IPHONE
                data = UIImageJPEGRepresentation(thumbnail.UIImage, COMPRESSION_QUALITY);
#else
                data = dataForImage(thumbnail.CGImage);
#endif
            }
        }
        if (this->semaphore) {
            dispatch_semaphore_signal(this->semaphore);
        }
    }];
    
    dispatch_time_t waitTime = dispatch_time(DISPATCH_TIME_NOW, WAIT_60_SECONDS * NSEC_PER_SEC);
    dispatch_semaphore_wait(semaphore, waitTime);
    jpegout->assign((char*) data.bytes, data.length);
    return data;
}

void GfxProviderCG::freebitmap() {
    w = h = 0;
}

#endif

void ios_statsid(std::string *statsid) {
    NSMutableDictionary *queryDictionary = [[NSMutableDictionary alloc] init];
    [queryDictionary setObject:(__bridge id)kSecClassGenericPassword forKey:(__bridge id)kSecClass];
    [queryDictionary setObject:@"statsid" forKey:(__bridge id)kSecAttrAccount];
    [queryDictionary setObject:@"MEGA" forKey:(__bridge id)kSecAttrService];
    [queryDictionary setObject:(__bridge id)(kSecAttrSynchronizableAny) forKey:(__bridge id)(kSecAttrSynchronizable)];
    [queryDictionary setObject:@YES forKey:(__bridge id)kSecReturnData];
    [queryDictionary setObject:(__bridge id)kSecMatchLimitOne forKey:(__bridge id)kSecMatchLimit];
    [queryDictionary setObject:(__bridge id)kSecAttrAccessibleAfterFirstUnlock forKey:(__bridge id)kSecAttrAccessible];

    CFTypeRef result = NULL;
    OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)queryDictionary, &result);

    switch (status) {
        case errSecSuccess: {
            NSString *uuidString = [[NSString alloc] initWithData:(__bridge_transfer NSData *)result encoding:NSUTF8StringEncoding];
            statsid->append([uuidString UTF8String]);
            break;
        }

        case errSecItemNotFound: {
            NSString *uuidString = [[[NSUUID alloc] init] UUIDString];

            NSData *uuidData = [uuidString dataUsingEncoding:NSUTF8StringEncoding];
            [queryDictionary setObject:uuidData forKey:(__bridge id)kSecValueData];
            [queryDictionary setObject:(__bridge id)kSecAttrAccessibleAfterFirstUnlock forKey:(__bridge id)kSecAttrAccessible];
            [queryDictionary removeObjectForKey:(__bridge id)kSecReturnData];
            [queryDictionary removeObjectForKey:(__bridge id)kSecMatchLimit];

            status = SecItemAdd((__bridge CFDictionaryRef)queryDictionary, NULL);

            switch (status) {
                case errSecSuccess: {
                    statsid->append([uuidString UTF8String]);
                    break;
                }
                case errSecDuplicateItem: {
                    [queryDictionary removeObjectForKey:(__bridge id)kSecAttrAccessible];
                    [queryDictionary setObject:@YES forKey:(__bridge id)kSecReturnData];
                    [queryDictionary setObject:(__bridge id)kSecMatchLimitOne forKey:(__bridge id)kSecMatchLimit];
                    
                    status = SecItemCopyMatching((__bridge CFDictionaryRef)queryDictionary, &result);
                    
                    switch (status) {
                        case errSecSuccess: {
                            NSString *uuidString = [[NSString alloc] initWithData:(__bridge_transfer NSData *)result encoding:NSUTF8StringEncoding];
                            statsid->append([uuidString UTF8String]);
                            break;
                        }
                    }
                    
                    [queryDictionary removeObjectForKey:(__bridge id)kSecReturnData];
                    [queryDictionary removeObjectForKey:(__bridge id)kSecMatchLimit];
                    NSMutableDictionary *attributesToUpdate = [[NSMutableDictionary alloc] init];
                    [attributesToUpdate setObject:(__bridge id)kSecAttrAccessibleAfterFirstUnlock forKey:(__bridge id)kSecAttrAccessible];
                    
                    status = SecItemUpdate((__bridge CFDictionaryRef)queryDictionary, (__bridge CFDictionaryRef)attributesToUpdate);
                    
                    switch (status) {
                        case errSecSuccess:
                            LOG_debug << "Update statsid keychain item to allow access it after first unlock";
                            break;
                            
                        default:
                            LOG_err << "SecItemUpdate failed with error code " << status;
                            break;
                    }
                    break;
                }
                default: {
                    LOG_err << "SecItemAdd failed with error code " << status;
                    break;
                }
            }
            break;
        }
        default: {
            LOG_err << "SecItemCopyMatching failed with error code " << status;
            break;
        }
    }
}

void ios_appbasepath(std::string *appbasepath) {
    appbasepath->assign([NSHomeDirectory() UTF8String]);
}
