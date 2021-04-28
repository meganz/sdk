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

#ifdef HAVE_PDFIUM
#include <mega/filesystem.h>
#include <mega/logging.h>
#include <fpdfview.h>

#define MAX_PDF_MEM_SIZE 1024*1024*100

namespace mega {

class PdfiumReader
{

private:
    static FPDF_BITMAP bitmap;
    static bool initialized;

public:

    static void init();
    static void * readBitmapFromPdf(int &w, int &h, int &orientation, const LocalPath &path, FileSystemAccess* fa, const LocalPath &workingDirFolder);
    static void freeBitmap();
    static void destroy();

protected:
    static std::mutex pdfMutex;

};
} // namespace

#endif
#endif
