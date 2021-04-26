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
namespace mega {

std::mutex PdfiumReader::pdfMutex;

PdfiumReader::PdfiumReader()
{
    std::lock_guard<std::mutex> g(pdfMutex);
    FPDF_LIBRARY_CONFIG config;
    config.version = 2;
    config.m_pUserFontPaths = nullptr;
    config.m_pIsolate = nullptr;
    config.m_v8EmbedderSlot = 0;
    {
        FPDF_InitLibraryWithConfig(&config);
    }

}

PdfiumReader::~PdfiumReader()
{
    std::lock_guard<std::mutex> g(pdfMutex);
    FPDF_DestroyLibrary();
}

void * PdfiumReader::readBitmapFromPdf(int &w, int &h, int &orientation, const LocalPath &path, FileSystemAccess* fa, const LocalPath &workingDirFolder)
{

    std::lock_guard<std::mutex> g(pdfMutex);

    FPDF_DOCUMENT pdf_doc = FPDF_LoadDocument(path.toPath(*fa).c_str(), nullptr);
#ifdef _WIN32
    LocalPath tmpFile;
    bool removetemporaryfile = false;
    std::unique_ptr<byte[]> buffer;

    if (pdf_doc == nullptr && FPDF_GetLastError() == FPDF_ERR_FILE)
    {
        std::unique_ptr<FileAccess> pdfFile = fa->newfileaccess();
        if (pdfFile->fopen(path))
        {
            if (pdfFile->size > MAX_PDF_MEM_SIZE)
            {
                LocalPath originPath = path;
                tmpFile = workingDirFolder;
                tmpFile.appendWithSeparator(LocalPath::fromPath(".megasyncpdftmpXXXXXX",*fa),false);
                if (fa->copylocal(originPath, tmpFile, pdfFile->mtime))
                {
                    pdf_doc  = FPDF_LoadDocument(tmpFile.toPath(*fa).c_str(), nullptr);
                    removetemporaryfile = true;
                }
            }
            else if (pdfFile->openf())
            {
                buffer.reset(new byte[pdfFile->size]);
                pdfFile->frawread(buffer.get(),pdfFile->size,0,true);
                pdfFile->closef();
                pdf_doc = FPDF_LoadMemDocument(buffer.get(), pdfFile->size, nullptr);
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

                if (!w || !h)
                {
                    LOG_err << "Error reading PDF page size for " << path.toPath(*fa).c_str();
                    FPDF_ClosePage(page);
                    FPDF_CloseDocument(pdf_doc);
#ifdef _WIN32
                if (removetemporaryfile)
                {
                    fa->unlinklocal(tmpFile);
                }
#endif
                    return nullptr;
                }

                // 4 bytes per pixel. BGRA format with 1. BGRx with 0
                bitmap = FPDFBitmap_Create(w, h, 1);
                if (!bitmap) //out of memory
                {
                    LOG_warn << "Error generating bitmap image (OOM)";
                    FPDF_ClosePage(page);
                    FPDF_CloseDocument(pdf_doc);
#ifdef _WIN32
                    if (removetemporaryfile)
                    {
                        fa->unlinklocal(tmpFile);
                    }
#endif
                    return nullptr;
                }

                FPDFBitmap_FillRect(bitmap, 0, 0, w, h, 0xFFFFFFFF);
                FPDF_RenderPageBitmap(bitmap, page, 0, 0, w, h, 2, 0);
                void* data = FPDFBitmap_GetBuffer(bitmap);

                FPDF_ClosePage(page);
                FPDF_CloseDocument(pdf_doc);
#ifdef _WIN32
                if (removetemporaryfile)
                {
                    fa->unlinklocal(tmpFile);
                }
#endif
                // Needed by Qt: ROTATION_DOWN = 3
                orientation = 3;
                return data;
            }
            else
            {
                FPDF_CloseDocument(pdf_doc);
                LOG_err << "Error loading PDF page to create thumb for " << path.toPath(*fa).c_str();
            }
        }
        else
        {
            FPDF_CloseDocument(pdf_doc);
            LOG_err << "Error getting number of pages for " << path.toPath(*fa).c_str();
        }
    }
    else
    {
        LOG_err << "Error loading PDF to create thumbnail for " << path.toPath(*fa).c_str() << " " << FPDF_GetLastError();
    }

#ifdef _WIN32
    if (removetemporaryfile)
    {
        fa->unlinklocal(tmpFile);
    }
#endif

    return nullptr;

}

void PdfiumReader::freeBitmap()
{
    std::lock_guard<std::mutex> g(pdfMutex);
    FPDFBitmap_Destroy(bitmap);
}

} // namespace mega
#endif
