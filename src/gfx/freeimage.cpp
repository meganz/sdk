/**
 * @file freeimage.cpp
 * @brief Graphics layer using FreeImage
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

#include "mega.h"
#include "mega/gfx/freeimage.h"

#ifdef USE_FREEIMAGE

#ifdef _WIN32
#define FreeImage_GetFileTypeX FreeImage_GetFileTypeU
#define FreeImage_LoadX FreeImage_LoadU
typedef const wchar_t freeimage_filename_char_t;
#else
#define FreeImage_GetFileTypeX FreeImage_GetFileType
#define FreeImage_LoadX FreeImage_Load
typedef const char freeimage_filename_char_t;
#endif

#if FREEIMAGE_MAJOR_VERSION < 3 || FREEIMAGE_MINOR_VERSION < 13
#define OLD_FREEIMAGE
#endif


#ifdef HAVE_FFMPEG
extern "C" {
#ifdef _WIN32
#pragma warning(disable:4996)
#pragma warning(push)
#pragma warning(disable:4242)
#pragma warning(disable:4244)
#endif
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
#include <libavutil/display.h>
#include <libavutil/imgutils.h>
#ifdef _WIN32
#pragma warning(pop)
#endif
}
#endif

namespace mega {

#if defined(HAVE_FFMPEG) || defined(HAVE_PDFIUM)
std::mutex GfxProviderFreeImage::gfxMutex;
#endif

#ifdef FREEIMAGE_LIB
std::mutex GfxProviderFreeImage::libFreeImageInitializedMutex;
unsigned GfxProviderFreeImage::libFreeImageInitialized = 0;
#endif

GfxProviderFreeImage::GfxProviderFreeImage()
{
    dib = NULL;
    w = 0;
    h = 0;

#ifdef FREEIMAGE_LIB
    {
        std::unique_lock<std::mutex> guard(libFreeImageInitializedMutex);
        if (!libFreeImageInitialized)
        {
            FreeImage_Initialise(TRUE);
            libFreeImageInitialized++;
        }
    }
#endif

#ifdef HAVE_PDFIUM
    pdfiumInitialized = false;
#endif

#ifdef HAVE_FFMPEG
//    av_log_set_level(AV_LOG_VERBOSE);
#endif
}

GfxProviderFreeImage::~GfxProviderFreeImage()
{
#ifdef FREEIMAGE_LIB
    {
        std::unique_lock<std::mutex> guard(libFreeImageInitializedMutex);
        if (libFreeImageInitialized)
        {
            FreeImage_DeInitialise();
            libFreeImageInitialized--;
        }
    }
#endif

#ifdef HAVE_PDFIUM
    gfxMutex.lock();
    if (pdfiumInitialized)
    {
        PdfiumReader::destroy();
    }
    gfxMutex.unlock();
#endif
}

#ifdef USE_MEDIAINFO
bool GfxProviderFreeImage::readbitmapMediaInfo(const LocalPath& imagePath)
{
    const pair<string, string>& cover = MediaProperties::getCoverFromId3v2(imagePath.localpath);
    if (cover.first.empty())
    {
        return false;
    }

    FREE_IMAGE_FORMAT format = FIF_UNKNOWN;
    int flags = 0;
    if (cover.second == "jpg")
    {
        format = FIF_JPEG;
        flags = JPEG_EXIFROTATE | JPEG_FAST;
    }
    else if (cover.second == "png")
    {
        format = FIF_PNG;
    }

    if (format == FIF_UNKNOWN)
    {
        // It either didn't have a cover, or there was a problem reading it,
        // in which case it should have been already logged by now.
        return false;
    }

    BYTE* dataBytes = (BYTE*)cover.first.c_str();
    FIMEMORY* dataMem = FreeImage_OpenMemory(dataBytes, (DWORD)cover.first.size());
    dib = FreeImage_LoadFromMemory(format, dataMem, flags);
    FreeImage_CloseMemory(dataMem);
    if (!dib)
    {
        LOG_warn << "Error converting raw MediaInfo bitmap from memory.";
        return false;
    }

    w = static_cast<int>(FreeImage_GetWidth(dib));
    h = static_cast<int>(FreeImage_GetHeight(dib));

    return true;
}
#endif

bool GfxProviderFreeImage::readbitmapFreeimage(FileSystemAccess*, const LocalPath& imagePath, int size)
{

    // FIXME: race condition, need to use open file instead of filename
    FREE_IMAGE_FORMAT fif = FreeImage_GetFileTypeX(imagePath.localpath.c_str());

    if (fif == FIF_UNKNOWN)
    {
        return false;
    }

#ifndef OLD_FREEIMAGE
    if (fif == FIF_JPEG)
    {
        // load JPEG (scale & EXIF-rotate)
        if (!(dib = FreeImage_LoadX(fif, imagePath.localpath.c_str(),
                                    JPEG_EXIFROTATE | JPEG_FAST | (size << 16))))
        {
            return false;
        }
    }
    else
#endif
    {
        // load all other image types - for RAW formats, rely on embedded preview
        if (!(dib = FreeImage_LoadX(fif, imagePath.localpath.c_str(),
#ifndef OLD_FREEIMAGE
                                    (fif == FIF_RAW) ? RAW_PREVIEW : 0)))
#else
                                    0)))
#endif
        {
            return false;
        }
    }

    w = static_cast<int>(FreeImage_GetWidth(dib));
    h = static_cast<int>(FreeImage_GetHeight(dib));

    return w > 0 && h > 0;
}

#ifdef HAVE_FFMPEG

#ifdef AV_CODEC_CAP_TRUNCATED
#define CAP_TRUNCATED AV_CODEC_CAP_TRUNCATED
#else
#define CAP_TRUNCATED CODEC_CAP_TRUNCATED
#endif

const char *GfxProviderFreeImage::supportedformatsFfmpeg()
{
    return  ".264.265.3g2.3gp.3gpa.3gpp.3gpp2.mp3"
            ".avi.dde.divx.evo.f4v.flv.gvi.h261.h263.h264.h265.hevc"
            ".ismt.ismv.ivf.jpm.k3g.m1v.m2p.m2s.m2t.m2v.m4s.m4t.m4v.mac.mkv.mk3d"
            ".mks.mov.mp1v.mp2v.mp4.mp4v.mpeg.mpg.mpgv.mpv.mqv.ogm.ogv"
            ".qt.sls.tmf.trp.ts.ty.vc1.vob.vr.webm.wmv.";
}

bool GfxProviderFreeImage::isFfmpegFile(const string& ext)
{
    const char* ptr;
    if ((ptr = strstr(supportedformatsFfmpeg(), ext.c_str())) && ptr[ext.size()] == '.')
    {
        return true;
    }
    return false;
}


template<class Deleter, class Ptr>
class ScopeGuard {
public:
    ScopeGuard(Deleter deleter, Ptr arg) : mDeleter(deleter), mArg(arg) {}
    ~ScopeGuard() { mDeleter(mArg); }
private:
    Deleter mDeleter;
    Ptr mArg;
};

template<class F, class P>
ScopeGuard<F, P> makeScopeGuard(F f, P p){ return ScopeGuard<F, P>(f, p);	}

bool GfxProviderFreeImage::readbitmapFfmpeg(FileSystemAccess* fa, const LocalPath& imagePath, int size)
{
#ifndef DEBUG
    av_log_set_level(AV_LOG_PANIC);
#endif

    // Open video file
    AVFormatContext* formatContext = nullptr;
#if defined(LIBAVFORMAT_VERSION_MAJOR) && LIBAVFORMAT_VERSION_MAJOR < 58
    // deprecated/no longer required in FFMPEG 4.0:
    av_register_all();
#endif
    if (avformat_open_input(&formatContext, imagePath.toPath().c_str(), NULL, NULL))
    {
        LOG_warn << "Error opening video: " << imagePath;
        return false;
    }

    auto fmtContextGuard = makeScopeGuard(avformat_close_input, &formatContext);

    // Get stream information
    if (avformat_find_stream_info(formatContext, NULL))
    {
        LOG_warn << "Stream info not found: " << imagePath;
        return false;
    }


    // Find first video stream type
    AVStream *videoStream = NULL;
    int videoStreamIdx = 0;
    for (unsigned i = 0; i < formatContext->nb_streams; i++)
    {
        if (formatContext->streams[i]->codecpar && formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoStream = formatContext->streams[i];
            videoStreamIdx = i;
            break;
        }
    }

    if (!videoStream)
    {
        LOG_warn << "Video stream not found: " << imagePath;
        return false;
    }

    // Get codec params to determine video frame dimensions
    AVCodecParameters *codecParm = videoStream->codecpar;
    int width = codecParm->width;
    int height = codecParm->height;
    if (width <= 0 || height <= 0)
    {
        LOG_warn << "Invalid video dimensions: " << width << ", " << height;
        return false;
    }

    if (codecParm->format == AV_PIX_FMT_NONE)
    {
        LOG_warn << "Invalid pixel format: " << codecParm->format;
        return false;
    }

    // Find decoder for video stream
    AVCodecID codecId = codecParm->codec_id;
    AVCodec* decoder = avcodec_find_decoder(codecId);
    if (!decoder)
    {
        LOG_warn << "Codec not found: " << codecId;
        return false;
    }

    AVCodecContext *codecContext = avcodec_alloc_context3(decoder);
    auto codecContextGuard = makeScopeGuard(avcodec_free_context, &codecContext);
    if (codecContext && avcodec_parameters_to_context(codecContext, codecParm) < 0)
    {
        LOG_warn << "Could not copy codec parameters to context";
        return false;
    }

    // Force seeking to key frames
    formatContext->seek2any = false;
    videoStream->skip_to_keyframe = true;
    if (decoder->capabilities & CAP_TRUNCATED)
    {
        codecContext->flags |= CAP_TRUNCATED;
    }

    AVPixelFormat sourcePixelFormat = static_cast<AVPixelFormat>(codecParm->format);
    AVPixelFormat targetPixelFormat = AV_PIX_FMT_BGR24; //raw data expected by freeimage is in this format
    SwsContext* swsContext = sws_getContext(width, height, sourcePixelFormat,
                                            width, height, targetPixelFormat,
                                            SWS_FAST_BILINEAR, NULL, NULL, NULL);
    auto swsContextGuard = makeScopeGuard(sws_freeContext, swsContext);
    if (!swsContext)
    {
        LOG_warn << "SWS Context not found: " << sourcePixelFormat;
        return false;
    }

    // Open codec
    if (avcodec_open2(codecContext, decoder, NULL) < 0)
    {
        LOG_warn << "Error opening codec: " << codecId;
        return false;
    }

    //Allocate video frames
    AVFrame* videoFrame = av_frame_alloc();
    auto videoFrameGuard = makeScopeGuard(av_frame_free, &videoFrame);

    AVFrame* targetFrame = av_frame_alloc();
    auto targetFrameGuard = makeScopeGuard(av_frame_free, &targetFrame);

    if (!videoFrame || !targetFrame)
    {
        LOG_warn << "Error allocating video frames";
        return false;
    }

    targetFrame->format = targetPixelFormat;
    targetFrame->width = width;
    targetFrame->height = height;
    if (av_image_alloc(targetFrame->data, targetFrame->linesize, targetFrame->width, targetFrame->height, targetPixelFormat, 32) < 0)
    {
        LOG_warn << "Error allocating frame";
        return false;
    }

    auto targetFrameDataGuard = makeScopeGuard(av_freep, &targetFrame->data[0]);

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

    string extension;
    if (fa->getextension(imagePath, extension)
            && strcmp(extension.c_str(),".mp3") && seek_target > 0
            && av_seek_frame(formatContext, videoStreamIdx, seek_target, AVSEEK_FLAG_BACKWARD) < 0)
    {
        LOG_warn << "Error seeking video";
        return false;
    }

    AVPacket packet;
    av_init_packet(&packet);
    packet.data = NULL;
    packet.size = 0;

    int scalingResult;
    int actualNumFrames = 0;

    // Read frames until succesfull decodification or reach limit of 220 frames
    while (actualNumFrames < 220 && av_read_frame(formatContext, &packet) >= 0)
    {
       auto avPacketGuard = makeScopeGuard(av_packet_unref, &packet);
       if (packet.stream_index == videoStream->index)
       {
           int ret = avcodec_send_packet(codecContext, &packet);
           if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
           {
               break;
           }

           while (avcodec_receive_frame(codecContext, videoFrame) >= 0)
           {
                if (sourcePixelFormat != codecContext->pix_fmt)
                {
                    LOG_warn << "Error: pixel format changed from " << sourcePixelFormat << " to " << codecContext->pix_fmt;
                    return false;
                }

                scalingResult = sws_scale(swsContext, videoFrame->data, videoFrame->linesize,
                                          0, codecParm->height, targetFrame->data, targetFrame->linesize);

                if (scalingResult > 0)
                {
                    const int legacy_align = 1;
                    int imagesize = av_image_get_buffer_size(targetPixelFormat, width, height, legacy_align);
                    FIMEMORY fmemory;
                    fmemory.data = malloc(imagesize);
                    if (!fmemory.data)
                    {
                        LOG_warn << "Error allocating image copy buffer";
                        return false;
                    }
                    auto fmemoryDataGuard = makeScopeGuard(free, fmemory.data);

                    if (av_image_copy_to_buffer((uint8_t *)fmemory.data, imagesize,
                                targetFrame->data, targetFrame->linesize,
                                targetPixelFormat, width, height, legacy_align) <= 0)
                    {
                        LOG_warn << "Error copying frame";
                        return false;
                    }

                    //int pitch = imagesize/height;
                    int pitch = width*3;

                    if (!(dib = FreeImage_ConvertFromRawBits((BYTE*)fmemory.data,width,height,
                                                             pitch, 24, FI_RGBA_RED_SHIFT, FI_RGBA_GREEN_MASK,
                                                             FI_RGBA_BLUE_MASK | 0xFFFF, TRUE) ) )
                    {
                        LOG_warn << "Error loading freeimage from memory: " << imagePath;
                    }
                    else
                    {
                        LOG_verbose << "SUCCESS loading freeimage from memory: "<< imagePath;
                    }

                    LOG_debug << "Video image ready";

                    w = FreeImage_GetWidth(dib);
                    h = FreeImage_GetHeight(dib);

                    return w > 0 && h > 0;
                }
           }

           actualNumFrames++;
       }
    }


    LOG_warn << "Error reading frame";
    return false;
}

#endif

#ifdef HAVE_PDFIUM
const char* GfxProviderFreeImage::supportedformatsPDF()
{
    return ".pdf.";
}

bool GfxProviderFreeImage::isPdfFile(const string &ext)
{
    const char* ptr;
    if ((ptr = strstr(supportedformatsPDF(), ext.c_str())) && ptr[ext.size()] == '.')
    {
        return true;
    }
    return false;
}

bool GfxProviderFreeImage::readbitmapPdf(FileSystemAccess* fa, const LocalPath& imagePath, int size)
{
    std::lock_guard<std::mutex> g(gfxMutex);
    if (!pdfiumInitialized)
    {
        pdfiumInitialized = true;
        PdfiumReader::init();
    }

    int orientation;
#ifdef _WIN32
    wstring tmpPath;
    tmpPath.resize(MAX_PATH);
    LocalPath workingDir;
    if (!GetTempPathW(MAX_PATH, (LPWSTR)tmpPath.data())) // If the function fails, the return value is zero.
    {
        LOG_warn << "Error getting temporary path to process pdf.";
        workingDir.clear();
    }
    else
    {
        workingDir = LocalPath::fromPlatformEncodedAbsolute(tmpPath.c_str());
    }

    unique_ptr<char[]> data = PdfiumReader::readBitmapFromPdf(w, h, orientation, imagePath, fa, workingDir);
#else
    unique_ptr<char[]> data = PdfiumReader::readBitmapFromPdf(w, h, orientation, imagePath, fa);
#endif

    if (!data || !w || !h)
    {
        return false;
    }

    dib = FreeImage_ConvertFromRawBits(reinterpret_cast<BYTE*>(data.get()), w, h, w * 4, 32, 0xFF0000, 0x00FF00, 0x0000FF);
    if (!dib)
    {
        LOG_warn << "Error converting raw pdfium bitmap from memory: " << imagePath;
        return false;
    }
    FreeImage_FlipHorizontal(dib);

    return true;
}
#endif

const char* GfxProviderFreeImage::supportedformats()
{
    if (sformats.empty())
    {
        //Disable thumbnail creation temporarily for .tiff.tif.exr
        sformats+=".jpg.png.bmp.jpeg.cut.dds.g3.gif.hdr.ico.iff.ilbm"
           ".jbig.jng.jif.koala.pcd.mng.pcx.pbm.pgm.ppm.pfm.pict.pic.pct.pds.raw.3fr.ari"
           ".arw.bay.crw.cr2.cap.dcs.dcr.dng.drf.eip.erf.fff.iiq.k25.kdc.mdc.mef.mos.mrw"
           ".nef.nrw.obm.orf.pef.ptx.pxn.r3d.raf.raw.rwl.rw2.rwz.sr2.srf.srw.x3f.ras.tga"
           ".xbm.xpm.jp2.j2k.jpf.jpx.";
#ifdef HAVE_FFMPEG
        sformats.append(supportedformatsFfmpeg());
#endif
#ifdef HAVE_PDFIUM
        sformats.append(supportedformatsPDF());
#endif
#ifdef USE_MEDIAINFO
        sformats.append(MediaProperties::supportedformatsMediaInfo());
#endif
    }

    return sformats.c_str();
}

const char *GfxProviderFreeImage::supportedvideoformats()
{
    return NULL;
}

bool GfxProviderFreeImage::readbitmap(FileSystemAccess* fa, const LocalPath& localname, int size)
{

    bool bitmapLoaded = false;
    string extension;
    if (fa->getextension(localname, extension))
    {
#ifdef USE_MEDIAINFO
        if (MediaProperties::isMediaFilenameExtAudio(extension))
        {
            return readbitmapMediaInfo(localname);
        }
#endif
#ifdef HAVE_FFMPEG
        if (isFfmpegFile(extension))
        {
            bitmapLoaded = true;
            if (!readbitmapFfmpeg(fa, localname, size) )
            {
                return false;
            }
        }
#endif
#ifdef HAVE_PDFIUM
        if (isPdfFile(extension))
        {
            bitmapLoaded = true;
            if (!readbitmapPdf(fa, localname, size) )
            {
                return false;
            }
        }
#endif
    }
    if (!bitmapLoaded)
    {
        if (!readbitmapFreeimage(fa, localname, size))
        {
            return false;
        }
    }

    return true;
}

bool GfxProviderFreeImage::resizebitmap(int rw, int rh, string* jpegout)
{
    FIBITMAP* tdib;
    FIMEMORY* hmem;
    int px, py;

    if (!w || !h) return false;

    if (dib == NULL) return false;

    transform(w, h, rw, rh, px, py);

    if (!w || !h) return false;

    jpegout->clear();

    if ((tdib = FreeImage_Rescale(dib, w, h, FILTER_BILINEAR)))
    {
        FreeImage_Unload(dib);

        dib = tdib;

        if ((tdib = FreeImage_Copy(dib, px, py, px + rw, py + rh)))
        {
            FreeImage_Unload(dib);

            dib = tdib;

            WORD bpp = (WORD)FreeImage_GetBPP(dib);
            if (bpp != 24) {
                if ((tdib = FreeImage_ConvertTo24Bits(dib)) == NULL) {
                    FreeImage_Unload(dib);
                    dib = tdib;
                    return 0;
                }
                FreeImage_Unload(dib);
                dib = tdib;
            }

            if ((hmem = FreeImage_OpenMemory()))
            {
                if (FreeImage_SaveToMemory(FIF_JPEG, dib, hmem,
                #ifndef OLD_FREEIMAGE
                    JPEG_BASELINE | JPEG_OPTIMIZE |
                #endif
                    85))
                {
                    BYTE* tdata;
                    DWORD tlen;

                    FreeImage_AcquireMemory(hmem, &tdata, &tlen);
                    jpegout->assign((char*)tdata, tlen);
                }

                FreeImage_CloseMemory(hmem);
            }
        }
    }

    return !jpegout->empty();
}

void GfxProviderFreeImage::freebitmap()
{
    if (dib != NULL)
    {
        FreeImage_Unload(dib);
    }
}
} // namespace

#endif
