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

#include "mega/gfx/freeimage.h"

#include "mega.h"
#include "mega/scoped_helpers.h"

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
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/mathematics.h>
#include <libavutil/display.h>
#include <libavutil/imgutils.h>
}
#endif

namespace mega {

#if defined(HAVE_FFMPEG) || defined(HAVE_PDFIUM)
std::mutex GfxProviderFreeImage::gfxMutex;
#endif

std::mutex FreeImageInstance::mLock;
std::size_t FreeImageInstance::mNumReferences = 0;

FreeImageInstance::FreeImageInstance()
{
    std::lock_guard<std::mutex> guard(mLock);

    // Make sure reference count doesn't overflow.
    assert(mNumReferences < std::numeric_limits<std::size_t>::max());

    // Increment reference counter.
    ++mNumReferences;

#ifdef FREEIMAGE_LIB
    // Iniitalize if necessary.
    if (mNumReferences == 1)
    {
        FreeImage_Initialise(TRUE);
    }
#endif // FREEIMAGE_LIB
}

FreeImageInstance::~FreeImageInstance()
{
    std::lock_guard<std::mutex> guard(mLock);

    // Make sure reference count doesn't borrow.
    assert(mNumReferences > 0);

    // Decrement reference counter.
    --mNumReferences;

#ifdef FREEIMAGE_LIB
    // Deinitialize if necessary.
    if (mNumReferences == 0)
    {
        FreeImage_DeInitialise();
    }
#endif // FREEIMAGE_LIB
}

GfxProviderFreeImage::GfxProviderFreeImage()
{
    dib = NULL;
    w = 0;
    h = 0;

#ifdef HAVE_PDFIUM
    pdfiumInitialized = false;
#endif

#ifdef HAVE_FFMPEG
//    av_log_set_level(AV_LOG_VERBOSE);
#endif
}

GfxProviderFreeImage::~GfxProviderFreeImage()
{
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
    const StringPair& cover = MediaProperties::getCoverFromId3v2(imagePath.localpath);
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

bool GfxProviderFreeImage::readbitmapFreeimage(const LocalPath& imagePath, int size)
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
        dib = FreeImage_LoadX(fif,
                              imagePath.localpath.c_str(),
                              JPEG_EXIFROTATE | JPEG_FAST | (size << 16));
        if (!dib)
        {
            return false;
        }
    }
    else
#endif
    {
        // load all other image types - for RAW formats, rely on embedded preview
        (dib = FreeImage_LoadX(fif,
                               imagePath.localpath.c_str(),
#ifndef OLD_FREEIMAGE
                               (fif == FIF_RAW) ? RAW_PREVIEW : 0));
#else
                               0));
#endif
        if (!dib)
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
    ptr = strstr(supportedformatsFfmpeg(), ext.c_str());
    if (ptr && ptr[ext.size()] == '.')
    {
        return true;
    }
    return false;
}

bool GfxProviderFreeImage::readbitmapFfmpeg(const LocalPath& imagePath, int /*size*/)
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
    if (avformat_open_input(&formatContext, imagePath.toPath(false).c_str(), NULL, NULL))
    {
        LOG_warn << "Error opening video: " << imagePath;
        return false;
    }

    auto fmtContextGuard = makeUniqueFrom(&formatContext, avformat_close_input);

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
            videoStreamIdx = static_cast<int>(i);
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
    auto decoder = avcodec_find_decoder(codecId);
    if (!decoder)
    {
        LOG_warn << "Codec not found: " << codecId;
        return false;
    }

    AVCodecContext *codecContext = avcodec_alloc_context3(decoder);
    auto codecContextGuard = makeUniqueFrom(&codecContext, avcodec_free_context);
    if (!codecContext || avcodec_parameters_to_context(codecContext, codecParm) < 0)
    {
        LOG_warn << "Could not copy codec parameters to context";
        return false;
    }

    // Force seeking to key frames
    formatContext->seek2any = false;
    if (decoder->capabilities & CAP_TRUNCATED)
    {
        codecContext->flags |= CAP_TRUNCATED;
    }

    AVPixelFormat sourcePixelFormat = static_cast<AVPixelFormat>(codecParm->format);
    AVPixelFormat targetPixelFormat = AV_PIX_FMT_BGR24; //raw data expected by freeimage is in this format
    SwsContext* swsContext = sws_getContext(width, height, sourcePixelFormat,
                                            width, height, targetPixelFormat,
                                            SWS_FAST_BILINEAR, NULL, NULL, NULL);
    auto swsContextGuard = makeUniqueFrom(swsContext, sws_freeContext);
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
    auto videoFrameGuard = makeUniqueFrom(&videoFrame, av_frame_free);

    AVFrame* targetFrame = av_frame_alloc();
    auto targetFrameGuard = makeUniqueFrom(&targetFrame, av_frame_free);

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

    auto targetFrameDataGuard = makeUniqueFrom(&targetFrame->data[0], av_freep);

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

    string extension = imagePath.extension();
    if (!extension.empty()
            && strcmp(extension.c_str(),".mp3") && seek_target > 0
            && av_seek_frame(formatContext, videoStreamIdx, seek_target, AVSEEK_FLAG_BACKWARD) < 0)
    {
        LOG_warn << "Error seeking video";
        return false;
    }

    AVPacket* p = av_packet_alloc();
    std::unique_ptr<AVPacket*, std::function<void(AVPacket**)>> pCleanup{ &p, [](AVPacket** pkt) { av_packet_free(pkt); } };

    AVPacket& packet = *p;
    packet.data = NULL;
    packet.size = 0;

    // Compute the video's rotation.
    auto rotation = [&]()
    {
        // Retrieve the video's display matrix.
        auto* matrix = av_stream_get_side_data(videoStream, AV_PKT_DATA_DISPLAYMATRIX, nullptr);

        // No display matrix? No rotation.
        if (!matrix)
        {
            return 0;
        }

        // Retrieve the video's rotation.
        return (int)av_display_rotation_get((int32_t*)matrix);
    }();

    int scalingResult;
    int actualNumFrames = 0;
    int result = 0;

    // Read frames until succesfull decodification or reach limit of 220 frames
    while (actualNumFrames < 220 && result != AVERROR_EOF)
    {
        // Try and read a packet from the file.
        result = av_read_frame(formatContext, &packet);

        // Couldn't read a packet from the file due to some (hard) error.
        //
        // Note that we don't break here if we couldn't read a packet
        // because we hit the end of the file. This is because in this case,
        // the packet will be a dummy which we'll feed into the codec below.
        if (result < 0 && result != AVERROR_EOF)
        {
            break;
        }

        // Make sure any data contained in the packet is released at the end
        // of this iteration.
        auto avPacketGuard = makeUniqueFrom(&packet, av_packet_unref);

        // We're only interested in video packets.
        if (packet.stream_index == videoStream->index)
        {
            // Feed the packet we retrieved from the file into our codec.
            //
            // Note that the packet we feed into the codec will be a dummy
            // if we hit the end of the file. The reason we still need to
            // process this dummy packet is that doing so will put the codec
            // into "drain mode."
            //
            // This is necessary because even though we've hit the end of
            // the file, the codec may still contain frames for us to
            // process.
            result = avcodec_send_packet(codecContext, &packet);

            // Encountered a hard error passing the packet to the codec.
            if (result < 0 && result != AVERROR(EAGAIN) && result != AVERROR_EOF)
            {
                break;
            }

            // Keep extracting decoded frames from the codec for as long as
            // we can. If we haven't hit the end of the file, this function
            // will return EAGAIN which means it needs us to feed the codec
            // more packets. If the function returns EOF, it means that the
            // codec has been drained and no further decoded frames are
            // possible.
            //
            // Note that this function can only return EOF if the codec had
            // entered "draining mode." That is, it'll only happen if
            // av_read_frame above also returned EOF.
            while ((result = avcodec_receive_frame(codecContext, videoFrame)) >= 0)
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
                    fmemory.data = malloc(static_cast<size_t>(imagesize));
                    if (!fmemory.data)
                    {
                        LOG_warn << "Error allocating image copy buffer";
                        return false;
                    }
                    auto fmemoryDataGuard = makeUniqueFrom(fmemory.data, free);

                    if (av_image_copy_to_buffer((uint8_t *)fmemory.data, imagesize,
                                targetFrame->data, targetFrame->linesize,
                                targetPixelFormat, width, height, legacy_align) <= 0)
                    {
                        LOG_warn << "Error copying frame";
                        return false;
                    }

                    int pitch = width * 3;

                    // Assume we can't generate the image from our raw frame.
                    w = 0;
                    h = 0;

                    // Try and generate an image from our raw frame.
                    (dib = FreeImage_ConvertFromRawBits((BYTE*)fmemory.data,
                                                        width,
                                                        height,
                                                        pitch,
                                                        24,
                                                        FI_RGBA_RED_SHIFT,
                                                        FI_RGBA_GREEN_MASK,
                                                        FI_RGBA_BLUE_MASK | 0xFFFF,
                                                        TRUE));
                    if (!dib)
                    {
                        LOG_warn << "Error loading freeimage from memory: " << imagePath;
                        return false;
                    }

                    // Invert any rotation if necessary.
                    if (rotation)
                    {
                        if (auto* temp = FreeImage_Rotate(dib, rotation))
                        {
                            FreeImage_Unload(dib);
                            dib = temp;
                        }
                        else
                        {
                            LOG_warn << "Couldn't remove rotation from image: " << imagePath;
                        }
                    }

                    w = static_cast<int>(FreeImage_GetWidth(dib));
                    h = static_cast<int>(FreeImage_GetHeight(dib));

                    LOG_debug << "Video image ready";

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
    ptr = strstr(supportedformatsPDF(), ext.c_str());
    if (ptr && ptr[ext.size()] == '.')
    {
        return true;
    }
    return false;
}

bool GfxProviderFreeImage::readbitmapPdf(const LocalPath& imagePath, int /*size*/)
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

    unique_ptr<char[]> data = PdfiumReader::readBitmapFromPdf(w, h, orientation, imagePath, workingDir);
#else
    unique_ptr<char[]> data = PdfiumReader::readBitmapFromPdf(w, h, orientation, imagePath);
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
        //Disable thumbnail creation temporarily for .tiff.tif.exr.pict.pic.pct
        sformats+=".jpg.png.bmp.jpeg.cut.dds.g3.gif.hdr.ico.iff.ilbm"
           ".jbig.jng.jif.koala.pcd.mng.pcx.pbm.pgm.ppm.pfm.pds.raw.3fr.ari"
           ".arw.bay.crw.cr2.cap.dcs.dcr.dng.drf.eip.erf.fff.iiq.k25.kdc.mdc.mef.mos.mrw"
           ".nef.nrw.obm.orf.pef.ptx.pxn.r3d.raf.raw.rwl.rw2.rwz.sr2.srf.srw.x3f.ras.tga"
           ".xbm.xpm.jp2.j2k.jpf.jpx.webp.";
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

bool GfxProviderFreeImage::readbitmap(const LocalPath& localname, int size)
{

    bool bitmapLoaded = false;
    if (string extension = localname.extension(); !extension.empty())
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
            if (!readbitmapFfmpeg(localname, size) )
            {
                return false;
            }
        }
#endif
#ifdef HAVE_PDFIUM
        if (isPdfFile(extension))
        {
            bitmapLoaded = true;
            if (!readbitmapPdf(localname, size) )
            {
                return false;
            }
        }
#endif
    }
    if (!bitmapLoaded)
    {
        if (!readbitmapFreeimage(localname, size))
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

    tdib = FreeImage_Rescale(dib, w, h, FILTER_BILINEAR);
    if (tdib)
    {
        FreeImage_Unload(dib);

        dib = tdib;

        tdib = FreeImage_Copy(dib, px, py, px + rw, py + rh);
        if (tdib)
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

            hmem = FreeImage_OpenMemory();
            if (hmem)
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
