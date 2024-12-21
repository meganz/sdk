#!/bin/sh

set -e

COMMIT=93081d594f7efff72958a79251f53731b99e902b
ARCHS="arm64 x64 arm64-simulator"
LIPO_COMMAND="lipo -create"
LIBWEBRTC_A="libwebrtc.a"
ADDITIONAL_LIBS="libnative_api.a libnative_video.a libvideocapture_objc.a libvideoframebuffer_objc.a"
FOLDERS_TO_REMOVE="src crypto buildtools tools test out resources tools_webrtc ios testing audio build_overrides examples depot_tools lib rtc_tools build data style-guide base stats common_audio third_party/blink third_party/libjpeg_turbo third_party/ply third_party/hamcrest third_party/iccjpeg third_party/yasm third_party/libjingle_xmpp third_party/arcore-android-sdk third_party/ink third_party/ffmpeg third_party/gif_player third_party/android_platform third_party/webxr_test_pages third_party/pyjson5 third_party/crc32c third_party/mozilla third_party/netty-tcnative third_party/markupsafe third_party/checkstyle third_party/ow2_asm third_party/harfbuzz-ng third_party/llvm-build third_party/mocha third_party/pylint third_party/gvr-android-sdk third_party/shaderc third_party/qcms third_party/widevine third_party/libXNVCtrl third_party/libsrtp third_party/gson third_party/wayland-protocols third_party/blanketjs third_party/isimpledom third_party/libFuzzer third_party/libjpeg third_party/libpng third_party/android_media third_party/libxml third_party/instrumented_libraries third_party/libxslt third_party/mako third_party/jsoncpp third_party/libsync third_party/spirv-cross third_party/chromevox third_party/leveldatabase third_party/libaom third_party/opus third_party/freetype third_party/microsoft_webauthn third_party/ashmem third_party/tcmalloc third_party/grpc third_party/openxr third_party/guava third_party/crashpad third_party/wds third_party/fuchsia-sdk third_party/junit third_party/jinja2 third_party/openvr third_party/auto third_party/jsr-305 third_party/android_sdk third_party/expat third_party/icu4j third_party/jdk third_party/markdown third_party/brotli third_party/SPIRV-Tools third_party/objenesis third_party/decklink third_party/libaddressinput third_party/cacheinvalidation third_party/axe-core third_party/usb_ids third_party/colorama third_party/ub-uiautomator third_party/inspector_protocol third_party/pymock third_party/simplejson third_party/libdrm third_party/modp_b64 third_party/hunspell third_party/minizip third_party/android_crazy_linker third_party/apache-win32 third_party/webgl third_party/wtl third_party/polymer third_party/libevdev third_party/openscreen third_party/nasm third_party/libovr third_party/afl third_party/s2cellid third_party/binutils third_party/sfntly third_party/android_swipe_refresh third_party/android_deps third_party/netty4 third_party/cld_3 third_party/logilab third_party/sqlite third_party/r8 third_party/lcov third_party/minigbm third_party/google_input_tools third_party/flot third_party/google_android_play_core third_party/pystache third_party/zlib third_party/glslang third_party/apache-mac third_party/google_trust_services third_party/cct_dynamic_module third_party/tlslite third_party/android_support_test_runner third_party/fontconfig third_party/requests third_party/android_build_tools third_party/wayland third_party/jacoco third_party/errorprone third_party/apple_apsl third_party/libprotobuf-mutator third_party/adobe third_party/Python-Markdown third_party/javalang third_party/web-animations-js third_party/libwebp third_party/dom_distiller_js third_party/libwebm third_party/pywebsocket third_party/android_data_chart third_party/re2 third_party/pexpect third_party/libvpx third_party/libphonenumber third_party/sudden_motion_sensor third_party/snappy third_party/bouncycastle third_party/usrsctp third_party/win_build_output third_party/chaijs third_party/pycoverage third_party/webdriver third_party/gestures third_party/libcxx-pretty-printers third_party/feed third_party/d3 third_party/closure_compiler third_party/vulkan third_party/accessibility-audit third_party/bazel third_party/libsecret third_party/android_system_sdk third_party/google_appengine_cloudstorage third_party/android_protobuf third_party/unrar third_party/dav1d third_party/private-join-and-compute third_party/qunit third_party/apk-patch-size-estimator third_party/devscripts third_party/depot_tools third_party/googletest third_party/speech-dispatcher third_party/lottie third_party/sqlite4java third_party/spirv-headers third_party/robolectric third_party/proguard third_party/jstemplate third_party/iaccessible2 third_party/apache-portable-runtime third_party/rnnoise third_party/khronos third_party/woff2 third_party/mockito third_party/gradle_wrapper third_party/pffft third_party/flatbuffers third_party/arcore-android-sdk-client third_party/ots third_party/accessibility_test_framework third_party/motemplate third_party/openh264 third_party/glfw third_party/node third_party/smhasher third_party/google-truth third_party/gtest-parallel third_party/.git third_party/byte_buddy third_party/one_euro_filter third_party/v4l-utils third_party/google_toolbox_for_mac third_party/intellij third_party/emoji-segmenter third_party/libusb third_party/mesa_headers third_party/quic_trace third_party/android_opengl third_party/gvr-android-keyboard third_party/ocmock third_party/ced third_party/libudev third_party/breakpad third_party/catapult third_party/ijar third_party/metrics_proto third_party/protobuf third_party/sinonjs third_party/test_fonts third_party/espresso third_party/webrtc_overrides third_party/icu third_party/libipp third_party/custom_tabs_client third_party/xstream third_party/lzma_sdk third_party/bspatch third_party/material_design_icons third_party/protoc_javalite third_party/liblouis"

mkdir webrtc
pushd webrtc
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
DEPOT_TOOLS_PATH=$PWD/depot_tools/
export PATH=$PATH:$DEPOT_TOOLS_PATH
$DEPOT_TOOLS_PATH/fetch --nohooks webrtc_ios
pushd src

git checkout $COMMIT
$DEPOT_TOOLS_PATH/gclient sync

git apply ../../../../../patches/webRtcSocketIosPatch.patch

mkdir lib
pushd lib

for ARCH in $ARCHS
do

TARGET_CPU=$ARCH
TARGET_ENVIRONMENT=""
if [ "${ARCH}" == "arm64-simulator" ]; then
TARGET_CPU="arm64"
TARGET_ENVIRONMENT="simulator"
fi
echo $ARCH
echo $TARGET_ENVIRONMENT
echo $TARGET_CPU

$DEPOT_TOOLS_PATH/gn gen $ARCH --args='target_os="ios" target_environment="'$TARGET_ENVIRONMENT'" target_cpu="'$TARGET_CPU'" rtc_include_tests=false rtc_build_examples=false treat_warnings_as_errors=false fatal_linker_warnings=false use_custom_libcxx=false is_debug=false ios_deployment_target="13.0" rtc_build_tools=false rtc_enable_protobuf=false is_clang=true is_component_build=false ios_enable_code_signing=false'

pushd $ARCH
$DEPOT_TOOLS_PATH/ninja -C .
popd
done
LIPO_COMMAND="$LIPO_COMMAND $PWD/x64/obj/$LIBWEBRTC_A"
LIPO_COMMAND="$LIPO_COMMAND $PWD/arm64-simulator/obj/$LIBWEBRTC_A"
`$LIPO_COMMAND -output $LIBWEBRTC_A`
xcodebuild -create-xcframework -library $LIBWEBRTC_A -library $PWD/arm64/obj/$LIBWEBRTC_A -output ../../../xcframework/libwebrtc.xcframework

for ADDITIONAL_LIB in $ADDITIONAL_LIBS
do
LIPO_COMMAND="lipo -create"
LIPO_COMMAND="$LIPO_COMMAND $PWD/x64/obj/sdk/$ADDITIONAL_LIB"
LIPO_COMMAND="$LIPO_COMMAND $PWD/arm64-simulator/obj/sdk/$ADDITIONAL_LIB"
`$LIPO_COMMAND -output $ADDITIONAL_LIB`

xcodebuild -create-xcframework -library $ADDITIONAL_LIB -library $PWD/arm64/obj/sdk/$ADDITIONAL_LIB -output ../../../xcframework/${ADDITIONAL_LIB%.*}.xcframework
done

popd # lib -> src

popd # src -> webrtc

mv src/* .

for FOLDER_TO_REMOVE in $FOLDERS_TO_REMOVE
do
rm -rf $FOLDER_TO_REMOVE
done

find . -type f -not -name "*.h" -exec rm -f {} \;
find . -type d -depth -empty -delete

popd # webrtc -> 3rdparty
