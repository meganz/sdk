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

static void ReadShortFormats(std::vector<MediaFileInfo::MediaCodecs::shortformatrec>& vec, JSON& json);

unsigned MediaFileInfo::Lookup(const std::string& name, std::map<std::string, MediaFileInfo::MediaCodecs::idrecord>& data, unsigned notfoundvalue)
{
    

    if (mediaCodecs.containers.empty())
    {
        std::string temp =  R"([[1,"3g2a"],[2,"3ge6"],[3,"3ge7"],[4,"3gg6"],[5,"3gp1"],[6,"3gp2"],[7,"3gp3"],[8,"3gp4"],[9,"3gp5"],[10,"3gp6"],[11,"3gp7"],[12,"3gp8"],[13,"3gp9"],[14,"AAC"],[15,"AAF"],[16,"AC-3"],[17,"ADIF"],[18,"ADTS"],[19,"AIFF"],[20,"ALS"],[21,"AMR"],[22,"AMV"],[23,"AU"],[24,"AVC"],[25,"AVI"],[26,"AVS Video"],[27,"Atrac"],[28,"Atrac3"],[29,"BDAV"],[30,"Blu-ray Clip info"],[31,"Blu-ray Index"],[32,"Blu-ray Movie object"],[33,"Blu-ray Playlist"],[34,"CAF"],[35,"CAQV"],[36,"CDDA"],[37,"CDXA"],[38,"DASH MPD"],[39,"DPG"],[40,"DTS"],[41,"DTS-HD"],[42,"DV"],[43,"DVD Video"],[44,"Dirac"],[45,"DivX"],[46,"DolbyE"],[47,"E-AC-3"],[48,"Extended Module"],[49,"FACE"],[50,"FFV1"],[51,"FFV2"],[52,"FLAC"],[53,"FLC"],[54,"FLI"],[55,"FLIC"],[56,"Flash Video"],[57,"G.719"],[58,"G.722"],[59,"G.722.1"],[60,"G.723"],[61,"G.729"],[62,"G.729.1"],[63,"GXF"],[64,"Google Video"],[65,"H.261"],[66,"H.263"],[67,"HDS F4M"],[68,"HEVC"],[69,"HLS"],[70,"ISM"],[71,"IVF"],[72,"Impulse Tracker"],[73,"JP20"],[74,"JPM "],[75,"JPX "],[76,"KDDI"],[77,"LA"],[78,"LXF"],[79,"M4A "],[80,"M4B "],[81,"M4P "],[82,"M4V "],[83,"M4VH"],[84,"M4VP"],[85,"MIDI"],[86,"MJ2S"],[87,"MJP2"],[88,"MP2T"],[89,"MP4T"],[90,"MPEG Audio"],[91,"MPEG Video"],[92,"MPEG-2 TS"],[93,"MPEG-4"],[94,"MPEG-4 TS"],[95,"MPEG-4 Visual"],[96,"MPEG-PS"],[97,"MPEG-TS"],[98,"MQT "],[99,"MSNV"],[100,"MTV"],[101,"MXF"],[102,"Matroska"],[103,"Module"],[104,"Monkey's Audio"],[105,"Musepack SV7"],[106,"Musepack SV8"],[107,"NSV"],[108,"NUT"],[109,"Ogg"],[110,"OpenMG"],[111,"Opus"],[112,"PMP"],[113,"PTX"],[114,"PlayLater Video"],[115,"QCELP"],[116,"QLCM"],[117,"QTCA"],[118,"QTI "],[119,"QuickTime"],[120,"RIFF-MIDI"],[121,"RIFF-MMP"],[122,"RKAU"],[123,"RealMedia"],[124,"SDV "],[125,"SKM"],[126,"SLS"],[127,"Scream Tracker 3"],[128,"ShockWave"],[129,"Shorten"],[130,"Speex"],[131,"TAK"],[132,"Theora"],[133,"TrueHD"],[134,"TwinVQ"],[135,"VC-1"],[136,"VP8"],[137,"Vorbis"],[138,"WTV"],[139,"WavPack"],[140,"Wave"],[141,"Wave64"],[142,"WebM"],[143,"Windows Media"],[144,"YUV"],[145,"YUV4MPEG2"],[146,"avc1"],[147,"f4v "],[148,"iphE"],[149,"isml"],[150,"iso2"],[151,"iso4"],[152,"iso5"],[153,"isom"],[154,"mmp4"],[155,"mp41"],[156,"mp42"],[157,"mp4s"],[158,"mp71"],[159,"mp7b"],[160,"ndas"],[161,"ndsc"],[162,"ndsh"],[163,"ndsm"],[164,"ndsp"],[165,"ndss"],[166,"ndxc"],[167,"ndxh"],[168,"ndxm"],[169,"ndxp"],[170,"ndxs"],[171,"piff"],[172,"qt  "]],)"
                            R"([[1,0],[2,16777216],[3,33554432],[4,33554448],[5,50331648],[6,1978],[7,"2VUY"],[8,"2Vuy"],[9,"2vuy"],[10,"3IV0"],[11,"3IV1"],[12,"3IV2"],[13,"3IVD"],[14,"3IVX"],[15,"3VID"],[16,"8BPS"],[17,"AAS4"],[18,"AASC"],[19,"ABYR"],[20,"ACTL"],[21,"ADV1"],[22,"ADVJ"],[23,"AEIK"],[24,"AEMI"],[25,"AFLC"],[26,"AFLI"],[27,"AHDV"],[28,"AJPG"],[29,"ALPH"],[30,"AMM2"],[31,"AMPG"],[32,"AMR "],[33,"AMV3"],[34,"ANIM"],[35,"AP41"],[36,"AP42"],[37,"ASLC"],[38,"ASV1"],[39,"ASV2"],[40,"ASVX"],[41,"ATM4"],[42,"AUR2"],[43,"AURA"],[44,"AUVX"],[45,"AV1X"],[46,"AV1x"],[47,"AVC"],[48,"AVC1"],[49,"AVD1"],[50,"AVDJ"],[51,"AVDN"],[52,"AVDV"],[53,"AVI1"],[54,"AVI2"],[55,"AVID"],[56,"AVIS"],[57,"AVJI"],[58,"AVMP"],[59,"AVR "],[60,"AVRN"],[61,"AVRn"],[62,"AVS Video"],[63,"AVUI"],[64,"AVUP"],[65,"AVd1"],[66,"AVdh"],[67,"AVdn"],[68,"AVdv"],[69,"AVin"],[70,"AVmp"],[71,"AVrp"],[72,"AYUV"],[73,"AZPR"],[74,"AZRP"],[75,"BGR "],[76,"BHIV"],[77,"BINK"],[78,"BIT"],[79,"BIT "],[80,"BITM"],[81,"BLOX"],[82,"BLZ0"],[83,"BT20"],[84,"BTCV"],[85,"BTVC"],[86,"BW00"],[87,"BW10"],[88,"BXBG"],[89,"BXRG"],[90,"BXY2"],[91,"BXYV"],[92,"CC12"],[93,"CDV5"],[94,"CDVC"],[95,"CDVH"],[96,"CFCC"],[97,"CFHD"],[98,"CGDI"],[99,"CHAM"],[100,"CHQX"],[101,"CJPG"],[102,"CLJR"],[103,"CLLC"],[104,"CLPL"],[105,"CM10"],[106,"CMYK"],[107,"COL0"],[108,"COL1"],[109,"CPLA"],[110,"CRAM"],[111,"CSCD"],[112,"CT10"],[113,"CTRX"],[114,"CUVC"],[115,"CVID"],[116,"CWLT"],[117,"CYUV"],[118,"CYUY"],[119,"D261"],[120,"D263"],[121,"DAVC"],[122,"DC25"],[123,"DCAP"],[124,"DCL1"],[125,"DCT0"],[126,"DFSC"],[127,"DIB "],[128,"DIV1"],[129,"DIV2"],[130,"DIV3"],[131,"DIV4"],[132,"DIV5"],[133,"DIV6"],[134,"DIVX"],[135,"DJPG"],[136,"DM4V"],[137,"DMB1"],[138,"DMB2"],[139,"DMK2"],[140,"DP02"],[141,"DP16"],[142,"DP18"],[143,"DP26"],[144,"DP28"],[145,"DP96"],[146,"DP98"],[147,"DP9L"],[148,"DPS0"],[149,"DPSC"],[150,"DRWX"],[151,"DSVD"],[152,"DTMT"],[153,"DTNT"],[154,"DUCK"],[155,"DV10"],[156,"DV25"],[157,"DV50"],[158,"DVAN"],[159,"DVC "],[160,"DVCP"],[161,"DVCS"],[162,"DVE2"],[163,"DVH1"],[164,"DVIS"],[165,"DVL "],[166,"DVLP"],[167,"DVMA"],[168,"DVNM"],[169,"DVOO"],[170,"DVOR"],[171,"DVPN"],[172,"DVPP"],[173,"DVR "],[174,"DVR1"],[175,"DVRS"],[176,"DVSD"],[177,"DVSL"],[178,"DVTV"],[179,"DVVT"],[180,"DVX1"],[181,"DVX2"],[182,"DVX3"],[183,"DX50"],[184,"DXGM"],[185,"DXT1"],[186,"DXT2"],[187,"DXT3"],[188,"DXT4"],[189,"DXT5"],[190,"DXTC"],[191,"DXTN"],[192,"DXTn"],[193,"Dirac"],[194,"EKQ0"],[195,"ELK0"],[196,"EM2V"],[197,"EQK0"],[198,"ESCP"],[199,"ETV1"],[200,"ETV2"],[201,"ETVC"],[202,"FFDS"],[203,"FFV1"],[204,"FFV2"],[205,"FFVH"],[206,"FLC"],[207,"FLI"],[208,"FLIC"],[209,"FLJP"],[210,"FLV1"],[211,"FLV4"],[212,"FMJP"],[213,"FMP4"],[214,"FPS1"],[215,"FRLE"],[216,"FRWA"],[217,"FRWD"],[218,"FRWT"],[219,"FRWU"],[220,"FVF1"],[221,"FVFW"],[222,"FXT1"],[223,"G2M2"],[224,"G2M3"],[225,"GEPJ"],[226,"GJPG"],[227,"GLCC"],[228,"GLZW"],[229,"GM40"],[230,"GMP4"],[231,"GPEG"],[232,"GPJM"],[233,"GREY"],[234,"GWLT"],[235,"GXVE"],[236,"H.261"],[237,"H.263"],[238,"H260"],[239,"H261"],[240,"H262"],[241,"H263"],[242,"H264"],[243,"H265"],[244,"H266"],[245,"H267"],[246,"H268"],[247,"H269"],[248,"HD10"],[249,"HDX4"],[250,"HEVC"],[251,"HFYU"],[252,"HMCR"],[253,"HMRR"],[254,"Hap1"],[255,"Hap5"],[256,"HapY"],[257,"I420"],[258,"IAN "],[259,"ICLB"],[260,"IDM0"],[261,"IF09"],[262,"IFO9"],[263,"IGOR"],[264,"IJPG"],[265,"ILVC"],[266,"ILVR"],[267,"IMAC"],[268,"IMC1"],[269,"IMC2"],[270,"IMC3"],[271,"IMC4"],[272,"IMG "],[273,"IMJG"],[274,"IPDV"],[275,"IPJ2"],[276,"IR21"],[277,"IRAW"],[278,"ISME"],[279,"IUYV"],[280,"IV30"],[281,"IV31"],[282,"IV32"],[283,"IV33"],[284,"IV34"],[285,"IV35"],[286,"IV36"],[287,"IV37"],[288,"IV38"],[289,"IV39"],[290,"IV40"],[291,"IV41"],[292,"IV42"],[293,"IV43"],[294,"IV44"],[295,"IV45"],[296,"IV46"],[297,"IV47"],[298,"IV48"],[299,"IV49"],[300,"IV50"],[301,"IY41"],[302,"IYU1"],[303,"IYU2"],[304,"IYUV"],[305,"JBYR"],[306,"JFIF"],[307,"JPEG"],[308,"JPG"],[309,"JPGL"],[310,"KMVC"],[311,"L261"],[312,"L263"],[313,"LAGS"],[314,"LBYR"],[315,"LCMW"],[316,"LCW2"],[317,"LEAD"],[318,"LGRY"],[319,"LIA1"],[320,"LJ2K"],[321,"LJPG"],[322,"LMP2"],[323,"LOCO"],[324,"LSCR"],[325,"LSV0"],[326,"LSVC"],[327,"LSVM"],[328,"LSVW"],[329,"LSVX"],[330,"LZO1"],[331,"Ljpg"],[332,"M101"],[333,"M105"],[334,"M261"],[335,"M263"],[336,"M4CC"],[337,"M4S2"],[338,"MC12"],[339,"MC24"],[340,"MCAM"],[341,"MCZM"],[342,"MDVD"],[343,"MDVF"],[344,"MHFY"],[345,"MJ2C"],[346,"MJPA"],[347,"MJPB"],[348,"MJPG"],[349,"MJPX"],[350,"ML20"],[351,"MLCY"],[352,"MMES"],[353,"MMIF"],[354,"MNVD"],[355,"MP2V"],[356,"MP2v"],[357,"MP41"],[358,"MP42"],[359,"MP43"],[360,"MP4S"],[361,"MP4V"],[362,"MPEG"],[363,"MPEG Video"],[364,"MPEG-1V"],[365,"MPEG-2V"],[366,"MPEG-4 Visual"],[367,"MPEG-4V"],[368,"MPG1"],[369,"MPG2"],[370,"MPG3"],[371,"MPG4"],[372,"MPGI"],[373,"MPNG"],[374,"MRCA"],[375,"MRLE"],[376,"MSS1"],[377,"MSS2"],[378,"MSUC"],[379,"MSUD"],[380,"MSV1"],[381,"MSVC"],[382,"MSZH"],[383,"MTGA"],[384,"MTX1"],[385,"MTX2"],[386,"MTX3"],[387,"MTX4"],[388,"MTX5"],[389,"MTX6"],[390,"MTX7"],[391,"MTX8"],[392,"MTX9"],[393,"MV10"],[394,"MV11"],[395,"MV12"],[396,"MV99"],[397,"MVC1"],[398,"MVC2"],[399,"MVC9"],[400,"MVI1"],[401,"MVI2"],[402,"MWV1"],[403,"MYUV"],[404,"NAVI"],[405,"NDIG"],[406,"NHVU"],[407,"NO16"],[408,"NT00"],[409,"NTN1"],[410,"NTN2"],[411,"NUV1"],[412,"NV12"],[413,"NV21"],[414,"NVDS"],[415,"NVHS"],[416,"NVHU"],[417,"NVS0"],[418,"NVS1"],[419,"NVS2"],[420,"NVS3"],[421,"NVS4"],[422,"NVS5"],[423,"NVS6"],[424,"NVS7"],[425,"NVS8"],[426,"NVS9"],[427,"NVT0"],[428,"NVT1"],[429,"NVT2"],[430,"NVT3"],[431,"NVT4"],[432,"NVT5"],[433,"NVT6"],[434,"NVT7"],[435,"NVT8"],[436,"NVT9"],[437,"NY12"],[438,"NYUV"],[439,"ONYX"],[440,"PCLE"],[441,"PDVC"],[442,"PGVV"],[443,"PHMO"],[444,"PIM1"],[445,"PIM2"],[446,"PIMJ"],[447,"PIXL"],[448,"PNG"],[449,"PNG1"],[450,"PNTG"],[451,"PVEZ"],[452,"PVMM"],[453,"PVW2"],[454,"PVWV"],[455,"PXLT"],[456,"PlayLater Video"],[457,"Q1.0"],[458,"Q1.1"],[459,"QDGX"],[460,"QDRW"],[461,"QPEG"],[462,"QPEQ"],[463,"R210"],[464,"R411"],[465,"R420"],[466,"RAVI"],[467,"RAV_"],[468,"RAW"],[469,"RAW "],[470,"RGB"],[471,"RGB "],[472,"RGB1"],[473,"RGB2"],[474,"RGBA"],[475,"RGBO"],[476,"RGBP"],[477,"RGBQ"],[478,"RGBR"],[479,"RGBT"],[480,"RIVA"],[481,"RL4"],[482,"RL8"],[483,"RLE "],[484,"RLE4"],[485,"RLE8"],[486,"RLND"],[487,"RMP4"],[488,"ROQV"],[489,"RT21"],[490,"RTV0"],[491,"RUD0"],[492,"RV10"],[493,"RV13"],[494,"RV20"],[495,"RV30"],[496,"RV40"],[497,"RVX "],[498,"S263"],[499,"S422"],[500,"SAN3"],[501,"SANM"],[502,"SCCD"],[503,"SDCC"],[504,"SEDG"],[505,"SEG4"],[506,"SEGA"],[507,"SFMC"],[508,"SHR0"],[509,"SHR1"],[510,"SHR2"],[511,"SHR3"],[512,"SHR4"],[513,"SHR5"],[514,"SHR6"],[515,"SHR7"],[516,"SIF1"],[517,"SJDS"],[518,"SJPG"],[519,"SL25"],[520,"SL50"],[521,"SLDV"],[522,"SLIF"],[523,"SLMJ"],[524,"SMSC"],[525,"SMSD"],[526,"SMSV"],[527,"SNOW"],[528,"SP40"],[529,"SP44"],[530,"SP53"],[531,"SP54"],[532,"SP55"],[533,"SP56"],[534,"SP57"],[535,"SP58"],[536,"SP61"],[537,"SPIG"],[538,"SPLC"],[539,"SPRK"],[540,"SQZ2"],[541,"STVA"],[542,"STVB"],[543,"STVC"],[544,"STVX"],[545,"STVY"],[546,"SUDS"],[547,"SV10"],[548,"SVQ1"],[549,"SVQ2"],[550,"SVQ3"],[551,"SWC1"],[552,"Shr0"],[553,"Shr1"],[554,"Shr2"],[555,"Shr3"],[556,"Shr4"],[557,"T420"],[558,"TGA "],[559,"THEO"],[560,"TIFF"],[561,"TIM2"],[562,"TLMS"],[563,"TLST"],[564,"TM10"],[565,"TM20"],[566,"TM2A"],[567,"TM2X"],[568,"TMIC"],[569,"TMOT"],[570,"TR20"],[571,"TRLE"],[572,"TSCC"],[573,"TV10"],[574,"TVJP"],[575,"TVMJ"],[576,"TY0N"],[577,"TY2C"],[578,"TY2N"],[579,"Theora"],[580,"U263"],[581,"U<Y "],[582,"U<YA"],[583,"UCOD"],[584,"ULH0"],[585,"ULH2"],[586,"ULRA"],[587,"ULRG"],[588,"ULTI"],[589,"ULY0"],[590,"ULY2"],[591,"UMP4"],[592,"UYNV"],[593,"UYVP"],[594,"UYVU"],[595,"UYVY"],[596,"V210"],[597,"V261"],[598,"V422"],[599,"V655"],[600,"VBLE"],[601,"VC-1"],[602,"VCR1"],[603,"VCR2"],[604,"VCR3"],[605,"VCR4"],[606,"VCR5"],[607,"VCR6"],[608,"VCR7"],[609,"VCR8"],[610,"VCR9"],[611,"VCWV"],[612,"VDCT"],[613,"VDOM"],[614,"VDOW"],[615,"VDST"],[616,"VDTZ"],[617,"VGPX"],[618,"VIDM"],[619,"VIDS"],[620,"VIFP"],[621,"VIV1"],[622,"VIV2"],[623,"VIVO"],[624,"VIXL"],[625,"VJPG"],[626,"VLV1"],[627,"VMNC"],[628,"VP30"],[629,"VP31"],[630,"VP32"],[631,"VP40"],[632,"VP50"],[633,"VP60"],[634,"VP61"],[635,"VP62"],[636,"VP6A"],[637,"VP6F"],[638,"VP70"],[639,"VP71"],[640,"VP72"],[641,"VP8"],[642,"VP80"],[643,"VQC1"],[644,"VQC2"],[645,"VQJP"],[646,"VQS4"],[647,"VR21"],[648,"VSSH"],[649,"VSSV"],[650,"VSSW"],[651,"VTLP"],[652,"VX1K"],[653,"VX2K"],[654,"VXSP"],[655,"VYU9"],[656,"VYUY"],[657,"V_DIRAC"],[658,"V_FFV1"],[659,"V_MPEG1"],[660,"V_MPEG2"],[661,"V_MPEG4/IS0/AP"],[662,"V_MPEG4/IS0/ASP"],[663,"V_MPEG4/IS0/AVC"],[664,"V_MPEG4/IS0/SP"],[665,"V_MPEG4/ISO/AP"],[666,"V_MPEG4/ISO/ASP"],[667,"V_MPEG4/ISO/AVC"],[668,"V_MPEG4/ISO/SP"],[669,"V_MPEG4/MS/V2"],[670,"V_MPEG4/MS/V3"],[671,"V_MPEGH/ISO/HEVC"],[672,"V_PRORES"],[673,"V_REAL/RV10"],[674,"V_REAL/RV20"],[675,"V_REAL/RV30"],[676,"V_REAL/RV40"],[677,"V_THEORA"],[678,"V_UNCOMPRESSED"],[679,"V_VP8"],[680,"V_VP9"],[681,"Vodei"],[682,"WBVC"],[683,"WHAM"],[684,"WINX"],[685,"WJPG"],[686,"WMV1"],[687,"WMV2"],[688,"WMV3"],[689,"WMVA"],[690,"WMVP"],[691,"WNIX"],[692,"WNV1"],[693,"WNVA"],[694,"WRLE"],[695,"WRPR"],[696,"WV1F"],[697,"WVC1"],[698,"WVLT"],[699,"WVP2"],[700,"WZCD"],[701,"WZDC"],[702,"X263"],[703,"X264"],[704,"XJPG"],[705,"XLV0"],[706,"XMPG"],[707,"XVID"],[708,"XVIX"],[709,"XWV0"],[710,"XWV1"],[711,"XWV2"],[712,"XWV3"],[713,"XWV4"],[714,"XWV5"],[715,"XWV6"],[716,"XWV7"],[717,"XWV8"],[718,"XWV9"],[719,"XXAN"],[720,"XYZP"],[721,"Y211"],[722,"Y216"],[723,"Y411"],[724,"Y41B"],[725,"Y41P"],[726,"Y41T"],[727,"Y422"],[728,"Y42B"],[729,"Y42T"],[730,"Y444"],[731,"Y8  "],[732,"Y800"],[733,"YC12"],[734,"YCCK"],[735,"YMPG"],[736,"YU12"],[737,"YU92"],[738,"YUNV"],[739,"YUV"],[740,"YUV2"],[741,"YUV4MPEG2"],[742,"YUV8"],[743,"YUV9"],[744,"YUVP"],[745,"YUY2"],[746,"YUYP"],[747,"YUYV"],[748,"YV12"],[749,"YV16"],[750,"YV92"],[751,"YVU9"],[752,"YVYU"],[753,"ZLIB"],[754,"ZMBV"],[755,"ZPEG"],[756,"ZYGO"],[757,"ac16"],[758,"ac32"],[759,"acBG"],[760,"ai12"],[761,"ai13"],[762,"ai15"],[763,"ai16"],[764,"ai1p"],[765,"ai1q"],[766,"ai22"],[767,"ai23"],[768,"ai25"],[769,"ai26"],[770,"ai2p"],[771,"ai2q"],[772,"ai52"],[773,"ai53"],[774,"ai55"],[775,"ai56"],[776,"ai5p"],[777,"ai5q"],[778,"ap4c"],[779,"ap4h"],[780,"ap4x"],[781,"apch"],[782,"apcn"],[783,"apco"],[784,"apcs"],[785,"avc1"],[786,"avc2"],[787,"avc3"],[788,"avc4"],[789,"avcp"],[790,"avr "],[791,"b16g"],[792,"b32a"],[793,"b48r"],[794,"b64a"],[795,"base"],[796,"blit"],[797,"blnd"],[798,"blur"],[799,"cmyk"],[800,"cvid"],[801,"cyuv"],[802,"divx"],[803,"drac"],[804,"dslv"],[805,"dv25"],[806,"dv50"],[807,"dv5n"],[808,"dv5p"],[809,"dvc "],[810,"dvcp"],[811,"dvh1"],[812,"dvh2"],[813,"dvh3"],[814,"dvh4"],[815,"dvh5"],[816,"dvh6"],[817,"dvhd"],[818,"dvhp"],[819,"dvhq"],[820,"dvpp"],[821,"dvsd"],[822,"dvsl"],[823,"encv"],[824,"gif "],[825,"h261"],[826,"h263"],[827,"h264"],[828,"hcpa"],[829,"hdv1"],[830,"hdv2"],[831,"hdv3"],[832,"hdv4"],[833,"hdv5"],[834,"hdv6"],[835,"hdv7"],[836,"hdv8"],[837,"hdv9"],[838,"hdva"],[839,"hdvb"],[840,"hdvc"],[841,"hdvd"],[842,"hdve"],[843,"hdvf"],[844,"hev1"],[845,"hvc1"],[846,"i263"],[847,"icod"],[848,"j420"],[849,"jpeg"],[850,"kpcd"],[851,"m1v "],[852,"m2v1"],[853,"mJPG"],[854,"mjp2"],[855,"mjpa"],[856,"mjpb"],[857,"mmes"],[858,"mp4v"],[859,"mp4v-20"],[860,"mp4v-20-1"],[861,"mp4v-20-10"],[862,"mp4v-20-100"],[863,"mp4v-20-11"],[864,"mp4v-20-113"],[865,"mp4v-20-114"],[866,"mp4v-20-12"],[867,"mp4v-20-129"],[868,"mp4v-20-130"],[869,"mp4v-20-145"],[870,"mp4v-20-146"],[871,"mp4v-20-147"],[872,"mp4v-20-148"],[873,"mp4v-20-16"],[874,"mp4v-20-161"],[875,"mp4v-20-162"],[876,"mp4v-20-163"],[877,"mp4v-20-17"],[878,"mp4v-20-177"],[879,"mp4v-20-178"],[880,"mp4v-20-179"],[881,"mp4v-20-18"],[882,"mp4v-20-180"],[883,"mp4v-20-193"],[884,"mp4v-20-194"],[885,"mp4v-20-1d"],[886,"mp4v-20-1e"],[887,"mp4v-20-1f"],[888,"mp4v-20-2"],[889,"mp4v-20-209"],[890,"mp4v-20-21"],[891,"mp4v-20-210"],[892,"mp4v-20-211"],[893,"mp4v-20-22"],[894,"mp4v-20-225"],[895,"mp4v-20-226"],[896,"mp4v-20-227"],[897,"mp4v-20-228"],[898,"mp4v-20-229"],[899,"mp4v-20-230"],[900,"mp4v-20-231"],[901,"mp4v-20-232"],[902,"mp4v-20-240"],[903,"mp4v-20-241"],[904,"mp4v-20-242"],[905,"mp4v-20-243"],[906,"mp4v-20-244"],[907,"mp4v-20-245"],[908,"mp4v-20-247"],[909,"mp4v-20-248"],[910,"mp4v-20-249"],[911,"mp4v-20-250"],[912,"mp4v-20-251"],[913,"mp4v-20-252"],[914,"mp4v-20-253"],[915,"mp4v-20-29"],[916,"mp4v-20-3"],[917,"mp4v-20-30"],[918,"mp4v-20-31"],[919,"mp4v-20-32"],[920,"mp4v-20-33"],[921,"mp4v-20-34"],[922,"mp4v-20-4"],[923,"mp4v-20-42"],[924,"mp4v-20-5"],[925,"mp4v-20-50"],[926,"mp4v-20-51"],[927,"mp4v-20-52"],[928,"mp4v-20-6"],[929,"mp4v-20-61"],[930,"mp4v-20-62"],[931,"mp4v-20-63"],[932,"mp4v-20-64"],[933,"mp4v-20-66"],[934,"mp4v-20-71"],[935,"mp4v-20-72"],[936,"mp4v-20-8"],[937,"mp4v-20-81"],[938,"mp4v-20-82"],[939,"mp4v-20-9"],[940,"mp4v-20-91"],[941,"mp4v-20-92"],[942,"mp4v-20-93"],[943,"mp4v-20-94"],[944,"mp4v-20-97"],[945,"mp4v-20-98"],[946,"mp4v-20-99"],[947,"mp4v-20-a1"],[948,"mp4v-20-a2"],[949,"mp4v-20-a3"],[950,"mp4v-20-b1"],[951,"mp4v-20-b2"],[952,"mp4v-20-b3"],[953,"mp4v-20-b4"],[954,"mp4v-20-c1"],[955,"mp4v-20-c2"],[956,"mp4v-20-d1"],[957,"mp4v-20-d2"],[958,"mp4v-20-d3"],[959,"mp4v-20-e1"],[960,"mp4v-20-e2"],[961,"mp4v-20-e3"],[962,"mp4v-20-e4"],[963,"mp4v-20-e5"],[964,"mp4v-20-e6"],[965,"mp4v-20-e7"],[966,"mp4v-20-e8"],[967,"mp4v-20-f0"],[968,"mp4v-20-f1"],[969,"mp4v-20-f2"],[970,"mp4v-20-f3"],[971,"mp4v-20-f4"],[972,"mp4v-20-f5"],[973,"mp4v-20-f7"],[974,"mp4v-20-f8"],[975,"mp4v-20-f9"],[976,"mp4v-20-fa"],[977,"mp4v-20-fb"],[978,"mp4v-20-fc"],[979,"mp4v-20-fd"],[980,"mpeg"],[981,"mpg1"],[982,"mpg2"],[983,"mx3n"],[984,"mx3p"],[985,"mx4n"],[986,"mx4p"],[987,"mx5n"],[988,"mx5p"],[989,"myuv"],[990,"ncpa"],[991,"ovc1"],[992,"png "],[993,"r210"],[994,"raw"],[995,"raw "],[996,"rle "],[997,"rle  "],[998,"rpza"],[999,"s263"],[1000,"s422"],[1001,"smc "],[1002,"smsv"],[1003,"tscc"],[1004,"v210"],[1005,"vc-1"],[1006,"x263"],[1007,"x264"],[1008,"xd50"],[1009,"xd51"],[1010,"xd52"],[1011,"xd53"],[1012,"xd54"],[1013,"xd55"],[1014,"xd56"],[1015,"xd57"],[1016,"xd58"],[1017,"xd59"],[1018,"xd5a"],[1019,"xd5b"],[1020,"xd5c"],[1021,"xd5d"],[1022,"xd5e"],[1023,"xd5f"],[1024,"xdh2"],[1025,"xdhd"],[1026,"xdv0"],[1027,"xdv1"],[1028,"xdv2"],[1029,"xdv3"],[1030,"xdv4"],[1031,"xdv5"],[1032,"xdv6"],[1033,"xdv7"],[1034,"xdv8"],[1035,"xdv9"],[1036,"xdva"],[1037,"xdvb"],[1038,"xdvc"],[1039,"xdvd"],[1040,"xdve"],[1041,"xdvf"],[1042,"yuv2"],[1043,"yuvs"],[1044,"yuvu"],[1045,"yuvx"]],)"
                            R"([[1,".mp3"],[2,0],[3,"05589F81-C356-11CE-BF01-00AA0055595A"],[4,1],[5,10],[6,100],[7,1000],[8,1001],[9,1002],[10,1003],[11,1004],[12,101],[13,102],[14,103],[15,11],[16,1100],[17,1101],[18,1102],[19,1103],[20,1104],[21,111],[22,112],[23,12],[24,120],[25,121],[26,123],[27,125],[28,13],[29,130],[30,131],[31,132],[32,133],[33,134],[34,135],[35,14],[36,140],[37,1400],[38,1401],[39,15],[40,150],[41,1500],[42,151],[43,155],[44,16],[45,160],[46,161],[47,162],[48,163],[49,17],[50,170],[51,171],[52,172],[53,173],[54,174],[55,175],[56,176],[57,177],[58,178],[59,18],[60,180],[61,"181C"],[62,"181E"],[63,19],[64,190],[65,1971],[66,"1A"],[67,"1C03"],[68,"1C07"],[69,"1C0C"],[70,"1F03"],[71,"1FC4"],[72,2],[73,20],[74,200],[75,2000],[76,2001],[77,2002],[78,2003],[79,2004],[80,2005],[81,2006],[82,2007],[83,202],[84,203],[85,2048],[86,21],[87,210],[88,215],[89,216],[90,22],[91,220],[92,23],[93,230],[94,24],[95,240],[96,241],[97,25],[98,250],[99,251],[100,26],[101,260],[102,27],[103,270],[104,271],[105,272],[106,273],[107,28],[108,280],[109,281],[110,285],[111,3],[112,30],[113,300],[114,31],[115,32],[116,33],[117,3313],[118,34],[119,35],[120,350],[121,351],[122,36],[123,37],[124,38],[125,39],[126,"3A"],[127,"3B"],[128,"3C"],[129,"3D"],[130,4],[131,40],[132,400],[133,401],[134,402],[135,41],[136,4143],[137,42],[138,4201],[139,4243],[140,"43AC"],[141,45],[142,450],[143,5],[144,50],[145,500],[146,501],[147,51],[148,"518590A2-A184-11D0-8522-00C04FD9BAF3"],[149,52],[150,53],[151,55],[152,"564C"],[153,"566F"],[154,5756],[155,"58CB7144-23E9-BFAA-A119-FFFA01E4CE62"],[156,59],[157,6],[158,60],[159,61],[160,62],[161,63],[162,64],[163,65],[164,66],[165,67],[166,"674F"],[167,6750],[168,6751],[169,"676F"],[170,6770],[171,6771],[172,680],[173,681],[174,69],[175,7],[176,70],[177,700],[178,71],[179,72],[180,73],[181,74],[182,75],[183,76],[184,77],[185,78],[186,79],[187,"7A"],[188,"7A21"],[189,"7A22"],[190,"7B"],[191,8],[192,80],[193,81],[194,8180],[195,82],[196,83],[197,84],[198,85],[199,86],[200,88],[201,89],[202,"8A"],[203,"8AE"],[204,"8B"],[205,"8C"],[206,9],[207,91],[208,92],[209,93],[210,94],[211,97],[212,98],[213,99],[214,"A"],[215,"A0"],[216,"A1"],[217,"A100"],[218,"A101"],[219,"A102"],[220,"A103"],[221,"A104"],[222,"A105"],[223,"A106"],[224,"A107"],[225,"A109"],[226,"A2"],[227,"A3"],[228,"A4"],[229,"AAC"],[230,"AC-3"],[231,"AC3+"],[232,"AD98D184-AAC3-11D0-A41C-00A0C9223196"],[233,"ADIF"],[234,"ADTS"],[235,"ALS"],[236,"AMR"],[237,"APE"],[238,"AU"],[239,"A_AAC"],[240,"A_AAC/MPEG2/LC"],[241,"A_AAC/MPEG2/LC/SBR"],[242,"A_AAC/MPEG2/MAIN"],[243,"A_AAC/MPEG2/SSR"],[244,"A_AAC/MPEG4/LC"],[245,"A_AAC/MPEG4/LC/PS"],[246,"A_AAC/MPEG4/LC/SBR"],[247,"A_AAC/MPEG4/LC/SBR/PS"],[248,"A_AAC/MPEG4/LTP"],[249,"A_AAC/MPEG4/MAIN"],[250,"A_AAC/MPEG4/MAIN/PS"],[251,"A_AAC/MPEG4/MAIN/SBR"],[252,"A_AAC/MPEG4/MAIN/SBR/PS"],[253,"A_AAC/MPEG4/SSR"],[254,"A_AC3"],[255,"A_AC3/BSID10"],[256,"A_AC3/BSID9"],[257,"A_ALAC"],[258,"A_DTS"],[259,"A_EAC3"],[260,"A_FLAC"],[261,"A_MLP"],[262,"A_MPEG/L1"],[263,"A_MPEG/L2"],[264,"A_MPEG/L3"],[265,"A_OPUS"],[266,"A_PCM/FLOAT/IEEE"],[267,"A_PCM/INT/BIG"],[268,"A_PCM/INT/LIT"],[269,"A_REAL/14_4"],[270,"A_REAL/28_8"],[271,"A_REAL/ATRC"],[272,"A_REAL/COOK"],[273,"A_REAL/RALF"],[274,"A_REAL/SIPR"],[275,"A_TRUEHD"],[276,"A_TTA1"],[277,"A_VORBIS"],[278,"A_WAVPACK4"],[279,"Atrac"],[280,"Atrac3"],[281,"B0"],[282,"C"],[283,"CAF"],[284,"DFAC"],[285,"DTS"],[286,"DTS-HD"],[287,"DolbyE"],[288,"E-AC-3"],[289,"EMWC"],[290,"Extended Module"],[291,"F1AC"],[292,"FF"],[293,"FFFE"],[294,"FFFF"],[295,"FLAC"],[296,"G.719"],[297,"G.722"],[298,"G.722.1"],[299,"G.723"],[300,"G.729"],[301,"G.729.1"],[302,"Impulse Tracker"],[303,"LA"],[304,"MAC3"],[305,"MAC6"],[306,"MIDI"],[307,"MP2A"],[308,"MP4A"],[309,"MPEG Audio"],[310,"MPEG-1A"],[311,"MPEG-1A L1"],[312,"MPEG-1A L2"],[313,"MPEG-1A L3"],[314,"MPEG-2.5A"],[315,"MPEG-2.5A L1"],[316,"MPEG-2.5A L2"],[317,"MPEG-2.5A L3"],[318,"MPEG-2A"],[319,"MPEG-2A L1"],[320,"MPEG-2A L2"],[321,"MPEG-2A L3"],[322,"MPEG-4 AAC LC"],[323,"MPEG-4 AAC LTP"],[324,"MPEG-4 AAC SSR"],[325,"MPEG-4 AAC main"],[326,"Module"],[327,"Monkey's Audio"],[328,"Musepack SV7"],[329,"Musepack SV8"],[330,"NONE"],[331,"OPUS"],[332,"OpenMG"],[333,"Opus"],[334,"QCELP"],[335,"QDM1"],[336,"QDM2"],[337,"QDMC"],[338,"QLCM"],[339,"Qclp"],[340,"RIFF-MIDI"],[341,"RKAU"],[342,"SAMR"],[343,"SLS"],[344,"SWF ADPCM"],[345,"Scream Tracker 3"],[346,"Shorten"],[347,"Speex"],[348,"TAK"],[349,"TrueHD"],[350,"TwinVQ"],[351,"Vorbis"],[352,"WMA2"],[353,"WavPack"],[354,"Wave"],[355,"Wave64"],[356,"aac "],[357,"ac-3"],[358,"alac"],[359,"alaw"],[360,"dtsc"],[361,"dtse"],[362,"dtsh"],[363,"dtsl"],[364,"dvca"],[365,"dvhd"],[366,"dvsd"],[367,"dvsl"],[368,"ec-3"],[369,"enca"],[370,"fl32"],[371,"fl64"],[372,"ima4"],[373,"in24"],[374,"in32"],[375,"lpcm"],[376,"mp4a"],[377,"mp4a-40"],[378,"mp4a-40-1"],[379,"mp4a-40-12"],[380,"mp4a-40-13"],[381,"mp4a-40-14"],[382,"mp4a-40-15"],[383,"mp4a-40-16"],[384,"mp4a-40-17"],[385,"mp4a-40-19"],[386,"mp4a-40-2"],[387,"mp4a-40-20"],[388,"mp4a-40-21"],[389,"mp4a-40-22"],[390,"mp4a-40-23"],[391,"mp4a-40-24"],[392,"mp4a-40-25"],[393,"mp4a-40-26"],[394,"mp4a-40-27"],[395,"mp4a-40-28"],[396,"mp4a-40-29"],[397,"mp4a-40-3"],[398,"mp4a-40-32"],[399,"mp4a-40-33"],[400,"mp4a-40-34"],[401,"mp4a-40-35"],[402,"mp4a-40-36"],[403,"mp4a-40-4"],[404,"mp4a-40-5"],[405,"mp4a-40-6"],[406,"mp4a-40-7"],[407,"mp4a-40-8"],[408,"mp4a-40-9"],[409,"mp4a-66"],[410,"mp4a-67"],[411,"mp4a-68"],[412,"mp4a-69"],[413,"mp4a-6B"],[414,"nmos"],[415,"owma"],[416,"raw "],[417,"sac3"],[418,"samr"],[419,"sawb"],[420,"sevc"],[421,"sowt"],[422,"twos"],[423,"ulaw"],[424,"vdva"]],)"
                            R"([[1,79,0,386],[2,90,0,309],[3,25,707,151],[4,25,183,151],[5,25,134,151],[6,102,671,258],[7,102,671,254],[8,102,667,258],[9,102,667,254],[10,143,686,46],[11,119,859,386],[12,109,579,351],[13,142,680,265],[14,142,679,277],[15,84,785,386],[16,172,785,386],[17,156,785,386],[18,153,785,386]]])";
        JSON j;
        j.pos = temp.data();
        ReadIdRecords(mediaCodecs.containers, j);
        ReadIdRecords(mediaCodecs.videocodecs, j);
        ReadIdRecords(mediaCodecs.audiocodecs, j);
        ReadShortFormats(mediaCodecs.shortformats, j);
        mediaCodecsReceived = true;
    }


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



void MediaFileInfo::ReadIdRecords(std::map<std::string, MediaCodecs::idrecord>& data, JSON& json)
{
    bool working = json.enterarray();
    if (working)
    {
        while (working = json.enterarray())
        {
            MediaFileInfo::MediaCodecs::idrecord rec;
            std::string idString;
            working = json.storeobject(&idString) &&
                      json.storeobject(&rec.mediainfoname);
            json.storeobject(&rec.mediasourcemimetype);
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

void MediaFileInfo::onCodecMappingsReceiptStatic(MegaClient* client)
{
    client->mediaFileInfo.onCodecMappingsReceipt(client);
}

void MediaFileInfo::onCodecMappingsReceipt(MegaClient* client)
{
    downloadedCodecMapsVersion = 0;
    ReadIdRecords(mediaCodecs.containers, client->json);
    ReadIdRecords(mediaCodecs.videocodecs, client->json);
    ReadIdRecords(mediaCodecs.audiocodecs, client->json);
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
            ZenLib::Ztring vci = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("CodecID"), MediaInfoLib::Info_Text);  
            ZenLib::Ztring vcf = minfo.Get(MediaInfoLib::Stream_Video, 0, __T("Format"), MediaInfoLib::Info_Text);  
            ZenLib::Ztring aci = minfo.Get(MediaInfoLib::Stream_Audio, 0, __T("CodecID"), MediaInfoLib::Info_Text);
            ZenLib::Ztring acf = minfo.Get(MediaInfoLib::Stream_Audio, 0, __T("Format"), MediaInfoLib::Info_Text);
            ZenLib::Ztring ad = minfo.Get(MediaInfoLib::Stream_Audio, 0, __T("Duration"), MediaInfoLib::Info_Text);

            width = vw.To_int32u();
            height = vh.To_int32u();
            fps = vr.To_int32u();
            playtime = (coalesce(vd.To_int32u(), ad.To_int32u()) + 500) / 1000;  // converting ms to sec
            videocodecNames = vci.To_Local();
            videocodecFormat = vcf.To_Local();
            audiocodecNames = aci.To_Local();
            audiocodecFormat = acf.To_Local();
            containerName = gf.To_Local();
            is_VFR = vrm.To_Local() == "VFR"; // variable frame rate - send through as 0 in fps field

#ifdef _DEBUG
            LOG_info << "MediaInfo on " << filename.To_Local() << " |" << vw.To_Local() << " " << vh.To_Local() << " " << vd.To_Local() << " " << vr.To_Local() << " |\"" << gf.To_Local() << "\",\"" << vci.To_Local() << "\",\"" << vcf.To_Local() << "\",\"" << aci.To_Local() << "\",\"" << acf.To_Local() << "\"";
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

