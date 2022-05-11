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
#include <CoreGraphics/CGBitmapContext.h>
#include <ImageIO/CGImageDestination.h>
#include <MobileCoreServices/UTCoreTypes.h>
#include <ImageIO/CGImageProperties.h>
#import <Foundation/NSString.h>
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIImage.h>
#import <MobileCoreServices/UTType.h>

using namespace mega;

#ifndef USE_FREEIMAGE

GfxProviderCG::GfxProviderCG()
    : imageSource(NULL)
{
    w = h = 0;
    thumbnailParams = CFDictionaryCreateMutable(kCFAllocatorDefault, 3,
                                                &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionaryAddValue(thumbnailParams, kCGImageSourceCreateThumbnailWithTransform, kCFBooleanTrue);
    CFDictionaryAddValue(thumbnailParams, kCGImageSourceCreateThumbnailFromImageAlways, kCFBooleanTrue);

    float comp = 0.75f;
    CFNumberRef compression = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloatType, &comp);
    imageParams = CFDictionaryCreate(kCFAllocatorDefault, (const void **)&kCGImageDestinationLossyCompressionQuality, (const void **)&compression, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFRelease(compression);
}

GfxProviderCG::~GfxProviderCG() {
    freebitmap();
    if (thumbnailParams) {
        CFRelease(thumbnailParams);
    }
    if (imageParams) {
        CFRelease(imageParams);
    }
}

const char* GfxProviderCG::supportedformats() {
    if ([[NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleExecutable"] isEqualToString:@"MEGAFiles"]) {
        return "";
    }
    return ".bmp.cr2.crw.cur.dng.gif.heic.ico.j2c.jp2.jpf.jpeg.jpg.nef.orf.pbm.pdf.pgm.png.pnm.ppm.psd.raf.rw2.rwl.tga.tif.tiff.3g2.3gp.avi.m4v.mov.mp4.mqv.qt.webp.";
}

const char* GfxProviderCG::supportedvideoformats() {
    return NULL;
}

bool GfxProviderCG::readbitmap(FileSystemAccess* fa, const LocalPath& name, int size) {
    string absolutename;
    NSString *sourcePath;
    if (PosixFileSystemAccess::appbasepath && !name.beginsWithSeparator()) {
        absolutename = PosixFileSystemAccess::appbasepath;
        absolutename.append(name.platformEncoded());
        sourcePath = [NSString stringWithCString:absolutename.c_str() encoding:[NSString defaultCStringEncoding]];
    } else {
        sourcePath = [NSString stringWithCString:name.platformEncoded().c_str() encoding:[NSString defaultCStringEncoding]];
    }
    
    if (sourcePath == nil) {
        return false;
    }
    
    NSURL *sourceURL = [NSURL fileURLWithPath:sourcePath isDirectory:NO];
    if (sourceURL == nil) {
        return false;
    }

    w = h = 0;

    CFMutableDictionaryRef imageOptions = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionaryAddValue(imageOptions, kCGImageSourceShouldCache, kCFBooleanFalse);

    CFStringRef fileExtension = (__bridge CFStringRef)[sourcePath pathExtension];
    CFStringRef fileUTI = UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension, fileExtension, NULL);
    if (UTTypeConformsTo(fileUTI, kUTTypeMovie)) {
        AVURLAsset *asset = [[AVURLAsset alloc] initWithURL:sourceURL options:nil];
        AVAssetImageGenerator *generator = [[AVAssetImageGenerator alloc] initWithAsset:asset];
        generator.appliesPreferredTrackTransform = TRUE;
        CMTime requestedTime = CMTimeMake(1, 60);
        CGImageRef imgRef = [generator copyCGImageAtTime:requestedTime actualTime:NULL error:NULL];
        if (imgRef) {
            NSData *imgData = UIImageJPEGRepresentation([UIImage imageWithCGImage:imgRef], 1);
            if (imgData) {
                imageSource = CGImageSourceCreateWithData((__bridge CFDataRef)imgData, imageOptions);
            }
            CGImageRelease(imgRef);
        }
    } else {
        imageSource = CGImageSourceCreateWithURL((__bridge CFURLRef)sourceURL, imageOptions);
    }

    if (fileUTI) {
        CFRelease(fileUTI);
    }

    if (!imageSource) {
        CFRelease(imageOptions);
        return false;
    }

    CFDictionaryRef imageProperties = CGImageSourceCopyPropertiesAtIndex(imageSource, 0, imageOptions);
    if (imageProperties) { // trying to get width and heigth from properties
        CFNumberRef width = (CFNumberRef)CFDictionaryGetValue(imageProperties, kCGImagePropertyPixelWidth);
        CFNumberRef heigth = (CFNumberRef)CFDictionaryGetValue(imageProperties, kCGImagePropertyPixelHeight);
        if (width && heigth) {
            CGFloat value;
            if (CFNumberGetValue(width, kCFNumberCGFloatType, &value)) {
                w = value;
            }
            if (CFNumberGetValue(heigth, kCFNumberCGFloatType, &value)) {
                h = value;
            }
        }
        CFRelease(imageProperties);
    }

    if (imageOptions) {
        CFRelease(imageOptions);
    }

    if (!(w && h)) { // trying to get fake size from thumbnail
        CGImageRef thumbnail = createThumbnailWithMaxSize(size);
        if (!thumbnail) {
            return false;
        }
        w = (int) CGImageGetWidth(thumbnail);
        h = (int) CGImageGetHeight(thumbnail);
        CGImageRelease(thumbnail);
    }
    return w && h;
}

CGImageRef GfxProviderCG::createThumbnailWithMaxSize(int size) {
    CFNumberRef maxSize = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &size);
    CFDictionarySetValue(thumbnailParams, kCGImageSourceThumbnailMaxPixelSize, maxSize);
    CFRelease(maxSize);

    return CGImageSourceCreateThumbnailAtIndex(imageSource, 0, thumbnailParams);
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

int GfxProviderCG::maxSizeForThumbnail(const int rw, const int rh) {
    if (rh) { // rectangular rw*rh bounding box
        return std::max(rw, rh);
    }
    // square rw*rw crop thumbnail
    return ceil(rw * ((double)std::max(w, h) / (double)std::min(w, h)));
}

bool GfxProviderCG::resizebitmap(int rw, int rh, string* jpegout) {
    if (!imageSource) {
        return false;
    }

    jpegout->clear();

    CGImageRef image = createThumbnailWithMaxSize(maxSizeForThumbnail(rw, rh));
    if (!rh) { // Make square image
        CGImageRef newImage = CGImageCreateWithImageInRect(image, tileRect(CGImageGetWidth(image), CGImageGetHeight(image)));
        if (image) {
            CGImageRelease(image);
        }
        image = newImage;
    }
    CFMutableDataRef data = CFDataCreateMutable(kCFAllocatorDefault, 0);
    if (!data) {
        CGImageRelease(image);
        return false;
    }
    CGImageDestinationRef destination = CGImageDestinationCreateWithData(data, kUTTypeJPEG, 1, NULL);
    if (!destination) {
        CGImageRelease(image);
        CFRelease(data);
        return false;
    }
    CGImageDestinationAddImage(destination, image, imageParams);
    bool success = CGImageDestinationFinalize(destination);
    CGImageRelease(image);
    CFRelease(destination);

    jpegout->assign((char*)CFDataGetBytePtr(data), CFDataGetLength(data));
    CFRelease(data);
    return success;
}

void GfxProviderCG::freebitmap() {
    if (imageSource) {
        CFRelease(imageSource);
        imageSource = NULL;
    }
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
