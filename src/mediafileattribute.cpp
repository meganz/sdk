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

#ifdef USE_MEDIAINFO
#include "MediaInfo/MediaInfo.h"
#include "MediaInfo/MediaInfo_Config.h"
#include "ZenLib/Ztring.h"
#endif

namespace mega {



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

VideoProperties::VideoProperties()
    : shortformat(254)
    , width(0)
    , height(0)
    , fps(0)
    , playtime(0)
    , container(0)
    , videocodec(0)
    , audiocodec(0) 
{
}


bool VideoProperties::operator==(const VideoProperties& o) const
{ 
    return shortformat == o.shortformat && width == o.width && height == o.height && fps == o.fps && playtime == o.playtime && 
        (shortformat || (container == o.container && videocodec == o.videocodec && audiocodec == o.audiocodec));
}



// shortformat must be 0 if the format is exotic - in that case, container/videocodec/audiocodec must be valid
// if shortformat is > 0, container/videocodec/audiocodec are ignored and no attribute 9 is returned.
// fakey is an 4-uint32 Array with the attribute key (the nonce from the file key)
std::string VideoProperties::encodeVideoPropertiesAttributes(VideoProperties vp, uint32_t fakey[4])
{
    vp.width <<= 1;
    if (vp.width >= 32768) vp.width = ((vp.width - 32768) >> 3) + 1;
    if (vp.width >= 32768) vp.width = 32767;

    vp.height <<= 1;
    if (vp.height >= 32768) vp.height = ((vp.height - 32768) >> 3) + 1;
    if (vp.height >= 32768) vp.height = 32767;

    vp.playtime <<= 1;
    if (vp.playtime >= 262144) vp.playtime = ((vp.playtime - 262200) / 60) + 1;
    if (vp.playtime >= 262144) vp.playtime = 262143;

    if (vp.fps > 255) vp.fps = 255;

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

    std::string result = formatfileattr(8, v, sizeof v, fakey);

    if (!vp.shortformat) 
    {
        memset(v, 0, sizeof v);
        v[3] = (vp.audiocodec >> 4) & 255;
        v[2] = ((vp.videocodec >> 8) & 15) + ((vp.audiocodec & 15) << 4);
        v[1] = vp.videocodec & 255;
        v[0] = byte(vp.container);
        result.append("/");
        result.append(formatfileattr(9, v, sizeof v, fakey));
    }
    return result;
}

VideoProperties VideoProperties::decodeVideoPropertiesAttributes(const std::string& attrs, uint32_t fakey[4])
{
    VideoProperties r;
    size_t pos = attrs.find(":8*");

    if (pos != std::string::npos && pos + 3 + 11 <= attrs.size())
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

        r.fps = (v[3] >> 6) + ((v[4] & 63) << 2);

        r.playtime = (v[4] >> 7) + (v[5] << 1) + (v[6] << 9);
        if (v[4] & 64) r.playtime = r.playtime * 60 + 131100;

        if (!(r.shortformat = v[7]))
        {
            pos = attrs.find(":9*");

            if (pos != std::string::npos && pos + 3 + 11 <= attrs.size())
            {
                Base64::atob(attrs.substr(pos + 3, 11), binary);
                assert(binary.size() == 8);
                memcpy(v, binary.data(), std::min<size_t>(sizeof v, binary.size()));
                xxteaDecrypt((uint32_t*)v, sizeof(v) / 4, fakey);

                r.container = v[0];
                r.videocodec = v[1] + ((v[2] & 15) << 8);
                r.audiocodec = (v[2] >> 4) + (v[3] << 4);
            }
        }
    }

    return r;
}

#ifdef USE_MEDIAINFO

bool VideoProperties::isVideoFilenameExt(const std::string& ext)
{
    // todo: extract this list from mediainfo library
    static const char* supportedformats = ".264.265.3g2.3ga.3gp.3gpa.3gpp.3gpp2.act.aif.aifc.aiff.amr.asf.au.avc.avi.caf.dd+.dif.divx.dv.eac3.ec3.evo.f4a.f4b.f4v.flv.h261.h263.h264.h265.hevc.isma.ismt.ismv.jpm.jpx.k3g.kar.lxf.m1a.m1v.m2a.m2p.m2s.m2t.m2v.m4a.m4b.m4p.m4s.m4t.m4v.m4v.mid.midi.mov.mp1.mp1v.mp2.mp2v.mp3.mp4.mp4v.mpa1.mpa2.mpeg.mpg.mpgv.mpv.mqv.ogg.ogm.opus.pss.qt.spx.tmf.tp.trp.ts.ty.vc1.vob.wav.webm.wma.wmv.wtv.";

    for (const char* ptr = supportedformats; NULL != (ptr = strstr(ptr, ext.c_str())); ptr += ext.size())
    {
        if (ptr[ext.size()] == '.')
        {
            return true;
        }
    }
    return false;
}

static const char* codecsInOrder =  // In the order they appear in Codecs.CSV in MediInfoLib 17.10.  Add additional codecs at the end so indexes stay unchanged
"| BIT| JPG| PNG| RAW| RGB| RL4| RL8|1978|2VUY|3IV0|3IV1|3IV2|3IVD|3IVX|3VID|8BPS|AAS4|AASC|ABYR|ACTL|ADV1|ADVJ|AEIK|AEMI|AFLC|AFLI|AHDV|AJPG|ALPH|AMPG|AMR |ANIM|AP41|AP42|ASLC|ASV1|ASV2|ASVX|ATM4|"
"AUR2|AURA|AUVX|AV1X|AVC1|AVD1|AVDJ|AVDN|AVDV|AVI1|AVI2|AVID|AVIS|AVMP|AVR |AVRn|AVRN|AVUI|AVUP|AYUV|AZPR|AZRP|BGR |BHIV|BINK|BIT |BITM|BLOX|BLZ0|BT20|BTCV|BTVC|BW00|BW10|BXBG|BXRG|BXY2|BXYV|CC12|"
"CDV5|CDVC|CDVH|CFCC|CFHD|CGDI|CHAM|CJPG|CLJR|CLLC|CLPL|CM10|CMYK|COL0|COL1|CPLA|CRAM|CSCD|CT10|CTRX|CUVC|CVID|cvid|CWLT|CYUV|CYUY|D261|D263|DAVC|DC25|DCAP|DCL1|DCT0|DFSC|DIB |DIV1|DIV2|DIV3|DIV4|"
"DIV5|DIV6|DIVX|divx|DJPG|DM4V|DMB1|DMB2|DMK2|DP02|DP16|DP18|DP26|DP28|DP96|DP98|DP9L|DPS0|DPSC|DRWX|DSVD|DTMT|DTNT|DUCK|DV10|DV25|DV50|DVAN|DVC |dvc |DVCP|dvcp|DVCS|DVE2|DVH1|dvhd|dvhd|DVIS|DVL |"
"DVLP|DVMA|DVNM|DVOR|DVPN|DVPP|DVR |DVR1|DVRS|DVSD|dvsd|dvsd|dvsl|dvsl|DVSL|DVX1|DVX2|DVX3|DX50|DXGM|DXSB|DXT1|DXT2|DXT3|DXT4|DXT5|DXTC|DXTn|DXTN|EKQ0|ELK0|EM2V|EMWC|EQK0|ESCP|ETV1|ETV2|ETVC|FFDS|"
"FFV1|FFVH|FLIC|FLJP|FLV1|FLV4|FMJP|FMP4|FPS1|FRLE|FRWA|FRWD|FRWT|FRWU|FVF1|FVFW|FXT1|G2M2|G2M3|GEPJ|GJPG|GLCC|GLZW|GPEG|GPJM|GREY|GWLT|GXVE|H260|H261|H262|H263|H264|h264|H265|H266|H267|H268|H269|"
"HD10|HDX4|HFYU|HMCR|HMRR|i263|I420|IAN |ICLB|IDM0|IF09|IFO9|IGOR|IJPG|ILVC|ILVR|IMAC|IMC1|IMC2|IMC3|IMC4|IMG |IMJG|IPDV|IPJ2|IR21|IRAW|ISME|IUYV|IV30|IV31|IV32|IV33|IV34|IV35|IV36|IV37|IV38|IV39|"
"IV40|IV41|IV42|IV43|IV44|IV45|IV46|IV47|IV48|IV49|IV50|IY41|IYU1|IYU2|IYUV|JBYR|JFIF|JPEG|jpeg|JPG|JPGL|KMVC|kpcd|L261|L263|LAGS|LBYR|LCMW|LCW2|LEAD|LGRY|LIA1|LJ2K|LJPG|Ljpg|LMP2|LOCO|LSCR|LSV0|"
"LSVC|LSVM|LSVW|LSVX|LZO1|M101|M261|M263|M4CC|M4S2|MC12|MC24|MCAM|MCZM|MDVD|MDVF|MHFY|MJ2C|MJPA|MJPB|MJPG|mJPG|MJPX|ML20|MMES|MMIF|MNVD|MP2A|MP2T|MP2V|MP2v|MP41|MP42|MP43|mp4a|MP4A|MP4S|mp4s|MP4T|"
"MP4V|mp4v|MPEG|mpeg|MPG1|MPG2|MPG3|MPG4|MPGI|MPNG|MRCA|MRLE|MSS1|MSS2|MSUC|MSUD|MSV1|MSVC|MSZH|MTGA|MTX1|MTX2|MTX3|MTX4|MTX5|MTX6|MTX7|MTX8|MTX9|MV10|MV11|MV12|MV99|MVC1|MVC2|MVC9|MVI1|MVI2|MWV1|"
"MYUV|NAVI|NDIG|NHVU|NO16|NT00|NTN1|NTN2|NUV1|NV12|NV21|NVDS|NVHS|NVHU|NVS0|NVS1|NVS2|NVS3|NVS4|NVS5|NVS6|NVS7|NVS8|NVS9|NVT0|NVT1|NVT2|NVT3|NVT4|NVT5|NVT6|NVT7|NVT8|NVT9|NY12|NYUV|ONYX|PCLE|PDVC|"
"PGVV|PHMO|PIM1|PIM2|PIMJ|PIXL|PNG|PNG1|PVEZ|PVMM|PVW2|PVWV|PXLT|Q1.0|Q1.1|Qclp|QDGX|QDM1|QDM2|QDRW|QPEG|QPEQ|R210|R411|R420|RAV_|RAVI|RAW |raw |RGB |RGB1|RGBA|RGBO|RGBP|RGBQ|RGBR|RGBT|RIVA|RL4|RL8|"
"RLE |rle  |RLE4|RLE8|RLND|RMP4|ROQV|rpza|RT21|RTV0|RUD0|RV10|rv10|RV13|rv20|RV20|rv30|RV30|rv40|RV40|RVX |S263|S422|s422|SAMR|SAN3|SANM|SCCD|SDCC|SEDG|SEG4|SEGA|SFMC|SHR0|SHR1|SHR2|SHR3|SHR4|SHR5|"
"SHR6|SHR7|SJPG|SL25|SL50|SLDV|SLIF|SLMJ|smc |SMSC|SMSD|SMSV|smsv|SNOW|SP40|SP44|SP53|SP54|SP55|SP56|SP57|SP58|SP61|SPIG|SPLC|SPRK|SQZ2|STVA|STVB|STVC|STVX|STVY|subp|SV10|SVQ1|SVQ2|SVQ3|SWC1|T420|"
"TGA |THEO|TIFF|TIM2|TLMS|TLST|TM10|TM20|TM2A|TM2X|TMIC|TMOT|TR20|TRLE|TSCC|TV10|TVJP|TVMJ|TY0N|TY2C|TY2N|U<Y |U<YA|U263|UCOD|ULH0|ULH2|ULRA|ULRG|ULTI|ULY0|ULY2|UMP4|UYNV|UYVP|UYVU|UYVY|V210|V261|"
"V422|V655|VBLE|VCR1|VCR2|VCR3|VCR4|VCR5|VCR6|VCR7|VCR8|VCR9|VCWV|VDCT|VDOM|VDOW|VDST|VDTZ|VGPX|VIDM|VIDS|VIFP|VIV1|VIV2|VIVO|VIXL|VJPG|VLV1|VMNC|VP30|VP31|VP32|VP40|VP50|VP60|VP61|VP62|VP6A|VP6F|"
"VP70|VP71|VP72|VQC1|VQC2|VQJP|VQS4|VR21|VSSH|VSSV|VSSW|VTLP|VX1K|VX2K|VXSP|VYU9|VYUY|WBVC|WHAM|WINX|WJPG|WMV1|WMV2|WMV3|WMVA|WMVP|WNIX|WNV1|WNVA|WRLE|WRPR|WV1F|WVC1|WVLT|WVP2|WZCD|WZDC|X263|x263|"
"X264|x264|XJPG|XLV0|XMPG|XVID|XVIX|XWV0|XWV1|XWV2|XWV3|XWV4|XWV5|XWV6|XWV7|XWV8|XWV9|XXAN|XYZP|Y211|Y216|Y411|Y41B|Y41P|Y41T|Y422|Y42B|Y42T|Y444|Y8  |Y800|YC12|YCCK|YMPG|YU12|YU92|YUNV|YUV2|YUV8|"
"YUV9|YUVP|YUY2|YUYP|YUYV|YV12|YV16|YV92|YVU9|YVYU|ZLIB|ZMBV|ZPEG|ZYGO|V_UNCOMPRESSED|V_DIRAC|V_MPEG4/ISO/SP|V_MPEG4/ISO/ASP|V_MPEG4/ISO/AP|V_MPEG4/ISO/AVC|V_MPEG4/MS/V2|V_MPEG4/MS/V3|V_MPEG1|"
"V_MPEG2|V_REAL/RV10|V_REAL/RV20|V_REAL/RV30|V_REAL/RV40|V_THEORA|A_MPEG/L1|A_MPEG/L2|A_MPEG/L3|A_PCM/INT/BIG|A_PCM/INT/LIT|A_PCM/FLOAT/IEEE|A_AC3|A_AC3/BSID9|A_AC3/BSID10|A_DTS|A_EAC3|A_FLAC|"
"A_OPUS|A_TTA1|A_VORBIS|A_WAVPACK4|A_REAL/14_4|A_REAL/28_8|A_REAL/COOK|A_REAL/SIPR|A_REAL/RALF|A_REAL/ATRC|A_AAC|A_AAC/MPEG2/MAIN|A_AAC/MPEG2/LC|A_AAC/MPEG2/LC/SBR|A_AAC/MPEG2/SSR|A_AAC/MPEG4/MAIN|"
"A_AAC/MPEG4/MAIN/SBR|A_AAC/MPEG4/MAIN/SBR/PS|A_AAC/MPEG4/MAIN/PS|A_AAC/MPEG4/LC|A_AAC/MPEG4/LC/SBR|A_AAC/MPEG4/LC/SBR/PS|A_AAC/MPEG4/LC/PS|A_AAC/MPEG4/SSR|A_AAC/MPEG4/LTP|14_4|14.4|lpcJ|28_8|28.8|"
"cook|dnet|sipr|rtrc|ralf|whrl|atrc|raac|racp|OPUS|Vorbis|Theora|mp4a|mp4v|avc1|h263|s263|samr|sawb|sevc|tx3g|encv|enca|enct|ima4|raw |twos|sowt|alac|sac3|in24|in32|fl32|fl64|alaw|ulaw|.mp3|"
"MPEG-4 AAC main|MPEG-4 AAC LC|MPEG-4 AAC SSR|MPEG-4 AAC LTP|AVC|MPEG-1V|MPEG-2V|MPEG-4V|MPEG-1A|MPEG-1A L1|MPEG-1A L2|MPEG-1A L3|MPEG-2A|MPEG-2A L1|MPEG-2A L2|MPEG-2A L3|MPEG-2.5A|MPEG-2.5A L1|"
"MPEG-2.5A L2|MPEG-2.5A L3|APE|ALS|Vodei|SWF ADPCM|AC3+|TrueHD|VC-1|LA|0|1|2|3|4|5|6|7|8|9|A|C|10|11|12|13|14|15|16|17|18|19|1A|20|21|22|23|24|25|26|27|28|30|31|32|33|34|35|36|37|38|39|3A|3B|3C|3D|"
"40|41|42|42|42|45|50|51|52|53|55|59|60|61|62|63|64|65|66|67|69|70|71|72|73|74|75|76|77|78|79|7A|7B|80|81|82|83|84|85|86|88|89|8A|8B|8C|91|92|93|94|97|98|99|A0|A1|A2|A3|A4|B0|FF|100|101|102|103|111|"
"112|120|121|123|125|130|131|132|133|134|135|135|140|140|140|150|151|155|160|161|162|163|163|170|171|172|173|174|175|176|177|178|180|190|200|202|203|210|215|216|220|230|240|241|250|251|260|270|271|"
"272|273|280|281|285|300|350|351|400|401|402|450|500|501|680|681|700|8AE|AAC|1000|1001|1002|1003|1004|1100|1101|1102|1103|1104|1400|1401|1500|181C|181E|1971|1C03|1C07|1C0C|1F03|1FC4|2000|2001|2002|"
"2003|2004|2005|2006|2007|3313|4143|4201|4243|43AC|564C|566F|5756|674F|6750|6751|676F|6770|6771|7A21|7A22|A100|A101|A102|A103|A104|A105|A106|A107|A109|DFAC|F1AC|FFFE|FFFF|S_TEXT/UTF8|S_TEXT/SSA|"
"S_TEXT/ASS|S_TEXT/USF|S_KATE|S_DVBSUB|S_IMAGE/BMP|S_VOBSUB|S_HDMV/PGS|S_HDMV/TEXTST|";

static const char* containersInOrder =  // In the order they appear in Format.CSV in MediInfoLib 17.10.  Add additional containers at the end so indexes stay unchanged
"|AAF|AIFF|AMV|AVI|BDAV|Blu-ray Clip info|Blu-ray Index|Blu-ray Movie object|Blu-ray Playlist|CDDA|CDXA|DASH MPD|DV|DivX|DPG|DVD Video|Flash Video|GXF|HDS F4M|HLS|Google Video|ISM|IVF|LXF|"
"Matroska|MPEG-PS|MPEG-TS|MPEG-4|MTV|MXF|NSV|NUT|Ogg|PMP|PTX|QuickTime|RealMedia|RIFF-MMP|ShockWave|SKM|WebM|Windows Media|WTV|AVC|AVS Video|Dirac|FFV1|FFV2|FLC|FLI|FLIC|H.261|H.263|HEVC|"
"MPEG Video|MPEG-4 Visual|Theora|VC-1|YUV4MPEG2|VP8|YUV|AAC|AC-3|ADIF|ADTS|ALS|AMR|Atrac|Atrac3|AU|CAF|DolbyE|DTS|DTS-HD|E-AC-3|Extended Module|FLAC|G.719|G.722|G.722.1|G.723|G.729|G.729.1|"
"Impulse Tracker|LA|MIDI|Module|Monkey's Audio|MPEG Audio|OpenMG|Musepack SV7|Musepack SV8|QCELP|QLCM|RIFF-MIDI|RKAU|Scream Tracker 3|Shorten|SLS|Speex|Opus|TAK|TrueHD|TwinVQ|Vorbis|Wave|"
"Wave64|WavPack|Arri Raw|Bitmap|BPG|DDS|DPX|EXR|DIB|GIF|ICO|JNG|JPEG|JPEG 2000|LZ77|MNG|PCX|PNG|PSD|RIFF Palette|RLE|TIFF|TGA|7-Zip|ACE|ELF|ISO 9660|MZ|RAR|ZIP|Adobe encore DVD|AQTitle|ASS|"
"Captions 32|Captions Inc|CPC Captioning|Cheeta|N19|PDF|SAMI|SCC|SubRip|TTML|SSA|WebVTT|Blender|AutoCAD|PlayLater Video|WTV|";

static unsigned getStringIdListIndex(const std::string& stringId, const char* stringList)
{
    std::string searchstr = "|" + stringId + "|";
    const char* pos = strstr(stringList, searchstr.c_str());
    if (!pos)
    {
        return (unsigned)-1;
    }

    unsigned index = 0;
    for (const char* s = stringList; s != pos; ++s)
    {
        if (*s == '|')
        {
            index += 1;
        }
    }
    return index;
}

unsigned getCodecIndex(const std::string& codecId)
{
    return getStringIdListIndex(codecId, codecsInOrder);
}

unsigned getContainerIndex(const std::string& containerId)
{
    return getStringIdListIndex(containerId, containersInOrder);
}

static inline uint32_t coalesce(uint32_t a, uint32_t b)
{
    return a != 0 ? a : b;
}

#define MEGA_MEDIAINFO_CONFIG_REVISION 1 // increment on lib config change etc

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

static unsigned DaysSince1Jan2000()
{
    time_t rawtime = time(NULL);
    struct tm *ptm = gmtime(&rawtime);
    return (ptm->tm_year - 100) * 365 + ptm->tm_yday;  // date when we tried (days since 1 Jan 2000), to prevent retrying too often
}

// Check if we should retry video property extraction, due to previous failure with older library
bool VideoProperties::timeToRetryVideoPropertyExtraction(const std::string& fileattributes, uint32_t fakey[4])
{
    VideoProperties vp = decodeVideoPropertiesAttributes(fileattributes, fakey);

    if (vp.shortformat == 255) // todo: do we want to try again eventually when shortformat == 1 ( ie. we didn't get the codec info)
    {
        if (vp.width < PrecomputedMediaInfoLibVersion ||   
           (vp.fps & 0x10) && (vp.fps & 0xF) != MEGA_MEDIAINFO_CONFIG_REVISION)
        {
            // we could also try periodically based on the date, however that is probably not necessary yet
            return true;
        }
    }
    return false;
}

std::string VideoProperties::extractVideoPropertyFileAttributes(const std::string& localFilename, uint32_t attributekey[4])
{
    VideoProperties vp;

    try
    {
        MediaInfoLib::MediaInfo minfo;

        if (minfo.Open(ZenLib::Ztring((wchar_t*)localFilename.data(), localFilename.size()/2)))
        {
            if (!minfo.Count_Get(MediaInfoLib::Stream_General, 0))
            {
                LOG_warn << "no general information found in file";
            }
            if (!minfo.Count_Get(MediaInfoLib::Stream_Video, 0))
            {
                LOG_warn << "no video information found in file";
            }
            if (!minfo.Count_Get(MediaInfoLib::Stream_Audio, 0))
            {
                LOG_warn << "no audio information found in file";
            }

            ZenLib::Ztring gf = minfo.Get(MediaInfoLib::Stream_General, 0, __T("Format"), MediaInfoLib::Info_Text);
            ZenLib::Ztring gd = minfo.Get(MediaInfoLib::Stream_General, 0, __T("Duration"), MediaInfoLib::Info_Text);
            ZenLib::Ztring vw = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("Width"), MediaInfoLib::Info_Text);
            ZenLib::Ztring vh = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("Height"), MediaInfoLib::Info_Text);
            ZenLib::Ztring vd = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("Duration"), MediaInfoLib::Info_Text);
            ZenLib::Ztring vr = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("FrameRate"), MediaInfoLib::Info_Text);
            ZenLib::Ztring vci = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("CodecID"), MediaInfoLib::Info_Text);
            ZenLib::Ztring aci = minfo.Get(MediaInfoLib::Stream_Audio, 0, __T("CodecID"), MediaInfoLib::Info_Text);
            ZenLib::Ztring ad = minfo.Get(MediaInfoLib::Stream_Audio, 0, __T("Duration"), MediaInfoLib::Info_Text);

            vp.width = vw.To_int32u();
            vp.height = vh.To_int32u();
            vp.fps = vr.To_int32u();
            vp.playtime = (coalesce(vd.To_int32u(), coalesce(ad.To_int32u(), gd.To_int32u())) + 500) / 1000;  // converting ms to sec
            vp.videocodec = getCodecIndex(vci.To_Local()) & 4095;
            vp.audiocodec = getCodecIndex(aci.To_Local()) & 4095;
            vp.container = getContainerIndex(gf.To_Local()) & 4095;
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

    if (vp.width == 0 || vp.height == 0 || vp.fps == 0 || vp.playtime == 0)
    {
        LOG_warn << "mediainfo failed to extract video information for this file";
        vp = VideoProperties();
        vp.shortformat = 255;   // mediaInfo could not interpret this file.  Maybe a later version can.
        vp.width = PrecomputedMediaInfoLibVersion;              // mediaInfoLib version that couldn't do it.  1710 for now
        vp.height = DaysSince1Jan2000();                        // option to prevent retrying too often
        vp.fps = (1 << 4) | MEGA_MEDIAINFO_CONFIG_REVISION;     // which client (1 = sdk) and which scheme (1 = first scheme, increment on lib config change etc)
    }
    else if (vp.container == 4095 || vp.videocodec == 4095 || vp.audiocodec == 4095)
    {
        LOG_warn << "mediainfo only produced attribute 8 information for this file. " << vp.container << " " << vp.videocodec << " " << vp.audiocodec;
        vp.shortformat = 1; // only attribute 8 valid
    }
    else
    {
        vp.shortformat = 0; // attribute 8 and 9 valid
    }

    std::string mediafileattributes = VideoProperties::encodeVideoPropertiesAttributes(vp, attributekey);

#ifdef _DEBUG
    std::string simServerAttribs = ":" + mediafileattributes;
    size_t pos = simServerAttribs.find("/");
    if (pos != std::string::npos)
        simServerAttribs.replace(pos, 1, ":");
    VideoProperties decVp = VideoProperties::decodeVideoPropertiesAttributes(simServerAttribs, attributekey);
    assert(vp == decVp);
#endif

    return mediafileattributes;
}
#endif

} // namespace

