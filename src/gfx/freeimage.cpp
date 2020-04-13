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

#ifdef HAVE_FFMPEG
std::mutex GfxProcFreeImage::gfxMutex;
#endif

GfxProcFreeImage::GfxProcFreeImage()
{
    dib = NULL;
    w = 0;
    h = 0;

#ifdef FREEIMAGE_LIB
	FreeImage_Initialise(TRUE);
#endif
#ifdef HAVE_FFMPEG
    gfxMutex.lock();
    av_register_all();
    avcodec_register_all();
//    av_log_set_level(AV_LOG_VERBOSE);
    gfxMutex.unlock();
#endif
}


#ifdef HAVE_FFMPEG

#ifdef AV_CODEC_CAP_TRUNCATED
#define CAP_TRUNCATED AV_CODEC_CAP_TRUNCATED
#else
#define CAP_TRUNCATED CODEC_CAP_TRUNCATED
#endif

const char *GfxProcFreeImage::supportedformatsFfmpeg()
{
    return  ".264.265.3g2.3gp.3gpa.3gpp.3gpp2.mp3"
            ".avi.dde.divx.evo.f4v.flv.gvi.h261.h263.h264.h265.hevc"
            ".ismt.ismv.ivf.jpm.k3g.m1v.m2p.m2s.m2t.m2v.m4s.m4t.m4v.mac.mkv.mk3d"
            ".mks.mov.mp1v.mp2v.mp4.mp4v.mpeg.mpg.mpgv.mpv.mqv.ogm.ogv"
            ".qt.sls.tmf.trp.ts.ty.vc1.vob.vr.webm.wmv.";
}

bool GfxProcFreeImage::readbitmapFfmpeg(FileAccess* fa, LocalPath& localImagePath, int size)
{
#ifndef DEBUG
    av_log_set_level(AV_LOG_PANIC);
#endif

    string imagePath = localImagePath.toPath(*client->fsaccess);  // WIN32 ffmpeg uses utf8 rather than wide strings

    // Open video file
    AVFormatContext* formatContext = avformat_alloc_context();
    if (avformat_open_input(&formatContext, imagePath.c_str(), NULL, NULL))
    {
        LOG_warn << "Error opening video: " << imagePath;
        return NULL;
    }

    // Get stream information
    if (avformat_find_stream_info(formatContext, NULL))
    {
        LOG_warn << "Stream info not found: " << imagePath;
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
        LOG_warn << "Video stream not found: " << imagePath;
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
    AVPixelFormat targetPixelFormat = AV_PIX_FMT_BGR24; //raw data expected by freeimage is in this format
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

    char ext[8];

    if (client->fsaccess->getextension(localImagePath,ext,8)
            && strcmp(ext,".mp3") && seek_target > 0
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
                    int fav = targetPixelFormat;
                    int imagesize = avpicture_get_size((enum AVPixelFormat)fav, width, height);
                    FIMEMORY fmemory;
                    fmemory.data = malloc(imagesize);

                    if (avpicture_layout((AVPicture *)targetFrame, (enum AVPixelFormat)fav,
                                    width, height, (unsigned char*)fmemory.data, imagesize) <= 0)
                    {
                        LOG_warn << "Error copying frame";
                        av_packet_unref(&packet);
                        av_frame_free(&videoFrame);
                        avcodec_close(&codecContext);
                        av_freep(&targetFrame->data[0]);
                        av_frame_free(&targetFrame);
                        sws_freeContext(swsContext);
                        avformat_close_input(&formatContext);
                        free (fmemory.data);
                        return NULL;
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

                    free (fmemory.data);

                    LOG_debug << "Video image ready";

                    av_packet_unref(&packet);
                    av_frame_free(&videoFrame);
                    avcodec_close(&codecContext);
                    av_freep(&targetFrame->data[0]);
                    av_frame_free(&targetFrame);
                    sws_freeContext(swsContext);
                    avformat_close_input(&formatContext);

                    w = FreeImage_GetWidth(dib);
                    h = FreeImage_GetHeight(dib);

                    if (!w || !h)
                    {
                        return false;
                    }

                    return true;
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


const char* GfxProcFreeImage::supportedformats()
{
    if (!sformats.size())
    {
        sformats+=".jpg.png.bmp.tif.tiff.jpeg.cut.dds.exr.g3.gif.hdr.ico.iff.ilbm"
           ".jbig.jng.jif.koala.pcd.mng.pcx.pbm.pgm.ppm.pfm.pict.pic.pct.pds.raw.3fr.ari"
           ".arw.bay.crw.cr2.cap.dcs.dcr.dng.drf.eip.erf.fff.iiq.k25.kdc.mdc.mef.mos.mrw"
           ".nef.nrw.obm.orf.pef.ptx.pxn.r3d.raf.raw.rwl.rw2.rwz.sr2.srf.srw.x3f.ras.tga"
           ".xbm.xpm.jp2.j2k.jpf.jpx.";
#ifdef HAVE_FFMPEG
        sformats.append(supportedformatsFfmpeg());
#endif
    }

    return sformats.c_str();
}

bool GfxProcFreeImage::readbitmap(FileAccess* fa, LocalPath& localname, int size)
{
#ifdef _WIN32
    ScopedLengthRestore restoreLen(localname);
    localname.editStringDirect()->append("", 1);
#endif

#ifdef HAVE_FFMPEG
    char ext[8];
    bool isvideo = false;
    if (client->fsaccess->getextension(localname, ext, sizeof ext))
    {
        const char* ptr;
        if ((ptr = strstr(supportedformatsFfmpeg(), ext)) && ptr[strlen(ext)] == '.')
        {
            isvideo = true;
            if (!readbitmapFfmpeg(fa, localname, size) )
            {
                return false;
            }
        }
    }
    if (!isvideo)
    {
#endif

    // FIXME: race condition, need to use open file instead of filename
    FREE_IMAGE_FORMAT fif = FreeImage_GetFileTypeX((freeimage_filename_char_t*)localname.editStringDirect()->data());

    if (fif == FIF_UNKNOWN)
    {
        return false;
    }

 #ifndef OLD_FREEIMAGE
    if (fif == FIF_JPEG)
    {
        // load JPEG (scale & EXIF-rotate)
        if (!(dib = FreeImage_LoadX(fif, (freeimage_filename_char_t*) localname.editStringDirect()->data(),
                                    JPEG_EXIFROTATE | JPEG_FAST | (size << 16))))
        {
            return false;
        }
    }
    else
#endif
    {
        // load all other image types - for RAW formats, rely on embedded preview
        if (!(dib = FreeImage_LoadX(fif, (freeimage_filename_char_t*)localname.editStringDirect()->data(),
                #ifndef OLD_FREEIMAGE
                                    (fif == FIF_RAW) ? RAW_PREVIEW : 0)))
                #else
                                    0)))
                #endif
        {
            return false;
        }
    }

#ifdef HAVE_FFMPEG
    }
#endif
    w = FreeImage_GetWidth(dib);
    h = FreeImage_GetHeight(dib);

    if (!w || !h)
    {
        return false;
    }

    return true;
}

bool GfxProcFreeImage::resizebitmap(int rw, int rh, string* jpegout)
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

    return !!jpegout->size();
}

void GfxProcFreeImage::freebitmap()
{
    if (dib != NULL)
    {
        FreeImage_Unload(dib);
    }
}
} // namespace

#endif
