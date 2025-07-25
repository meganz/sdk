
cmake_minimum_required(VERSION 3.19)
project(pdfium_by_cmake)

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -D_DEBUG -DDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -D_DEBUG -DDEBUG")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /EHsc /MP")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /MP ")
	add_definitions(-DHAVE_BOOLEAN -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
endif()

if(APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -isysroot /Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk" )
endif()

add_definitions(-DCMS_NO_REGISTER_KEYWORD -DUNICODE -D_UNICODE -DUSE_LIBJPEG_TURBO -DPNG_PREFIX -DUSE_SYSTEM_ICUUC -DUSE_SYSTEM_LIBJPEG -DUSE_SYSTEM_LCMS2 -DUSE_SYSTEM_ZLIB -DUSE_SYSTEM_LIBOPENJPEG2 -DOPJ_STATIC -DDEFINE_PS_TABLES)
# no skia, V8, or xfa (xfa seems to require V8 anyway)

find_package(OpenJPEG          REQUIRED)
find_package(lcms              REQUIRED)
find_package(JPEG              REQUIRED)
find_package(libpng            REQUIRED)
find_package(Freetype          REQUIRED)
find_package(ICU COMPONENTS uc REQUIRED)

include_directories(
    ${JPEG_INCLUDE_DIRS}
    ${ZLIB_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/abseil-cpp
    "."
)

if(WIN32)
     set(WIN 1)
else()
     set(WIN 0)
endif()

if (APPLE)
    set(MAC 1)
else()
    set(MAC 0)
endif()

if (NOT WIN32 AND NOT APPLE)
    set(LINUX 1)
else()
    set(LINUX 0)
endif()

set(ANDROID 0)


add_library(pdfium STATIC)

# File extraction:
# Configure the project using the depot_tools, using the following parameters. It may require some adjustments depending on the OS:
#    gn gen ../out/build_x64 --args="is_component_build=false use_custom_libcxx=false treat_warnings_as_errors=false fatal_linker_warnings=false pdf_enable_v8=false pdf_is_complete_lib=true use_sysroot=false is_clang=false is_debug=true"
# Compile with ninja using -v and -j1 parameters.
#    ninja -C ../out/build_x64 -j1 -v :pdfium > build_output.txt
# Process the output with the following AWK script:
# awk '
# {
#   for (i = 1; i < NF; i++) {
#     if ($i ~ /\.(c|cc|cpp|cxx|C|c\+\+)$/ && ($(i+1) == "-o" || $(i+1) ~ /\.(obj)$/)) {
#       sub("^\\.\\.\\/\\.\\.\\/pdfium/", "", $i)
#       print $i
#     }
#   }
# }
# ' build_output.txt > build_output_processed.txt
# Output will need some manual parsing and adjustments.
target_sources(pdfium PRIVATE
constants/annotation_common.cpp
constants/appearance.cpp
constants/font_encodings.cpp
constants/form_fields.cpp
constants/page_object.cpp
constants/stream_dict_common.cpp
constants/transparency.cpp
core/fdrm/fx_crypt.cpp
core/fdrm/fx_crypt_aes.cpp
core/fdrm/fx_crypt_sha.cpp
core/fpdfapi/cmaps/CNS1/Adobe-CNS1-UCS2_5.cpp
core/fpdfapi/cmaps/CNS1/B5pc-H_0.cpp
core/fpdfapi/cmaps/CNS1/B5pc-V_0.cpp
core/fpdfapi/cmaps/CNS1/CNS-EUC-H_0.cpp
core/fpdfapi/cmaps/CNS1/CNS-EUC-V_0.cpp
core/fpdfapi/cmaps/CNS1/ETen-B5-H_0.cpp
core/fpdfapi/cmaps/CNS1/ETen-B5-V_0.cpp
core/fpdfapi/cmaps/CNS1/ETenms-B5-H_0.cpp
core/fpdfapi/cmaps/CNS1/ETenms-B5-V_0.cpp
core/fpdfapi/cmaps/CNS1/HKscs-B5-H_5.cpp
core/fpdfapi/cmaps/CNS1/HKscs-B5-V_5.cpp
core/fpdfapi/cmaps/CNS1/UniCNS-UCS2-H_3.cpp
core/fpdfapi/cmaps/CNS1/UniCNS-UCS2-V_3.cpp
core/fpdfapi/cmaps/CNS1/UniCNS-UTF16-H_0.cpp
core/fpdfapi/cmaps/CNS1/cmaps_cns1.cpp
core/fpdfapi/cmaps/GB1/Adobe-GB1-UCS2_5.cpp
core/fpdfapi/cmaps/GB1/GB-EUC-H_0.cpp
core/fpdfapi/cmaps/GB1/GB-EUC-V_0.cpp
core/fpdfapi/cmaps/GB1/GBK-EUC-H_2.cpp
core/fpdfapi/cmaps/GB1/GBK-EUC-V_2.cpp
core/fpdfapi/cmaps/GB1/GBK2K-H_5.cpp
core/fpdfapi/cmaps/GB1/GBK2K-V_5.cpp
core/fpdfapi/cmaps/GB1/GBKp-EUC-H_2.cpp
core/fpdfapi/cmaps/GB1/GBKp-EUC-V_2.cpp
core/fpdfapi/cmaps/GB1/GBpc-EUC-H_0.cpp
core/fpdfapi/cmaps/GB1/GBpc-EUC-V_0.cpp
core/fpdfapi/cmaps/GB1/UniGB-UCS2-H_4.cpp
core/fpdfapi/cmaps/GB1/UniGB-UCS2-V_4.cpp
core/fpdfapi/cmaps/GB1/cmaps_gb1.cpp
core/fpdfapi/cmaps/Japan1/83pv-RKSJ-H_1.cpp
core/fpdfapi/cmaps/Japan1/90ms-RKSJ-H_2.cpp
core/fpdfapi/cmaps/Japan1/90ms-RKSJ-V_2.cpp
core/fpdfapi/cmaps/Japan1/90msp-RKSJ-H_2.cpp
core/fpdfapi/cmaps/Japan1/90msp-RKSJ-V_2.cpp
core/fpdfapi/cmaps/Japan1/90pv-RKSJ-H_1.cpp
core/fpdfapi/cmaps/Japan1/Add-RKSJ-H_1.cpp
core/fpdfapi/cmaps/Japan1/Add-RKSJ-V_1.cpp
core/fpdfapi/cmaps/Japan1/Adobe-Japan1-UCS2_4.cpp
core/fpdfapi/cmaps/Japan1/EUC-H_1.cpp
core/fpdfapi/cmaps/Japan1/EUC-V_1.cpp
core/fpdfapi/cmaps/Japan1/Ext-RKSJ-H_2.cpp
core/fpdfapi/cmaps/Japan1/Ext-RKSJ-V_2.cpp
core/fpdfapi/cmaps/Japan1/H_1.cpp
core/fpdfapi/cmaps/Japan1/UniJIS-UCS2-HW-H_4.cpp
core/fpdfapi/cmaps/Japan1/UniJIS-UCS2-HW-V_4.cpp
core/fpdfapi/cmaps/Japan1/UniJIS-UCS2-H_4.cpp
core/fpdfapi/cmaps/Japan1/UniJIS-UCS2-V_4.cpp
core/fpdfapi/cmaps/Japan1/V_1.cpp
core/fpdfapi/cmaps/Japan1/cmaps_japan1.cpp
core/fpdfapi/cmaps/Korea1/Adobe-Korea1-UCS2_2.cpp
core/fpdfapi/cmaps/Korea1/KSC-EUC-H_0.cpp
core/fpdfapi/cmaps/Korea1/KSC-EUC-V_0.cpp
core/fpdfapi/cmaps/Korea1/KSCms-UHC-HW-H_1.cpp
core/fpdfapi/cmaps/Korea1/KSCms-UHC-HW-V_1.cpp
core/fpdfapi/cmaps/Korea1/KSCms-UHC-H_1.cpp
core/fpdfapi/cmaps/Korea1/KSCms-UHC-V_1.cpp
core/fpdfapi/cmaps/Korea1/KSCpc-EUC-H_0.cpp
core/fpdfapi/cmaps/Korea1/UniKS-UCS2-H_1.cpp
core/fpdfapi/cmaps/Korea1/UniKS-UCS2-V_1.cpp
core/fpdfapi/cmaps/Korea1/UniKS-UTF16-H_0.cpp
core/fpdfapi/cmaps/Korea1/cmaps_korea1.cpp
core/fpdfapi/cmaps/fpdf_cmaps.cpp
core/fpdfapi/edit/cpdf_contentstream_write_utils.cpp
core/fpdfapi/edit/cpdf_creator.cpp
core/fpdfapi/edit/cpdf_npagetooneexporter.cpp
core/fpdfapi/edit/cpdf_pagecontentgenerator.cpp
core/fpdfapi/edit/cpdf_pagecontentmanager.cpp
core/fpdfapi/edit/cpdf_pageexporter.cpp
core/fpdfapi/edit/cpdf_pageorganizer.cpp
core/fpdfapi/edit/cpdf_stringarchivestream.cpp
core/fpdfapi/font/cfx_cttgsubtable.cpp
core/fpdfapi/font/cfx_stockfontarray.cpp
core/fpdfapi/font/cpdf_cid2unicodemap.cpp
core/fpdfapi/font/cpdf_cidfont.cpp
core/fpdfapi/font/cpdf_cmap.cpp
core/fpdfapi/font/cpdf_cmapparser.cpp
core/fpdfapi/font/cpdf_font.cpp
core/fpdfapi/font/cpdf_fontencoding.cpp
core/fpdfapi/font/cpdf_fontglobals.cpp
core/fpdfapi/font/cpdf_simplefont.cpp
core/fpdfapi/font/cpdf_tounicodemap.cpp
core/fpdfapi/font/cpdf_truetypefont.cpp
core/fpdfapi/font/cpdf_type1font.cpp
core/fpdfapi/font/cpdf_type3char.cpp
core/fpdfapi/font/cpdf_type3font.cpp
core/fpdfapi/page/cpdf_allstates.cpp
core/fpdfapi/page/cpdf_annotcontext.cpp
core/fpdfapi/page/cpdf_basedcs.cpp
core/fpdfapi/page/cpdf_clippath.cpp
core/fpdfapi/page/cpdf_color.cpp
core/fpdfapi/page/cpdf_colorspace.cpp
core/fpdfapi/page/cpdf_colorstate.cpp
core/fpdfapi/page/cpdf_contentmarkitem.cpp
core/fpdfapi/page/cpdf_contentmarks.cpp
core/fpdfapi/page/cpdf_contentparser.cpp
core/fpdfapi/page/cpdf_devicecs.cpp
core/fpdfapi/page/cpdf_dib.cpp
core/fpdfapi/page/cpdf_docpagedata.cpp
core/fpdfapi/page/cpdf_expintfunc.cpp
core/fpdfapi/page/cpdf_form.cpp
core/fpdfapi/page/cpdf_formobject.cpp
core/fpdfapi/page/cpdf_function.cpp
core/fpdfapi/page/cpdf_generalstate.cpp
core/fpdfapi/page/cpdf_graphicstates.cpp
core/fpdfapi/page/cpdf_iccprofile.cpp
core/fpdfapi/page/cpdf_image.cpp
core/fpdfapi/page/cpdf_imageloader.cpp
core/fpdfapi/page/cpdf_imageobject.cpp
core/fpdfapi/page/cpdf_indexedcs.cpp
core/fpdfapi/page/cpdf_meshstream.cpp
core/fpdfapi/page/cpdf_occontext.cpp
core/fpdfapi/page/cpdf_page.cpp
core/fpdfapi/page/cpdf_pageimagecache.cpp
core/fpdfapi/page/cpdf_pagemodule.cpp
core/fpdfapi/page/cpdf_pageobject.cpp
core/fpdfapi/page/cpdf_pageobjectholder.cpp
core/fpdfapi/page/cpdf_path.cpp
core/fpdfapi/page/cpdf_pathobject.cpp
core/fpdfapi/page/cpdf_pattern.cpp
core/fpdfapi/page/cpdf_patterncs.cpp
core/fpdfapi/page/cpdf_psengine.cpp
core/fpdfapi/page/cpdf_psfunc.cpp
core/fpdfapi/page/cpdf_sampledfunc.cpp
core/fpdfapi/page/cpdf_shadingobject.cpp
core/fpdfapi/page/cpdf_shadingpattern.cpp
core/fpdfapi/page/cpdf_stitchfunc.cpp
core/fpdfapi/page/cpdf_streamcontentparser.cpp
core/fpdfapi/page/cpdf_streamparser.cpp
core/fpdfapi/page/cpdf_textobject.cpp
core/fpdfapi/page/cpdf_textstate.cpp
core/fpdfapi/page/cpdf_tilingpattern.cpp
core/fpdfapi/page/cpdf_transferfunc.cpp
core/fpdfapi/page/cpdf_transferfuncdib.cpp
core/fpdfapi/page/cpdf_transparency.cpp
core/fpdfapi/parser/cfdf_document.cpp
core/fpdfapi/parser/cpdf_array.cpp
core/fpdfapi/parser/cpdf_boolean.cpp
core/fpdfapi/parser/cpdf_cross_ref_avail.cpp
core/fpdfapi/parser/cpdf_cross_ref_table.cpp
core/fpdfapi/parser/cpdf_crypto_handler.cpp
core/fpdfapi/parser/cpdf_data_avail.cpp
core/fpdfapi/parser/cpdf_dictionary.cpp
core/fpdfapi/parser/cpdf_document.cpp
core/fpdfapi/parser/cpdf_encryptor.cpp
core/fpdfapi/parser/cpdf_flateencoder.cpp
core/fpdfapi/parser/cpdf_hint_tables.cpp
core/fpdfapi/parser/cpdf_indirect_object_holder.cpp
core/fpdfapi/parser/cpdf_linearized_header.cpp
core/fpdfapi/parser/cpdf_name.cpp
core/fpdfapi/parser/cpdf_null.cpp
core/fpdfapi/parser/cpdf_number.cpp
core/fpdfapi/parser/cpdf_object.cpp
core/fpdfapi/parser/cpdf_object_avail.cpp
core/fpdfapi/parser/cpdf_object_stream.cpp
core/fpdfapi/parser/cpdf_object_walker.cpp
core/fpdfapi/parser/cpdf_page_object_avail.cpp
core/fpdfapi/parser/cpdf_parser.cpp
core/fpdfapi/parser/cpdf_read_validator.cpp
core/fpdfapi/parser/cpdf_reference.cpp
core/fpdfapi/parser/cpdf_security_handler.cpp
core/fpdfapi/parser/cpdf_simple_parser.cpp
core/fpdfapi/parser/cpdf_stream.cpp
core/fpdfapi/parser/cpdf_stream_acc.cpp
core/fpdfapi/parser/cpdf_string.cpp
core/fpdfapi/parser/cpdf_syntax_parser.cpp
core/fpdfapi/parser/fpdf_parser_decode.cpp
core/fpdfapi/parser/fpdf_parser_utility.cpp
core/fpdfapi/parser/object_tree_traversal_util.cpp
core/fpdfapi/render/charposlist.cpp
core/fpdfapi/render/cpdf_devicebuffer.cpp
core/fpdfapi/render/cpdf_docrenderdata.cpp
core/fpdfapi/render/cpdf_imagerenderer.cpp
core/fpdfapi/render/cpdf_pagerendercontext.cpp
core/fpdfapi/render/cpdf_progressiverenderer.cpp
core/fpdfapi/render/cpdf_rendercontext.cpp
core/fpdfapi/render/cpdf_renderoptions.cpp
core/fpdfapi/render/cpdf_rendershading.cpp
core/fpdfapi/render/cpdf_renderstatus.cpp
core/fpdfapi/render/cpdf_rendertiling.cpp
core/fpdfapi/render/cpdf_textrenderer.cpp
core/fpdfapi/render/cpdf_type3cache.cpp
core/fpdfapi/render/cpdf_type3glyphmap.cpp
$<${WIN}:core/fpdfapi/render/cpdf_scaledrenderbuffer.cpp>
$<${WIN}:core/fpdfapi/render/cpdf_windowsrenderdevice.cpp>
core/fpdfdoc/cpdf_aaction.cpp
core/fpdfdoc/cpdf_action.cpp
core/fpdfdoc/cpdf_annot.cpp
core/fpdfdoc/cpdf_annotlist.cpp
core/fpdfdoc/cpdf_apsettings.cpp
core/fpdfdoc/cpdf_bafontmap.cpp
core/fpdfdoc/cpdf_bookmark.cpp
core/fpdfdoc/cpdf_bookmarktree.cpp
core/fpdfdoc/cpdf_color_utils.cpp
core/fpdfdoc/cpdf_defaultappearance.cpp
core/fpdfdoc/cpdf_dest.cpp
core/fpdfdoc/cpdf_filespec.cpp
core/fpdfdoc/cpdf_formcontrol.cpp
core/fpdfdoc/cpdf_formfield.cpp
core/fpdfdoc/cpdf_generateap.cpp
core/fpdfdoc/cpdf_icon.cpp
core/fpdfdoc/cpdf_iconfit.cpp
core/fpdfdoc/cpdf_interactiveform.cpp
core/fpdfdoc/cpdf_link.cpp
core/fpdfdoc/cpdf_linklist.cpp
core/fpdfdoc/cpdf_metadata.cpp
core/fpdfdoc/cpdf_nametree.cpp
core/fpdfdoc/cpdf_numbertree.cpp
core/fpdfdoc/cpdf_pagelabel.cpp
core/fpdfdoc/cpdf_structelement.cpp
core/fpdfdoc/cpdf_structtree.cpp
core/fpdfdoc/cpdf_viewerpreferences.cpp
core/fpdfdoc/cpvt_fontmap.cpp
core/fpdfdoc/cpvt_section.cpp
core/fpdfdoc/cpvt_variabletext.cpp
core/fpdfdoc/cpvt_wordinfo.cpp
core/fpdftext/cpdf_linkextract.cpp
core/fpdftext/cpdf_textpage.cpp
core/fpdftext/cpdf_textpagefind.cpp
core/fpdftext/unicodenormalizationdata.cpp
core/fxcodec/basic/basicmodule.cpp
core/fxcodec/data_and_bytes_consumed.cpp
core/fxcodec/fax/faxmodule.cpp
core/fxcodec/flate/flatemodule.cpp
core/fxcodec/fx_codec.cpp
core/fxcodec/icc/icc_transform.cpp
core/fxcodec/jbig2/JBig2_ArithDecoder.cpp
core/fxcodec/jbig2/JBig2_ArithIntDecoder.cpp
core/fxcodec/jbig2/JBig2_BitStream.cpp
core/fxcodec/jbig2/JBig2_Context.cpp
core/fxcodec/jbig2/JBig2_DocumentContext.cpp
core/fxcodec/jbig2/JBig2_GrdProc.cpp
core/fxcodec/jbig2/JBig2_GrrdProc.cpp
core/fxcodec/jbig2/JBig2_HtrdProc.cpp
core/fxcodec/jbig2/JBig2_HuffmanDecoder.cpp
core/fxcodec/jbig2/JBig2_HuffmanTable.cpp
core/fxcodec/jbig2/JBig2_Image.cpp
core/fxcodec/jbig2/JBig2_PatternDict.cpp
core/fxcodec/jbig2/JBig2_PddProc.cpp
core/fxcodec/jbig2/JBig2_SddProc.cpp
core/fxcodec/jbig2/JBig2_Segment.cpp
core/fxcodec/jbig2/JBig2_SymbolDict.cpp
core/fxcodec/jbig2/JBig2_TrdProc.cpp
core/fxcodec/jbig2/jbig2_decoder.cpp
core/fxcodec/jpeg/jpeg_common.cpp
core/fxcodec/jpeg/jpegmodule.cpp
core/fxcodec/jpx/cjpx_decoder.cpp
core/fxcodec/jpx/jpx_decode_utils.cpp
core/fxcodec/scanlinedecoder.cpp
core/fxcrt/binary_buffer.cpp
core/fxcrt/bytestring.cpp
core/fxcrt/cfx_bitstream.cpp
core/fxcrt/cfx_datetime.cpp
core/fxcrt/cfx_read_only_span_stream.cpp
core/fxcrt/cfx_read_only_string_stream.cpp
core/fxcrt/cfx_read_only_vector_stream.cpp
core/fxcrt/cfx_seekablestreamproxy.cpp
core/fxcrt/cfx_timer.cpp
core/fxcrt/debug/alias.cc
core/fxcrt/fx_bidi.cpp
core/fxcrt/fx_codepage.cpp
core/fxcrt/fx_coordinates.cpp
core/fxcrt/fx_extension.cpp
core/fxcrt/fx_memory.cpp
core/fxcrt/fx_number.cpp
core/fxcrt/fx_random.cpp
core/fxcrt/fx_stream.cpp
core/fxcrt/fx_string.cpp
core/fxcrt/fx_system.cpp
core/fxcrt/fx_unicode.cpp
core/fxcrt/observed_ptr.cpp
core/fxcrt/string_data_template.cpp
core/fxcrt/string_template.cpp
core/fxcrt/widestring.cpp
core/fxcrt/widetext_buffer.cpp
core/fxcrt/xml/cfx_xmlchardata.cpp
core/fxcrt/xml/cfx_xmldocument.cpp
core/fxcrt/xml/cfx_xmlelement.cpp
core/fxcrt/xml/cfx_xmlinstruction.cpp
core/fxcrt/xml/cfx_xmlnode.cpp
core/fxcrt/xml/cfx_xmlparser.cpp
core/fxcrt/xml/cfx_xmltext.cpp
core/fxcrt/fx_memory_malloc.cpp
$<${WIN}:core/fxcrt/cfx_fileaccess_windows.cpp>
$<${MAC}:core/fxcrt/cfx_fileaccess_posix.cpp>
$<${LINUX}:core/fxcrt/cfx_fileaccess_posix.cpp>
$<${WIN}:core/fxcrt/code_point_view.cpp>
$<${WIN}:core/fxcrt/fx_folder_windows.cpp>
$<${MAC}:core/fxcrt/fx_folder_posix.cpp>
$<${LINUX}:core/fxcrt/fx_folder_posix.cpp>
$<${WIN}:core/fxcrt/win/win_util.cc>
core/fxge/agg/cfx_agg_bitmapcomposer.cpp
core/fxge/agg/cfx_agg_cliprgn.cpp
core/fxge/agg/cfx_agg_devicedriver.cpp
core/fxge/agg/cfx_agg_imagerenderer.cpp
core/fxge/calculate_pitch.cpp
core/fxge/cfx_color.cpp
core/fxge/cfx_defaultrenderdevice.cpp
core/fxge/cfx_drawutils.cpp
core/fxge/cfx_face.cpp
core/fxge/cfx_folderfontinfo.cpp
core/fxge/cfx_font.cpp
core/fxge/cfx_fontcache.cpp
core/fxge/cfx_fontmapper.cpp
core/fxge/cfx_fontmgr.cpp
core/fxge/cfx_gemodule.cpp
core/fxge/cfx_glyphbitmap.cpp
core/fxge/cfx_glyphcache.cpp
core/fxge/cfx_graphstate.cpp
core/fxge/cfx_graphstatedata.cpp
core/fxge/cfx_path.cpp
core/fxge/cfx_renderdevice.cpp
core/fxge/cfx_substfont.cpp
core/fxge/cfx_unicodeencoding.cpp
core/fxge/dib/blend.cpp
core/fxge/dib/cfx_bitmapstorer.cpp
core/fxge/dib/cfx_cmyk_to_srgb.cpp
core/fxge/dib/cfx_dibbase.cpp
core/fxge/dib/cfx_dibitmap.cpp
core/fxge/dib/cfx_imagestretcher.cpp
core/fxge/dib/cfx_imagetransformer.cpp
core/fxge/dib/cfx_scanlinecompositor.cpp
core/fxge/dib/cstretchengine.cpp
core/fxge/dib/fx_dib.cpp
core/fxge/fontdata/chromefontdata/FoxitDingbats.cpp
core/fxge/fontdata/chromefontdata/FoxitFixed.cpp
core/fxge/fontdata/chromefontdata/FoxitFixedBold.cpp
core/fxge/fontdata/chromefontdata/FoxitFixedBoldItalic.cpp
core/fxge/fontdata/chromefontdata/FoxitFixedItalic.cpp
core/fxge/fontdata/chromefontdata/FoxitSans.cpp
core/fxge/fontdata/chromefontdata/FoxitSansBold.cpp
core/fxge/fontdata/chromefontdata/FoxitSansBoldItalic.cpp
core/fxge/fontdata/chromefontdata/FoxitSansItalic.cpp
core/fxge/fontdata/chromefontdata/FoxitSansMM.cpp
core/fxge/fontdata/chromefontdata/FoxitSerif.cpp
core/fxge/fontdata/chromefontdata/FoxitSerifBold.cpp
core/fxge/fontdata/chromefontdata/FoxitSerifBoldItalic.cpp
core/fxge/fontdata/chromefontdata/FoxitSerifItalic.cpp
core/fxge/fontdata/chromefontdata/FoxitSerifMM.cpp
core/fxge/fontdata/chromefontdata/FoxitSymbol.cpp
core/fxge/freetype/fx_freetype.cpp
core/fxge/fx_font.cpp
core/fxge/renderdevicedriver_iface.cpp
core/fxge/scoped_font_transform.cpp
core/fxge/text_char_pos.cpp
core/fxge/text_glyph_pos.cpp
$<${LINUX}:core/fxge/linux/fx_linux_impl.cpp>
$<${MAC}:core/fxge/apple/fx_apple_impl.cpp>
$<${MAC}:core/fxge/apple/fx_apple_platform.cpp>
$<${MAC}:core/fxge/apple/fx_quartz_device.cpp>
$<${WIN}:core/fxge/cfx_windowsrenderdevice.cpp>
$<${WIN}:core/fxge/win32/cfx_psfonttracker.cpp>
$<${WIN}:core/fxge/win32/cfx_psrenderer.cpp>
$<${WIN}:core/fxge/win32/cgdi_device_driver.cpp>
$<${WIN}:core/fxge/win32/cgdi_display_driver.cpp>
$<${WIN}:core/fxge/win32/cgdi_plus_ext.cpp>
$<${WIN}:core/fxge/win32/cgdi_printer_driver.cpp>
$<${WIN}:core/fxge/win32/cps_printer_driver.cpp>
$<${WIN}:core/fxge/win32/cpsoutput.cpp>
$<${WIN}:core/fxge/win32/ctext_only_printer_driver.cpp>
$<${WIN}:core/fxge/win32/cwin32_platform.cpp>
fpdfsdk/cpdfsdk_annot.cpp
fpdfsdk/cpdfsdk_annotiteration.cpp
fpdfsdk/cpdfsdk_annotiterator.cpp
fpdfsdk/cpdfsdk_appstream.cpp
fpdfsdk/cpdfsdk_baannot.cpp
fpdfsdk/cpdfsdk_customaccess.cpp
fpdfsdk/cpdfsdk_filewriteadapter.cpp
fpdfsdk/cpdfsdk_formfillenvironment.cpp
fpdfsdk/cpdfsdk_helpers.cpp
fpdfsdk/cpdfsdk_interactiveform.cpp
fpdfsdk/cpdfsdk_pageview.cpp
fpdfsdk/cpdfsdk_pauseadapter.cpp
fpdfsdk/cpdfsdk_renderpage.cpp
fpdfsdk/cpdfsdk_widget.cpp
fpdfsdk/fpdf_annot.cpp
fpdfsdk/fpdf_attachment.cpp
fpdfsdk/fpdf_catalog.cpp
fpdfsdk/fpdf_dataavail.cpp
fpdfsdk/fpdf_doc.cpp
fpdfsdk/fpdf_editimg.cpp
fpdfsdk/fpdf_editpage.cpp
fpdfsdk/fpdf_editpath.cpp
fpdfsdk/fpdf_edittext.cpp
fpdfsdk/fpdf_ext.cpp
fpdfsdk/fpdf_flatten.cpp
fpdfsdk/fpdf_formfill.cpp
fpdfsdk/fpdf_javascript.cpp
fpdfsdk/fpdf_ppo.cpp
fpdfsdk/fpdf_progressive.cpp
fpdfsdk/fpdf_save.cpp
fpdfsdk/fpdf_searchex.cpp
fpdfsdk/fpdf_signature.cpp
fpdfsdk/fpdf_structtree.cpp
fpdfsdk/fpdf_sysfontinfo.cpp
fpdfsdk/fpdf_text.cpp
fpdfsdk/fpdf_thumbnail.cpp
fpdfsdk/fpdf_transformpage.cpp
fpdfsdk/fpdf_view.cpp
fpdfsdk/formfiller/cffl_button.cpp
fpdfsdk/formfiller/cffl_checkbox.cpp
fpdfsdk/formfiller/cffl_combobox.cpp
fpdfsdk/formfiller/cffl_fieldaction.cpp
fpdfsdk/formfiller/cffl_formfield.cpp
fpdfsdk/formfiller/cffl_interactiveformfiller.cpp
fpdfsdk/formfiller/cffl_listbox.cpp
fpdfsdk/formfiller/cffl_perwindowdata.cpp
fpdfsdk/formfiller/cffl_pushbutton.cpp
fpdfsdk/formfiller/cffl_radiobutton.cpp
fpdfsdk/formfiller/cffl_textfield.cpp
fpdfsdk/formfiller/cffl_textobject.cpp
fpdfsdk/pwl/cpwl_button.cpp
fpdfsdk/pwl/cpwl_caret.cpp
fpdfsdk/pwl/cpwl_cbbutton.cpp
fpdfsdk/pwl/cpwl_cblistbox.cpp
fpdfsdk/pwl/cpwl_combo_box.cpp
fpdfsdk/pwl/cpwl_edit.cpp
fpdfsdk/pwl/cpwl_edit_impl.cpp
fpdfsdk/pwl/cpwl_list_box.cpp
fpdfsdk/pwl/cpwl_list_ctrl.cpp
fpdfsdk/pwl/cpwl_sbbutton.cpp
fpdfsdk/pwl/cpwl_scroll_bar.cpp
fpdfsdk/pwl/cpwl_special_button.cpp
fpdfsdk/pwl/cpwl_wnd.cpp
fxjs/cjs_event_context_stub.cpp
fxjs/cjs_runtimestub.cpp
fxjs/ijs_runtime.cpp
third_party/agg23/agg_curves.cpp
third_party/agg23/agg_path_storage.cpp
third_party/agg23/agg_rasterizer_scanline_aa.cpp
third_party/agg23/agg_vcgen_dash.cpp
third_party/agg23/agg_vcgen_stroke.cpp
third_party/abseil-cpp/absl/base/internal/cycleclock.cc
third_party/abseil-cpp/absl/base/internal/spinlock.cc
third_party/abseil-cpp/absl/base/internal/sysinfo.cc
third_party/abseil-cpp/absl/base/internal/thread_identity.cc
third_party/abseil-cpp/absl/base/internal/unscaledcycleclock.cc
third_party/abseil-cpp/absl/base/log_severity.cc
third_party/abseil-cpp/absl/base/internal/low_level_alloc.cc
third_party/abseil-cpp/absl/base/internal/raw_logging.cc
third_party/abseil-cpp/absl/base/internal/spinlock_wait.cc
third_party/abseil-cpp/absl/base/internal/strerror.cc
third_party/abseil-cpp/absl/base/internal/throw_delegate.cc
third_party/abseil-cpp/absl/base/internal/tracing.cc
third_party/abseil-cpp/absl/container/internal/hashtablez_sampler.cc
third_party/abseil-cpp/absl/container/internal/hashtablez_sampler_force_weak_definition.cc
third_party/abseil-cpp/absl/container/internal/raw_hash_set.cc
third_party/abseil-cpp/absl/crc/internal/cpu_detect.cc
third_party/abseil-cpp/absl/crc/crc32c.cc
third_party/abseil-cpp/absl/crc/internal/crc_memcpy_fallback.cc
third_party/abseil-cpp/absl/crc/internal/crc_memcpy_x86_arm_combined.cc
third_party/abseil-cpp/absl/crc/internal/crc_non_temporal_memcpy.cc
third_party/abseil-cpp/absl/crc/internal/crc_cord_state.cc
third_party/abseil-cpp/absl/crc/internal/crc.cc
third_party/abseil-cpp/absl/crc/internal/crc_x86_arm_combined.cc
third_party/abseil-cpp/absl/debugging/internal/address_is_readable.cc
third_party/abseil-cpp/absl/debugging/internal/elf_mem_image.cc
third_party/abseil-cpp/absl/debugging/internal/vdso_support.cc
third_party/abseil-cpp/absl/debugging/internal/decode_rust_punycode.cc
third_party/abseil-cpp/absl/debugging/internal/demangle.cc
third_party/abseil-cpp/absl/debugging/internal/demangle_rust.cc
third_party/abseil-cpp/absl/debugging/internal/examine_stack.cc
third_party/abseil-cpp/absl/debugging/failure_signal_handler.cc
third_party/abseil-cpp/absl/debugging/stacktrace.cc
third_party/abseil-cpp/absl/debugging/symbolize.cc
third_party/abseil-cpp/absl/debugging/internal/utf8_for_code_point.cc
third_party/abseil-cpp/absl/hash/internal/city.cc
third_party/abseil-cpp/absl/hash/internal/hash.cc
third_party/abseil-cpp/absl/hash/internal/low_level_hash.cc
third_party/abseil-cpp/absl/log/die_if_null.cc
third_party/abseil-cpp/absl/log/globals.cc
third_party/abseil-cpp/absl/log/initialize.cc
third_party/abseil-cpp/absl/log/log_entry.cc
third_party/abseil-cpp/absl/log/log_sink.cc
third_party/abseil-cpp/absl/log/internal/check_op.cc
third_party/abseil-cpp/absl/log/internal/conditions.cc
third_party/abseil-cpp/absl/log/internal/fnmatch.cc
third_party/abseil-cpp/absl/log/internal/log_format.cc
third_party/abseil-cpp/absl/log/internal/globals.cc
third_party/abseil-cpp/absl/log/internal/log_message.cc
third_party/abseil-cpp/absl/log/internal/log_sink_set.cc
third_party/abseil-cpp/absl/log/internal/nullguard.cc
third_party/abseil-cpp/absl/log/internal/proto.cc
third_party/abseil-cpp/absl/log/internal/vlog_config.cc
third_party/abseil-cpp/absl/numeric/int128.cc
third_party/abseil-cpp/absl/profiling/internal/exponential_biased.cc
third_party/abseil-cpp/absl/random/discrete_distribution.cc
third_party/abseil-cpp/absl/random/gaussian_distribution.cc
third_party/abseil-cpp/absl/random/seed_gen_exception.cc
third_party/abseil-cpp/absl/random/seed_sequences.cc
third_party/abseil-cpp/absl/random/internal/randen_round_keys.cc
third_party/abseil-cpp/absl/random/internal/pool_urbg.cc
third_party/abseil-cpp/absl/random/internal/randen.cc
third_party/abseil-cpp/absl/random/internal/randen_detect.cc
third_party/abseil-cpp/absl/random/internal/randen_hwaes.cc
third_party/abseil-cpp/absl/random/internal/randen_slow.cc
third_party/abseil-cpp/absl/random/internal/seed_material.cc
third_party/abseil-cpp/absl/status/internal/status_internal.cc
third_party/abseil-cpp/absl/status/status.cc
third_party/abseil-cpp/absl/status/status_payload_printer.cc
third_party/abseil-cpp/absl/status/statusor.cc
third_party/abseil-cpp/absl/strings/cord.cc
third_party/abseil-cpp/absl/strings/cord_analysis.cc
third_party/abseil-cpp/absl/strings/cord_buffer.cc
third_party/abseil-cpp/absl/strings/internal/cord_internal.cc
third_party/abseil-cpp/absl/strings/internal/cord_rep_btree.cc
third_party/abseil-cpp/absl/strings/internal/cord_rep_btree_navigator.cc
third_party/abseil-cpp/absl/strings/internal/cord_rep_btree_reader.cc
third_party/abseil-cpp/absl/strings/internal/cord_rep_consume.cc
third_party/abseil-cpp/absl/strings/internal/cord_rep_crc.cc
third_party/abseil-cpp/absl/strings/internal/cordz_functions.cc
third_party/abseil-cpp/absl/strings/internal/cordz_handle.cc
third_party/abseil-cpp/absl/strings/internal/cordz_info.cc
third_party/abseil-cpp/absl/strings/internal/escaping.cc
third_party/abseil-cpp/absl/strings/internal/ostringstream.cc
third_party/abseil-cpp/absl/strings/internal/utf8.cc
third_party/abseil-cpp/absl/strings/internal/str_format/arg.cc
third_party/abseil-cpp/absl/strings/internal/str_format/bind.cc
third_party/abseil-cpp/absl/strings/internal/str_format/extension.cc
third_party/abseil-cpp/absl/strings/internal/str_format/float_conversion.cc
third_party/abseil-cpp/absl/strings/internal/str_format/output.cc
third_party/abseil-cpp/absl/strings/internal/str_format/parser.cc
third_party/abseil-cpp/absl/strings/string_view.cc
third_party/abseil-cpp/absl/strings/ascii.cc
third_party/abseil-cpp/absl/strings/charconv.cc
third_party/abseil-cpp/absl/strings/escaping.cc
third_party/abseil-cpp/absl/strings/internal/charconv_bigint.cc
third_party/abseil-cpp/absl/strings/internal/charconv_parse.cc
third_party/abseil-cpp/absl/strings/internal/damerau_levenshtein_distance.cc
third_party/abseil-cpp/absl/strings/internal/memutil.cc
third_party/abseil-cpp/absl/strings/internal/stringify_sink.cc
third_party/abseil-cpp/absl/strings/match.cc
third_party/abseil-cpp/absl/strings/numbers.cc
third_party/abseil-cpp/absl/strings/str_cat.cc
third_party/abseil-cpp/absl/strings/str_replace.cc
third_party/abseil-cpp/absl/strings/str_split.cc
third_party/abseil-cpp/absl/strings/substitute.cc
third_party/abseil-cpp/absl/synchronization/internal/graphcycles.cc
third_party/abseil-cpp/absl/synchronization/internal/kernel_timeout.cc
third_party/abseil-cpp/absl/synchronization/barrier.cc
third_party/abseil-cpp/absl/synchronization/blocking_counter.cc
third_party/abseil-cpp/absl/synchronization/internal/create_thread_identity.cc
third_party/abseil-cpp/absl/synchronization/internal/futex_waiter.cc
third_party/abseil-cpp/absl/synchronization/internal/per_thread_sem.cc
third_party/abseil-cpp/absl/synchronization/internal/pthread_waiter.cc
third_party/abseil-cpp/absl/synchronization/internal/sem_waiter.cc
third_party/abseil-cpp/absl/synchronization/internal/stdcpp_waiter.cc
third_party/abseil-cpp/absl/synchronization/internal/waiter_base.cc
third_party/abseil-cpp/absl/synchronization/internal/win32_waiter.cc
third_party/abseil-cpp/absl/synchronization/mutex.cc
third_party/abseil-cpp/absl/synchronization/notification.cc
third_party/abseil-cpp/absl/time/civil_time.cc
third_party/abseil-cpp/absl/time/clock.cc
third_party/abseil-cpp/absl/time/duration.cc
third_party/abseil-cpp/absl/time/format.cc
third_party/abseil-cpp/absl/time/time.cc
third_party/abseil-cpp/absl/time/internal/cctz/src/civil_time_detail.cc
third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_fixed.cc
third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_format.cc
third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_if.cc
third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_impl.cc
third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_info.cc
third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_libc.cc
third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_lookup.cc
third_party/abseil-cpp/absl/time/internal/cctz/src/time_zone_posix.cc
third_party/abseil-cpp/absl/time/internal/cctz/src/zone_info_source.cc
third_party/abseil-cpp/absl/types/bad_optional_access.cc
third_party/abseil-cpp/absl/types/bad_variant_access.cc
)

target_include_directories(pdfium
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/public> # For the top level projects.
        $<INSTALL_INTERFACE:include> # For the external projects.
    )

target_link_libraries(pdfium
    openjp2
    JPEG::JPEG
    ${BZIP2_LIBRARIES}
    Freetype::Freetype
    ICU::uc
)

file(GLOB_RECURSE PUB_HEADERS "public/*.h" "public/cpp/*.h")

set_property(TARGET pdfium
    PROPERTY PUBLIC_HEADER ${PUB_HEADERS})

install(
    TARGETS pdfium
    EXPORT pdfiumTargets
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    PUBLIC_HEADER DESTINATION include
    )

install(EXPORT pdfiumTargets
        FILE "pdfiumTargets.cmake"
        NAMESPACE PDFIUM::
        DESTINATION share/pdfium
        )
