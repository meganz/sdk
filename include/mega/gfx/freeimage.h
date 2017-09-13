/**
 * @file freeimage.h
 * @brief Graphics layer implementation using FreeImage
 *
 * (c) 2014 by Mega Limited, Wellsford, New Zealand
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

#ifdef USE_FREEIMAGE
#ifndef GFX_CLASS
#define GFX_CLASS GfxProcFreeImage

#include <FreeImage.h>

namespace mega {
// bitmap graphics processor
class MEGA_API GfxProcFreeImage : public GfxProc
{
    enum {
        ROTATION_UP             = 1,    // No rotation (top-left)
        ROTATION_UP_MIRRORED    = 2,    // Horizontal mirrored (top-right)
        ROTATION_DOWN           = 3,    // Rotated 180 degrees (bottom-right)
        ROTATION_DOWN_MIRRORED  = 4,    // Vertical mirrored (bottom-left)
        ROTATION_LEFT_MIRRORED  = 5,    // Rotated and mirrored (left-top)
        ROTATION_LEFT           = 6,    // Rotated 270 degrees (left-bottom)
        ROTATION_RIGHT_MIRRORED = 7,    // Rotated and mirrored (right-bottom)
        ROTATION_RIGHT          = 8     // Rotated 90 degrees (right-top)
    };

    FIBITMAP* dib;

    bool readbitmap(FileAccess*, string*, int);
    bool resizebitmap(int, int, string*);
    void freebitmap();

public:
	GfxProcFreeImage();

protected:
    const char* supportedformats();
};
} // namespace

#endif
#endif
