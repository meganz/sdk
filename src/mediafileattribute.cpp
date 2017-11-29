/**
 * @file mediafileattribute.cpp
 * @brief Classes for file attributes fetching
 *
 * (c) 2013-2017 by Mega Limited, Auckland, New Zealand
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

#include "mega/mediafileattribute.h"
#include "mega/logging.h"
#include "mega/base64.h"
#include "mega/command.h"
#include "mega/megaclient.h"

#ifdef USE_MEDIAINFO
#include "MediaInfo/MediaInfo.h"
#include "MediaInfo/MediaInfo_Config.h"
#include "ZenLib/Ztring.h"
#endif

namespace mega {

#define MEDIA_INFO_METHODOLOGY_VERSION 1    // Increment this anytime we change the way we use mediainfo, eq query new or different fields etc


#ifdef USE_MEDIAINFO

MediaFileInfo::MediaFileInfo()
    : mediaCodecsRequested(false)
    , mediaCodecsReceived(false)
    , downloadedCodecMapsVersion(0)
{
}

void MediaFileInfo::requestCodecMappingsOneTime(MegaClient* client, string* ifSuitableFilename)
{
    if (!mediaCodecsRequested)
    {
        if (ifSuitableFilename)
        {
            char ext[8];
            if (!client->fsaccess->getextension(ifSuitableFilename, ext, sizeof(ext))
                || !MediaProperties::isMediaFilenameExt(ext))
            {
                return;
            }
        }

        client->reqs.add(new CommandMediaCodecs(client, &MediaFileInfo::onCodecMappingsReceiptStatic));
        mediaCodecsRequested = true;
    }
}

unsigned MediaFileInfo::Lookup(const std::string& name, std::map<std::string, MediaFileInfo::MediaCodecs::idrecord>& data, unsigned notfoundvalue)
{
    std::map<std::string, MediaFileInfo::MediaCodecs::idrecord>::iterator i = data.find(name);
    return i == data.end() ? notfoundvalue : i->second.id;
}

byte MediaFileInfo::LookupShortFormat(unsigned containerid, unsigned videocodecid, unsigned audiocodecid)
{
    for (unsigned i = mediaCodecs.shortformats.size(); i--; )
    {
        // only 256 entries max, so iterating will be very quick
        MediaCodecs::shortformatrec& r = mediaCodecs.shortformats[i];
        if (r.containerid == containerid && r.videocodecid == videocodecid && r.audiocodecid == audiocodecid)
        {
            return r.shortformatid;
        }
    }
    return 0;  // 0 indicates an exotic combination, which requires attribute 9
}



void MediaFileInfo::ReadIdRecords(std::map<std::string, MediaCodecs::idrecord>& data, MegaClient* client)
{
    bool working = client->json.enterarray();
    if (working)
    {
        while (working = client->json.enterarray())
        {
            MediaFileInfo::MediaCodecs::idrecord rec;
            std::string idString;
            working = client->json.storeobject(&idString) &&
                      client->json.storeobject(&rec.mediainfoname);
            client->json.storeobject(&rec.mediasourcemimetype);
            if (working)
            {
                rec.id = atoi(idString.c_str());
                if (!rec.id)
                {
                    downloadedCodecMapsVersion += atoi(rec.mediainfoname.c_str());
                }
                else
                {
                    data[rec.mediainfoname] = rec;
                }
            }
            client->json.leavearray();
        }
        client->json.leavearray();
    }
}

static void ReadShortFormats(std::vector<MediaFileInfo::MediaCodecs::shortformatrec>& vec, JSON& json)
{
    bool working = json.enterarray();
    if (working)
    {
        while (working = json.enterarray())
        {
            MediaFileInfo::MediaCodecs::shortformatrec rec;
            unsigned id = atoi(json.getvalue());
            assert(id >= 0 && id < 256);
            std::string a, b, c;
            working = json.storeobject(&a) && json.storeobject(&b) && json.storeobject(&c);
            if (working)
            {
                rec.shortformatid = byte(id);
                rec.containerid = atoi(a.c_str());
                rec.videocodecid = atoi(b.c_str());
                rec.audiocodecid = atoi(c.c_str());
                vec.push_back(rec);
            }
            json.leavearray();
        }
        json.leavearray();
    }
}

void MediaFileInfo::onCodecMappingsReceiptStatic(MegaClient* client)
{
    client->mediaFileInfo.onCodecMappingsReceipt(client);
}

void MediaFileInfo::onCodecMappingsReceipt(MegaClient* client)
{
    downloadedCodecMapsVersion = 0;
    ReadIdRecords(mediaCodecs.containers, client);
    ReadIdRecords(mediaCodecs.videocodecs, client);
    ReadIdRecords(mediaCodecs.audiocodecs, client);
    ReadShortFormats(mediaCodecs.shortformats, client->json);
    mediaCodecsReceived = true;

    // update any download transfers we already processed
    for (unsigned i = queuedForDownloadTranslation.size(); i--; )
    {
        queuedvp& q = queuedForDownloadTranslation[i];
        sendOrQueueMediaPropertiesFileAttributes(q.filehandle, q.vp, q.fakey, client, NULL);
    }
    queuedForDownloadTranslation.clear();

    // resume any upload transfers that were waiting for this
    
    for (std::map<handle, queuedvp>::iterator i = uploadFileAttributes.begin(); i != uploadFileAttributes.end(); )
    {
        handle th = i->second.transferhandle;
        ++i;   // the call below may remove this item from the map
        client->pendingfa[pair<handle, fatype>(th, fa_media)] = pair<handle, int>(0, 0);
        client->checkfacompletion(th); 
    }
}

void MediaFileInfo::sendOrQueueMediaPropertiesFileAttributes(handle fh, MediaProperties& vp, uint32_t fakey[4], MegaClient* client, handle* uploadTransferHandle)
{
    if (uploadTransferHandle)
    {
        MediaFileInfo::queuedvp q;
        q.filehandle = fh;
        q.transferhandle = *uploadTransferHandle;
        q.vp = vp;
        memcpy(q.fakey, fakey, sizeof(q.fakey));
        uploadFileAttributes[q.filehandle] = q;

        if (mediaCodecsReceived)
        {
            // indicate we have this attribute ready to go. Otherwise the transfer will be put on hold till we can
            client->pendingfa[pair<handle, fatype>(*uploadTransferHandle, fa_media)] = pair<handle, int>(0, 0);
        }
    }
    else
    {
        if (!mediaCodecsReceived)
        {
            MediaFileInfo::queuedvp q;
            q.filehandle = fh;
            q.vp = vp;
            memcpy(q.fakey, fakey, sizeof(q.fakey));
            queuedForDownloadTranslation.push_back(q);
        }
        else
        {
            std::string mediafileattributes = vp.convertMediaPropertyFileAttributes(fakey, client->mediaFileInfo);
            client->reqs.add(new CommandAttachFADirect(fh, mediafileattributes.c_str()));
        }
    }
}

void MediaFileInfo::addUploadMediaFileAttributes(handle& fh, std::string* s)
{
    std::map<handle, MediaFileInfo::queuedvp>::iterator i = uploadFileAttributes.find(fh);
    if (i != uploadFileAttributes.end())
    {
        if (!s->empty())
        {
            *s += "/";
        }
        *s += i->second.vp.convertMediaPropertyFileAttributes(i->second.fakey, *this);
        uploadFileAttributes.erase(i);
    }
}

#endif  // USE_MEDIAINFO

// ----------------------------------------- xxtea encryption / decryption --------------------------------------------------------

static uint32_t endianDetectionValue = 0x01020304;

inline bool DetectBigEndian()
{
    return 0x01 == *(byte*)&endianDetectionValue;
}

inline uint32_t EndianConversion32(uint32_t x)
{
    return ((x & 0xff000000u) >> 24) | ((x & 0x00ff0000u) >> 8) | ((x & 0x0000ff00u) << 8) | ((x & 0x000000ffu) << 24);
}

inline void EndianConversion32(uint32_t* v, unsigned vlen)
{
    for (; vlen--; ++v)
        *v = EndianConversion32(*v);
}

uint32_t DELTA = 0x9E3779B9;

inline uint32_t mx(uint32_t sum, uint32_t y, uint32_t z, uint32_t p, uint32_t e, const uint32_t key[4])
{
    return (((z >> 5) ^ (y << 2)) + ((y >> 3) ^ (z << 4))) ^ ((sum ^ y) + (key[(p & 3) ^ e] ^ z));
}


void xxteaEncrypt(uint32_t* v, uint32_t vlen, uint32_t key[4])
{
    if (DetectBigEndian())
    {
        EndianConversion32(v, vlen);
        EndianConversion32(key, 4);
    }

    uint32_t n = vlen - 1;
    uint32_t z = v[n];
    uint32_t q = 6 + 52 / vlen;
    uint32_t sum = 0;
    for (; q > 0; --q)
    {
        sum += DELTA;
        uint32_t e = (sum >> 2) & 3;
        for (unsigned p = 0; p < n; ++p)
        {
            uint32_t y = v[p + 1];
            z = v[p] = v[p] + mx(sum, y, z, p, e, key);
        }
        uint32_t y = v[0];
        z = v[n] = v[n] + mx(sum, y, z, n, e, key);
    }

    if (DetectBigEndian())
    {
        EndianConversion32(v, vlen);
        EndianConversion32(key, 4);
    }
}

void xxteaDecrypt(uint32_t* v, uint32_t vlen, uint32_t key[4])
{
    if (DetectBigEndian())
    {
        EndianConversion32(v, vlen);
        EndianConversion32(key, 4);
    }
    uint32_t n = vlen - 1;
    uint32_t y = v[0];
    uint32_t q = 6 + 52 / vlen;
    uint32_t sum = q * DELTA;
    for (; sum != 0; sum -= DELTA)
    {
        uint32_t e = (sum >> 2) & 3;
        for (unsigned p = n; p > 0; --p)
        {
            uint32_t z = v[p - 1];
            y = v[p] = v[p] - mx(sum, y, z, p, e, key);
        }
        uint32_t z = v[n];
        y = v[0] = v[0] - mx(sum, y, z, 0, e, key);
    }
    if (DetectBigEndian())
    {
        EndianConversion32(v, vlen);
        EndianConversion32(key, 4);
    }
}

std::string formatfileattr(uint32_t id, byte* data, unsigned datalen, uint32_t fakey[4]) 
{
    assert(datalen % 4 == 0);
    xxteaEncrypt((uint32_t*)data, datalen/4, fakey);

    std::string encb64;
    Base64::btoa(std::string((char*)data, datalen), encb64);

    std::ostringstream result;
    result << id << "*" << encb64;
    return result.str();
}

// ----------------------------------------- MediaProperties --------------------------------------------------------

MediaProperties::MediaProperties()
    : shortformat(254)
    , width(0)
    , height(0)
    , fps(0)
    , playtime(0)
    , containerid(0)
    , videocodecid(0)
    , audiocodecid(0)
    , is_VFR(false)
    , no_audio(false)
{
}


bool MediaProperties::operator==(const MediaProperties& o) const
{ 
    return shortformat == o.shortformat && width == o.width && height == o.height && fps == o.fps && playtime == o.playtime &&
        (shortformat || (containerid == o.containerid && videocodecid == o.videocodecid && audiocodecid == o.audiocodecid));
}



// shortformat must be 0 if the format is exotic - in that case, container/videocodec/audiocodec must be valid
// if shortformat is > 0, container/videocodec/audiocodec are ignored and no attribute 9 is returned.
// fakey is an 4-uint32 Array with the file attribute key (the nonce from the file key)
std::string MediaProperties::encodeMediaPropertiesAttributes(MediaProperties vp, uint32_t fakey[4])
{
    vp.width <<= 1;
    if (vp.width >= 32768) vp.width = ((vp.width - 32768) >> 3) | 1;
    if (vp.width >= 32768) vp.width = 32767;

    vp.height <<= 1;
    if (vp.height >= 32768) vp.height = ((vp.height - 32768) >> 3) | 1;
    if (vp.height >= 32768) vp.height = 32767;

    vp.playtime <<= 1;
    if (vp.playtime >= 262144) vp.playtime = ((vp.playtime - 262200) / 60) | 1;
    if (vp.playtime >= 262144) vp.playtime = 262143;

    vp.fps <<= 1;
    if (vp.fps >= 256) vp.fps = ((vp.fps - 256) >> 3) | 1;
    if (vp.fps >= 256) vp.fps = 255;

    // LE code below
    byte v[8];
    v[7] = vp.shortformat;
    v[6] = vp.playtime >> 10;
    v[5] = (vp.playtime >> 2) & 255;
    v[4] = ((vp.playtime & 3) << 6) + (vp.fps >> 2);
    v[3] = ((vp.fps & 3) << 6) + ((vp.height >> 9) & 63);
    v[2] = (vp.height >> 1) & 255;
    v[1] = ((vp.width >> 8) & 127) + ((vp.height & 1) << 7);
    v[0] = vp.width & 255;

    std::string result = formatfileattr(fa_media, v, sizeof v, fakey);

    if (!vp.shortformat) // exotic combination of container/codecids
    {
        memset(v, 0, sizeof v);
        v[3] = (vp.audiocodecid >> 4) & 255;
        v[2] = ((vp.videocodecid >> 8) & 15) + ((vp.audiocodecid & 15) << 4);
        v[1] = vp.videocodecid & 255;
        v[0] = byte(vp.containerid);
        result.append("/");
        result.append(formatfileattr(fa_mediaext, v, sizeof v, fakey));
    }
    return result;
}

MediaProperties MediaProperties::decodeMediaPropertiesAttributes(const std::string& attrs, uint32_t fakey[4])
{
    MediaProperties r;

    int ppo = Node::hasfileattribute(&attrs, fa_media);
    int pos = ppo - 1;
    if (ppo && pos + 3 + 11 <= (int)attrs.size())
    {
        std::string binary;
        Base64::atob(attrs.substr(pos + 3, 11), binary);
        assert(binary.size() == 8);
        byte v[8];
        memcpy(v, binary.data(), std::min<size_t>(sizeof v, binary.size()));
        xxteaDecrypt((uint32_t*)v, sizeof(v)/4, fakey);

        r.width = (v[0] >> 1) + ((v[1] & 127) << 7);
        if (v[0] & 1) r.width = (r.width << 3) + 16384;

        r.height = v[2] + ((v[3] & 63) << 8);
        if (v[1] & 128) r.height = (r.height << 3) + 16384;

        r.fps = (v[3] >> 7) + ((v[4] & 63) << 1);
        if (v[3] & 64) r.fps = (r.fps << 3) + 128;

        r.playtime = (v[4] >> 7) + (v[5] << 1) + (v[6] << 9);
        if (v[4] & 64) r.playtime = r.playtime * 60 + 131100;

        if (!(r.shortformat = v[7]))
        {
            int ppo = Node::hasfileattribute(&attrs, fa_mediaext);
            int pos = ppo - 1;
            if (ppo && pos + 3 + 11 <= (int)attrs.size())
            {
                Base64::atob(attrs.substr(pos + 3, 11), binary);
                assert(binary.size() == 8);
                memcpy(v, binary.data(), std::min<size_t>(sizeof v, binary.size()));
                xxteaDecrypt((uint32_t*)v, sizeof(v) / 4, fakey);

                r.containerid = v[0];
                r.videocodecid = v[1] + ((v[2] & 15) << 8);
                r.audiocodecid = (v[2] >> 4) + (v[3] << 4);
            }
        }
    }

    return r;
}

#ifdef USE_MEDIAINFO

bool MediaProperties::isMediaFilenameExt(const std::string& ext)
{
    static const char* supportedformats = 
        ".264.265.3g2.3ga.3gp.3gpa.3gpp.3gpp2.act.aif.aifc.aiff.amr.asf.au.avc.avi.caf.dd+.dif.divx.dv.eac3.ec3"
        ".evo.f4a.f4b.f4v.flv.h261.h263.h264.h265.hevc.isma.ismt.ismv.jpm.jpx.k3g.lxf.m1a.m1v.m2a.m2p.m2s.m2t"
        ".m2v.m4a.m4b.m4p.m4s.m4t.m4v.m4v.mkv.mk3d.mka.mks.mov.mp1.mp1v.mp2.mp2v.mp3.mp4.mp4v.mpa1.mpa2.mpeg"
        ".mpg.mpgv.mpv.mqv.ogg.ogm.ogv.opus.pss.qt.spx.tmf.tp.trp.ts.ty.vc1.vob.wav.webm.wma.wmv.wtv.";

    for (const char* ptr = supportedformats; NULL != (ptr = strstr(ptr, ext.c_str())); ptr += ext.size())
    {
        if (ptr[ext.size()] == '.')
        {
            return true;
        }
    }
    return false;
}

static inline uint32_t coalesce(uint32_t a, uint32_t b)
{
    return a != 0 ? a : b;
}

static unsigned MediaInfoLibVersion()
{
    std::string s = MediaInfoLib::MediaInfo_Config().Info_Version_Get().To_Local();   // eg. __T("MediaInfoLib - v17.10")
    unsigned version = 0, column = 1;
    for (unsigned i = s.size(); i--; )
    {
        if (isdigit(s[i]))
        {
            version = version + column * (s[i] - '0');
            column *= 10;
        }
        else if (s[i] != '.')
        {
            break;
        }
    }
    return version;
}

static unsigned PrecomputedMediaInfoLibVersion = MediaInfoLibVersion();



bool MediaFileInfo::timeToRetryMediaPropertyExtraction(const std::string& fileattributes, uint32_t fakey[4])
{
    // Check if we should retry video property extraction, due to previous failure with older library
    MediaProperties vp = MediaProperties::decodeMediaPropertiesAttributes(fileattributes, fakey);

    if (vp.shortformat == 255) 
    {
        if (vp.width < PrecomputedMediaInfoLibVersion)
        {
            return true;
        }
        else if (vp.height < MEDIA_INFO_METHODOLOGY_VERSION)
        {
            return true;
        } 
        else if (vp.playtime < downloadedCodecMapsVersion)
        {
            return true;
        }
    }
    return false;
}

void MediaProperties::extractMediaPropertyFileAttributes(const std::string& localFilename)
{
    try
    {
        MediaInfoLib::MediaInfo minfo;

#ifdef _WIN32        
        ZenLib::Ztring filename((wchar_t*)localFilename.data(), localFilename.size() / 2);
#else
        ZenLib::Ztring filename(localFilename.data(), localFilename.size());
#endif
        if (minfo.Open(filename))
        {
            if (!minfo.Count_Get(MediaInfoLib::Stream_General, 0))
            {
                LOG_warn << "no general information found in file " << filename.To_Local();
            }
            if (!minfo.Count_Get(MediaInfoLib::Stream_Video, 0))
            {
                LOG_warn << "no video information found in file " << filename.To_Local();
            }
            if (!minfo.Count_Get(MediaInfoLib::Stream_Audio, 0))
            {
                LOG_warn << "no audio information found in file " << filename.To_Local();
                no_audio = true;
            }

            ZenLib::Ztring gf = minfo.Get(MediaInfoLib::Stream_General, 0, __T("Format"), MediaInfoLib::Info_Text);
            ZenLib::Ztring vw = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("Width"), MediaInfoLib::Info_Text);
            ZenLib::Ztring vh = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("Height"), MediaInfoLib::Info_Text);
            ZenLib::Ztring vd = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("Duration"), MediaInfoLib::Info_Text);
            ZenLib::Ztring vr = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("FrameRate"), MediaInfoLib::Info_Text);
            ZenLib::Ztring vrm = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("FrameRate_Mode"), MediaInfoLib::Info_Text);
            ZenLib::Ztring vci = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("CodecID"), MediaInfoLib::Info_Text);  // todo: Perhaps we should use "Format" here
            ZenLib::Ztring aci = minfo.Get(MediaInfoLib::Stream_Audio, 0, __T("CodecID"), MediaInfoLib::Info_Text);
            ZenLib::Ztring ad = minfo.Get(MediaInfoLib::Stream_Audio, 0, __T("Duration"), MediaInfoLib::Info_Text);

            width = vw.To_int32u();
            height = vh.To_int32u();
            fps = vr.To_int32u();
            playtime = (coalesce(vd.To_int32u(), ad.To_int32u()) + 500) / 1000;  // converting ms to sec
            videocodecName = vci.To_Local();
            audiocodecName = aci.To_Local();
            containerName = gf.To_Local();
            is_VFR = vrm.To_Local() == "VFR"; // variable frame rate - send through as 0 in fps field

#ifdef _DEBUG
            LOG_info << "MediaInfo on " << filename.To_Local() << " " << vw.To_Local() << " " << vh.To_Local() << " " << vd.To_Local() << " " << vr.To_Local() << " |\"" << gf.To_Local() << "\" \"" << vci.To_Local() << "\" \"" << aci.To_Local() << "\"";
#endif
        }
        else
        {
            LOG_warn << "mediainfo could not open the file " << filename.To_Local();
        }
    }
    catch (std::exception& e)
    {
        LOG_err << "exception caught reading meda file attibutes: " << e.what();
    }
    catch (...)
    {
        LOG_err << "unknown excption caught reading media file attributes";
    }
}

std::string MediaProperties::convertMediaPropertyFileAttributes(uint32_t fakey[4], MediaFileInfo& mediaInfo)
{
    containerid = mediaInfo.Lookup(containerName, mediaInfo.mediaCodecs.containers, 0);
    videocodecid = mediaInfo.Lookup(videocodecName, mediaInfo.mediaCodecs.videocodecs, 0);
    audiocodecid = mediaInfo.Lookup(audiocodecName, mediaInfo.mediaCodecs.audiocodecs, 0);

    if ((!videocodecid && !audiocodecid || !containerid) ||
        (videocodecid && (!width || !height || (!fps && !is_VFR) || !playtime || (!audiocodecid && !no_audio))) ||
        (!videocodecid && audiocodecid && (!playtime || width || height)))
    {
        LOG_warn << "mediainfo failed to extract media information for this file";
        shortformat = 255;   // mediaInfo could not interpret this file.  Maybe a later version can.
        width = PrecomputedMediaInfoLibVersion;          // mediaInfoLib version that couldn't do it.  1710 at time of writing (ie oct 2017 tag)
        height = MEDIA_INFO_METHODOLOGY_VERSION;         // updated when we change relevant stuff in the executable
        playtime = mediaInfo.downloadedCodecMapsVersion;           // updated when we add more codec names etc
    }
    else
    {
        // attribute 8 valid, and either shortformat specifies a common combination of (containerid, videocodecid, audiocodecid),
        // or we make an attribute 9 with those values, and set shortformat=0.
        shortformat = mediaInfo.LookupShortFormat(containerid, videocodecid, audiocodecid);
    }

#ifdef _DEBUG
    LOG_info << "MediaInfo converted: " << (int)shortformat << "," << width << "," << height << "," << fps << "," << playtime << "," << videocodecid << "," << audiocodecid << "," << containerid;
#endif

    std::string mediafileattributes = MediaProperties::encodeMediaPropertiesAttributes(*this, fakey);

#ifdef _DEBUG
    // double check decode is the opposite of encode
    std::string simServerAttribs = ":" + mediafileattributes;
    size_t pos = simServerAttribs.find("/");
    if (pos != std::string::npos)
        simServerAttribs.replace(pos, 1, ":");
    MediaProperties decVp = MediaProperties::decodeMediaPropertiesAttributes(simServerAttribs, fakey);
    assert(*this == decVp);
#endif

    return mediafileattributes;
}
#endif

} // namespace

