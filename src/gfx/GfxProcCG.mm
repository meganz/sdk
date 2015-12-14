/**
 * @file GfxProcCG.mm
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

#include "GfxProcCG.h"
#include <CoreGraphics/CGBitmapContext.h>
#include <ImageIO/CGImageDestination.h>
#include <MobileCoreServices/UTCoreTypes.h>
#include <ImageIO/CGImageProperties.h>
#import <Foundation/NSString.h>
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIImage.h>
#import <MobileCoreServices/UTType.h>

using namespace mega;

GfxProcCG::GfxProcCG()
    : GfxProc()
    , imageSource(NULL)
    , w(0)
    , h(0)
{
    thumbnailParams = CFDictionaryCreateMutable(kCFAllocatorDefault, 3,
                                                &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionaryAddValue(thumbnailParams, kCGImageSourceCreateThumbnailWithTransform, kCFBooleanTrue);
    CFDictionaryAddValue(thumbnailParams, kCGImageSourceCreateThumbnailFromImageAlways, kCFBooleanTrue);

    float comp = 0.75f;
    CFNumberRef compression = CFNumberCreate(kCFAllocatorDefault, kCFNumberFloatType, &comp);
    imageParams = CFDictionaryCreate(kCFAllocatorDefault, (const void **)&kCGImageDestinationLossyCompressionQuality, (const void **)&compression, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFRelease(compression);
}

GfxProcCG::~GfxProcCG() {
    freebitmap();
    if (thumbnailParams) {
        CFRelease(thumbnailParams);
    }
    if (imageParams) {
        CFRelease(imageParams);
    }
}

const char* GfxProcCG::supportedformats() {
    return ".jpg.png.bmp.tif.tiff.jpeg.gif.pdf.ico.cur.mov.mp4.m4v.3gp.";
}

bool GfxProcCG::readbitmap(FileAccess* fa, string* name, int size) {
    NSString *nameString = [NSString stringWithCString:name->c_str()
                                              encoding:[NSString defaultCStringEncoding]];
    
    CFStringRef fileExtension = (__bridge CFStringRef) [nameString pathExtension];
    CFStringRef fileUTI = UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension, fileExtension, NULL);
    
    CGDataProviderRef dataProvider = NULL;
    
    if (UTTypeConformsTo(fileUTI, kUTTypeMovie)) {
        NSURL *videoURL = [NSURL fileURLWithPath:nameString];
        AVURLAsset *asset = [[AVURLAsset alloc] initWithURL:videoURL options:nil];
        AVAssetImageGenerator *generator = [[AVAssetImageGenerator alloc] initWithAsset:asset];
        generator.appliesPreferredTrackTransform = TRUE;
        CMTime requestedTime = CMTimeMake(1, 60);
        CGImageRef imgRef = [generator copyCGImageAtTime:requestedTime actualTime:NULL error:NULL];
        
        UIImage *thumbnailImage = [[UIImage alloc] initWithCGImage:imgRef];
        CGImageRelease(imgRef);
        
        [UIImageJPEGRepresentation(thumbnailImage, 1) writeToFile:nameString.stringByDeletingPathExtension atomically:YES];
        
        dataProvider = CGDataProviderCreateWithFilename([nameString.stringByDeletingPathExtension UTF8String]);
        [[NSFileManager defaultManager] removeItemAtPath:nameString.stringByDeletingPathExtension error:nil];
    } else {
        dataProvider = CGDataProviderCreateWithFilename(name->c_str());
    }
    
    if (!dataProvider) {
        return false;
    }

    CFMutableDictionaryRef imageOptions = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                                                       &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionaryAddValue(imageOptions, kCGImageSourceShouldCache, kCFBooleanFalse);

    imageSource = CGImageSourceCreateWithDataProvider(dataProvider, imageOptions);
    CGDataProviderRelease(dataProvider);
    if (!imageSource) {
        return false;
    }

    CFDictionaryRef imageProperties = CGImageSourceCopyPropertiesAtIndex(imageSource, 0, imageOptions);
    if (imageProperties) { // trying to get width and heigth from properties
        CFNumberRef width = (CFNumberRef)CFDictionaryGetValue(imageProperties, kCGImagePropertyPixelWidth);
        CFNumberRef heigth = (CFNumberRef)CFDictionaryGetValue(imageProperties, kCGImagePropertyPixelHeight);
        if (width && heigth) {
            CFNumberGetValue(width, kCFNumberCGFloatType, &w);
            CFNumberGetValue(heigth, kCFNumberCGFloatType, &h);
        }
        CFRelease(imageProperties);
    }
    if (!((int)w && (int)h)) { // trying to get fake size from thumbnail
        CGImageRef thumbnail = createThumbnailWithMaxSize(100);
        if (!thumbnail) {
            return false;
        }
        w = CGImageGetWidth(thumbnail);
        h = CGImageGetHeight(thumbnail);
        CGImageRelease(thumbnail);
    }
    return (int)w && (int)h;
}

CGImageRef GfxProcCG::createThumbnailWithMaxSize(int size) {
    const double maxSizeDouble = size;
    CFNumberRef maxSize = CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &maxSizeDouble);
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

int GfxProcCG::maxSizeForThumbnail(const int rw, const int rh) {
    if (rh) { // rectangular rw*rh bounding box
        return std::max(rw, rh);
    }
    // square rw*rw crop thumbnail
    return (int)(rw * std::max(w, h) / std::min(w, h));
}

bool GfxProcCG::resizebitmap(int rw, int rh, string* jpegout) {
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
        return false;
    }
    CGImageDestinationRef destination = CGImageDestinationCreateWithData(data, kUTTypeJPEG, 1, NULL);
    if (!destination) {
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

void GfxProcCG::freebitmap() {
    if (imageSource) {
        CFRelease(imageSource);
        imageSource = NULL;
    }
    w = h = 0;
}
