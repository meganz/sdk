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
#include "ZenLib/Ztring.h"
#endif

namespace mega {

#define MEDIA_INFO_BUILD 1    // Increment this anytime we change the way we use mediainfo, eq query new or different fields etc.  Needs to be coordinated with the way the webclient works also.

#ifdef USE_MEDIAINFO

uint32_t GetMediaInfoVersion()
{
    static uint32_t version = 0;

    if (version == 0)
    {
        std::string s = ZenLib::Ztring(MediaInfoLib::MediaInfo::Option_Static(__T("Info_Version")).c_str()).To_Local();   // eg. __T("MediaInfoLib - v17.10")
        unsigned column = 1;
        for (unsigned i = s.size(); i--; )
        {
            if (isdigit(s[i]))
            {
                version += column * (s[i] - '0');
                column *= 10;
            }
            else if (s[i] == 'v')
            {
                break;
            }
        }
        assert(version != 0);
    }

    return version;
}

MediaFileInfo::MediaFileInfo()
    : mediaCodecsRequested(false)
    , mediaCodecsReceived(false)
    , mediaCodecsFailed(false)
    , downloadedCodecMapsVersion(0)
{
    LOG_debug << "MediaInfo version: " << GetMediaInfoVersion();
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

        LOG_debug << "Requesting code mappings";
        client->reqs.add(new CommandMediaCodecs(client, &MediaFileInfo::onCodecMappingsReceiptStatic));
        mediaCodecsRequested = true;
    }
}

static void ReadShortFormats(std::vector<MediaFileInfo::MediaCodecs::shortformatrec>& vec, JSON& json);

unsigned MediaFileInfo::Lookup(const std::string& name, std::map<std::string, unsigned>& data, unsigned notfoundvalue)
{
    size_t seppos = name.find(" / ");
    if (seppos != std::string::npos)
    {
        // CodecId can contain a list in order of preference, separated by " / "
        size_t pos = 0;
        while (seppos != std::string::npos)
        {
            unsigned result = MediaFileInfo::Lookup(name.substr(pos, seppos), data, notfoundvalue);
            if (result != notfoundvalue)
                return result;
            pos = seppos + 3;
            seppos = name.find(" / ", pos);
        }
        return MediaFileInfo::Lookup(name.substr(pos), data, notfoundvalue);
    }

    std::map<std::string, unsigned>::iterator i = data.find(name);
    return i == data.end() ? notfoundvalue : i->second;
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

void MediaFileInfo::ReadIdRecords(std::map<std::string, unsigned>& data, JSON& json)
{
    if (json.enterarray())
    {
        while (json.enterarray())
        {
            assert(json.isnumeric());
            m_off_t id = json.getint();
            std::string name;
            if (json.storeobject(&name) && id > 0)
            {
                data[name] = (unsigned) id;
            }
            json.leavearray();
        }
        json.leavearray();
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

void MediaFileInfo::onCodecMappingsReceiptStatic(MegaClient* client, int codecListVersion)
{
    client->mediaFileInfo.onCodecMappingsReceipt(client, codecListVersion);
}

void MediaFileInfo::onCodecMappingsReceipt(MegaClient* client, int codecListVersion)
{
    if (codecListVersion < 0)
    {
        LOG_err << "Error getting media codec mappings";

        mediaCodecsFailed = true;
        queuedForDownloadTranslation.clear();
    }
    else
    {
        LOG_debug << "Media codec mappings correctly received";

        downloadedCodecMapsVersion = codecListVersion;
        assert(downloadedCodecMapsVersion < 10000);
        client->json.enterarray();
        ReadIdRecords(mediaCodecs.containers, client->json);
        ReadIdRecords(mediaCodecs.videocodecs, client->json);
        ReadIdRecords(mediaCodecs.audiocodecs, client->json);
        ReadShortFormats(mediaCodecs.shortformats, client->json);
        client->json.leavearray();
        mediaCodecsReceived = true;

        // update any download transfers we already processed
        for (unsigned i = queuedForDownloadTranslation.size(); i--; )
        {
            queuedvp& q = queuedForDownloadTranslation[i];
            sendOrQueueMediaPropertiesFileAttributesForExistingFile(q.vp, q.fakey, client, q.handle);
        }
        queuedForDownloadTranslation.clear();
    }

    // resume any upload transfers that were waiting for this
    for (std::map<handle, queuedvp>::iterator i = uploadFileAttributes.begin(); i != uploadFileAttributes.end(); )
    {
        handle th = i->second.handle;
        ++i;   // the call below may remove this item from the map

        // indicate that file attribute 8 can be retrieved now, allowing the transfer to complete
        client->pendingfa[pair<handle, fatype>(th, fa_media)] = pair<handle, int>(0, 0);
        client->checkfacompletion(th);
    }
}

unsigned MediaFileInfo::queueMediaPropertiesFileAttributesForUpload(MediaProperties& vp, uint32_t fakey[4], MegaClient* client, handle uploadHandle)
{
    if (mediaCodecsFailed)
    {
        return 0;  // we can't do it - let the transfer complete anyway
    }

    MediaFileInfo::queuedvp q;
    q.handle = uploadHandle;
    q.vp = vp;
    memcpy(q.fakey, fakey, sizeof(q.fakey));
    uploadFileAttributes[uploadHandle] = q;
    LOG_debug << "Media attribute enqueued for upload";

    if (mediaCodecsReceived)
    {
        // indicate we have this attribute ready to go. Otherwise the transfer will be put on hold till we can
        client->pendingfa[pair<handle, fatype>(uploadHandle, fa_media)] = pair<handle, int>(0, 0);
    }
    return 1;
}

void MediaFileInfo::sendOrQueueMediaPropertiesFileAttributesForExistingFile(MediaProperties& vp, uint32_t fakey[4], MegaClient* client, handle fileHandle)
{
    if (mediaCodecsFailed)
    {
        return;  // we can't do it
    }

    if (!mediaCodecsReceived)
    {
        MediaFileInfo::queuedvp q;
        q.handle = fileHandle;
        q.vp = vp;
        memcpy(q.fakey, fakey, sizeof(q.fakey));
        queuedForDownloadTranslation.push_back(q);
        LOG_debug << "Media attribute enqueued for existing file";
    }
    else
    {
        LOG_debug << "Sending media attributes";
        std::string mediafileattributes = vp.convertMediaPropertyFileAttributes(fakey, client->mediaFileInfo);
        client->reqs.add(new CommandAttachFA(fileHandle, fa_media, mediafileattributes.c_str(), 0));
    }
}

void MediaFileInfo::addUploadMediaFileAttributes(handle& uploadhandle, std::string* s)
{
    std::map<handle, MediaFileInfo::queuedvp>::iterator i = uploadFileAttributes.find(uploadhandle);
    if (i != uploadFileAttributes.end())
    {
        if (!mediaCodecsFailed)
        {
            if (!s->empty())
            {
                *s += "/";
            }
            *s += i->second.vp.convertMediaPropertyFileAttributes(i->second.fakey, *this);
            LOG_debug << "Media attributes added to putnodes";
        }
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
    }
    else
    {
        // match webclient
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
    }
    else
    {
        EndianConversion32(key, 4);
    }
}

void xxteaDecrypt(uint32_t* v, uint32_t vlen, uint32_t key[4])
{
    if (DetectBigEndian())
    {
        EndianConversion32(v, vlen);
    }
    else
    {
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
    }
    else
    {
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
        LOG_debug << "The file requires extended media attributes";

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
        ".264.265.3g2.3ga.3gp.3gpa.3gpp.3gpp2.aac.aacp.ac3.act.adts.aif.aifc.aiff.als.apl.at3.avc"
        ".avi.dd+.dde.divx.dts.dtshd.eac3.ec3.evo.f4a.f4b.f4v.flac.gvi.h261.h263.h264.h265.hevc.isma"
        ".ismt.ismv.ivf.jpm.k3g.m1a.m1v.m2a.m2p.m2s.m2t.m2v.m4a.m4b.m4p.m4s.m4t.m4v.m4v.mac.mkv.mk3d"
        ".mka.mks.mlp.mov.mp1.mp1v.mp2.mp2v.mp3.mp4.mp4v.mpa1.mpa2.mpeg.mpg.mpgv.mpv.mqv.ogg.ogm.ogv"
        ".omg.opus.qt.sls.spx.thd.tmf.trp.ts.ty.vc1.vob.vr.w64.wav.webm.wma.wmv.";

    assert(ext.size() >= 2 && ext[0] == '.');
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

bool MediaFileInfo::timeToRetryMediaPropertyExtraction(const std::string& fileattributes, uint32_t fakey[4])
{
    // Check if we should retry video property extraction, due to previous failure with older library
    MediaProperties vp = MediaProperties::decodeMediaPropertiesAttributes(fileattributes, fakey);
    if (vp.shortformat == 255) 
    {
        if (vp.fps < MEDIA_INFO_BUILD)
        {
            LOG_debug << "Media extraction retry needed with a newer build. Old: "
                      << vp.fps << "  New: " << MEDIA_INFO_BUILD;
            return true;
        } 
        if (vp.width < GetMediaInfoVersion())
        {
            LOG_debug << "Media extraction retry needed with a newer MediaInfo version. Old: "
                      << vp.width << "  New: " << GetMediaInfoVersion();
            return true;
        }
        if (vp.playtime < downloadedCodecMapsVersion)
        {
            LOG_debug << "Media extraction retry needed with newer code mappings. Old: "
                      << vp.playtime << "  New: " << downloadedCodecMapsVersion;
            return true;
        }
    }
    return false;
}

bool mediaInfoOpenFileWithLimits(MediaInfoLib::MediaInfo& mi, std::string filename, FileAccess* fa, unsigned maxBytesToRead, unsigned maxSeconds)
{
    if (!fa->fopen(&filename, true, false))
    {
        LOG_err << "could not open local file for mediainfo";
        return false;
    }

    m_off_t filesize = fa->size; 
    size_t totalBytesRead = 0, jumps = 0;
    size_t opened = mi.Open_Buffer_Init(filesize, 0);
    m_off_t readpos = 0;
    time_t startTime = 0;

    for (;;)
    {
        byte buf[30 * 1024];

        unsigned n = unsigned(std::min<m_off_t>(filesize - readpos, sizeof(buf)));
        if (n == 0)
        {
            break;
        }

        if (totalBytesRead > maxBytesToRead || (startTime != 0 && ((time(NULL) - startTime) > maxSeconds)))
        {
            LOG_warn << "could not extract mediainfo data within reasonable limits";
            mi.Open_Buffer_Finalize();
            fa->closef();
            return false;
        }

        if (!fa->frawread(buf, n, readpos))
        {
            LOG_err << "could not read local file";
            mi.Open_Buffer_Finalize();
            fa->closef();
            return false;
        }
        readpos += n;
        if (startTime == 0)
        {
            startTime = time(NULL);
        }

        totalBytesRead += n;
        size_t bitfield = mi.Open_Buffer_Continue((byte*)buf, n);
        bool accepted = bitfield & 1;
        bool filled = bitfield & 2;
        bool updated = bitfield & 4;
        bool finalised = bitfield & 8;
        if (filled || finalised)
        {
            break;
        }

        if (accepted)
        {
            bool hasGeneral = 0 < mi.Count_Get(MediaInfoLib::Stream_General, 0);
            bool hasVideo = 0 < mi.Count_Get(MediaInfoLib::Stream_Video, 0);
            bool hasAudio = 0 < mi.Count_Get(MediaInfoLib::Stream_Audio, 0);

            bool genDuration = !mi.Get(MediaInfoLib::Stream_General, 0, __T("Duration"), MediaInfoLib::Info_Text).empty();
            bool vidDuration = !mi.Get(MediaInfoLib::Stream_Video, 0, __T("Duration"), MediaInfoLib::Info_Text).empty();
            bool audDuration = !mi.Get(MediaInfoLib::Stream_Audio, 0, __T("Duration"), MediaInfoLib::Info_Text).empty();

            if (hasVideo && hasAudio && vidDuration && audDuration)
            {
                break;
            }
        }

        m_off_t requestPos = mi.Open_Buffer_Continue_GoTo_Get();
        if (requestPos != (m_off_t)-1)
        {
            readpos = requestPos;
            opened = mi.Open_Buffer_Init(filesize, readpos);
            jumps += 1;
        }
    }

    mi.Open_Buffer_Finalize();
    fa->closef();
    return true;
}

void MediaProperties::extractMediaPropertyFileAttributes(const std::string& localFilename, FileSystemAccess* fsa)
{
    FileAccess* tmpfa = fsa->newfileaccess();
    if (tmpfa)
    {
        try
        {
            MediaInfoLib::MediaInfo minfo;

            if (mediaInfoOpenFileWithLimits(minfo, localFilename, tmpfa, 10485760, 3))  // we can read more off local disk
            {
                if (!minfo.Count_Get(MediaInfoLib::Stream_General, 0))
                {
                    LOG_warn << "mediainfo: no general information found in file";
                }
                if (!minfo.Count_Get(MediaInfoLib::Stream_Video, 0))
                {
                    LOG_warn << "mediainfo: no video information found in file";
                }
                if (!minfo.Count_Get(MediaInfoLib::Stream_Audio, 0))
                {
                    LOG_warn << "mediainfo: no audio information found in file";
                    no_audio = true;
                }

                ZenLib::Ztring gci = minfo.Get(MediaInfoLib::Stream_General, 0, __T("CodecID"), MediaInfoLib::Info_Text);
                ZenLib::Ztring gf = minfo.Get(MediaInfoLib::Stream_General, 0, __T("Format"), MediaInfoLib::Info_Text);
                ZenLib::Ztring gd = minfo.Get(MediaInfoLib::Stream_General, 0, __T("Duration"), MediaInfoLib::Info_Text);
                ZenLib::Ztring vw = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("Width"), MediaInfoLib::Info_Text);
                ZenLib::Ztring vh = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("Height"), MediaInfoLib::Info_Text);
                ZenLib::Ztring vd = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("Duration"), MediaInfoLib::Info_Text);
                ZenLib::Ztring vr = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("FrameRate"), MediaInfoLib::Info_Text);
                ZenLib::Ztring vrm = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("FrameRate_Mode"), MediaInfoLib::Info_Text);
                ZenLib::Ztring vci = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("CodecID"), MediaInfoLib::Info_Text);
                ZenLib::Ztring vcf = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("Format"), MediaInfoLib::Info_Text);
                ZenLib::Ztring aci = minfo.Get(MediaInfoLib::Stream_Audio, 0, __T("CodecID"), MediaInfoLib::Info_Text);
                ZenLib::Ztring acf = minfo.Get(MediaInfoLib::Stream_Audio, 0, __T("Format"), MediaInfoLib::Info_Text);
                ZenLib::Ztring ad = minfo.Get(MediaInfoLib::Stream_Audio, 0, __T("Duration"), MediaInfoLib::Info_Text);

                width = vw.To_int32u();
                height = vh.To_int32u();
                fps = vr.To_int32u();
                playtime = (coalesce(gd.To_int32u(), coalesce(vd.To_int32u(), ad.To_int32u()))) / 1000;
                videocodecNames = vci.To_Local();
                videocodecFormat = vcf.To_Local();
                audiocodecNames = aci.To_Local();
                audiocodecFormat = acf.To_Local();
                containerName = gci.To_Local(); 
                containerFormat = gf.To_Local();
                is_VFR = vrm.To_Local() == "VFR"; // variable frame rate - send through as 0 in fps field
                if (!fps)
                {
                    ZenLib::Ztring vrn = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("FrameRate_Num"), MediaInfoLib::Info_Text);
                    ZenLib::Ztring vrd = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("FrameRate_Den"), MediaInfoLib::Info_Text);
                    uint32_t num = vrn.To_int32u();
                    uint32_t den = vrd.To_int32u();
                    if (num > 0 && den > 0)
                    {
                        fps = (num + den / 2) / den;
                    }
                }
                if (!fps)
                {
                    ZenLib::Ztring vro = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("FrameRate_Original"), MediaInfoLib::Info_Text);
                    fps = vro.To_int32u();
                }

                if (SimpleLogger::logCurrentLevel >= logDebug)
                {
                    string path, local = localFilename;
                    fsa->local2path(&local, &path);
                    LOG_debug << "MediaInfo on " << path << " | " << vw.To_Local() << " " << vh.To_Local() << " " << vd.To_Local() << " " << vr.To_Local() << " |\"" << gci.To_Local() << "\",\"" << gf.To_Local() << "\",\"" << vci.To_Local() << "\",\"" << vcf.To_Local() << "\",\"" << aci.To_Local() << "\",\"" << acf.To_Local() << "\"";
                }
            }
        }
        catch (std::exception& e)
        {
            LOG_err << "exception caught reading media file attibutes: " << e.what();
        }
        catch (...)
        {
            LOG_err << "unknown excption caught reading media file attributes";
        }
        delete tmpfa;
    }
}

std::string MediaProperties::convertMediaPropertyFileAttributes(uint32_t fakey[4], MediaFileInfo& mediaInfo)
{
    containerid = mediaInfo.Lookup(containerName, mediaInfo.mediaCodecs.containers, 0);
    if (!containerid)
    {
        containerid = mediaInfo.Lookup(containerFormat, mediaInfo.mediaCodecs.containers, 0);
    }
    videocodecid = mediaInfo.Lookup(videocodecNames, mediaInfo.mediaCodecs.videocodecs, 0);
    if (!videocodecid)
    {
        videocodecid = mediaInfo.Lookup(videocodecFormat, mediaInfo.mediaCodecs.videocodecs, 0);
    }
    audiocodecid = mediaInfo.Lookup(audiocodecNames, mediaInfo.mediaCodecs.audiocodecs, 0);
    if (!audiocodecid)
    {
        audiocodecid = mediaInfo.Lookup(audiocodecFormat, mediaInfo.mediaCodecs.audiocodecs, 0);
    }

    if (!(containerid && (
            (videocodecid && width && height && /*(fps || is_VFR) &&*/ (audiocodecid || no_audio)) || 
            (audiocodecid && !videocodecid)))) 
    {
        LOG_warn << "mediainfo failed to extract media information for this file";
        shortformat = 255;                                  // mediaInfo could not fully identify this file.  Maybe a later version can.
        fps = MEDIA_INFO_BUILD;                             // updated when we change relevant things in this executable
        width = GetMediaInfoVersion();                      // mediaInfoLib version that couldn't do it.  1710 at time of writing (ie oct 2017 tag)
        height = 0;
        playtime = mediaInfo.downloadedCodecMapsVersion;    // updated when we add more codec names etc
    }
    else
    {
        LOG_debug << "mediainfo processed the file correctly";

        // attribute 8 valid, and either shortformat specifies a common combination of (containerid, videocodecid, audiocodecid),
        // or we make an attribute 9 with those values, and set shortformat=0.
        shortformat = mediaInfo.LookupShortFormat(containerid, videocodecid, audiocodecid);
    }

    LOG_debug << "MediaInfo converted: " << (int)shortformat << "," << width << "," << height << "," << fps << "," << playtime << "," << videocodecid << "," << audiocodecid << "," << containerid;

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
