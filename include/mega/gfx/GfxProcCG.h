//
//  GfxProcCG.h
//  MegaLib
//
//  Created by Andrei Stoleru on 16.03.2014.
//  Copyright (c) 2014 MEGA. All rights reserved.
//

#ifndef __MegaLib__CGMegaGFX__
#define __MegaLib__CGMegaGFX__

#include "mega.h"
#include <ImageIO/CGImageSource.h>

// bitmap graphics processor
class MEGA_API GfxProcCG : public mega::GfxProc
{
    CGImageSourceRef imageSource;
    CFDictionaryRef imageParams;
    CFMutableDictionaryRef thumbnailParams;
    CGFloat w, h;
    CGImageRef createThumbnailWithMaxSize(int size);
    int maxSizeForThumbnail(const int rw, const int rh);
private: // mega::GfxProc implementations
    bool isgfx(mega::string* name);
    bool readbitmap(mega::FileAccess*, mega::string*, int);
    bool resizebitmap(int, int, mega::string*);
    void freebitmap();
public:
    GfxProcCG();
    ~GfxProcCG();
};

#endif /* defined(__MegaLib__CGMegaGFX__) */
