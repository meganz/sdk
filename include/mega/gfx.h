/**
 * @file gfx.h
 * @brief Bitmap graphics processing
 *
 * (c) 2014 by Mega Limited, Auckland, New Zealand
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

#ifndef GFX_H
#define GFX_H 1

namespace mega {
using namespace std;

// bitmap graphics processor
class MEGA_API GfxProc
{
    // read and store bitmap
    virtual bool readbitmap(FileAccess*, string*, int) = 0;

    // resize stored bitmap and store result as JPEG
    virtual bool resizebitmap(int, int, string*) = 0;
    
    // free stored bitmap
    virtual void freebitmap() = 0;

protected:
    // coordinate transformation
    static void transform(int&, int&, int&, int&, int&, int&);

    // list of supported extensions (NULL if no pre-filtering is needed)
    virtual const char* supportedformats();

    // list of supported video extensions (NULL if no pre-filtering is needed)
    virtual const char* supportedvideoformats();

public:
    // check whether the filename looks like a supported media type
    bool isgfx(string*);

    // check whether the filename looks like a video
    bool isvideo(string*);

    // generate all dimensions, write to metadata server and attach to PUT transfer or existing node
    // handle is uploadhandle or nodehandle
    // - must respect JPEG EXIF rotation tag
    // - must save at 85% quality (120*120 pixel result: ~4 KB)
    int gendimensionsputfa(FileAccess*, string*, handle, SymmCipher*, int = -1, bool checkAccess = true);

    // FIXME: read dynamically from API server
    typedef enum { THUMBNAIL, PREVIEW } meta_t;
    typedef enum { AVATAR250X250 } avatar_t;

    // generate and save a fa to a file
    bool savefa(string*, int, int, string*);

    // - w*0: largest square crop at the center (landscape) or at 1/6 of the height above center (portrait)
    // - w*h: resize to fit inside w*h bounding box
    static const int dimensions[][2];
    static const int dimensionsavatar[][2];

    MegaClient* client;
    int w, h;

    GfxProc();
    virtual ~GfxProc() { }
};
} // namespace

#endif
