#!/bin/bash

set -e

function printUsage() {
    echo ""
    echo " -- PDFium Build Script for Linux -- "
    echo ""
    echo " Manage downloading and building PDFium for Linux."
    echo ""
    echo " It will optionally build and download pdfium sources and create a tarball"
    echo ""
    echo " Usage: "
    echo ""
    echo " $0 [-h] [-p <work_path>] [-d] [-b] [-i <path>]"
    echo ""
    echo ""
    echo " work_path: Optional. Defines where to download, build sources and left the tarball."
    echo " -h: Print help message and exits."
    echo " -p: Optional working directory. It defaults to the current path."
    echo " -d: Only download sources and create tarball."
    echo " -b: Builds pdfium. Implies -d if not executed previously."
    echo " -i: Installs pdfium in the desired path. /usr is the default one if no other is indicated. Ensure you have permissions to install in the destination path."
    echo "     Implies -b if not executed previously."
    echo ""
}

PDFium_BRANCH=chromium/5247
download_pdfium=0
build_pdfium=0
install_pdfium=0
install_path=/usr
BASEDIR="$PWD"

while getopts ':hp:dbi' OPTION; do
    case $OPTION in
    h)
        printUsage
        exit
    ;;
    p)
        BASEDIR="`readlink -f $OPTARG`"
    ;;
    d)
        download_pdfium=1
    ;;
    b)
        build_pdfium=1
    ;;
    i)
        install_pdfium=1
        if [ "${@:$OPTIND}" ] && ! `echo "${@:$OPTIND}" | grep "^-" >/dev/null 2>&1`; then
            install_path="`readlink -f ${@:$OPTIND}`"
            OPTIND=$(($OPTIND + 1))
        fi
    ;;
    ?)
        printUsage
        exit 2
    ;;
    esac
done
shift $(($OPTIND - 1))

if [ ! -d "$BASEDIR" ]; then
    echo
    echo "$BASEDIR is not a valid directory."
    echo
    exit 1
fi

if [ $download_pdfium -eq 0 -a $build_pdfium -eq 0 -a $install_pdfium -eq 0 ]; then
    printUsage
    echo "Nothing to do!"
    echo ""
    exit 0
fi

CURDIR="$PWD"
outputDIR_NAME="pdfium-mega-${PDFium_BRANCH/chromium\//}.0"
outputTAR_NAME="pdfium-mega_${PDFium_BRANCH/chromium\//}.0"
BASEDIR="${BASEDIR}/workspace"
DEPOT_TOOLS_DIR="${BASEDIR}/depot_tools"
PDFIUMDIR="${BASEDIR}/pdfium"
BUILD_DIR="${BASEDIR}/build"
TARBALL_PKG_DIR="${BASEDIR}/${outputDIR_NAME}"
TARBALL_SRCS_DIR="${BASEDIR}/${outputDIR_NAME}/${outputDIR_NAME}"
DepotTools_URL='https://chromium.googlesource.com/chromium/tools/depot_tools.git'
PDFium_URL='https://pdfium.googlesource.com/pdfium.git'

# Check that we have the previous step or force it.
if [ $install_pdfium -eq 1 ] && [ $build_pdfium -eq 0 ] && [ ! -f "${BASEDIR}/build.success" ]; then
    echo "No compiled binaries detected, forcing -b (build) option."
    build_pdfium=1
fi

if [ $build_pdfium -eq 1 ] && [ $download_pdfium -eq 0 ] && [ ! -f "${BASEDIR}"/../"${outputTAR_NAME}.tar.gz" ]; then
    echo "No sources detected, forcing -d (download) option."
    download_pdfium=1
fi


if [ $download_pdfium -eq 1 ];then

    echo
    echo "* Download pdfium sources and create tarball."
    echo

    echo "** Preparing directories..."
    rm -f "${BASEDIR}/../${outputTAR_NAME}.tar.gz"
    rm -rf "${TARBALL_PKG_DIR}"
    mkdir -p "${BASEDIR}"

    # Download depot_tools if not present in this location
    echo -n "** Getting depot tools..."
    if [ ! -d "${DEPOT_TOOLS_DIR}" ] || [ ! -f "${BASEDIR}"/depot_tools.success ]; then
        echo
        rm -rf "${DEPOT_TOOLS_DIR}"
        git clone "$DepotTools_URL" "${DEPOT_TOOLS_DIR}"
        touch "${BASEDIR}"/depot_tools.success
    else
        echo " Already downloaded."
    fi
    export PATH="$DEPOT_TOOLS_DIR:$PATH"

    # Get pdfium sources if not present. Check branch also.
    echo -n "** Getting PDFium sources..."
    if [ ! -d "${PDFIUMDIR}" ] || [ "${PDFium_BRANCH}" != "`cat \"${BASEDIR}\"/pdfium.success 2>/dev/null`" ]; then
        echo
        rm -rf "${PDFIUMDIR}"
        rm -rf "${BASEDIR}"/pdfium.success
        cd "${BASEDIR}"
        gclient config --unmanaged "$PDFium_URL"
        gclient sync

        # Checkout branch
        echo "** Checking out ${PDFium_BRANCH} branch..."
        git -C pdfium checkout "${PDFium_BRANCH}"
        gclient sync -D

        # Generate Ninja files
        echo "** Generating Ninja files..."
        cd "${PDFIUMDIR}"
        gn gen "out" --args='is_debug=false use_sysroot=false use_custom_libcxx=false pdf_is_complete_lib=true pdf_is_standalone=true pdf_enable_v8=false pdf_enable_xfa=false pdf_use_skia=false pdf_use_skia_paths=false pdf_bundle_freetype=true symbol_level=0 use_goma=false is_component_build=false clang_use_chrome_plugins=false'

        echo "${PDFium_BRANCH}" > "${BASEDIR}"/pdfium.success

    else
        echo " Already downloaded."
    fi

    echo "** Preparing sources package..."
    mkdir -p "${TARBALL_SRCS_DIR}"
    cd "${BASEDIR}"
    cp  -p --parents depot_tools/gn* ${TARBALL_SRCS_DIR}/
    cp  -p --parents depot_tools/*.py ${TARBALL_SRCS_DIR}/
    cp  -p --parents depot_tools/ninja ${TARBALL_SRCS_DIR}/
    cp  -p --parents depot_tools/ninja-linux64 ${TARBALL_SRCS_DIR}/
    cd ${PDFIUMDIR}
    cp -rp --parents fxjs ${TARBALL_SRCS_DIR}/
    cp -rp --parents fxbarcode ${TARBALL_SRCS_DIR}/
    cp -rp --parents out ${TARBALL_SRCS_DIR}/
    cp -rp --parents constants ${TARBALL_SRCS_DIR}/
    cp -rp --parents fpdfsdk ${TARBALL_SRCS_DIR}/
    cp -rp --parents tools/clang/scripts/update.py ${TARBALL_SRCS_DIR}/
    cp -rp --parents build_overrides ${TARBALL_SRCS_DIR}/
    cp -rp --parents buildtools ${TARBALL_SRCS_DIR}/
    cp -rp --parents core ${TARBALL_SRCS_DIR}/
    cp  -p --parents public/*.h ${TARBALL_SRCS_DIR}/
    cp  -p --parents public/cpp/*.h ${TARBALL_SRCS_DIR}/
    cp -rp --parents build/config ${TARBALL_SRCS_DIR}/
    cp -rp --parents build/toolchain ${TARBALL_SRCS_DIR}/
    cp -rp --parents build/win ${TARBALL_SRCS_DIR}/
    cp  -p --parents build/*.gni ${TARBALL_SRCS_DIR}/
    cp  -p --parents build/*.py ${TARBALL_SRCS_DIR}/
    cp  -p --parents build/*.h ${TARBALL_SRCS_DIR}/
    cp  -p --parents build/BUILD.gn ${TARBALL_SRCS_DIR}/

    cp -rp --parents third_party/libopenjpeg ${TARBALL_SRCS_DIR}/
    cp -rp --parents third_party/agg23 ${TARBALL_SRCS_DIR}/
    cp -rp --parents third_party/libpng16 ${TARBALL_SRCS_DIR}/
    cp -rp --parents third_party/jinja2 ${TARBALL_SRCS_DIR}/
    cp -rp --parents third_party/googletest ${TARBALL_SRCS_DIR}/
    cp -rp --parents third_party/llvm-build ${TARBALL_SRCS_DIR}/
    cp -rp --parents third_party/base ${TARBALL_SRCS_DIR}/
    cp -rp --parents third_party/markupsafe ${TARBALL_SRCS_DIR}/
    cp -rp --parents third_party/freetype ${TARBALL_SRCS_DIR}/
    cp -rp --parents third_party/skia_shared ${TARBALL_SRCS_DIR}/
    cp -rp --parents third_party/nasm ${TARBALL_SRCS_DIR}/
    cp -rp --parents third_party/libtiff ${TARBALL_SRCS_DIR}/
    cp -rp --parents third_party/zlib ${TARBALL_SRCS_DIR}/
    cp -rp --parents third_party/lcms ${TARBALL_SRCS_DIR}/
    cp -rp --parents third_party/abseil-cpp ${TARBALL_SRCS_DIR}/
    cp -rp --parents third_party/NotoSansCJK ${TARBALL_SRCS_DIR}/
    cp -rp --parents third_party/icu ${TARBALL_SRCS_DIR}/
    cp -rp --parents third_party/libjpeg_turbo ${TARBALL_SRCS_DIR}/
    cp -rp --parents third_party/bigint ${TARBALL_SRCS_DIR}/
    cp  -p --parents third_party/BUILD.gn third_party/DEPS ${TARBALL_SRCS_DIR}/

    cp  -p --parents testing/test.gni testing/image_diff/BUILD.gn testing/fuzzers/BUILD.gn testing/gmock/BUILD.gn testing/BUILD.gn testing/gtest/BUILD.gn ${TARBALL_SRCS_DIR}/
    cp  -p --parents third_party/test_fonts/BUILD.gn ${TARBALL_SRCS_DIR}/
    cp  -p --parents samples/BUILD.gn ${TARBALL_SRCS_DIR}/
    cp  -p --parents skia/BUILD.gn ${TARBALL_SRCS_DIR}/

    cp  -p --parents pdfium.gni BUILD.gn .gn ${TARBALL_SRCS_DIR}/

    cd "${TARBALL_PKG_DIR}"
    tar czf "${outputTAR_NAME}.tar.gz" --exclude-vcs "${outputDIR_NAME}"
    rm -rf "${TARBALL_SRCS_DIR}"

    # Add packaging scripts.
    cp "${CURDIR}"/packaging/* "${TARBALL_PKG_DIR}"

    # Create a final tarball with all the required files.
    cd "${BASEDIR}"
    tar czf "${outputTAR_NAME}.tar.gz" "${outputDIR_NAME}"
    rm -rf "${TARBALL_PKG_DIR}"
    mv "${outputTAR_NAME}.tar.gz" "${BASEDIR}"/../

    echo "** Tarball ready in `readlink -f ${BASEDIR}/../${outputTAR_NAME}.tar.gz`"

fi


if [ $build_pdfium -eq 1 ];then
    echo
    echo "* Build pdfium"
    echo

    echo "** Preparing directories..."
    rm -rf ${BUILD_DIR}
    rm -rf "${BASEDIR}"/build.success
    mkdir -p "${BUILD_DIR}"

    echo "** Unpacking sources..."
    tar xzf "${BASEDIR}"/../"${outputTAR_NAME}.tar.gz" -C "${BUILD_DIR}"
    cd "${BUILD_DIR}/${outputDIR_NAME}"
    tar xzf "${outputTAR_NAME}.tar.gz"
    cd "${outputDIR_NAME}"

    echo "** Start building pdfium..."
    ./depot_tools/ninja -C out pdfium

    echo "** Cleaning after build..."
    rm -rf `ls | grep -v "out\|public"`
    touch "${BASEDIR}"/build.success

    echo "** PDFium built in ${BUILD_DIR}/${outputDIR_NAME}/${outputDIR_NAME}"

fi


if [ $install_pdfium -eq 1 ];then
    echo
    echo "* Install pdfium"
    echo

    install -D "${BUILD_DIR}/${outputDIR_NAME}/${outputDIR_NAME}/out/obj/libpdfium.a" "${install_path}"/lib/libpdfium.a
    for i in `find "${BUILD_DIR}/${outputDIR_NAME}/${outputDIR_NAME}/public" -type f`; do install -D $i "${install_path}"/include/"${i#*public}" ;done

    echo "** PDFium installed in ${install_path}"

fi
