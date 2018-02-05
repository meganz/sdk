Mega SDK optionally depends on MediaInfoLib library (https://github.com/MediaArea/MediaInfoLib)
The latest version of that at the time of writing was v17.10, and there is a tag to select if you want to use exactly that version.  
However the mainline produces far fewer build issues with our settings below, so that is recommended (as at 20 Dec 2017).

Turn on USE_MEDIAINFO in order to enable the dependency and functionality.    
If USE_MEDIAINFO is enabled, Mega SDK will analyse media files (video, audio) and set file attributes on them to assist the web interface in playing them.
Once set, the file attributes do not need to be analysed again.

MediaInfoLib also has its own dependency, ZenLib (https://github.com/MediaArea/ZenLib)

Mega SDK does not need all the functionality in MediaInfoLib so we recommend building it with settings that cut down the binary size.
Build instructions for MediaInfoLib and ZenLib can be found in their respective root folders once downloaded, they both support many build systems.

The settings we recommend building with are:

for the GNU build system (using configure):

--with-libz-static --host=le32-unknown-nacl --enable-minimize-size --enable-minimal --disable-archive --disable-image --disable-tag --disable-text --disable-swf --disable-flv --disable-hdsf4m 
--disable-cdxa --disable-dpg --disable-pmp --disable-rm --disable-wtv --disable-mxf --disable-dcp --disable-aaf --disable-bdav --disable-bdmv --disable-dvdv --disable-gxf --disable-mixml --disable-skm 
--disable-nut --disable-tsp --disable-hls --disable-dxw --disable-dvdif --disable-dashmpd --disable-aic --disable-avsv --disable-canopus --disable-ffv1 --disable-flic --disable-huffyuv --disable-prores 
--disable-y4m --disable-adpcm --disable-amr --disable-amv --disable-ape --disable-au --disable-la --disable-celt --disable-midi --disable-mpc --disable-openmg --disable-pcm --disable-ps2a --disable-rkau 
--disable-speex --disable-tak --disable-tta --disable-twinvq --disable-references 


for windows (these can be added to the top of MediaInfoLib's Setup.h, which is much more straightforward than editing the visual studio project file):

#define MEDIAINFO_MINIMIZESIZE
#define MEDIAINFO_MINIMAL_YES
#define MEDIAINFO_ARCHIVE_NO 
#define MEDIAINFO_IMAGE_NO 
#define MEDIAINFO_TAG_NO 
#define MEDIAINFO_TEXT_NO 
#define MEDIAINFO_SWF_NO 
#define MEDIAINFO_FLV_NO 
#define MEDIAINFO_HDSF4M_NO 
#define MEDIAINFO_CDXA_NO 
#define MEDIAINFO_DPG_NO 
#define MEDIAINFO_PMP_NO 
#define MEDIAINFO_RM_NO 
#define MEDIAINFO_WTV_NO 
#define MEDIAINFO_MXF_NO 
#define MEDIAINFO_DCP_NO 
#define MEDIAINFO_AAF_NO 
#define MEDIAINFO_BDAV_NO 
#define MEDIAINFO_BDMV_NO 
#define MEDIAINFO_DVDV_NO 
#define MEDIAINFO_GXF_NO 
#define MEDIAINFO_MIXML_NO 
#define MEDIAINFO_SKM_NO 
#define MEDIAINFO_NUT_NO 
#define MEDIAINFO_TSP_NO 
#define MEDIAINFO_HLS_NO 
#define MEDIAINFO_DXW_NO 
#define MEDIAINFO_DVDIF_NO 
#define MEDIAINFO_DASHMPD_NO 
#define MEDIAINFO_AIC_NO 
#define MEDIAINFO_AVSV_NO 
#define MEDIAINFO_CANOPUS_NO 
#define MEDIAINFO_FFV1_NO 
#define MEDIAINFO_FLIC_NO 
#define MEDIAINFO_HUFFYUV_NO 
#define MEDIAINFO_PRORES_NO 
#define MEDIAINFO_Y4M_NO 
#define MEDIAINFO_ADPCM_NO 
#define MEDIAINFO_AMR_NO 
#define MEDIAINFO_AMV_NO 
#define MEDIAINFO_APE_NO 
#define MEDIAINFO_AU_NO 
#define MEDIAINFO_LA_NO 
#define MEDIAINFO_CELT_NO 
#define MEDIAINFO_MIDI_NO 
#define MEDIAINFO_MPC_NO 
#define MEDIAINFO_OPENMG_NO 
#define MEDIAINFO_PCM_NO 
#define MEDIAINFO_PS2A_NO 
#define MEDIAINFO_RKAU_NO 
#define MEDIAINFO_SPEEX_NO 
#define MEDIAINFO_TAK_NO 
#define MEDIAINFO_TTA_NO 
#define MEDIAINFO_TWINVQ_NO 
#define MEDIAINFO_REFERENCES_NO 

#define MEDIAINFO_ADVANCED_NO 
#define MEDIAINFO_ADVANCED2_NO  
#define MEDIAINFO_OTHER_NO  
#define MEDIAINFO_ANCILLARY_NO 
#define MEDIAINFO_SEQUENCEINFO_NO 
#define MEDIAINFO_AFDBARDATA_NO 
#define MEDIAINFO_MPCSV8_NO 
#define MEDIAINFO_S3M_NO 
#define MEDIAINFO_DOLBYE_NO 
#define MEDIAINFO_CAF_NO 
#define MEDIAINFO_IT_NO 
#define MEDIAINFO_PCMVOB_NO 
#define MEDIAINFO_MOD_NO 
#define MEDIAINFO_DIRAC_NO 
#define MEDIAINFO_FRAPS_NO 
#define MEDIAINFO_LAGARITH_NO 
#define MEDIAINFO_LXF_NO 
#define MEDIAINFO_PTX_NO 
#define MEDIAINFO_ISM_NO 
#define MEDIAINFO_P2_NO 
#define MEDIAINFO_IVF_NO 
#define MEDIAINFO_VBI_NO 
#define MEDIAINFO_XDCAM_NO 
#define MEDIAINFO_TIMECODE_NO 
#define MEDIAINFO_XM_NO 
#define MEDIAINFO_VORBISCOM_NO 
