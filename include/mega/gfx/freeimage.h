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
#include <mega/filesystem.h>
#include <mega/gfx.h>
#include "mega/gfx/gfx_pdfium.h"

namespace mega {
// bitmap graphics processor
class MEGA_API GfxProcFreeImage : public GfxProc
{
    FIBITMAP* dib;

    bool readbitmap(FileAccess*, const LocalPath&, int);
    bool resizebitmap(int, int, string*);
    void freebitmap();

public:
	GfxProcFreeImage();
    ~GfxProcFreeImage();

protected:

    string sformats;
    const char* supportedformats();

    bool readbitmapFreeimage(FileAccess*, const LocalPath&, int);

#if defined(HAVE_FFMPEG)  || defined(HAVE_PDFIUM)
    static std::mutex gfxMutex;
#endif

#ifdef HAVE_FFMPEG
    const char* supportedformatsFfmpeg();
    bool isFfmpegFile(const string &ext);
    bool readbitmapFfmpeg(FileAccess*, const LocalPath&, int);
#endif

#ifdef HAVE_PDFIUM
    const char* supportedformatsPDF();
    bool isPdfFile(const string &ext);
    bool readbitmapPdf(FileAccess*, const LocalPath&, int);
#endif

};
} // namespace

#endif
#endif
