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
#define GFX_CLASS GfxProviderFreeImage

#include <FreeImage.h>
#include <mutex>
#include <mega/filesystem.h>
#include <mega/gfx.h>
#include "mega/gfx/gfx_pdfium.h"

namespace mega {

// Thread-safe RAII management of the FreeImage library.
class FreeImageInstance
{
    // Serializes access to mNumReferences.
    static std::mutex mLock;

    // How many providers are referencing FreeImage?
    static std::size_t mNumReferences;

public:
    FreeImageInstance();

    ~FreeImageInstance();
}; // FreeImageInstance

// bitmap graphics processor
class MEGA_API GfxProviderFreeImage : public IGfxLocalProvider
{
    FreeImageInstance mLibraryInstance;

#ifdef HAVE_PDFIUM
    bool pdfiumInitialized;
#endif
    FIBITMAP* dib;

public:
    bool readbitmap(const LocalPath&, int) override;
    bool resizebitmap(int, int, string*, Hint hint) override;
    void freebitmap() override;

    const char* supportedformats() override;
    const char* supportedvideoformats() override;

    GfxProviderFreeImage();
    ~GfxProviderFreeImage();

protected:

    string sformats;
    bool readbitmapFreeimage(const LocalPath&, int);

#if defined(HAVE_FFMPEG)  || defined(HAVE_PDFIUM)
    static std::mutex gfxMutex;
#endif

#ifdef HAVE_FFMPEG
    const char* supportedformatsFfmpeg();
    bool isFfmpegFile(const string &ext);
    bool readbitmapFfmpeg(const LocalPath&, int);
#endif

#ifdef HAVE_PDFIUM
    const char* supportedformatsPDF();
    bool isPdfFile(const string &ext);
    bool readbitmapPdf(const LocalPath&, int);
#endif

#ifdef USE_MEDIAINFO
    bool readbitmapMediaInfo(const LocalPath& imagePath);
#endif
};
} // namespace

#endif
#endif
