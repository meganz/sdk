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
#ifdef _WIN32
    //Remove temporary files from previous executions:
    QDir dir(QDir::tempPath());
    dir.setNameFilters(QStringList() << QString::fromUtf8(".megasyncpdftmp*"));
    dir.setFilter(QDir::Files);
    foreach(QString dirFile, dir.entryList())
    {
        LOG_warn << "Removing unexpected temporary file found from previous executions: " << dirFile.toUtf8().constData();
        dir.remove(dirFile);
    }
#endif

}

PdfiumReader::~PdfiumReader()
{
    std::lock_guard<std::mutex> g(pdfMutex);
    FPDF_DestroyLibrary();
}

void * PdfiumReader::readBitmapFromPdf(int &w, int &h, int &orientation, const LocalPath &path, FileSystemAccess* fa, const LocalPath &workingDirFolder)
{

#ifdef _WIN32
    FPDF_DOCUMENT pdf_doc  = FPDF_LoadDocument(imagePath.toLocal8Bit().constData(), NULL);
    QString temporaryfile;
    bool removetemporaryfile = false;
    QByteArray qba;

    if (pdf_doc == NULL && FPDF_GetLastError() == FPDF_ERR_FILE)
    {
        QFile qf(imagePath);

        if (qf.size() > MAX_PDF_MEM_SIZE )
        {
            {
                QTemporaryFile tmpfile(QDir::tempPath() + QDir::separator() + QString::fromUtf8( ".megasyncpdftmpXXXXXX"));
                if (tmpfile.open())
                {
                    temporaryfile = tmpfile.fileName();
                }
            }
            if (temporaryfile.size() && QFile::copy(imagePath,temporaryfile))
            {
                pdf_doc  = FPDF_LoadDocument(temporaryfile.toLocal8Bit().constData(), NULL);
                removetemporaryfile = true;
            }
        }
        else if (qf.open(QIODevice::ReadOnly))
        {
            qba = qf.readAll();
            pdf_doc  = FPDF_LoadMemDocument(qba.constData(), qba.size(), NULL);
        }
    }
#else
    FPDF_DOCUMENT pdf_doc  = FPDF_LoadDocument(path.toPath(*fa).c_str(), nullptr);
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
                    QFile::remove(temporaryfile);
                }
#endif
                    return nullptr;
                }

                // 4 bytes per pixel. BGRx/BGRA format.
                bitmap = FPDFBitmap_Create(w, h, 1);
                if (!bitmap) //out of memory
                {
                    LOG_warn << "Error generating bitmap image (OOM)";
                    FPDF_ClosePage(page);
                    FPDF_CloseDocument(pdf_doc);
#ifdef _WIN32
                    if (removetemporaryfile)
                    {
                        QFile::remove(temporaryfile);
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
                    QFile::remove(temporaryfile);
                }
#endif

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
        QFile::remove(temporaryfile);
    }
#endif

    return nullptr;

}

void PdfiumReader::freeBitmap()
{
    FPDFBitmap_Destroy(bitmap);
}

} // namespace mega
