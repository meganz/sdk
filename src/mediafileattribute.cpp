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

#define MEDIA_INFO_BUILD 1    // Increment this anytime we change the way we use mediainfo, eq query new or different fields etc.  Needs to be coordinated with the way the webclient works also.


#ifdef USE_MEDIAINFO

MediaFileInfo::MediaFileInfo()
    : mediaCodecsRequested(false)
    , mediaCodecsReceived(false)
    , mediaCodecsFailed(false)
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

unsigned MediaFileInfo::Lookup(const std::string& name, std::map<std::string, unsigned>& data, unsigned notfoundvalue)
{
    //if (mediaCodecs.containers.empty())
    //{
    //    std::string latest = R"([[1,"3g2a"],[2,"3ge6"],[3,"3ge7"],[4,"3gg6"],[5,"3gp1"],[6,"3gp2"],[7,"3gp3"],[8,"3gp4"],[9,"3gp5"],[10,"3gp6"],[11,"3gp7"],[12,"3gp8"],[13,"3gp9"],[14,"AAC"],[15,"AAF"],[16,"AC-3"],[17,"ADIF"],[18,"ADTS"],[19,"AIFF"],[20,"ALS"],[21,"AMR"],[22,"AMV"],[23,"AU"],[24,"AVC"],[25,"AVI"],[26,"AVS Video"],[27,"CAF"],[28,"CAQV"],[29,"DTS"],[30,"DTS-HD"],[31,"DV"],[32,"Dirac"],[33,"DivX"],[34,"DolbyE"],[35,"E-AC-3"],[36,"FACE"],[37,"FFV1"],[38,"FFV2"],[39,"FLAC"],[40,"G.719"],[41,"G.722"],[42,"G.722.1"],[43,"G.723"],[44,"G.729"],[45,"G.729.1"],[46,"GXF"],[47,"Google Video"],[48,"H.261"],[49,"H.263"],[50,"HEVC"],[51,"IVF"],[52,"Impulse Tracker"],[53,"JP20"],[54,"JPM "],[55,"JPX "],[56,"KDDI"],[57,"LA"],[58,"LXF"],[59,"M4A "],[60,"M4B "],[61,"M4P "],[62,"M4V "],[63,"M4VH"],[64,"M4VP"],[65,"MJ2S"],[66,"MJP2"],[67,"MLP"],[68,"MP2T"],[69,"MP4T"],[70,"MPEG Audio"],[71,"MPEG Video"],[72,"MPEG-2 TS"],[73,"MPEG-4"],[74,"MPEG-4 TS"],[75,"MPEG-4 Visual"],[76,"MPEG-PS"],[77,"MPEG-TS"],[78,"MQT "],[79,"MSNV"],[80,"MTV"],[81,"Matroska"],[82,"Monkey's Audio"],[83,"Musepack SV7"],[84,"Musepack SV8"],[85,"NSV"],[86,"NUT"],[87,"Ogg"],[88,"OpenMG"],[89,"Opus"],[90,"PlayLater Video"],[91,"QCELP"],[92,"QLCM"],[93,"QTCA"],[94,"QTI "],[95,"QuickTime"],[96,"RIFF-MMP"],[97,"RKAU"],[98,"SDV "],[99,"SLS"],[100,"Shorten"],[101,"Speex"],[102,"TAK"],[103,"Theora"],[104,"TrueHD"],[105,"TwinVQ"],[106,"VC-1"],[107,"VP8"],[108,"Vorbis"],[109,"WTV"],[110,"WavPack"],[111,"Wave"],[112,"Wave64"],[113,"WebM"],[114,"Windows Media"],[115,"XAVC"],[116,"YUV"],[117,"YUV4MPEG2"],[118,"avc1"],[119,"avs2"],[120,"dash"],[121,"dby1"],[122,"f4v "],[123,"hvc1"],[124,"iphE"],[125,"isml"],[126,"iso2"],[127,"iso4"],[128,"iso5"],[129,"isom"],[130,"jpx "],[131,"kddi"],[132,"mmp4"],[133,"mobi"],[134,"mp41"],[135,"mp42"],[136,"mp4s"],[137,"mp71"],[138,"mp7b"],[139,"mqt "],[140,"ndas"],[141,"ndsc"],[142,"ndsh"],[143,"ndsm"],[144,"ndsp"],[145,"ndss"],[146,"ndxc"],[147,"ndxh"],[148,"ndxm"],[149,"ndxp"],[150,"ndxs"],[151,"piff"],[152,"qt  "],[153,"wmf "]],)"
    //        R"([[1,0],[2,"0x00026932"],[3,"0x01000000"],[4,"0x02000000"],[5,"0x02000010"],[6,"0x03000000"],[7,"0x04000000"],[8,"0x04006931"],[9,16],[10,1978],[11,2],[12,27],[13,"2VUY"],[14,"2Vuy"],[15,"2vuy"],[16,36],[17,"3IV0"],[18,"3IV1"],[19,"3IV2"],[20,"3IVD"],[21,"3IVX"],[22,"3VID"],[23,"422P"],[24,"8BPS"],[25,"AAS4"],[26,"AASC"],[27,"ABYR"],[28,"ACDV"],[29,"ACTL"],[30,"ADV1"],[31,"ADVJ"],[32,"AEIK"],[33,"AEMI"],[34,"AFLC"],[35,"AFLI"],[36,"AHDV"],[37,"AJPG"],[38,"ALPH"],[39,"AMM2"],[40,"AMPG"],[41,"AMR "],[42,"AMV3"],[43,"ANIM"],[44,"AP41"],[45,"AP42"],[46,"ASLC"],[47,"ASV1"],[48,"ASV2"],[49,"ASVX"],[50,"ATM4"],[51,"AUR2"],[52,"AURA"],[53,"AUVX"],[54,"AV1X"],[55,"AV1x"],[56,"AVC"],[57,"AVC1"],[58,"AVD1"],[59,"AVDJ"],[60,"AVDN"],[61,"AVDV"],[62,"AVI1"],[63,"AVI2"],[64,"AVID"],[65,"AVIS"],[66,"AVJI"],[67,"AVMP"],[68,"AVR "],[69,"AVRN"],[70,"AVRn"],[71,"AVS Video"],[72,"AVUI"],[73,"AVUP"],[74,"AVd1"],[75,"AVdh"],[76,"AVdn"],[77,"AVdv"],[78,"AVin"],[79,"AVj2"],[80,"AVmp"],[81,"AVrp"],[82,"AYUV"],[83,"AZPR"],[84,"AZRP"],[85,"BGR "],[86,"BHIV"],[87,"BINK"],[88,"BIT"],[89,"BIT "],[90,"BITM"],[91,"BLOX"],[92,"BLZ0"],[93,"BT20"],[94,"BTCV"],[95,"BTVC"],[96,"BW00"],[97,"BW10"],[98,"BXBG"],[99,"BXRG"],[100,"BXY2"],[101,"BXYV"],[102,"CC12"],[103,"CDV5"],[104,"CDVC"],[105,"CDVH"],[106,"CFCC"],[107,"CFHD"],[108,"CGDI"],[109,"CHAM"],[110,"CHQX"],[111,"CJPG"],[112,"CLJR"],[113,"CLLC"],[114,"CLPL"],[115,"CM10"],[116,"CMYK"],[117,"COL0"],[118,"COL1"],[119,"CPLA"],[120,"CRAM"],[121,"CSCD"],[122,"CT10"],[123,"CTRX"],[124,"CUVC"],[125,"CVC1"],[126,"CVID"],[127,"CWLT"],[128,"CYUV"],[129,"CYUY"],[130,"D261"],[131,"D263"],[132,"DAVC"],[133,"DC25"],[134,"DCAP"],[135,"DCL1"],[136,"DCOD"],[137,"DCT0"],[138,"DFSC"],[139,"DIB "],[140,"DIGI"],[141,"DIRC"],[142,"DIV1"],[143,"DIV2"],[144,"DIV3"],[145,"DIV4"],[146,"DIV5"],[147,"DIV6"],[148,"DIVX"],[149,"DJPG"],[150,"DM4V"],[151,"DMB1"],[152,"DMB2"],[153,"DMK2"],[154,"DP02"],[155,"DP16"],[156,"DP18"],[157,"DP26"],[158,"DP28"],[159,"DP96"],[160,"DP98"],[161,"DP9L"],[162,"DPS0"],[163,"DPSC"],[164,"DRWX"],[165,"DSVD"],[166,"DTMT"],[167,"DTNT"],[168,"DUCK"],[169,"DV10"],[170,"DV25"],[171,"DV50"],[172,"DVAN"],[173,"DVC "],[174,"DVCP"],[175,"DVCS"],[176,"DVE2"],[177,"DVH1"],[178,"DVIS"],[179,"DVL "],[180,"DVLP"],[181,"DVMA"],[182,"DVNM"],[183,"DVOO"],[184,"DVOR"],[185,"DVPN"],[186,"DVPP"],[187,"DVR "],[188,"DVR1"],[189,"DVRS"],[190,"DVSD"],[191,"DVSL"],[192,"DVTV"],[193,"DVVT"],[194,"DVX1"],[195,"DVX2"],[196,"DVX3"],[197,"DX50"],[198,"DXD3"],[199,"DXDI"],[200,"DXGM"],[201,"DXT1"],[202,"DXT2"],[203,"DXT3"],[204,"DXT4"],[205,"DXT5"],[206,"DXTC"],[207,"DXTN"],[208,"DXTn"],[209,"Dirac"],[210,"EKQ0"],[211,"ELK0"],[212,"EM2V"],[213,"EM4A"],[214,"EQK0"],[215,"ES07"],[216,"ESCP"],[217,"ETV1"],[218,"ETV2"],[219,"ETVC"],[220,"FFDS"],[221,"FFV1"],[222,"FFV2"],[223,"FFVH"],[224,"FICV"],[225,"FLC"],[226,"FLI"],[227,"FLIC"],[228,"FLJP"],[229,"FLV1"],[230,"FLV4"],[231,"FMJP"],[232,"FMP4"],[233,"FMVC"],[234,"FPS1"],[235,"FRLE"],[236,"FRWA"],[237,"FRWD"],[238,"FRWT"],[239,"FRWU"],[240,"FVF1"],[241,"FVFW"],[242,"FXT1"],[243,"G264"],[244,"G2M2"],[245,"G2M3"],[246,"G2M4"],[247,"GAVC"],[248,"GEOV"],[249,"GEPJ"],[250,"GJPG"],[251,"GLCC"],[252,"GLZW"],[253,"GM40"],[254,"GMP4"],[255,"GPEG"],[256,"GPJM"],[257,"GREY"],[258,"GWLT"],[259,"GXVE"],[260,"H.261"],[261,"H.263"],[262,"H260"],[263,"H261"],[264,"H262"],[265,"H263"],[266,"H264"],[267,"H265"],[268,"H266"],[269,"H267"],[270,"H268"],[271,"H269"],[272,"HD10"],[273,"HDX4"],[274,"HEVC"],[275,"HFYU"],[276,"HHE1"],[277,"HMCR"],[278,"HMRR"],[279,"HV60"],[280,"Hap1"],[281,"Hap5"],[282,"HapY"],[283,"I263"],[284,"I420"],[285,"IAN "],[286,"ICLB"],[287,"IDM0"],[288,"IF09"],[289,"IFO9"],[290,"IGOR"],[291,"IJPG"],[292,"ILVC"],[293,"ILVR"],[294,"IMAC"],[295,"IMC1"],[296,"IMC2"],[297,"IMC3"],[298,"IMC4"],[299,"IMG "],[300,"IMJG"],[301,"IMM4"],[302,"IMM5"],[303,"IPDV"],[304,"IPJ2"],[305,"IR21"],[306,"IRAW"],[307,"ISME"],[308,"IUYV"],[309,"IV30"],[310,"IV31"],[311,"IV32"],[312,"IV33"],[313,"IV34"],[314,"IV35"],[315,"IV36"],[316,"IV37"],[317,"IV38"],[318,"IV39"],[319,"IV40"],[320,"IV41"],[321,"IV42"],[322,"IV43"],[323,"IV44"],[324,"IV45"],[325,"IV46"],[326,"IV47"],[327,"IV48"],[328,"IV49"],[329,"IV50"],[330,"IY41"],[331,"IYU1"],[332,"IYU2"],[333,"IYUV"],[334,"JBYR"],[335,"JFIF"],[336,"JPEG"],[337,"JPG"],[338,"JPGL"],[339,"JRV1"],[340,"KDH4"],[341,"KDM4"],[342,"KGV1"],[343,"KMVC"],[344,"L261"],[345,"L263"],[346,"L264"],[347,"LAGS"],[348,"LBYR"],[349,"LCMW"],[350,"LCW2"],[351,"LEAD"],[352,"LGRY"],[353,"LIA1"],[354,"LJ2K"],[355,"LJPG"],[356,"LM20"],[357,"LMP2"],[358,"LOCO"],[359,"LSCR"],[360,"LSV0"],[361,"LSVC"],[362,"LSVM"],[363,"LSVW"],[364,"LSVX"],[365,"LZO1"],[366,"LZOC"],[367,"LZRW"],[368,"Ljpg"],[369,"M101"],[370,"M102"],[371,"M103"],[372,"M104"],[373,"M105"],[374,"M261"],[375,"M263"],[376,"M4CC"],[377,"M4S2"],[378,"M701"],[379,"M704"],[380,"M705"],[381,"MC12"],[382,"MC24"],[383,"MCAM"],[384,"MCZM"],[385,"MDVD"],[386,"MDVF"],[387,"MFZ0"],[388,"MHFY"],[389,"MJ2C"],[390,"MJLS"],[391,"MJPA"],[392,"MJPB"],[393,"MJPG"],[394,"MJPX"],[395,"ML20"],[396,"MLCY"],[397,"MMES"],[398,"MMIF"],[399,"MNVD"],[400,"MOHD"],[401,"MP2V"],[402,"MP2v"],[403,"MP41"],[404,"MP42"],[405,"MP43"],[406,"MP4S"],[407,"MP4V"],[408,"MPEG"],[409,"MPEG Video"],[410,"MPEG-1V"],[411,"MPEG-2V"],[412,"MPEG-4 Visual"],[413,"MPEG-4V"],[414,"MPG1"],[415,"MPG2"],[416,"MPG3"],[417,"MPG4"],[418,"MPGI"],[419,"MPNG"],[420,"MRCA"],[421,"MRLE"],[422,"MSA1"],[423,"MSC2"],[424,"MSS1"],[425,"MSS2"],[426,"MSUC"],[427,"MSUD"],[428,"MSV1"],[429,"MSVC"],[430,"MSZH"],[431,"MTGA"],[432,"MTX1"],[433,"MTX2"],[434,"MTX3"],[435,"MTX4"],[436,"MTX5"],[437,"MTX6"],[438,"MTX7"],[439,"MTX8"],[440,"MTX9"],[441,"MV10"],[442,"MV11"],[443,"MV12"],[444,"MV30"],[445,"MV43"],[446,"MV99"],[447,"MVC1"],[448,"MVC2"],[449,"MVC9"],[450,"MVDV"],[451,"MVI1"],[452,"MVI2"],[453,"MVLZ"],[454,"MWSC"],[455,"MWV1"],[456,"MYUV"],[457,"Mczm"],[458,"N264"],[459,"NAVI"],[460,"NDIG"],[461,"NHVU"],[462,"NO16"],[463,"NT00"],[464,"NTN1"],[465,"NTN2"],[466,"NUV1"],[467,"NV12"],[468,"NV21"],[469,"NVDS"],[470,"NVHS"],[471,"NVHU"],[472,"NVS0"],[473,"NVS1"],[474,"NVS2"],[475,"NVS3"],[476,"NVS4"],[477,"NVS5"],[478,"NVS6"],[479,"NVS7"],[480,"NVS8"],[481,"NVS9"],[482,"NVT0"],[483,"NVT1"],[484,"NVT2"],[485,"NVT3"],[486,"NVT4"],[487,"NVT5"],[488,"NVT6"],[489,"NVT7"],[490,"NVT8"],[491,"NVT9"],[492,"NY12"],[493,"NYUV"],[494,"ONYX"],[495,"P422"],[496,"PCLE"],[497,"PDVC"],[498,"PGVV"],[499,"PHMO"],[500,"PIM1"],[501,"PIM2"],[502,"PIMJ"],[503,"PIXL"],[504,"PLV1"],[505,"PNG"],[506,"PNG1"],[507,"PNTG"],[508,"PSIV"],[509,"PVEZ"],[510,"PVMM"],[511,"PVW2"],[512,"PVWV"],[513,"PXLT"],[514,"PlayLater Video"],[515,"Q1.0"],[516,"Q1.1"],[517,"QDGX"],[518,"QDRW"],[519,"QMP4"],[520,"QPEG"],[521,"QPEQ"],[522,"R10g"],[523,"R10k"],[524,"R210"],[525,"R411"],[526,"R420"],[527,"RAVI"],[528,"RAV_"],[529,"RAW"],[530,"RAW "],[531,"RAYL"],)"
    //        R"([532,"RGB"],[533,"RGB "],[534,"RGB1"],[535,"RGB2"],[536,"RGBA"],[537,"RGBO"],[538,"RGBP"],[539,"RGBQ"],[540,"RGBR"],[541,"RGBT"],[542,"RIVA"],[543,"RL4"],[544,"RL8"],[545,"RLE "],[546,"RLE4"],[547,"RLE8"],[548,"RLND"],[549,"RMP4"],[550,"ROQV"],[551,"RSCC"],[552,"RT21"],[553,"RTV0"],[554,"RUD0"],[555,"RV10"],[556,"RV13"],[557,"RV20"],[558,"RV30"],[559,"RV40"],[560,"RVX "],[561,"RuSH"],[562,"S263"],[563,"S422"],[564,"SAN3"],[565,"SANM"],[566,"SCCD"],[567,"SCLS"],[568,"SCPR"],[569,"SDCC"],[570,"SEDG"],[571,"SEG4"],[572,"SEGA"],[573,"SFMC"],[574,"SHQ0"],[575,"SHQ1"],[576,"SHQ2"],[577,"SHQ3"],[578,"SHQ4"],[579,"SHQ5"],[580,"SHQ7"],[581,"SHQ9"],[582,"SHR0"],[583,"SHR1"],[584,"SHR2"],[585,"SHR3"],[586,"SHR4"],[587,"SHR5"],[588,"SHR6"],[589,"SHR7"],[590,"SIF1"],[591,"SIRF"],[592,"SJDS"],[593,"SJPG"],[594,"SL25"],[595,"SL50"],[596,"SLDV"],[597,"SLIF"],[598,"SLMJ"],[599,"SMP4"],[600,"SMSC"],[601,"SMSD"],[602,"SMSV"],[603,"SMV2"],[604,"SN40"],[605,"SNOW"],[606,"SP40"],[607,"SP44"],[608,"SP53"],[609,"SP54"],[610,"SP55"],[611,"SP56"],[612,"SP57"],[613,"SP58"],[614,"SP61"],[615,"SPIG"],[616,"SPLC"],[617,"SPRK"],[618,"SPV1"],[619,"SQZ2"],[620,"STVA"],[621,"STVB"],[622,"STVC"],[623,"STVX"],[624,"STVY"],[625,"SUDS"],[626,"SUVF"],[627,"SV10"],[628,"SVQ1"],[629,"SVQ2"],[630,"SVQ3"],[631,"SWC1"],[632,"Shr0"],[633,"Shr1"],[634,"Shr2"],[635,"Shr3"],[636,"Shr4"],[637,"Shr5"],[638,"Shr6"],[639,"T263"],[640,"T420"],[641,"TGA "],[642,"THEO"],[643,"TIFF"],[644,"TIM2"],[645,"TLMS"],[646,"TLST"],[647,"TM10"],[648,"TM20"],[649,"TM2A"],[650,"TM2X"],[651,"TMIC"],[652,"TMOT"],[653,"TR20"],[654,"TRLE"],[655,"TSCC"],[656,"TV10"],[657,"TVJP"],[658,"TVMJ"],[659,"TY0N"],[660,"TY2C"],[661,"TY2N"],[662,"Theora"],[663,"U263"],[664,"U<Y "],[665,"U<YA"],[666,"UCOD"],[667,"ULH0"],[668,"ULH2"],[669,"ULRA"],[670,"ULRG"],[671,"ULTI"],[672,"ULY0"],[673,"ULY2"],[674,"UMP4"],[675,"UQY2"],[676,"UYNV"],[677,"UYNY"],[678,"UYVP"],[679,"UYVU"],[680,"UYVY"],[681,"V210"],[682,"V261"],[683,"V422"],[684,"V655"],[685,"VBLE"],[686,"VC-1"],[687,"VCR1"],[688,"VCR2"],[689,"VCR3"],[690,"VCR4"],[691,"VCR5"],[692,"VCR6"],[693,"VCR7"],[694,"VCR8"],[695,"VCR9"],[696,"VCWV"],[697,"VDCT"],[698,"VDEC"],[699,"VDOM"],[700,"VDOW"],[701,"VDST"],[702,"VDTZ"],[703,"VGMV"],[704,"VGPX"],[705,"VIDM"],[706,"VIDS"],[707,"VIFP"],[708,"VIV1"],[709,"VIV2"],[710,"VIVO"],[711,"VIXL"],[712,"VJPG"],[713,"VLV1"],[714,"VMNC"],[715,"VMnc"],[716,"VP30"],[717,"VP31"],[718,"VP32"],[719,"VP40"],[720,"VP50"],[721,"VP60"],[722,"VP61"],[723,"VP62"],[724,"VP6A"],[725,"VP6F"],[726,"VP70"],[727,"VP71"],[728,"VP72"],[729,"VP8"],[730,"VP80"],[731,"VP90"],[732,"VQC1"],[733,"VQC2"],[734,"VQJP"],[735,"VQS4"],[736,"VR21"],[737,"VSSH"],[738,"VSSV"],[739,"VSSW"],[740,"VTLP"],[741,"VX1K"],[742,"VX2K"],[743,"VXSP"],[744,"VXTR"],[745,"VYU9"],[746,"VYUY"],[747,"V_DIRAC"],[748,"V_FFV1"],[749,"V_MJPEG"],[750,"V_MPEG1"],[751,"V_MPEG2"],[752,"V_MPEG4/IS0/AP"],[753,"V_MPEG4/IS0/ASP"],[754,"V_MPEG4/IS0/AVC"],[755,"V_MPEG4/IS0/SP"],[756,"V_MPEG4/ISO/AP"],[757,"V_MPEG4/ISO/ASP"],[758,"V_MPEG4/ISO/AVC"],[759,"V_MPEG4/ISO/SP"],[760,"V_MPEG4/MS/V2"],[761,"V_MPEG4/MS/V3"],[762,"V_MPEGH/ISO/HEVC"],[763,"V_MS/VFW/FOURCC"],[764,"V_PRORES"],[765,"V_QUICKTIME"],[766,"V_REAL/RV10"],[767,"V_REAL/RV20"],[768,"V_REAL/RV30"],[769,"V_REAL/RV40"],[770,"V_THEORA"],[771,"V_UNCOMPRESSED"],[772,"V_VP8"],[773,"V_VP9"],[774,"Vodei"],[775,"WAWV"],[776,"WBVC"],[777,"WHAM"],[778,"WINX"],[779,"WJPG"],[780,"WMV1"],[781,"WMV2"],[782,"WMV3"],[783,"WMVA"],[784,"WMVP"],[785,"WNIX"],[786,"WNV1"],[787,"WNVA"],[788,"WPY2"],[789,"WRLE"],[790,"WRPR"],[791,"WV1F"],[792,"WVC1"],[793,"WVLT"],[794,"WVP2"],[795,"WZCD"],[796,"WZDC"],[797,"X263"],[798,"X264"],[799,"XJPG"],[800,"XLV0"],[801,"XMPG"],[802,"XVID"],[803,"XVIX"],[804,"XWV0"],[805,"XWV1"],[806,"XWV2"],[807,"XWV3"],[808,"XWV4"],[809,"XWV5"],[810,"XWV6"],[811,"XWV7"],[812,"XWV8"],[813,"XWV9"],[814,"XXAN"],[815,"XYZP"],[816,"Xxan"],[817,"Y211"],[818,"Y216"],[819,"Y411"],[820,"Y41B"],[821,"Y41P"],[822,"Y41T"],[823,"Y422"],[824,"Y42B"],[825,"Y42T"],[826,"Y444"],[827,"Y8  "],[828,"Y800"],[829,"YC12"],[830,"YCCK"],[831,"YLC0"],[832,"YMPG"],[833,"YU12"],[834,"YU92"],[835,"YUNV"],[836,"YUV"],[837,"YUV2"],[838,"YUV4MPEG2"],[839,"YUV8"],[840,"YUV9"],[841,"YUVP"],[842,"YUY2"],[843,"YUYP"],[844,"YUYV"],[845,"YV12"],[846,"YV16"],[847,"YV92"],[848,"YVU9"],[849,"YVYU"],[850,"ZECO"],[851,"ZJPG"],[852,"ZLIB"],[853,"ZMBV"],[854,"ZPEG"],[855,"ZYGO"],[856,"ZyGo"],[857,"a12v"],[858,"ac16"],[859,"ac32"],[860,"acBG"],[861,"ai12"],[862,"ai13"],[863,"ai15"],[864,"ai16"],[865,"ai1p"],[866,"ai1q"],[867,"ai22"],[868,"ai23"],[869,"ai25"],[870,"ai26"],[871,"ai2p"],[872,"ai2q"],[873,"ai52"],[874,"ai53"],[875,"ai55"],[876,"ai56"],[877,"ai5p"],[878,"ai5q"],[879,"ap4c"],[880,"ap4h"],[881,"ap4x"],[882,"apch"],[883,"apcn"],[884,"apco"],[885,"apcs"],[886,"auv2"],[887,"avc1"],[888,"avc2"],[889,"avc3"],[890,"avc4"],[891,"avcp"],[892,"avr "],[893,"avs2"],[894,"azpr"],[895,"b16g"],[896,"b32a"],[897,"b48r"],[898,"b64a"],[899,"base"],[900,"blit"],[901,"blnd"],[902,"blur"],[903,"cmyk"],[904,"cvid"],[905,"cyuv"],[906,"div3"],[907,"divx"],[908,"dmb1"],[909,"drac"],[910,"dslv"],[911,"dtPA"],[912,"dtnt"],[913,"dv25"],[914,"dv50"],[915,"dv5n"],[916,"dv5p"],[917,"dvc "],[918,"dvcp"],[919,"dvh1"],[920,"dvh2"],[921,"dvh3"],[922,"dvh4"],[923,"dvh5"],[924,"dvh6"],[925,"dvhd"],[926,"dvhp"],[927,"dvhq"],[928,"dvpp"],[929,"dvsd"],[930,"dvsl"],[931,"dx50"],[932,"encv"],[933,"fire"],[934,"flic"],[935,"gif "],[936,"gisz"],[937,"h261"],[938,"h263"],[939,"h264"],[940,"hcpa"],[941,"hdv1"],[942,"hdv2"],[943,"hdv3"],[944,"hdv4"],[945,"hdv5"],[946,"hdv6"],[947,"hdv7"],[948,"hdv8"],[949,"hdv9"],[950,"hdva"],[951,"hdvb"],[952,"hdvc"],[953,"hdvd"],[954,"hdve"],[955,"hdvf"],[956,"hev1"],[957,"hvc1"],[958,"i263"],[959,"icod"],[960,"j420"],[961,"jpeg"],[962,"kpcd"],[963,"lsvm"],[964,"lsvx"],[965,"m1v "],[966,"m2v1"],[967,"mJPG"],[968,"mjp2"],[969,"mjpa"],[970,"mjpb"],[971,"mmes"],[972,"mp4v"],[973,"mp4v-20"],[974,"mp4v-20-1"],[975,"mp4v-20-10"],[976,"mp4v-20-100"],[977,"mp4v-20-11"],[978,"mp4v-20-113"],[979,"mp4v-20-114"],[980,"mp4v-20-12"],[981,"mp4v-20-129"],[982,"mp4v-20-130"],[983,"mp4v-20-145"],[984,"mp4v-20-146"],[985,"mp4v-20-147"],[986,"mp4v-20-148"],[987,"mp4v-20-16"],[988,"mp4v-20-161"],[989,"mp4v-20-162"],[990,"mp4v-20-163"],[991,"mp4v-20-17"],[992,"mp4v-20-177"],[993,"mp4v-20-178"],[994,"mp4v-20-179"],[995,"mp4v-20-18"],[996,"mp4v-20-180"],[997,"mp4v-20-193"],[998,"mp4v-20-194"],[999,"mp4v-20-1d"],[1000,"mp4v-20-1e"],[1001,"mp4v-20-1f"],[1002,"mp4v-20-2"],[1003,"mp4v-20-209"],[1004,"mp4v-20-21"],[1005,"mp4v-20-210"],[1006,"mp4v-20-211"],[1007,"mp4v-20-22"],[1008,"mp4v-20-225"],[1009,"mp4v-20-226"],[1010,"mp4v-20-227"],[1011,"mp4v-20-228"],[1012,"mp4v-20-229"],[1013,"mp4v-20-230"],[1014,"mp4v-20-231"],[1015,"mp4v-20-232"],[1016,"mp4v-20-240"],[1017,"mp4v-20-241"],[1018,"mp4v-20-242"],[1019,"mp4v-20-243"],[1020,"mp4v-20-244"],[1021,"mp4v-20-245"],[1022,"mp4v-20-247"],[1023,"mp4v-20-248"],[1024,"mp4v-20-249"],[1025,"mp4v-20-250"],[1026,"mp4v-20-251"],[1027,"mp4v-20-252"],[1028,"mp4v-20-253"],[1029,"mp4v-20-29"],[1030,"mp4v-20-3"],[1031,"mp4v-20-30"],[1032,"mp4v-20-31"],[1033,"mp4v-20-32"],[1034,"mp4v-20-33"],[1035,"mp4v-20-34"],[1036,"mp4v-20-4"],[1037,"mp4v-20-42"],[1038,"mp4v-20-5"],[1039,"mp4v-20-50"],[1040,"mp4v-20-51"],[1041,"mp4v-20-52"],[1042,"mp4v-20-6"],[1043,"mp4v-20-61"],[1044,"mp4v-20-62"],[1045,"mp4v-20-63"],[1046,"mp4v-20-64"],[1047,"mp4v-20-66"],[1048,"mp4v-20-71"],[1049,"mp4v-20-72"],[1050,"mp4v-20-8"],[1051,"mp4v-20-81"],[1052,"mp4v-20-82"],[1053,"mp4v-20-9"],[1054,"mp4v-20-91"],[1055,"mp4v-20-92"],[1056,"mp4v-20-93"],[1057,"mp4v-20-94"],[1058,"mp4v-20-97"],[1059,"mp4v-20-98"],[1060,"mp4v-20-99"],[1061,"mp4v-20-a1"],[1062,"mp4v-20-a2"],[1063,"mp4v-20-a3"],[1064,"mp4v-20-b1"],[1065,"mp4v-20-b2"],[1066,"mp4v-20-b3"],[1067,"mp4v-20-b4"],[1068,"mp4v-20-c1"],[1069,"mp4v-20-c2"],[1070,"mp4v-20-d1"],[1071,"mp4v-20-d2"],[1072,"mp4v-20-d3"],[1073,"mp4v-20-e1"],[1074,"mp4v-20-e2"],[1075,"mp4v-20-e3"],[1076,"mp4v-20-e4"],[1077,"mp4v-20-e5"],[1078,"mp4v-20-e6"],[1079,"mp4v-20-e7"],[1080,"mp4v-20-e8"],[1081,"mp4v-20-f0"],[1082,"mp4v-20-f1"],[1083,"mp4v-20-f2"],[1084,"mp4v-20-f3"],[1085,"mp4v-20-f4"],[1086,"mp4v-20-f5"],[1087,"mp4v-20-f7"],[1088,"mp4v-20-f8"],[1089,"mp4v-20-f9"],[1090,"mp4v-20-fa"],[1091,"mp4v-20-fb"],[1092,"mp4v-20-fc"],[1093,"mp4v-20-fd"],[1094,"mp4v-61"],[1095,"mp4v-6A"],[1096,"mpeg"],[1097,"mpg1"],[1098,"mpg2"],[1099,"mx3n"],[1100,"mx3p"],[1101,"mx4n"],[1102,"mx4p"],[1103,"mx5n"],[1104,"mx5p"],[1105,"myuv"],[1106,"ncpa"],[1107,"ovc1"],[1108,"png "],[1109,"pxlt"],[1110,"qdrw"],[1111,"r10k"],[1112,"r210"],[1113,"raw"],[1114,"raw "],[1115,"rle "],[1116,"rle  "],[1117,"rle1"],[1118,"rpza"],[1119,"s263"],[1120,"s422"],[1121,"smc "],[1122,"smsv"],[1123,"tga "],[1124,"theo"],[1125,"tiff"],[1126,"tsc2"],[1127,"tscc"],[1128,"ty0n"],[1129,"v210"],[1130,"v308"],[1131,"v408"],[1132,"v410"],[1133,"vc-1"],[1134,"vivo"],[1135,"vp09"],[1136,"x263"],[1137,"x264"],[1138,"xd50"],[1139,"xd51"],[1140,"xd52"],[1141,"xd53"],[1142,"xd54"],[1143,"xd55"],[1144,"xd56"],[1145,"xd57"],[1146,"xd58"],[1147,"xd59"],[1148,"xd5a"],[1149,"xd5b"],[1150,"xd5c"],[1151,"xd5d"],[1152,"xd5e"],[1153,"xd5f"],[1154,"xdh2"],[1155,"xdhd"],[1156,"xdv0"],[1157,"xdv1"],[1158,"xdv2"],[1159,"xdv3"],[1160,"xdv4"],[1161,"xdv5"],[1162,"xdv6"],[1163,"xdv7"],[1164,"xdv8"],[1165,"xdv9"],[1166,"xdva"],[1167,"xdvb"],[1168,"xdvc"],[1169,"xdvd"],[1170,"xdve"],[1171,"xdvf"],[1172,"xmpg"],[1173,"xtor"],[1174,"yuv2"],[1175,"yuv4"],[1176,"yuv8"],[1177,"yuvs"],[1178,"yuvu"],[1179,"yuvx"],[1180,"yv12"]],)"
    //        R"([[1,".mp1"],[2,".mp3"],[3,0],[4,"00000001-0000-0010-8000-00AA00389B71"],[5,"05589F81-C356-11CE-BF01-00AA0055595A"],[6,1],[7,10],[8,100],[9,1000],[10,1001],[11,1002],[12,1003],[13,1004],[14,101],[15,102],[16,103],[17,11],[18,1100],[19,1101],[20,1102],[21,1103],[22,1104],[23,111],[24,112],[25,12],[26,120],[27,121],[28,123],[29,125],[30,128],[31,129],[32,13],[33,130],[34,131],[35,132],[36,133],[37,134],[38,135],[39,14],[40,140],[41,1400],[42,1401],[43,15],[44,150],[45,1500],[46,1501],[47,151],[48,155],[49,16],[50,160],[51,161],[52,162],[53,163],[54,17],[55,"17-2"],[56,"17-5"],[57,170],[58,171],[59,172],[60,173],[61,174],[62,175],[63,176],[64,177],[65,178],[66,18],[67,180],[68,"181C"],[69,"181E"],[70,19],[71,190],[72,1971],[73,"1A"],[74,"1C03"],[75,"1C07"],[76,"1C0C"],[77,"1F03"],[78,"1FC4"],[79,2],[80,20],[81,200],[82,2000],[83,2001],[84,2002],[85,2003],[86,2004],[87,2005],[88,2006],[89,2007],[90,202],[91,203],[92,2048],[93,21],[94,210],[95,215],[96,216],[97,22],[98,220],[99,23],[100,230],[101,24],[102,240],[103,241],[104,25],[105,250],[106,251],[107,26],[108,260],[109,27],[110,270],[111,271],[112,272],[113,273],[114,28],[115,280],[116,281],[117,285],[118,"28E"],[119,"28F"],[120,3],[121,30],[122,300],[123,31],[124,32],[125,33],[126,3313],[127,34],[128,35],[129,350],[130,351],[131,36],[132,37],[133,38],[134,39],[135,"3A"],[136,"3B"],[137,"3C"],[138,"3D"],[139,4],[140,40],[141,400],[142,401],[143,402],[144,41],[145,4143],[146,4150],[147,42],[148,4201],[149,4243],[150,"43AC"],[151,45],[152,450],[153,5],[154,50],[155,500],[156,501],[157,51],[158,"518590A2-A184-11D0-8522-00C04FD9BAF3"],[159,52],[160,53],[161,55],[162,"564C"],[163,"566F"],[164,5756],[165,"58CB7144-23E9-BFAA-A119-FFFA01E4CE62"],[166,59],[167,"594A"],[168,6],[169,60],[170,61],[171,62],[172,63],[173,64],[174,65],[175,66],[176,67],[177,"674F"],[178,6750],[179,6751],[180,"676F"],[181,6770],[182,6771],[183,680],[184,681],[185,69],[186,7],[187,70],[188,700],[189,"706D"],[190,71],[191,72],[192,73],[193,74],[194,75],[195,76],[196,77],[197,78],[198,79],[199,"7A"],[200,"7A21"],[201,"7A22"],[202,"7B"],[203,8],[204,80],[205,81],[206,8180],[207,"8180-5"],[208,82],[209,83],[210,84],[211,85],[212,86],[213,88],[214,89],[215,"8A"],[216,"8AE"],[217,"8B"],[218,"8C"],[219,9],[220,91],[221,92],[222,93],[223,94],[224,97],[225,98],[226,99],[227,"A"],[228,"A0"],[229,"A1"],[230,"A100"],[231,"A101"],[232,"A102"],[233,"A103"],[234,"A104"],[235,"A105"],[236,"A106"],[237,"A107"],[238,"A109"],[239,"A2"],[240,"A3"],[241,"A4"],[242,"A7FB87AF-2D02-42FB-A4D4-05CD93843BDD"],[243,"AAC"],[244,"AC-3"],[245,"AC3+"],[246,"AD98D184-AAC3-11D0-A41C-00A0C9223196"],[247,"ADIF"],[248,"ADTS"],[249,"ALS"],[250,"AMR"],[251,"APE"],[252,"AU"],[253,"A_AAC"],[254,"A_AAC-2"],[255,"A_AAC/MPEG2/LC"],[256,"A_AAC/MPEG2/LC/SBR"],[257,"A_AAC/MPEG2/MAIN"],[258,"A_AAC/MPEG2/SSR"],[259,"A_AAC/MPEG4/LC"],[260,"A_AAC/MPEG4/LC/PS"],[261,"A_AAC/MPEG4/LC/SBR"],[262,"A_AAC/MPEG4/LC/SBR/PS"],[263,"A_AAC/MPEG4/LTP"],[264,"A_AAC/MPEG4/MAIN"],[265,"A_AAC/MPEG4/MAIN/PS"],[266,"A_AAC/MPEG4/MAIN/SBR"],[267,"A_AAC/MPEG4/MAIN/SBR/PS"],[268,"A_AAC/MPEG4/SSR"],[269,"A_AC3"],[270,"A_AC3/BSID10"],[271,"A_AC3/BSID9"],[272,"A_ALAC"],[273,"A_DTS"],[274,"A_EAC3"],[275,"A_FLAC"],[276,"A_MLP"],[277,"A_MPEG/L1"],[278,"A_MPEG/L2"],[279,"A_MPEG/L3"],[280,"A_OPUS"],[281,"A_PCM/FLOAT/IEEE"],[282,"A_PCM/INT/BIG"],[283,"A_PCM/INT/LIT"],[284,"A_QUICKTIME/QDM2"],[285,"A_REAL/14_4"],[286,"A_REAL/28_8"],[287,"A_REAL/ATRC"],[288,"A_REAL/COOK"],[289,"A_REAL/RALF"],[290,"A_REAL/SIPR"],[291,"A_TRUEHD"],[292,"A_TTA1"],[293,"A_VORBIS"],[294,"A_WAVPACK4"],[295,"Atrac"],[296,"Atrac3"],[297,"B0"],[298,"C"],[299,"C5"],[300,"CAF"],[301,"DFAC"],[302,"DTS"],[303,"DTS-HD"],[304,"DWVW"],[305,"DolbyE"],[306,"E-AC-3"],[307,"E708"],[308,"EMWC"],[309,"Extended Module"],[310,"F1AC"],[311,"FF"],[312,"FF-2"],[313,"FFFE"],[314,"FFFF"],[315,"FL32"],[316,"FL64"],[317,"FLAC"],[318,"G.719"],[319,"G.722"],[320,"G.722.1"],[321,"G.723"],[322,"G.729"],[323,"G.729.1"],[324,"G726"],[325,"GSM "],[326,"Impulse Tracker"],[327,"LA"],[328,"MAC3"],[329,"MAC6"],[330,"MIDI"],[331,"MLP"],[332,"MP2A"],[333,"MP4A"],[334,"MPEG Audio"],[335,"MPEG-1A"],[336,"MPEG-1A L1"],[337,"MPEG-1A L2"],[338,"MPEG-1A L3"],[339,"MPEG-2.5A"],[340,"MPEG-2.5A L1"],[341,"MPEG-2.5A L2"],[342,"MPEG-2.5A L3"],[343,"MPEG-2A"],[344,"MPEG-2A L1"],[345,"MPEG-2A L2"],[346,"MPEG-2A L3"],[347,"MPEG-4 AAC LC"],[348,"MPEG-4 AAC LTP"],[349,"MPEG-4 AAC SSR"],[350,"MPEG-4 AAC main"],[351,"Module"],[352,"Monkey's Audio"],[353,"Musepack SV7"],[354,"Musepack SV8"],[355,"NONE"],[356,"OPUS"],[357,"OggV"],[358,"OpenMG"],[359,"Opus"],[360,"PCM"],[361,"QCELP"],[362,"QDM1"],[363,"QDM2"],[364,"QDMC"],[365,"QLCM"],[366,"Qclp"],[367,"RIFF-MIDI"],[368,"RKAU"],[369,"SAMR"],[370,"SLS"],[371,"SPXN"],[372,"SWF ADPCM"],[373,"Scream Tracker 3"],[374,"Shorten"],[375,"Speex"],[376,"TAK"],[377,"TrueHD"],[378,"TwinVQ"],[379,"Vorbis"],[380,"WMA2"],[381,"WavPack"],[382,"Wave"],[383,"Wave64"],[384,"aac "],[385,"ac-3"],[386,"agsm"],[387,"alac"],[388,"alaw"],[389,"dtsc"],[390,"dtse"],[391,"dtsh"],[392,"dtsl"],[393,"dvca"],[394,"dvhd"],[395,"dvsd"],[396,"dvsl"],[397,"ec-3"],[398,"enca"],[399,"fLaC"],[400,"fl32"],[401,"fl64"],[402,"ilbc"],[403,"ima4"],[404,"in24"],[405,"in32"],[406,"lpcm"],[407,"mp4a"],[408,"mp4a-40"],[409,"mp4a-40-1"],[410,"mp4a-40-12"],[411,"mp4a-40-13"],[412,"mp4a-40-14"],[413,"mp4a-40-15"],[414,"mp4a-40-16"],[415,"mp4a-40-17"],[416,"mp4a-40-19"],[417,"mp4a-40-2"],[418,"mp4a-40-20"],[419,"mp4a-40-21"],[420,"mp4a-40-22"],[421,"mp4a-40-23"],[422,"mp4a-40-24"],[423,"mp4a-40-25"],[424,"mp4a-40-26"],[425,"mp4a-40-27"],[426,"mp4a-40-28"],[427,"mp4a-40-29"],[428,"mp4a-40-3"],[429,"mp4a-40-32"],[430,"mp4a-40-33"],[431,"mp4a-40-34"],[432,"mp4a-40-35"],[433,"mp4a-40-36"],[434,"mp4a-40-4"],[435,"mp4a-40-5"],[436,"mp4a-40-6"],[437,"mp4a-40-7"],[438,"mp4a-40-8"],[439,"mp4a-40-9"],[440,"mp4a-66"],[441,"mp4a-67"],[442,"mp4a-67-1"],[443,"mp4a-68"],[444,"mp4a-69"],[445,"mp4a-6B"],[446,"mp4a-A9"],[447,"mp4a-D1"],[448,"mp4a-DD"],[449,"mp4a-E1"],[450,"nmos"],[451,"owma"],[452,"raw "],[453,"sac3"],[454,"samr"],[455,"sawb"],[456,"sawp"],[457,"sevc"],[458,"sowt"],[459,"spex"],[460,"sqcp"],[461,"twos"],[462,"ulaw"],[463,"vdva"]],)"
    //        R"([[1,129,887,417],[2,135,887,417],[3,64,887,417],[4,62,887,417],[5,152,887,417],[6,152,887,385],[7,152,887,397],[8,152,887,389],[9,152,956,417],[10,152,956,385],[11,152,956,397],[12,152,956,389],[13,129,887,385],[14,129,887,397],[15,129,887,446],[16,129,956,417],[17,129,956,385],[18,129,956,397],[19,129,956,446],[20,113,772,293],[21,113,772,280],[22,113,773,293],[23,113,773,280],[24,87,662,379],[25,87,662,359],[26,87,662,317],[27,95,973,417],[28,114,780,51],[29,114,781,51],[30,114,782,51],[31,114,405,51],[32,81,758,269],[33,81,758,273],[34,81,758,274],[35,81,758,254],[36,81,762,254],[37,81,762,274],[38,81,762,269],[39,81,762,273],[40,25,144,161],[41,25,148,161],[42,25,197,161],[43,25,802,161],[44,25,232,161],[45,25,266,161],[46,76,56,334],[47,76,409,334],[48,76,412,334],[49,70,0,334],[50,59,0,417],[51,39,0,317],[52,111,0,6],[53,73,887,417],[54,73,1119,454]])";
    //    JSON j;
    //    j.pos = latest.c_str();
    //    downloadedCodecMapsVersion = 1;
    //    ReadIdRecords(mediaCodecs.containers, j);
    //    ReadIdRecords(mediaCodecs.videocodecs, j);
    //    ReadIdRecords(mediaCodecs.audiocodecs, j);
    //    ReadShortFormats(mediaCodecs.shortformats, j);
    //    mediaCodecsReceived = true;

    //    for (auto sf : mediaCodecs.shortformats)
    //    {
    //        cout << "format " << (unsigned)sf.shortformatid;
    //        for (auto c : mediaCodecs.containers)
    //            if (c.second.id == sf.containerid)
    //                cout << " container " << c.first;
    //        for (auto c : mediaCodecs.videocodecs)
    //            if (c.second.id == sf.videocodecid)
    //                cout << " video " << c.first;
    //        for (auto c : mediaCodecs.audiocodecs)
    //            if (c.second.id == sf.audiocodecid)
    //                cout << " audio " << c.first;
    //        cout << endl;
    //    }

    //    std::vector<std::string> mpegNames = { "2dcc","3gvo","a3ds","ac-3","ac-4","alac","alaw","avc1","avc2","avc3","avc4","avcp","dra1","drac","dts+","dts-","dtsc","dtse","dtsh","dtsl","dtsx","dvav","dvhe","ec-3","enca","encf","encm","encs","enct","encv","fdp ","g719","g726","hev1","hvc1","ixse","m2ts","m4ae","m4ae","mett","metx","mha1","mha2","mhm1","mhm2","mjp2","mlix","mlpa","mp4a","mp4s","mp4v","mvc1","mvc2","mvc3","mvc4","oksd","Opus","pm2t","prtp","raw ","resv","rm2t","rrtp","rsrp","rtmd","rtp ","rv60","s263","samr","sawb","sawp","sevc","sm2t","sqcp","srtp","ssmv","STGS","stpp","svc1","svc2","svcM","tc64","tmcd","twos","tx3g","ulaw","unid","urim","vc-1","vp08","vp09","wvtt","agsm","alaw","CFHD","civd","c608","c708","drac","DV10","dvh5","dvh6","dvhp","dvi ","DVOO","DVOR","DVTV","DVVT","fl32","fl64","flic","gif","h261","h263","HD10","ima4","in24","in32","jpeg","lpcm","M105","mjpa","mjpb","Opus","png","PNTG","Qclp","QDM2","QDMC","rle","rpza","Shr0","Shr1","Shr2","Shr3","Shr4","SVQ1","SVQ3","tga","tiff","ulaw","vdva","WRLE" };
    //    for (auto n : mpegNames)
    //    {
    //        if (Lookup(n, mediaCodecs.videocodecs, 0) == 0 && Lookup(n, mediaCodecs.audiocodecs, 0) == 0)
    //            cout << " mpeg name " << n << " not found!" << endl;
    //    }

    //}

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
        mediaCodecsFailed = true;
    }
    else
    {
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
    MediaFileInfo::queuedvp q;
    q.handle = uploadHandle;
    q.vp = vp;
    memcpy(q.fakey, fakey, sizeof(q.fakey));
    uploadFileAttributes[uploadHandle] = q;

    if (mediaCodecsFailed)
    {
        return 0;  // we can't do it - let the transfer complete anyway
    }
    else if (mediaCodecsReceived)
    {
        // indicate we have this attribute ready to go. Otherwise the transfer will be put on hold till we can
        client->pendingfa[pair<handle, fatype>(uploadHandle, fa_media)] = pair<handle, int>(0, 0);
    }
    return 1;
}

void MediaFileInfo::sendOrQueueMediaPropertiesFileAttributesForExistingFile(MediaProperties& vp, uint32_t fakey[4], MegaClient* client, handle fileHandle)
{
    if (!mediaCodecsReceived)
    {
        MediaFileInfo::queuedvp q;
        q.handle = fileHandle;
        q.vp = vp;
        memcpy(q.fakey, fakey, sizeof(q.fakey));
        queuedForDownloadTranslation.push_back(q);
    }
    else
    {
        std::string mediafileattributes = vp.convertMediaPropertyFileAttributes(fakey, client->mediaFileInfo);
        client->reqs.add(new CommandAttachFA(fileHandle, fa_media, mediafileattributes.c_str(), 0));
    }
}

void MediaFileInfo::addUploadMediaFileAttributes(handle& uploadhandle, std::string* s)
{
    std::map<handle, MediaFileInfo::queuedvp>::iterator i = uploadFileAttributes.find(uploadhandle);
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
        if (vp.fps != MEDIA_INFO_BUILD)
        {
            return true;
        } 
        if (vp.width != PrecomputedMediaInfoLibVersion)
        {
            return true;
        }
        if (vp.playtime < downloadedCodecMapsVersion)
        {
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
    auto t = GetTickCount();

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

        if (totalBytesRead > maxBytesToRead || startTime != 0 && (time(NULL)-startTime > maxSeconds))
        {
            LOG_warn << "could not extract mediainfo data within reasonable limits";
            fa->closef();
            return false;
        }

        if (!fa->frawread(buf, n, readpos))
        {
            LOG_err << "could not read local file";
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
                    LOG_warn << "no general information found in file";
                }
                if (!minfo.Count_Get(MediaInfoLib::Stream_Video, 0))
                {
                    LOG_warn << "no video information found in file";
                }
                if (!minfo.Count_Get(MediaInfoLib::Stream_Audio, 0))
                {
                    LOG_warn << "no audio information found in file";
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
                playtime = (coalesce(gd.To_int32u(), coalesce(vd.To_int32u(), ad.To_int32u())) + 500) / 1000;  // converting ms to sec
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

#ifdef _DEBUG
                string path, local = localFilename;
                fsa->local2path(&local, &path);
                LOG_info << "MediaInfo on " << path << " | " << vw.To_Local() << " " << vh.To_Local() << " " << vd.To_Local() << " " << vr.To_Local() << " |\"" << gci.To_Local() << "\",\"" << gf.To_Local() << "\",\"" << vci.To_Local() << "\",\"" << vcf.To_Local() << "\",\"" << aci.To_Local() << "\",\"" << acf.To_Local() << "\"";
#endif
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
        width = PrecomputedMediaInfoLibVersion;             // mediaInfoLib version that couldn't do it.  1710 at time of writing (ie oct 2017 tag)
        height = 0;
        playtime = mediaInfo.downloadedCodecMapsVersion;    // updated when we add more codec names etc
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

