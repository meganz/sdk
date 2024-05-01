/**
 * @file gfx_pdfium.cpp
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

#include "mega/gfx/gfx_pdfium.h"

#ifdef HAVE_PDFIUM

#define MAX_PDF_MEM_SIZE 1024*1024*100

namespace mega {

std::mutex PdfiumReader::pdfMutex;
unsigned PdfiumReader::initialized = 0;

void PdfiumReader::init()
{
    std::lock_guard<std::mutex> g(pdfMutex);
    if (!initialized++)
    {
        FPDF_LIBRARY_CONFIG config;
        config.version = 2;
        config.m_pUserFontPaths = nullptr;
        config.m_pIsolate = nullptr;
        config.m_v8EmbedderSlot = 0;
        FPDF_InitLibraryWithConfig(&config);
        LOG_debug << "PDFium library initialized.";
    }
}

void PdfiumReader::destroy()
{
    std::lock_guard<std::mutex> g(pdfMutex);
    if (!--initialized)
    {
        FPDF_DestroyLibrary();
        LOG_debug << "PDFium library destroyed.";
    }
}

#ifdef _WIN32
std::unique_ptr<char[]> PdfiumReader::readBitmapFromPdf(int &w, int &h, int &orientation, const LocalPath &path, const LocalPath &workingDirFolder)
#else
std::unique_ptr<char[]> PdfiumReader::readBitmapFromPdf(int &w, int &h, int &orientation, const LocalPath &path)
#endif
{

    std::lock_guard<std::mutex> g(pdfMutex);
    assert (initialized);

    FPDF_DOCUMENT pdf_doc = FPDF_LoadDocument(path.toPath(false).c_str(), nullptr);
#ifdef _WIN32
    LocalPath tmpFilePath;
    bool removetemporaryfile = false;
    std::unique_ptr<byte[]> buffer;
    FSACCESS_CLASS fa;

    // In Windows it fails if the path has non-ascii chars
    if (pdf_doc == nullptr && FPDF_GetLastError() == FPDF_ERR_FILE)
    {
        std::unique_ptr<FileAccess> pdfFile = fa.newfileaccess();
        if (pdfFile->fopen(path, FSLogging::logOnError))
        {
            if (pdfFile->size > MAX_PDF_MEM_SIZE)
            {
                if (!workingDirFolder.empty())
                {
                    LocalPath originPath = path;
                    tmpFilePath = workingDirFolder;
                    tmpFilePath.appendWithSeparator(LocalPath::fromRelativePath(".megapdftmp"),false);
                    if (fa.copylocal(originPath, tmpFilePath, pdfFile->mtime))
                    {
                        pdf_doc = FPDF_LoadDocument(tmpFilePath.toPath(false).c_str(), nullptr);
                        removetemporaryfile = true;
                    }
                }
            }
            else if (pdfFile->openf(FSLogging::logOnError))
            {
                buffer.reset(new byte[pdfFile->size]);
                pdfFile->frawread(buffer.get(), static_cast<unsigned>(pdfFile->size), static_cast<m_off_t>(0), true, FSLogging::logOnError);
                pdfFile->closef();
                pdf_doc = FPDF_LoadMemDocument(buffer.get(), static_cast<int>(pdfFile->size), nullptr);
            }
        }
    }
#endif
    if (pdf_doc != nullptr)
    {
        int page_count = FPDF_GetPageCount(pdf_doc);
        if (page_count > 0)
        {
            FPDF_PAGE page = FPDF_LoadPage(pdf_doc, 0 /*pageIndex*/);
            if (page != nullptr)
            {
                w = static_cast<int>(FPDF_GetPageWidth(page));
                h = static_cast<int>(FPDF_GetPageHeight(page));

                // we should restrict the maximum size of PDF pages to process, otherwise
                // it may require too much memory (and CPU).
                // as a compromise, the A0 standarized size should be enough for most cases,
                // A0: 841 x 1188 mm -> 2384 x 3368 points (as returned by FPDF_GetPageX())
                // to allow some margins, and rotated ones, avoid larger than 3500 points in
                // any dimension, which would require a buffer of maximum 3500x3500x4 = ~47MB

                if ((!w || !h)  // error reading size
                        || (w > 3500 || h > 3500))  // page too large
                {
                    if (!w || !h)
                    {
                        LOG_err << "Error reading PDF page size for " << path;
                    }
                    else
                    {
                        LOG_err << "Page size too large. Skipping PDF preview for " << path;
                    }
                    FPDF_ClosePage(page);
                    FPDF_CloseDocument(pdf_doc);
#ifdef _WIN32
                if (removetemporaryfile)
                {
                    fa.unlinklocal(tmpFilePath);
                }
#endif
                    return nullptr;
                }

                // BGRA format, 4 bytes per pixel (32bits), byte order: blue, green, red, alpha.
                std::unique_ptr<char[]> buffer(new char[w*h*4]);
                FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(w, h, FPDFBitmap_BGRA, buffer.get(), w * 4);
                if (!bitmap) //out of memory
                {
                    LOG_warn << "Error generating bitmap image (OOM)";
                    FPDF_ClosePage(page);
                    FPDF_CloseDocument(pdf_doc);
#ifdef _WIN32
                    if (removetemporaryfile)
                    {
                        fa.unlinklocal(tmpFilePath);
                    }
#endif
                    return nullptr;
                }

                FPDFBitmap_FillRect(bitmap, 0, 0, w, h, 0xFFFFFFFF);
                FPDF_RenderPageBitmap(bitmap, page, 0, 0, w, h, 2, 0);
                FPDFBitmap_Destroy(bitmap);
                FPDF_ClosePage(page);
                FPDF_CloseDocument(pdf_doc);
#ifdef _WIN32
                if (removetemporaryfile)
                {
                    fa.unlinklocal(tmpFilePath);
                }
#endif
                // Needed by Qt: ROTATION_DOWN = 3
                orientation = 3;
                return buffer;
            }
            else
            {
                FPDF_CloseDocument(pdf_doc);
                LOG_err << "Error loading PDF page to create thumb for " << path;
            }
        }
        else
        {
            FPDF_CloseDocument(pdf_doc);
            LOG_err << "Error getting number of pages for " << path;
        }
    }
    else
    {
        LOG_err << "Error loading PDF to create thumbnail for " << path << " " << FPDF_GetLastError();
    }

#ifdef _WIN32
    if (removetemporaryfile)
    {
        fa.unlinklocal(tmpFilePath);
    }
#endif

    return nullptr;

}

} // namespace mega
#endif
