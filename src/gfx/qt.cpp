/**
 * @file gfxqt.cpp
 * @brief Graphics layer using QT
 *
 * (c) 2014 by Mega Limited, Wellsford, New Zealand
 * EXIF related functions are based on http://www.sentex.net/~mwandel/jhead/
 * Rewritten and published in public domain like
 * the original code by http://imonad.com
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

#include "mega.h"
#include "mega/gfx/qt.h"


#ifdef _WIN32
#include <windows.h>
#endif

#include <QFileInfo>
#include <QPainter>

#ifdef HAVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
#include <libavutil/display.h>
#include <libavutil/imgutils.h>
}
#endif

#ifdef HAVE_LIBRAW
#include <libraw/libraw.h>
#endif

#ifdef HAVE_PDFIUM
#include <fpdfview.h>
#ifdef _WIN32
#include <QDir>
#include <QTemporaryFile>
#define MAX_PDF_MEM_SIZE 1024*1024*100
#endif
#endif

namespace mega {

QByteArray *GfxProcQT::formatstring = NULL;

#ifdef HAVE_FFMPEG
std::mutex GfxProcQT::gfxMutex;
#endif

/************* EXIF STUFF **************/
#define M_SOI   0xD8          // Start Of Image (beginning of datastream)
#define M_SOS   0xDA          // Start Of Scan (begins compressed data)
#define M_EOI   0xD9          // End Of Image (end of datastream)
#define M_EXIF  0xE1          // Exif marker.  Also used for XMP data!

#define NUM_FORMATS   12
#define FMT_BYTE       1
#define FMT_STRING     2
#define FMT_USHORT     3
#define FMT_ULONG      4
#define FMT_URATIONAL  5
#define FMT_SBYTE      6
#define FMT_UNDEFINED  7
#define FMT_SSHORT     8
#define FMT_SLONG      9
#define FMT_SRATIONAL 10
#define FMT_SINGLE    11
#define FMT_DOUBLE    12

#define TAG_ORIENTATION        0x0112
#define TAG_INTEROP_OFFSET     0xA005
#define TAG_EXIF_OFFSET        0x8769

#define DIR_ENTRY_ADDR(Start, Entry) (Start+2+12*(Entry))

const int BytesPerFormat[] = {0,1,1,2,4,8,1,1,2,4,8,4,8};

//--------------------------------------------------------------------------
// Parse the marker stream until SOS or EOI is seen;
//--------------------------------------------------------------------------
int GfxProcQT::getExifOrientation(QString &filePath)
{
    QByteArray data;
    uint8_t c;
    bool ok;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return -1;
    ok = file.getChar((char *)&c);
    if((c != 0xFF) || !ok) return -1;

    ok = file.getChar((char *)&c);
    if((c != M_SOI) || !ok) return -1;

    for(;;)
    {
        int itemlen;
        int prev = 0;
        uint8_t marker = 0;
        uint8_t ll, lh;

        for(int i=0;;i++)
        {
            ok = file.getChar((char *)&marker);
            if(!ok) return -1;

            if ((marker != 0xFF) && (prev == 0xFF))
                break;

            prev = marker;
        }

        // Read the length of the section.
        ok = file.getChar((char *)&lh);
        if(!ok) return -1;

        ok = file.getChar((char *)&ll);
        if(!ok) return -1;

        itemlen = (lh << 8) | ll;
        if (itemlen < 2) return -1;

        data = QByteArray(file.read(itemlen-2)); // Read the whole section.
        if(data.size() != (itemlen-2))
        {
            return -1;
        }

        switch(marker)
        {
            case M_SOS:   // stop before hitting compressed data
            case M_EOI:   // in case it's a tables-only JPEG stream
                return -1;
            case M_EXIF:
                if(data.left(4) == "Exif")
                {
                    int orientation = processEXIF(&data, itemlen);
                    if((orientation >= 0) && (orientation <= 8))
                    {
                        return orientation;
                    }
                }
                break;
            default:
                // Skip any other sections.
                break;
        }
    }

    return -1;
}

// Convert a 16 bit unsigned value from file's native byte order
int Get16u(const void * Short, int MotorolaOrder){
    if (MotorolaOrder){
        return (((uchar *)Short)[0] << 8) | ((uchar *)Short)[1];
    }else{
        return (((uchar *)Short)[1] << 8) | ((uchar *)Short)[0];
    }
}

// Convert a 32 bit signed value from file's native byte order
int Get32s(const void * Long, int MotorolaOrder){
    if (MotorolaOrder){
        return  ((( char *)Long)[0] << 24) | (((uchar *)Long)[1] << 16)
              | (((uchar *)Long)[2] << 8 ) | (((uchar *)Long)[3] << 0 );
    }else{
        return  ((( char *)Long)[3] << 24) | (((uchar *)Long)[2] << 16)
              | (((uchar *)Long)[1] << 8 ) | (((uchar *)Long)[0] << 0 );
    }
}

// Convert a 32 bit unsigned value from file's native byte order
unsigned Get32u(const void * Long, int MotorolaOrder){
    return (unsigned)Get32s(Long, MotorolaOrder) & 0xffffffff;
}

int getFormatSize(int Format)
{
     switch(Format){
         case FMT_SBYTE:
         case FMT_BYTE:
            return 1;
         case FMT_USHORT:
            return 2;
         case FMT_ULONG:
            return 4;
         case FMT_URATIONAL:
         case FMT_SRATIONAL:
             return 8;
         case FMT_SSHORT:
            return 2;
         case FMT_SLONG:
            return 4;
     }
     return 0;
}

// Evaluate number, be it int, rational, or float from directory.
double ConvertAnyFormat(const void * ValuePtr, int Format, int MotorolaOrder){
     double Value;
     Value = 0;

     switch(Format){
         case FMT_SBYTE:     Value = *(signed char *)ValuePtr;  break;
         case FMT_BYTE:      Value = *(uchar *)ValuePtr;        break;

         case FMT_USHORT:    Value = Get16u(ValuePtr, MotorolaOrder);          break;
         case FMT_ULONG:     Value = Get32u(ValuePtr, MotorolaOrder);          break;

         case FMT_URATIONAL:
         case FMT_SRATIONAL:
             {
                 int Num, Den;
                 Num = Get32s(ValuePtr, MotorolaOrder);
                 Den = Get32s(4+(char *)ValuePtr, MotorolaOrder);
                 if (Den == 0){
                     Value = 0;
                 }else{
                     Value = (double)Num/Den;
                 }
                 break;
             }

         case FMT_SSHORT:    Value = (signed short)Get16u(ValuePtr, MotorolaOrder);  break;
         case FMT_SLONG:     Value = Get32s(ValuePtr, MotorolaOrder);                break;

         // Not sure if this is correct (never seen float used in Exif format)
         case FMT_SINGLE:    Value = (double)*(float *)ValuePtr;      break;
         case FMT_DOUBLE:    Value = *(double *)ValuePtr;             break;

         default: Value = 100;// Illegal format code


     }
     return Value;
}

// Process one of the nested EXIF directories.
int GfxProcQT::processEXIFDir(const char *DirStart, const char *OffsetBase, uint32_t exifSize, uint32_t nesting, int MotorolaOrder){
    int numDirEntries;
    int orientation;
    int tam;

    if(nesting>4) return -1; // Maximum Exif directory nesting exceeded (corrupt Exif header)

    numDirEntries = Get16u(DirStart, MotorolaOrder);
    for (int de=0; de<numDirEntries; de++)
    {
        int Tag, Format, Components;
        const char * DirEntry;
        const char * ValuePtr;
        int ByteCount;

        DirEntry = DIR_ENTRY_ADDR(DirStart, de);
        if((DirEntry + 8) > (OffsetBase + exifSize))
        {
            return -1;
        }

        Tag        = Get16u(DirEntry,   MotorolaOrder);
        Format     = Get16u(DirEntry+2, MotorolaOrder);
        Components = Get32u(DirEntry+4, MotorolaOrder);

        if(Format-1 >= NUM_FORMATS) continue; // (-1) catches illegal zero case as unsigned underflows to positive large.
        if((unsigned)Components > 0x10000) continue; // Too many components

        ByteCount = Components * BytesPerFormat[Format];

        if (ByteCount > 4){ // If its bigger than 4 bytes, the dir entry contains an offset.
            if((DirEntry + 12) > (OffsetBase + exifSize))
            {
                return -1;
            }

            unsigned OffsetVal = Get32u(DirEntry+8, MotorolaOrder);
            if (OffsetVal+ByteCount > exifSize) continue; // Bogus pointer offset and / or bytecount value
            ValuePtr = OffsetBase+OffsetVal;
        }else{ // 4 bytes or less and value is in the dir entry itself
            ValuePtr = DirEntry+8;
        }

        // Extract useful components of tag
        switch(Tag){
            case TAG_ORIENTATION:
                tam = getFormatSize(Format);
                if(!tam || ((ValuePtr + tam) > (OffsetBase + exifSize)))
                {
                    break;
                }

                orientation = (int)ConvertAnyFormat(ValuePtr, Format, MotorolaOrder);
                if (orientation >= 0 && orientation <= 8)
                    return orientation;
                break;

            case TAG_EXIF_OFFSET:
            case TAG_INTEROP_OFFSET:
                const char * SubdirStart;
                if((ValuePtr + 4) > (OffsetBase + exifSize))
                {
                    continue;
                }

                SubdirStart = OffsetBase + Get32u(ValuePtr, MotorolaOrder);
                if(!(SubdirStart < OffsetBase || SubdirStart > OffsetBase+ exifSize))
                {
                    orientation = processEXIFDir(SubdirStart, OffsetBase, exifSize, nesting+1, MotorolaOrder);
                    if (orientation >= 0 && orientation <= 8)
                        return orientation;
                }
                continue;
                break;

            default:
                // Skip any other sections.
                break;
        }
    }

    return -1;
}

// Process a EXIF marker
// Describes all the drivel that most digital cameras include...
int GfxProcQT::processEXIF(QByteArray *data, int itemlen){
    int MotorolaOrder = 0;
    if(data->size() < 8)
    {
        return -1;
    }

    if(data->mid(6,2) == "II") MotorolaOrder = 0;
    else if(data->mid(6,2) == "MM") MotorolaOrder = 1;
    else return -1;

    // get first offset
    QByteArray ttt(data->mid(10,4));
    const char *ttt2 = ttt.constData();
    uint32_t FirstOffset;
    FirstOffset = Get32u(ttt2, MotorolaOrder);

    if (FirstOffset < 8 || FirstOffset > 16){
            if (FirstOffset < 16 || int(FirstOffset) > itemlen-16)  return -1;  // invalid offset for first Exif IFD value ;
    }

    const char *dirStart = data->constData();
    const char *offsetBase = data->constData();

    dirStart   += 6 + FirstOffset;
    offsetBase += 6;

    // First directory starts 16 bytes in.  All offset are relative to 8 bytes in.
    return processEXIFDir(dirStart, offsetBase, itemlen-8, 0, MotorolaOrder);
}
/************* END OF EXIF STUFF **************/

#if defined(HAVE_PDFIUM) && defined(_WIN32)
typedef void (WINAPI *pFPDF_InitLibraryWithConfig) (const FPDF_LIBRARY_CONFIG* config);
typedef void (WINAPI *pFPDF_DestroyLibrary) ();

typedef FPDF_DOCUMENT (WINAPI *pFPDF_LoadDocument) (FPDF_STRING file_path, FPDF_BYTESTRING password);
typedef unsigned long (WINAPI *pFPDF_GetLastError) ();
typedef FPDF_DOCUMENT (WINAPI *pFPDF_LoadMemDocument) (const void* data_buf, int size, FPDF_BYTESTRING password);
typedef int (WINAPI *pFPDF_GetPageCount) (FPDF_DOCUMENT document);
typedef FPDF_PAGE (WINAPI *pFPDF_LoadPage) (FPDF_DOCUMENT document, int page_index);
typedef double (WINAPI *pFPDF_GetPageWidth) (FPDF_PAGE page);
typedef double (WINAPI *pFPDF_GetPageHeight) (FPDF_PAGE page);
typedef FPDF_BITMAP (WINAPI *pFPDFBitmap_CreateEx) (int width, int height, int format, void* first_scan, int stride);
typedef void (WINAPI *pFPDF_ClosePage) (FPDF_PAGE page);
typedef void (WINAPI *pFPDF_CloseDocument) (FPDF_DOCUMENT document);
typedef void (WINAPI *pFPDF_RenderPageBitmap) (FPDF_BITMAP bitmap, FPDF_PAGE page, int start_x, int start_y,
                                               int size_x, int size_y, int rotate, int flags);
typedef void (WINAPI *pFPDFBitmap_Destroy) (FPDF_BITMAP bitmap);

std::mutex pdfiumMutex;
pFPDF_InitLibraryWithConfig FPDF_InitLibraryWithConfig;
pFPDF_DestroyLibrary FPDF_DestroyLibrary;
pFPDF_LoadDocument FPDF_LoadDocument;
pFPDF_GetLastError FPDF_GetLastError;
pFPDF_LoadMemDocument FPDF_LoadMemDocument;
pFPDF_GetPageCount FPDF_GetPageCount;
pFPDF_LoadPage FPDF_LoadPage;
pFPDF_GetPageWidth FPDF_GetPageWidth;
pFPDF_GetPageHeight FPDF_GetPageHeight;
pFPDFBitmap_CreateEx FPDFBitmap_CreateEx;
pFPDF_ClosePage FPDF_ClosePage;
pFPDF_CloseDocument FPDF_CloseDocument;
pFPDF_RenderPageBitmap FPDF_RenderPageBitmap;
pFPDFBitmap_Destroy FPDFBitmap_Destroy;

bool pdfiumLoadedOk = false;
bool pdfiumLoadAttempted = false;
#endif

/*GfxProc implementation*/
GfxProcQT::GfxProcQT()
{
#ifdef HAVE_FFMPEG
    gfxMutex.lock();
    av_register_all();
    avcodec_register_all();
//    av_log_set_level(AV_LOG_VERBOSE);
    gfxMutex.unlock();
#endif

#ifdef HAVE_PDFIUM
#ifdef _WIN32
#pragma warning (disable: 4996 4191)
    if (!pdfiumLoadAttempted)
    {
        std::lock_guard g(pdfiumMutex);
        if (!pdfiumLoadAttempted)
        {
            pdfiumLoadAttempted = true;
            OSVERSIONINFOEX osvi;
            ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));
            osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
            if (GetVersionEx((OSVERSIONINFO*)&osvi) && osvi.dwMajorVersion >= 6)  // vista/server2008 or beyond for InitOnceExecuteOnce which pdfium uses.
            {
                HINSTANCE pdfiumDLL = NULL;
                WCHAR systemPath[MAX_PATH];
                GetSystemDirectory(systemPath, MAX_PATH);
                pdfiumDLL = LoadLibrary(L"pdfium.DLL"); //TODO: .exe path?

                if (pdfiumDLL)
                {
                    bool anyfailed = false;
                    anyfailed = anyfailed || !(FPDF_InitLibraryWithConfig = (pFPDF_InitLibraryWithConfig)::GetProcAddress(pdfiumDLL, "_FPDF_InitLibraryWithConfig@4"));
                    anyfailed = anyfailed || !(FPDF_DestroyLibrary = (pFPDF_DestroyLibrary)::GetProcAddress(pdfiumDLL, "_FPDF_DestroyLibrary@0"));
                    anyfailed = anyfailed || !(FPDF_LoadDocument = (pFPDF_LoadDocument)::GetProcAddress(pdfiumDLL, "_FPDF_LoadDocument@8"));
                    anyfailed = anyfailed || !(FPDF_GetLastError = (pFPDF_GetLastError)::GetProcAddress(pdfiumDLL, "_FPDF_GetLastError@0"));
                    anyfailed = anyfailed || !(FPDF_LoadMemDocument = (pFPDF_LoadMemDocument)::GetProcAddress(pdfiumDLL, "_FPDF_LoadMemDocument@12"));
                    anyfailed = anyfailed || !(FPDF_GetPageCount = (pFPDF_GetPageCount)::GetProcAddress(pdfiumDLL, "_FPDF_GetPageCount@4"));
                    anyfailed = anyfailed || !(FPDF_LoadPage = (pFPDF_LoadPage)::GetProcAddress(pdfiumDLL, "_FPDF_LoadPage@8"));
                    anyfailed = anyfailed || !(FPDF_GetPageWidth = (pFPDF_GetPageWidth)::GetProcAddress(pdfiumDLL, "_FPDF_GetPageWidth@4"));
                    anyfailed = anyfailed || !(FPDF_GetPageHeight = (pFPDF_GetPageHeight)::GetProcAddress(pdfiumDLL, "_FPDF_GetPageHeight@4"));
                    anyfailed = anyfailed || !(FPDFBitmap_CreateEx = (pFPDFBitmap_CreateEx)::GetProcAddress(pdfiumDLL, "_FPDFBitmap_CreateEx@20"));
                    anyfailed = anyfailed || !(FPDF_ClosePage = (pFPDF_ClosePage)::GetProcAddress(pdfiumDLL, "_FPDF_ClosePage@4"));
                    anyfailed = anyfailed || !(FPDF_CloseDocument = (pFPDF_CloseDocument)::GetProcAddress(pdfiumDLL, "_FPDF_CloseDocument@4"));
                    anyfailed = anyfailed || !(FPDF_RenderPageBitmap = (pFPDF_RenderPageBitmap)::GetProcAddress(pdfiumDLL, "_FPDF_RenderPageBitmap@32"));
                    anyfailed = anyfailed || !(FPDFBitmap_Destroy = (pFPDFBitmap_Destroy)::GetProcAddress(pdfiumDLL, "_FPDFBitmap_Destroy@4"));

                    if (anyfailed)
                    {
                        LOG_err << "Pdfium failed to load";
                        FreeLibrary(pdfiumDLL);
                    }
                    else
                    {
                        pdfiumLoadedOk = true;
                    }
                }
            }
        }
    }
#endif

    gfxMutex.lock();
    FPDF_LIBRARY_CONFIG config;
    config.version = 2;
    config.m_pUserFontPaths = NULL;
    config.m_pIsolate = NULL;
    config.m_v8EmbedderSlot = 0;
#ifdef _WIN32
    if (pdfiumLoadedOk)
#endif
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
    gfxMutex.unlock();
#endif
    image = NULL;
    orientation = -1;
    imageType = TYPE_NONE;
}

GfxProcQT::~GfxProcQT()
{
#ifdef HAVE_PDFIUM

#ifdef _WIN32
    if (pdfiumLoadedOk)
#endif
    {
        gfxMutex.lock();
        FPDF_DestroyLibrary();
        gfxMutex.unlock();
    }
#endif
}

bool GfxProcQT::readbitmap(FileAccess*, string* localname, int)
{
#ifdef _WIN32
    localname->append("", 1);
    imagePath = QString::fromWCharArray((wchar_t *)localname->c_str());
    if(imagePath.startsWith(QString::fromUtf8("\\\\?\\")))
        imagePath = imagePath.mid(4);
#else
    imagePath = QString::fromUtf8(localname->c_str());
#endif

    image = readbitmapQT(w, h, orientation, imageType, imagePath);

#ifdef _WIN32
    localname->resize(localname->size()-1);
#endif

    return (image != NULL);
}

bool GfxProcQT::resizebitmap(int rw, int rh, string* jpegout)
{
    if (!image)
    {
        image = readbitmapQT(w, h, orientation, imageType, imagePath);
        if (!image)
        {
            return false;
        }
    }

    QImage result = resizebitmapQT(image, orientation, w, h, rw, rh);
    QImageReader *oldImageReader = image;
    image = new QImageReader(image->device(), QByteArray("JPG"));
    delete oldImageReader;

    if (result.isNull())
    {
        return false;
    }
    jpegout->clear();

    //Remove transparency
    QImage finalImage(result.size(), QImage::Format_RGB32);
    finalImage.fill(QColor(Qt::white).rgb());
    QPainter painter(&finalImage);
    painter.drawImage(0, 0, result);

    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);
    finalImage.save(&buffer, "JPG", 85);
    jpegout->assign(ba.constData(), ba.size());
    return !!jpegout->size();
}

void GfxProcQT::freebitmap()
{
    if (image)
    {
        delete image->device();
        delete image;
    }
}

QImage GfxProcQT::createThumbnail(QString imagePath)
{
    int w, h, orientation;
    int imageType = TYPE_NONE;

    QFileInfo info(imagePath);
    if(!info.exists())
        return QImage();

    QString ext = QString::fromUtf8(".") + info.suffix() + QString::fromUtf8(".");
    if(!QString::fromUtf8(supportedformatsQT()).contains(ext, Qt::CaseInsensitive))
        return QImage();

    QImageReader *image = readbitmapQT(w, h, orientation, imageType, imagePath);
    if (!image)
    {
        return QImage();
    }

    QImage result = GfxProcQT::resizebitmapQT(image, orientation, w, h,
            GfxProc::dimensions[GfxProc::THUMBNAIL][0],
            GfxProc::dimensions[GfxProc::THUMBNAIL][1]);

    delete image->device();
    delete image;
    return result;
}

QImageReader *GfxProcQT::readbitmapQT(int &w, int &h, int &orientation, int &imageType, QString imagePath)
{
    QFileInfo info(imagePath);
    QString ext = QString::fromUtf8(".%1.").arg(info.suffix()).toLower();

#ifdef HAVE_FFMPEG
    if (strstr(GfxProcQT::supportedformatsFfmpeg(), ext.toUtf8().constData()))
    {
        imageType = TYPE_VIDEO;
        return readbitmapFfmpeg(w, h, orientation, imagePath);
    }
#endif

#ifdef HAVE_LIBRAW
    if (strstr(GfxProcQT::supportedformatsLibraw(), ext.toUtf8().constData()))
    {
        imageType = TYPE_RAW;
        return readbitmapLibraw(w, h, orientation, imagePath);
    }
#endif

#ifdef HAVE_PDFIUM
    if (strstr(GfxProcQT::supportedformatsPDF(), ext.toUtf8().constData()))
    {
        imageType = TYPE_PDF;
        return readbitmapPdf(w, h, orientation, imagePath);
    }
#endif

    imageType = TYPE_IMAGE;
    QImageReader* image = new QImageReader(imagePath);
    QSize s = image->size();
    if(!s.isValid() || !s.width() || !s.height())
    {
        delete image;
        return NULL;
    }

    QImage unscaled = image->read();
    QBuffer *buffer = new QBuffer();
    if (unscaled.isNull() || !buffer->open(QIODevice::ReadWrite) || !unscaled.save(buffer, "JPG", 85))
    {
        LOG_warn << "Error saving image to a memory buffer";
        delete buffer;
        delete image;
        return NULL;
    }

    delete image;
    buffer->seek(0);
    QImageReader *imageReader = new QImageReader(buffer, QByteArray("JPG"));

    orientation = getExifOrientation(imagePath);
    if(orientation < ROTATION_LEFT_MIRRORED)
    {
        //No rotation or 180º rotation
        w = s.width();
        h = s.height();
    }
    else
    {
        //90º or 270º rotation
        w = s.height();
        h = s.width();
    }

    return imageReader;
}

QImage GfxProcQT::resizebitmapQT(QImageReader *image, int orientation, int w, int h, int rw, int rh)
{
    int px, py;

    if (!w || !h) return QImage();
    transform(w, h, rw, rh, px, py);
    if (!w || !h) return QImage();

    //Assuming that the thumbnail is centered horizontally.
    //That is the case of MEGA thumbnails and makes the extraction easier and more efficient.
    if((orientation == ROTATION_DOWN) || (orientation == ROTATION_DOWN_MIRRORED) || (orientation == ROTATION_RIGHT_MIRRORED) || (orientation == ROTATION_RIGHT))
        py = (h-rh)-py;

    if(orientation < ROTATION_LEFT_MIRRORED)
    {
         //No rotation or 180º rotation
        image->setScaledSize(QSize(w, h));
        image->setScaledClipRect(QRect(px, py, rw, rh));
    }
    else
    {
        //90º or 270º rotation
        image->setScaledSize(QSize(h, w));
        image->setScaledClipRect(QRect(py, px, rh, rw));
    }

    QImage result = image->read();
    if (result.isNull())
    {
        LOG_err << "Error reading image: " << image->errorString().toUtf8().constData();
        return result;
    }

    image->device()->seek(0);

    QTransform transform;

    //Manage rotation
    switch(orientation)
    {
        case ROTATION_DOWN:
        case ROTATION_DOWN_MIRRORED:
            transform.rotate(180);
            break;
        case ROTATION_LEFT:
        case ROTATION_LEFT_MIRRORED:
            transform.rotate(90);
            break;
        case ROTATION_RIGHT:
        case ROTATION_RIGHT_MIRRORED:
            transform.rotate(270);
            break;
        default:
            break;
    }

    //Manage mirroring
    if((orientation == ROTATION_UP_MIRRORED) || (orientation == ROTATION_DOWN_MIRRORED))
        transform.scale(-1, 1);
    else if((orientation == ROTATION_LEFT_MIRRORED) || (orientation == ROTATION_RIGHT_MIRRORED))
        transform.scale(1, -1);

    if(!transform.isIdentity())
        result = result.transformed(transform);

    return result;
}

const char *GfxProcQT::supportedformatsQT()
{
    if (!formatstring)
    {
        formatstring = new QByteArray(".");
        QList<QByteArray> formats = QImageReader::supportedImageFormats();
        for(int i=0; i<formats.size(); i++)
            formatstring->append(QString::fromUtf8(formats[i]).toLower().toUtf8()).append(".");

#ifdef HAVE_FFMPEG
        formatstring->resize(formatstring->size() - 1);
        formatstring->append(supportedformatsFfmpeg());
#endif

#ifdef HAVE_LIBRAW
        formatstring->resize(formatstring->size() - 1);
        formatstring->append(supportedformatsLibraw());
#endif

#ifdef HAVE_PDFIUM
        formatstring->resize(formatstring->size() - 1);
        formatstring->append(supportedformatsPDF());
#endif
    }
    return formatstring->constData();
}

#ifdef HAVE_LIBRAW

const char *GfxProcQT::supportedformatsLibraw()
{
    return ".3fr.arw.cr2.crw.ciff.cs1.dcr.dng.erf.iiq.k25.kdc.mef.mos.mrw.nef.nrw.orf.pef.raf.raw.rw2.rwl.sr2.srf.srw.x3f.";
}

QImageReader *GfxProcQT::readbitmapLibraw(int &w, int &h, int &orientation, QString imagePath)
{
    LibRaw libRaw;
    int ret = libRaw.open_file(imagePath.toUtf8().constData());
    if (ret != LIBRAW_SUCCESS
            || libRaw.imgdata.sizes.width <= 0
            || libRaw.imgdata.sizes.height <= 0)
    {
        LOG_debug << "Unreadable RAW image";
        return NULL;
    }

    libraw_processed_image_t *output = NULL;
    const libraw_data_t &imgdata = libRaw.imgdata;
    LOG_debug << "Processing RAW image: " << imagePath.toUtf8().constData()
              << " " << imgdata.sizes.width
              << " " << imgdata.sizes.height
              << " " << imgdata.thumbnail.twidth
              << " " << imgdata.thumbnail.theight
              << " " << imgdata.sizes.flip;

    if (imgdata.thumbnail.twidth > 0 && imgdata.thumbnail.theight > 0)
    {
        ret = libRaw.unpack_thumb();
        if (ret == 0)
        {
            LOG_debug << "Extracting thumbnail from RAW image";
            output = libRaw.dcraw_make_mem_thumb();
        }
        else if (LIBRAW_FATAL_ERROR(ret))
        {
            LOG_debug << "Fatal error unpacking thumbnail";
            return NULL;
        }
    }

    if (!output)
    {
        ret = libRaw.unpack();
        if (ret == 0)
        {
            LOG_debug << "Extracting full RAW image";
            libRaw.dcraw_process();
            output = libRaw.dcraw_make_mem_image();
        }
        else if (LIBRAW_FATAL_ERROR(ret))
        {
            LOG_debug << "Fatal error unpacking image";
            return NULL;
        }
    }

    if (!output)
    {
        LOG_warn << "Unable to extract RAW image";
        return NULL;
    }

    QImage unscaled;
    if (output->type == LIBRAW_IMAGE_JPEG)
    {
        LOG_debug << "Converting RAW image in JPG format";

        unscaled.loadFromData(output->data, output->data_size, "JPEG");
        w = unscaled.width();
        h = unscaled.height();
    }
    else if (output->type == LIBRAW_IMAGE_BITMAP)
    {
        LOG_debug << "Converting RAW image in BITMAP format";

        int numPixels = output->width * output->height;
        int colorSize = output->bits / 8;
        int pixelSize = output->colors * colorSize;

        unscaled = QImage(output->width, output->height, QImage::Format_RGB32);
        uchar *pixels = unscaled.bits();
        uchar *data = output->data;
        for (int i = 0; i < numPixels; i++, data += pixelSize)
        {
            int index = i * 4;
            if (output->colors == 3)
            {
                pixels[index] = data[2 * colorSize];
                pixels[index + 1] = data[1 * colorSize];
                pixels[index + 2] = data[0];
            }
            else
            {
                pixels[index] = data[0];
                pixels[index + 1] = data[0];
                pixels[index + 2] = data[0];
            }
            pixels[index + 3] = 0xFF;
        }
        w = output->width;
        h = output->height;
    }

    LOG_debug << "Output image size: " << w << " " << h;

    bool rotated = false;
    if ((imgdata.sizes.width > imgdata.sizes.height && w < h)
            || (imgdata.sizes.width < imgdata.sizes.height && w > h))
    {
        rotated = true;
        LOG_debug << "RAW image already rotated by libraw";
    }

    orientation = 0;
    if (imgdata.sizes.flip != 0 && !rotated)
    {
        LOG_debug << "Image rotation needed " << imgdata.sizes.flip;
        if (imgdata.sizes.flip == 3)
        {
            orientation = ROTATION_DOWN;
        }
        else if (imgdata.sizes.flip == 5)
        {
            orientation = ROTATION_RIGHT;
        }
        else if (imgdata.sizes.flip == 6)
        {
            orientation = ROTATION_LEFT;
        }
    }

    LibRaw::dcraw_clear_mem(output);
    if (unscaled.isNull())
    {
        LOG_warn << "Unable to convert RAW image";
        return NULL;
    }

    QBuffer *buffer = new QBuffer();
    if (!buffer->open(QIODevice::ReadWrite) || !unscaled.save(buffer, "JPG", 85))
    {
        LOG_warn << "Error saving RAW image to a memory buffer";
        delete buffer;
        return NULL;
    }

    LOG_debug << "RAW image correctly extracted";
    if (orientation < ROTATION_LEFT_MIRRORED)
    {
        //No rotation or 180º rotation
        w = unscaled.width();
        h = unscaled.height();
    }
    else
    {
        //90º or 270º rotation
        w = unscaled.height();
        h = unscaled.width();
    }

    buffer->seek(0);
    QImageReader *imageReader = new QImageReader(buffer, QByteArray("JPG"));
    return imageReader;
}
#endif

#ifdef HAVE_PDFIUM
const char *GfxProcQT::supportedformatsPDF()
{
#ifdef _WIN32
    if (!pdfiumLoadedOk)
    {
        return "";
    }
#endif
    return ".pdf.";
}

QImageReader *GfxProcQT::readbitmapPdf(int &w, int &h, int &orientation, QString imagePath)
{
#ifdef _WIN32
    if (!pdfiumLoadedOk)
    {
        return NULL;
    }
#endif

    std::lock_guard<std::mutex> g(gfxMutex);
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
    FPDF_DOCUMENT pdf_doc  = FPDF_LoadDocument(imagePath.toUtf8().constData(), NULL);
#endif
    if (pdf_doc != NULL)
    {
        int page_count = FPDF_GetPageCount(pdf_doc);
        if (page_count  > 0)
        {
            FPDF_PAGE page = FPDF_LoadPage(pdf_doc, 0 /*pageIndex*/);
            if (page != NULL)
            {
                double page_width  = FPDF_GetPageWidth(page);
                double page_height = FPDF_GetPageHeight(page);

                QImage image(page_width, page_height, QImage::Format_ARGB32);
                image.fill(0xFFFFFFFF);

                FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(image.width(), image.height(),
                                                                 FPDFBitmap_BGRA,
                                                                 image.scanLine(0), image.bytesPerLine());
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
                    return NULL;
                }

                FPDF_RenderPageBitmap(bitmap, page, 0, 0, image.width(), image.height(), 0, 0);
                FPDFBitmap_Destroy(bitmap);
                bitmap = NULL;

                if (image.isNull())
                {
                    LOG_warn << "Unable to convert image from PDF file";
                    FPDF_ClosePage(page);
                    FPDF_CloseDocument(pdf_doc);
#ifdef _WIN32
                    if (removetemporaryfile)
                    {
                        QFile::remove(temporaryfile);
                    }
#endif
                    return NULL;
                }

                QBuffer *buffer = new QBuffer();
                if (!buffer->open(QIODevice::ReadWrite) || !image.save(buffer, "JPG", 85))
                {
                    LOG_warn << "Error extracting image";
                    delete buffer;
                    FPDF_ClosePage(page);
                    FPDF_CloseDocument(pdf_doc);
#ifdef _WIN32
                    if (removetemporaryfile)
                    {
                        QFile::remove(temporaryfile);
                    }
#endif
                    return NULL;
                }

                FPDF_ClosePage(page);
                FPDF_CloseDocument(pdf_doc);
#ifdef _WIN32
                if (removetemporaryfile)
                {
                    QFile::remove(temporaryfile);
                }
#endif
                w = image.width();
                h = image.height();

                buffer->seek(0);
                QImageReader *imageReader = new QImageReader(buffer, QByteArray("JPG"));
                return imageReader;
            }
            else
            {
                FPDF_CloseDocument(pdf_doc);
                LOG_err << "Error loading PDF page to create thumb for " << imagePath.toUtf8().constData();
            }
        }
        else
        {
            FPDF_CloseDocument(pdf_doc);
            LOG_err << "Error getting number of pages for " << imagePath.toUtf8().constData();
        }
    }
    else
    {
        LOG_err << "Error loading PDF to create thumbnail for " << imagePath.toUtf8().constData() << " " << FPDF_GetLastError();
    }

#ifdef _WIN32
    if (removetemporaryfile)
    {
        QFile::remove(temporaryfile);
    }
#endif
    return NULL;
}

#endif

#ifdef HAVE_FFMPEG

#ifdef AV_CODEC_CAP_TRUNCATED
#define CAP_TRUNCATED AV_CODEC_CAP_TRUNCATED
#else
#define CAP_TRUNCATED CODEC_CAP_TRUNCATED
#endif

const char *GfxProcQT::supportedformatsFfmpeg()
{
    return  ".264.265.3g2.3gp.3gpa.3gpp.3gpp2.mp3"
            ".avi.dde.divx.evo.f4v.flv.gvi.h261.h263.h264.h265.hevc"
            ".ismt.ismv.ivf.jpm.k3g.m1v.m2p.m2s.m2t.m2v.m4s.m4t.m4v.mac.mkv.mk3d"
            ".mks.mov.mp1v.mp2v.mp4.mp4v.mpeg.mpg.mpgv.mpv.mqv.ogm.ogv"
            ".qt.sls.tmf.trp.ts.ty.vc1.vob.vr.webm.wmv.";
}

QImageReader *GfxProcQT::readbitmapFfmpeg(int &w, int &h, int &orientation, QString imagePath)
{
    // Open video file
    AVFormatContext* formatContext = avformat_alloc_context();
    if (avformat_open_input(&formatContext, imagePath.toUtf8().constData(), NULL, NULL))
    {
        LOG_warn << "Error opening video: " << imagePath.toUtf8().constData();
        return NULL;
    }

    // Get stream information
    if (avformat_find_stream_info(formatContext, NULL))
    {
        LOG_warn << "Stream info not found: " << imagePath.toUtf8().constData();
        avformat_close_input(&formatContext);
        return NULL;
    }

    // Find first video stream type
    AVStream *videoStream = NULL;
    int videoStreamIdx = 0;
    for (unsigned i = 0; i < formatContext->nb_streams; i++)
    {
        if (formatContext->streams[i]->codec && formatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoStream = formatContext->streams[i];
            videoStreamIdx = i;
            break;
        }
    }

    if (!videoStream)
    {
        LOG_warn << "Video stream not found: " << imagePath.toUtf8().constData();
        avformat_close_input(&formatContext);
        return NULL;
    }

    // Get codec context to determine video frame dimensions
    AVCodecContext codecContext = *(videoStream->codec);
    int width = codecContext.width;
    int height = codecContext.height;
    if (width <= 0 || height <= 0)
    {
        LOG_warn << "Invalid video dimensions: " << width << ", " << height;
        avformat_close_input(&formatContext);
        return NULL;
    }

    if (codecContext.pix_fmt == AV_PIX_FMT_NONE)
    {
        LOG_warn << "Invalid pixel format: " << codecContext.pix_fmt;
        avformat_close_input(&formatContext);
        return NULL;
    }

    AVPixelFormat sourcePixelFormat = codecContext.pix_fmt;
    AVPixelFormat targetPixelFormat = AV_PIX_FMT_RGB32;
    SwsContext* swsContext = sws_getContext(width, height, sourcePixelFormat,
                                            width, height, targetPixelFormat,
                                            SWS_FAST_BILINEAR, NULL, NULL, NULL);
    if (!swsContext)
    {
        LOG_warn << "SWS Context not found: " << sourcePixelFormat;
        avformat_close_input(&formatContext);
        return NULL;
    }

    // Find decoder for video stream
    AVCodecID codecId = codecContext.codec_id;
    AVCodec* decoder = avcodec_find_decoder(codecId);
    if (!decoder)
    {
        LOG_warn << "Codec not found: " << codecId;
        sws_freeContext(swsContext);
        avformat_close_input(&formatContext);
        return NULL;
    }

    // Force seeking to key frames
    formatContext->seek2any = false;
    videoStream->skip_to_keyframe = true;
    if (decoder->capabilities & CAP_TRUNCATED)
    {
        codecContext.flags |= CAP_TRUNCATED;
    }

    // Open codec
    if (avcodec_open2(&codecContext, decoder, NULL) < 0)
    {
        LOG_warn << "Error opening codec: " << codecId;
        sws_freeContext(swsContext);
        avformat_close_input(&formatContext);
        return NULL;
    }

    //Allocate video frames
    AVFrame* videoFrame = av_frame_alloc();
    AVFrame* targetFrame = av_frame_alloc();
    if (!videoFrame || !targetFrame)
    {
        LOG_warn << "Error allocating video frames";
        if (videoFrame)
        {
            av_frame_free(&videoFrame);
        }
        if (targetFrame)
        {
            av_frame_free(&targetFrame);
        }
        sws_freeContext(swsContext);
        avformat_close_input(&formatContext);
        return NULL;
    }

    targetFrame->format = targetPixelFormat;
    targetFrame->width = width;
    targetFrame->height = height;
    if (av_image_alloc(targetFrame->data, targetFrame->linesize, targetFrame->width, targetFrame->height, targetPixelFormat, 32) < 0)
    {
        LOG_warn << "Error allocating frame";
        av_frame_free(&videoFrame);
        av_frame_free(&targetFrame);
        avcodec_close(&codecContext);
        sws_freeContext(swsContext);
        avformat_close_input(&formatContext);
        return NULL;
    }

    // Calculation of seeking point. We need to rescale time units (seconds) to AVStream.time_base units to perform the seeking
    // Timestamp in streams are measured in frames rather than seconds
    //int64_t frametimestamp = (int64_t)(5 * AV_TIME_BASE);  // Seek five seconds from the beginning

    int64_t seek_target = 0;
    if (videoStream->duration != AV_NOPTS_VALUE)
    {
        seek_target = videoStream->duration / 5;
    }
    else
    {
        seek_target = av_rescale_q(formatContext->duration / 5, av_get_time_base_q(), videoStream->time_base);
    }

    if (!imagePath.endsWith(QString::fromUtf8(".mp3"), Qt::CaseInsensitive) && seek_target > 0
            && av_seek_frame(formatContext, videoStreamIdx, seek_target, AVSEEK_FLAG_BACKWARD) < 0)
    {
        LOG_warn << "Error seeking video";
        av_frame_free(&videoFrame);
        av_freep(&targetFrame->data[0]);
        av_frame_free(&targetFrame);
        avcodec_close(&codecContext);
        sws_freeContext(swsContext);
        avformat_close_input(&formatContext);
        return NULL;
    }

    AVPacket packet;
    av_init_packet(&packet);
    packet.data = NULL;
    packet.size = 0;

    int decodedBytes;
    int scalingResult;
    int actualNumFrames = 0;
    int frameExtracted  = 0;

    // Read frames until succesfull decodification or reach limit of 220 frames
    while (actualNumFrames < 220 && av_read_frame(formatContext, &packet) >= 0)
    {
       if (packet.stream_index == videoStream->index)
       {
           decodedBytes = avcodec_decode_video2(&codecContext, videoFrame, &frameExtracted, &packet);
           if (frameExtracted && decodedBytes >= 0)
           {
                if (sourcePixelFormat != codecContext.pix_fmt)
                {
                    LOG_warn << "Error: pixel format changed from " << sourcePixelFormat << " to " << codecContext.pix_fmt;
                    av_packet_unref(&packet);
                    av_frame_free(&videoFrame);
                    avcodec_close(&codecContext);
                    av_freep(&targetFrame->data[0]);
                    av_frame_free(&targetFrame);
                    sws_freeContext(swsContext);
                    avformat_close_input(&formatContext);
                    return NULL;
                }

                scalingResult = sws_scale(swsContext, videoFrame->data, videoFrame->linesize,
                                     0, codecContext.height, targetFrame->data, targetFrame->linesize);

                if (scalingResult > 0)
                {
                    QImage image(width, height, QImage::Format_ARGB32);
                    if (avpicture_layout((AVPicture *)targetFrame, targetPixelFormat,
                                    width, height, image.bits(), image.byteCount()) <= 0)
                    {
                        LOG_warn << "Error copying frame";
                        av_packet_unref(&packet);
                        av_frame_free(&videoFrame);
                        avcodec_close(&codecContext);
                        av_freep(&targetFrame->data[0]);
                        av_frame_free(&targetFrame);
                        sws_freeContext(swsContext);
                        avformat_close_input(&formatContext);
                        return NULL;
                    }

                    QBuffer *buffer = new QBuffer();
                    if (!buffer->open(QIODevice::ReadWrite) || !image.save(buffer, "JPG", 85))
                    {
                        LOG_warn << "Error extracting image";
                        delete buffer;
                        av_packet_unref(&packet);
                        av_frame_free(&videoFrame);
                        avcodec_close(&codecContext);
                        av_freep(&targetFrame->data[0]);
                        av_frame_free(&targetFrame);
                        sws_freeContext(swsContext);
                        avformat_close_input(&formatContext);
                        return NULL;
                    }

                    orientation = ROTATION_UP;
                    uint8_t* displaymatrix = av_stream_get_side_data(videoStream, AV_PKT_DATA_DISPLAYMATRIX, NULL);
                    if (displaymatrix)
                    {
                        double rot = av_display_rotation_get((int32_t*) displaymatrix);
#if __cplusplus >= 201103L
#ifndef __APPLE__
                        if (!std::isnan(rot))
#else
                        if (!isnan(rot))
#endif
#else
                        if (!(rot != rot))
#endif
                        {
                            if (rot < -135 || rot > 135)
                            {
                                orientation = ROTATION_DOWN;
                            }
                            else if (rot < -45)
                            {
                                orientation = ROTATION_LEFT;
                                width = codecContext.height;
                                height = codecContext.width;
                            }
                            else if (rot > 45)
                            {
                                orientation = ROTATION_RIGHT;
                                width = codecContext.height;
                                height = codecContext.width;
                            }
                        }
                    }

                    w = width;
                    h = height;
                    buffer->seek(0);
                    QImageReader *imageReader = new QImageReader(buffer, QByteArray("JPG"));
                    LOG_debug << "Video image ready";

                    av_packet_unref(&packet);
                    av_frame_free(&videoFrame);
                    avcodec_close(&codecContext);
                    av_freep(&targetFrame->data[0]);
                    av_frame_free(&targetFrame);
                    sws_freeContext(swsContext);
                    avformat_close_input(&formatContext);
                    return imageReader;
                }
           }

           actualNumFrames++;
       }

       av_packet_unref(&packet);
    }

    LOG_warn << "Error reading frame";
    av_packet_unref(&packet);
    av_frame_free(&videoFrame);
    avcodec_close(&codecContext);
    av_freep(&targetFrame->data[0]);
    av_frame_free(&targetFrame);
    sws_freeContext(swsContext);
    avformat_close_input(&formatContext);
    return NULL;
}

#endif

const char *GfxProcQT::supportedformats()
{
    return supportedformatsQT();
}

const char *GfxProcQT::supportedvideoformats()
{
#ifdef HAVE_FFMPEG
    return supportedformatsFfmpeg();
#else
    return NULL;
#endif
}

} // namespace
