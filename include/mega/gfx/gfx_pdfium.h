/**
 * @file gfx_pdfium.h
 * @brief class to get bitmaps from PDF files using pdfium
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

#ifndef GFX_PDFIUM_H
#define GFX_PDFIUM_H 1

#include "mega/types.h"

#ifdef HAVE_PDFIUM
#include "mega/filesystem.h"
#include "mega/logging.h"
#include <fpdfview.h>

namespace mega {

class PdfiumReader
{

private:
    static unsigned initialized;

public:
    // Initializes the library and increases the internal counter of initializations. See destroy().
    // PdfiumReader member method calling init() is responsible for locking pdfMutex
    static void init();

#ifdef _WIN32
    // BGRA format, 4 bytes per pixel (32bits), byte order: blue, green, red, alpha.
    // init() is called internally if library is not initialized.
    // workingDirFolder : Path to create a temporary file.
    static unique_ptr<char[]> readBitmapFromPdf(int &w, int &h, int &orientation, const LocalPath &path, const LocalPath &workingDirFolder);
#else
    // Returns a bitmap in BGRA format, 4 bytes per pixel (32bits), byte order: blue, green, red, alpha.
    // init() is called internally if library is not initialized.
    static unique_ptr<char[]> readBitmapFromPdf(int &w, int &h, int &orientation, const LocalPath &path);
#endif
    // It decreases the initializations internal counter and destroys the library once it reaches zero.
    static void destroy();

protected:
    static std::mutex pdfMutex;

};
} // namespace

#endif
#endif
