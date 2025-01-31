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
#include "mega/megaapp.h"

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
        for (size_t i = s.size(); i--; )
        {
            if (is_digit(static_cast<unsigned>(s[i])))
            {
                version += column * static_cast<uint32_t>(s[i] - '0');
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

void MediaFileInfo::requestCodecMappingsOneTime(MegaClient* client, const LocalPath& ifSuitableFilename)
{
    if (!mediaCodecsReceived && !mediaCodecsRequested)
    {
        if (!ifSuitableFilename.empty())
        {
            string ext;
            if (!client->fsaccess->getextension(ifSuitableFilename, ext)
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
    for (size_t i = mediaCodecs.shortformats.size(); i--; )
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
        working = json.enterarray();
        while (working)
        {
            MediaFileInfo::MediaCodecs::shortformatrec rec;
            unsigned id = static_cast<unsigned>(atoi(json.getvalue()));
            assert(id < 256);
            std::string a, b, c;
            working = json.storeobject(&a) && json.storeobject(&b) && json.storeobject(&c);
            if (working)
            {
                rec.shortformatid = byte(id);
                rec.containerid = static_cast<unsigned>(atoi(a.c_str()));
                rec.videocodecid = static_cast<unsigned>(atoi(b.c_str()));
                rec.audiocodecid = static_cast<unsigned>(atoi(c.c_str()));
                vec.push_back(rec);
            }
            json.leavearray();
            working = json.enterarray();
        }
        json.leavearray();
    }
}

void MediaFileInfo::onCodecMappingsReceiptStatic(MegaClient* client, JSON& json, int codecListVersion)
{
    client->mediaFileInfo.onCodecMappingsReceipt(client, json, codecListVersion);
}

void MediaFileInfo::onCodecMappingsReceipt(MegaClient* client, JSON& json, int codecListVersion)
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

        downloadedCodecMapsVersion = static_cast<uint32_t>(codecListVersion);
        assert(downloadedCodecMapsVersion < 10000);
        json.enterarray();
        ReadIdRecords(mediaCodecs.containers, json);
        ReadIdRecords(mediaCodecs.videocodecs, json);
        ReadIdRecords(mediaCodecs.audiocodecs, json);
        ReadShortFormats(mediaCodecs.shortformats, json);
        json.leavearray();
        mediaCodecsReceived = true;

        // update any download transfers we already processed
        for (size_t i = queuedForDownloadTranslation.size(); i--; )
        {
            queuedvp& q = queuedForDownloadTranslation[i];
            sendOrQueueMediaPropertiesFileAttributesForExistingFile(q.vp, q.fakey, client, q.handle.nodeHandle());
        }
        queuedForDownloadTranslation.clear();
    }

    // resume any upload transfers that were waiting for this
    for (std::map<UploadHandle, queuedvp>::iterator i = uploadFileAttributes.begin(); i != uploadFileAttributes.end(); )
    {
        UploadHandle th = i->second.handle.uploadHandle();
        ++i;   // the call below may remove this item from the map

        // indicate that file attribute 8 can be retrieved now, allowing the transfer to complete
        if (auto uploadFAPtr = client->fileAttributesUploading.lookupExisting(th))
        {
            if (auto faPtr = uploadFAPtr->pendingfa.lookupExisting(fatype(fa_media)))
            {
                faPtr->valueIsSet = true;
            }
        }
        client->checkfacompletion(th);
    }

    client->app->mediadetection_ready();
}

unsigned MediaFileInfo::queueMediaPropertiesFileAttributesForUpload(MediaProperties& vp, uint32_t fakey[4], MegaClient* client, UploadHandle uploadHandle, Transfer* transfer)
{
    if (mediaCodecsFailed)
    {
        return 0;  // we can't do it - let the transfer complete anyway
    }

    MediaFileInfo::queuedvp q;
    q.handle = NodeOrUploadHandle(uploadHandle);
    q.vp = vp;
    memcpy(q.fakey, fakey, sizeof(q.fakey));
    uploadFileAttributes[uploadHandle] = q;
    LOG_debug << "Media attribute enqueued for upload";

    // indicate we have this attribute.  If we have the codec mappings, it can be encoded.  If not, hold the transfer until we do have it
    client->fileAttributesUploading.setFileAttributePending(uploadHandle, fatype(fa_media), transfer, mediaCodecsReceived);

    return 1;
}

void MediaFileInfo::sendOrQueueMediaPropertiesFileAttributesForExistingFile(MediaProperties& vp, uint32_t fakey[4], MegaClient* client, NodeHandle fileHandle)
{
    if (mediaCodecsFailed)
    {
        return;  // we can't do it
    }

    if (!mediaCodecsReceived)
    {
        MediaFileInfo::queuedvp q;
        q.handle = NodeOrUploadHandle(fileHandle);
        q.vp = vp;
        memcpy(q.fakey, fakey, sizeof(q.fakey));
        queuedForDownloadTranslation.push_back(q);
        LOG_debug << "Media attribute enqueued for existing file";
    }
    else
    {
        LOG_debug << "Sending media attributes";
        std::string mediafileattributes = vp.convertMediaPropertyFileAttributes(fakey, client->mediaFileInfo);
        client->putFileAttributes(fileHandle.as8byte(), fa_media, mediafileattributes.c_str(), 0);
    }
}

void MediaFileInfo::addUploadMediaFileAttributes(UploadHandle uploadhandle, std::string* s)
{
    std::map<UploadHandle, MediaFileInfo::queuedvp>::iterator i = uploadFileAttributes.find(uploadhandle);
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


void xxteaEncrypt(uint32_t* v, uint32_t vlen, uint32_t key[4], bool endianConv)
{
    if (endianConv)
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

    if (endianConv)
    {
        if (DetectBigEndian())
        {
            EndianConversion32(v, vlen);
        }
        else
        {
            EndianConversion32(key, 4);
        }
    }
}

void xxteaDecrypt(uint32_t* v, uint32_t vlen, uint32_t key[4], bool endianConv)
{
    if (endianConv)
    {
        if (DetectBigEndian())
        {
            EndianConversion32(v, vlen);
        }
        else
        {
            EndianConversion32(key, 4);
        }
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

    if (endianConv)
    {
        if (DetectBigEndian())
        {
            EndianConversion32(v, vlen);
        }
        else
        {
            EndianConversion32(key, 4);
        }
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
    : shortformat(UNKNOWN_FORMAT)
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

MediaProperties::MediaProperties(const std::string& deserialize)
{
    CacheableReader r(deserialize);
    r.unserializebyte(shortformat);
    r.unserializeu32(width);
    r.unserializeu32(height);
    r.unserializeu32(fps);
    r.unserializeu32(playtime);
    r.unserializeu32(containerid);
    r.unserializeu32(videocodecid);
    r.unserializeu32(audiocodecid);
    r.unserializebool(is_VFR);
    r.unserializebool(no_audio);
}

std::string MediaProperties::serialize()
{
    std::string s;
    CacheableWriter r(s);
    r.serializebyte(shortformat);
    r.serializeu32(width);
    r.serializeu32(height);
    r.serializeu32(fps);
    r.serializeu32(playtime);
    r.serializeu32(containerid);
    r.serializeu32(videocodecid);
    r.serializeu32(audiocodecid);
    r.serializebool(is_VFR);
    r.serializebool(no_audio);
    r.serializeexpansionflags();
    return s;
}

bool MediaProperties::isPopulated()
{
    return shortformat != UNKNOWN_FORMAT;
}

bool MediaProperties::isIdentified()
{
    return isPopulated() && shortformat != NOT_IDENTIFIED_FORMAT;
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
    v[6] = byte(vp.playtime >> 10);
    v[5] = byte((vp.playtime >> 2) & 255);
    v[4] = byte(((vp.playtime & 3) << 6) + (vp.fps >> 2));
    v[3] = byte(((vp.fps & 3) << 6) + ((vp.height >> 9) & 63));
    v[2] = byte((vp.height >> 1) & 255);
    v[1] = byte(((vp.width >> 8) & 127) + ((vp.height & 1) << 7));
    v[0] = byte(vp.width & 255);

    std::string result = formatfileattr(fa_media, v, sizeof v, fakey);

    if (!vp.shortformat) // exotic combination of container/codecids
    {
        LOG_debug << "The file requires extended media attributes";

        memset(v, 0, sizeof v);
        v[3] = (vp.audiocodecid >> 4) & 255;
        v[2] = static_cast<byte>(((vp.videocodecid >> 8) & 15) + ((vp.audiocodecid & 15) << 4));
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
        Base64::atob(attrs.substr(static_cast<size_t>(pos + 3), 11), binary);
        assert(binary.size() == 8);
        byte v[8];
        memcpy(v, binary.data(), std::min<size_t>(sizeof v, binary.size()));
        xxteaDecrypt((uint32_t*)v, sizeof(v)/4, fakey);

        r.width = static_cast<uint32_t>((v[0] >> 1) + ((v[1] & 127) << 7));
        if (v[0] & 1) r.width = (r.width << 3) + 16384;

        r.height = static_cast<uint32_t>(v[2] + ((v[3] & 63) << 8));
        if (v[1] & 128) r.height = (r.height << 3) + 16384;

        r.fps = static_cast<uint32_t>((v[3] >> 7) + ((v[4] & 63) << 1));
        if (v[3] & 64) r.fps = (r.fps << 3) + 128;

        r.playtime = static_cast<uint32_t>((v[4] >> 7) + (v[5] << 1) + (v[6] << 9));
        if (v[4] & 64) r.playtime = r.playtime * 60 + 131100;

        r.shortformat = v[7];
        if (!r.shortformat)
        {
            ppo = Node::hasfileattribute(&attrs, fa_mediaext);
            pos = ppo - 1;
            if (ppo && pos + 3 + 11 <= (int)attrs.size())
            {
                Base64::atob(attrs.substr(static_cast<size_t>(pos + 3), 11), binary);
                assert(binary.size() == 8);
                memcpy(v, binary.data(), std::min<size_t>(sizeof v, binary.size()));
                xxteaDecrypt((uint32_t*)v, sizeof(v) / 4, fakey);

                r.containerid = v[0];
                r.videocodecid = static_cast<uint32_t>(v[1] + ((v[2] & 15) << 8));
                r.audiocodecid = static_cast<uint32_t>((v[2] >> 4) + (v[3] << 4));
            }
        }
    }

    return r;
}

#ifdef USE_MEDIAINFO

const char* MediaProperties::supportedformatsMediaInfoAudio()
{
    // list compiled from https://github.com/MediaArea/MediaInfoLib/blob/v19.09/Source/Resource/Text/DataBase/Format.csv
    static constexpr char audioformats[] =
        ".aa3.aac.aacp.aaf.ac3.act.adts.aes3.aif.aifc.aiff.als.amr.ape.at3.at9.atrac.atrac3.atrac9.au.caf"
        ".dd+.dts.dtshd.eac3.ec3.fla.flac.kar.la.m1a.m2a.mac.mid.midi.mlp.mp1.mp2.mp3.mpa.mpa1.mpa2.mtv"
        ".oga.ogg.oma.omg.opus.mpc.mp+.qcp.ra.rka.s3m.shn.spdif.spx.tak.thd.vqf.w64.wav.wma.wv.wvc.xm.";

    static_assert(sizeof(audioformats) > 1 &&
                  audioformats[0] == '.' &&
                  audioformats[sizeof(audioformats) - 2] == '.',
                  "Supported audio formats need to start and end with '.'");

    return audioformats;
}

bool MediaProperties::isMediaFilenameExtAudio(const std::string& ext)
{
    for (const char* ptr = supportedformatsMediaInfoAudio();
         (ptr = strstr(ptr, ext.c_str())) != nullptr;
         ptr += ext.size())
    {
        if (ptr[ext.size()] == '.')
        {
            return true;
        }
    }
    return false;
}

const char* MediaProperties::supportedformatsMediaInfo()
{
    static constexpr char nonaudioformats[] =
        ".264.265.3g2.3ga.3gp.3gpa.3gpp.3gpp2.apl.avc.avi.dde.divx.evo.f4a.f4b.f4v.gvi.h261.h263.h264.h265.hevc"
        ".isma.ismt.ismv.ivf.jpm.k3g.m1v.m2p.m2s.m2t.m2v.m4a.m4b.m4p.m4s.m4t.m4v.mk3d.mka.mks.mkv.mov.mp1v.mp2v"
        ".mp4.mp4v.mpeg.mpg.mpgv.mpv.mqv.ogm.ogv.qt.sls.tmf.trp.ts.ty.vc1.vob.vr.webm.wmv.";

    static_assert(sizeof(nonaudioformats) > 1 &&
                  nonaudioformats[0] == '.' &&
                  nonaudioformats[sizeof(nonaudioformats) - 2] == '.',
                  "Supported formats need to start and end with '.'");

    static const std::string allFormats = string(nonaudioformats) + supportedformatsMediaInfoAudio(); // this is thread safe in C++11

    return allFormats.c_str();
}

bool MediaProperties::isMediaFilenameExt(const std::string& ext)
{
    for (const char* ptr = MediaProperties::supportedformatsMediaInfo(); NULL != (ptr = strstr(ptr, ext.c_str())); ptr += ext.size())
    {
        if (ptr[ext.size()] == '.')
        {
            return true;
        }
    }
    return false;
}

template<class T>
StringPair MediaProperties::getCoverFromId3v2(const T& file)
{
    MediaInfoLib::MediaInfo mi;
    mi.Option(__T("Cover_Data"), __T("base64")); // set this _before_ opening the file

    ZenLib::Ztring zFile(file.c_str()); // make this work with both narrow and wide std strings
    if (!mi.Open(zFile))
    {
        LOG_err << "MediaInfo: could not open local file to retrieve Cover: " << zFile.To_UTF8();
        return std::make_pair(std::string(), std::string());
    }

    // MIME (type/subtype) of the cover image.
    // According to id3v2 specs, it is "always an ISO-8859-1 text string".
    // Supported values are one of {"image/jpeg", "image/png"}.
    // However, allow "image/jpg" variant too because it occured for flac (MediaInfo bug?).
    const MediaInfoLib::String& coverMime = mi.Get(MediaInfoLib::Stream_General, 0, __T("Cover_Mime"));
    std::string syntheticExt;
    if (coverMime == __T("image/jpeg") || coverMime == __T("image/jpg"))
    {
        syntheticExt = "jpg";
    }
    else if (coverMime == __T("image/png"))
    {
        syntheticExt = "png";
    }
    else
    {
        if (!coverMime.empty())
        {
            LOG_warn << "MediaInfo: Cover_Mime contained garbage, ignored Cover for file " << zFile.To_UTF8();
        }

        return std::make_pair(std::string(), std::string());
    }

    // Cover data: binary data, base64 encoded.
    ZenLib::Ztring coverData = mi.Get(MediaInfoLib::Stream_General, 0, __T("Cover_Data"));
    std::string data = coverData.To_UTF8();
    if (data.empty())
    {
        return std::make_pair(std::string(), std::string());
    }
    data = mega::Base64::atob(data);

    return std::make_pair(data, syntheticExt);
}

// forward-declare this so the compiler will generate it (same conditional compilation as LocalPath::localpath)
#if defined(_WIN32)
template
StringPair MediaProperties::getCoverFromId3v2(const std::wstring&);
#else
template
StringPair MediaProperties::getCoverFromId3v2(const std::string&);
#endif

static inline uint32_t coalesce(uint32_t a, uint32_t b)
{
    return a != 0 ? a : b;
}

bool MediaFileInfo::timeToRetryMediaPropertyExtraction(const std::string& fileattributes, uint32_t fakey[4])
{
    // Check if we should retry video property extraction, due to previous failure with older library
    MediaProperties vp = MediaProperties::decodeMediaPropertiesAttributes(fileattributes, fakey);
    if (vp.isIdentified())
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

bool mediaInfoOpenFileWithLimits(MediaInfoLib::MediaInfo& mi, LocalPath& filename, FileAccess* fa, unsigned maxBytesToRead, unsigned maxSeconds)
{
    if (!fa->fopen(filename, true, false, FSLogging::logOnError))
    {
        LOG_err << "could not open local file for mediainfo";
        return false;
    }

    m_off_t filesize = fa->size;
    size_t totalBytesRead = 0;
    mi.Open_Buffer_Init(static_cast<ZenLib::int64u>(filesize), 0);
    m_off_t readpos = 0;
    m_time_t startTime = 0;

    bool hasVideo = false;
    bool vidDuration = false;
    for (;;)
    {
        byte buf[30 * 1024];

        unsigned n = unsigned(std::min<m_off_t>(filesize - readpos, sizeof(buf)));
        if (n == 0)
        {
            break;
        }

        if (totalBytesRead > maxBytesToRead || (startTime != 0 && ((m_time() - startTime) > maxSeconds)))
        {
            if (hasVideo && vidDuration)
            {
                break;
            }

            LOG_warn << "could not extract mediainfo data within reasonable limits";
            mi.Open_Buffer_Finalize();
            fa->closef();
            return false;
        }

        if (!fa->frawread(buf, n, readpos, true, FSLogging::logOnError))
        {
            LOG_err << "could not read local file";
            mi.Open_Buffer_Finalize();
            fa->closef();
            return false;
        }
        readpos += n;
        if (startTime == 0)
        {
            startTime = m_time();
        }

        totalBytesRead += n;
        size_t bitfield = mi.Open_Buffer_Continue((byte*)buf, n);
        // flag bitmask --> 1:accepted, 2:filled, 4:updated, 8:finalised
        bool accepted = bitfield & 1;
        bool filled = bitfield & 2;
        bool finalised = bitfield & 8;
        if (filled || finalised)
        {
            break;
        }

        if (accepted)
        {
            hasVideo = 0 < mi.Count_Get(MediaInfoLib::Stream_Video, 0);
            bool hasAudio = 0 < mi.Count_Get(MediaInfoLib::Stream_Audio, 0);

            vidDuration = !mi.Get(MediaInfoLib::Stream_Video, 0, __T("Duration"), MediaInfoLib::Info_Text).empty();
            bool audDuration = !mi.Get(MediaInfoLib::Stream_Audio, 0, __T("Duration"), MediaInfoLib::Info_Text).empty();

            if (hasVideo && hasAudio && vidDuration && audDuration)
            {
                break;
            }
        }

        m_off_t requestPos = static_cast<m_off_t>(mi.Open_Buffer_Continue_GoTo_Get());
        if (requestPos != (m_off_t)-1)
        {
            readpos = requestPos;
            mi.Open_Buffer_Init(static_cast<ZenLib::int64u>(filesize),
                                static_cast<ZenLib::int64u>(readpos));
        }
    }

    mi.Open_Buffer_Finalize();
    fa->closef();
    return true;
}

void MediaProperties::extractMediaPropertyFileAttributes(LocalPath& localFilename, FileSystemAccess* fsa)
{
    if (auto tmpfa = fsa->newfileaccess())
    {
        try
        {
            MediaInfoLib::MediaInfo minfo;

            if (mediaInfoOpenFileWithLimits(minfo, localFilename, tmpfa.get(), 10485760, 3))  // we can read more off local disk
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
                ZenLib::Ztring vfr = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("FrameRate"), MediaInfoLib::Info_Text);
                ZenLib::Ztring vrm = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("FrameRate_Mode"), MediaInfoLib::Info_Text);
                ZenLib::Ztring vci = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("CodecID"), MediaInfoLib::Info_Text);
                ZenLib::Ztring vcf = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("Format"), MediaInfoLib::Info_Text);
                ZenLib::Ztring vr = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("Rotation"), MediaInfoLib::Info_Text);
                ZenLib::Ztring aci = minfo.Get(MediaInfoLib::Stream_Audio, 0, __T("CodecID"), MediaInfoLib::Info_Text);
                ZenLib::Ztring acf = minfo.Get(MediaInfoLib::Stream_Audio, 0, __T("Format"), MediaInfoLib::Info_Text);
                ZenLib::Ztring ad = minfo.Get(MediaInfoLib::Stream_Audio, 0, __T("Duration"), MediaInfoLib::Info_Text);

                if (vr.To_int32u() == 90 || vr.To_int32u() == 270)
                {
                    width = vh.To_int32u();
                    height = vw.To_int32u();
                }
                else
                {
                    width = vw.To_int32u();
                    height = vh.To_int32u();
                }

                fps = vfr.To_int32u();
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

                if (SimpleLogger::getLogLevel() >= logDebug)
                {
                    LOG_debug << "MediaInfo on " << localFilename << " | " << vw.To_Local() << " " << vh.To_Local() << " " << vd.To_Local() << " " << vr.To_Local() << " |\"" << gci.To_Local() << "\",\"" << gf.To_Local() << "\",\"" << vci.To_Local() << "\",\"" << vcf.To_Local() << "\",\"" << aci.To_Local() << "\",\"" << acf.To_Local() << "\"";
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
        shortformat = NOT_IDENTIFIED_FORMAT;                // mediaInfo could not fully identify this file.  Maybe a later version can.
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
