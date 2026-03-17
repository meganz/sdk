# docker build --build-arg DISTRO=<DISTRO> --build-arg ARCH=<ARCH> --build-arg CMAKE_PRESET=<CMAKE_PRESET> -f /path/to/linux-build.dockerfile -t sdk-linux-build .
#
# Where <DISTRO> is one of the following:
# - debian:11
# - debian:12
# - debian:13
# - debian:testing
# - archlinux
# - almalinux:9
# - centos:stream9 (or quay.io/centos/centos:stream9)
# - opensuse/leap:15.6
# - opensuse/leap:16.0
# - opensuse/tumbleweed
# - ubuntu:22.04
# - ubuntu:24.04
# - ubuntu:25.04
# - ubuntu:25.10
# - fedora:42
# - fedora:43
#
# Where <ARCH> is one of the following:
# - x64 (default)
#
# Where <CMAKE_PRESET> is one of the following:
# - dev-unix (default)
#
# docker run -v /path/to/sdk:/mega/sdk sdk-linux-build

# Define global args only visible to "FROM" directive(s)
ARG DISTRO=ubuntu:22.04

# Map centos:stream* to quay.io namespace at pull time.
FROM ${DISTRO/*centos:stream/quay.io/centos/centos:stream}

# Define args visible in current "FROM" directive
# Redeclaring DISTRO without a value inherits the global default
ARG DISTRO
ARG CMAKE_PRESET=dev-unix
ARG ARCH=x64
ENV ARCH=$ARCH
ENV DISTRO=$DISTRO
ENV CMAKE_PRESET=$CMAKE_PRESET
ENV SDK_BUILD_ENV_FILE=/mega/sdk-build.env

WORKDIR /mega

RUN bash -euo pipefail <<'EOF'
echo "Building ${ARCH} architecture for ${DISTRO}"
SDK_BUILD_PATH_PREFIX=""
SDK_BUILD_CC=""
SDK_BUILD_CXX=""
SDK_BUILD_CMAKE_ARGS=""
PACKAGES="autoconf autoconf-archive ca-certificates curl git gzip nasm pkg-config python3 tar unzip zip"

case "${DISTRO}" in
    'opensuse'*)
        zypper -n --gpg-auto-import-keys refresh
        zypper -n update
        zypper -n install shadow gcc gcc-c++ openssh which findutils rpm-build git
        zypper -n install -t pattern devel_C_C++

        if [ "$DISTRO" = "opensuse/leap:16.0" ] || [ "$DISTRO" = "opensuse/tumbleweed" ]; then
            zypper -n remove zlib-ng-compat-devel || true
        fi

        PACKAGES="${PACKAGES} automake awk gcc-c++ cmake libtool make"
        if [ "$DISTRO" = "opensuse/leap:15.6" ]; then
            PACKAGES="${PACKAGES} python311"
        elif [ "$DISTRO" = "opensuse/leap:16.0" ] || [ "$DISTRO" = "opensuse/tumbleweed" ]; then
            PACKAGES="${PACKAGES} python313"
        fi
 
        if [ "$DISTRO" = "opensuse/leap:15.6" ]; then
            PACKAGES="${PACKAGES} gcc14 gcc14-c++"
            SDK_BUILD_CC="gcc-14"
            SDK_BUILD_CXX="g++-14"
            SDK_BUILD_CMAKE_ARGS="-DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14"
        fi

        zypper install -y --force-resolution ${PACKAGES}
        
        if [ "$DISTRO" = "opensuse/leap:15.6" ]; then
            mkdir -p /mega/python311
            ln -sf /usr/bin/python3.11 /mega/python311/python3
            SDK_BUILD_PATH_PREFIX="/mega/python311"
        elif [ "$DISTRO" = "opensuse/leap:16.0" ] || [ "$DISTRO" = "opensuse/tumbleweed" ]; then
            mkdir -p /mega/python313
            ln -sf /usr/bin/python3.13 /mega/python313/python3
            SDK_BUILD_PATH_PREFIX="/mega/python313"
        fi
    ;;
    'ubuntu'*|'debian'*)
        PACKAGES="$PACKAGES build-essential dh-autoreconf"
        export DEBCONF_NOWARNINGS=yes
        export DEBIAN_FRONTEND=noninteractive
        PACKAGES="${PACKAGES} cmake"
        
        apt-get update
        apt-get install -y ${PACKAGES}

        if [ "$DISTRO" = "debian:11" ]; then
            CMAKE_VERSION=3.20.2
            EXPECTED_CMAKE_INSTALLER_SHA256="ea497b4658816010e5850a3ed53845e430654640aabbe10d93fe67def9503e4d"
            CMAKE_INSTALLER=cmake-${CMAKE_VERSION}-linux-x86_64.sh
            curl -fsSL https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/${CMAKE_INSTALLER} -o cmake.sh
            echo "${EXPECTED_CMAKE_INSTALLER_SHA256}  cmake.sh" | sha256sum -c || {
                echo "unexpected checksum for ${CMAKE_INSTALLER}!"
                exit 1
            }
            chmod u+x cmake.sh
            ./cmake.sh --prefix=/usr --exclude-subdir --skip-license
        fi
    ;;
    'archlinux'*)
        PACKAGES="${PACKAGES} base-devel cmake"
        pacman --noconfirm -Syu --needed ${PACKAGES}
    ;;
    'fedora'*)
        dnf -y group install "development-tools"
        dnf -y remove zlib-ng-compat-devel || true
        dnf -y install which findutils rpm-build
        PACKAGES="${PACKAGES} gcc-c++ perl automake libtool awk cmake"
        # Until the VCPKG one is fixed, we need to install patchelf manually
        PACKAGES="${PACKAGES} patchelf"
        dnf install -y ${PACKAGES}
    ;;
    'almalinux'*|'centos:stream'*|'quay.io/centos/centos:stream'*)
        # EL9 derivatives need EPEL + CRB to provide nasm/patchelf.
        dnf -y groupinstall "Development Tools"
        dnf install -y epel-release dnf-plugins-core
        dnf config-manager --set-enabled crb
        dnf -y makecache
        PACKAGES="${PACKAGES//curl/curl-minimal}"
        PACKAGES="${PACKAGES//pkg-config/pkgconf-pkg-config}"
        PACKAGES="${PACKAGES} gcc gcc-c++ make perl automake libtool gawk cmake nasm patchelf"
        dnf install -y ${PACKAGES}
    ;;
    *)
        echo "Error. ${DISTRO} is unknown or unsupported."
        exit 1
esac

# persistent SDK build environment settings
{
    if [ -n "${SDK_BUILD_PATH_PREFIX}" ]; then
        echo "export PATH=\"${SDK_BUILD_PATH_PREFIX}:\$PATH\""
    fi
    if [ -n "${SDK_BUILD_CC}" ]; then
        echo "export CC=\"${SDK_BUILD_CC}\""
    fi
    if [ -n "${SDK_BUILD_CXX}" ]; then
        echo "export CXX=\"${SDK_BUILD_CXX}\""
    fi
    if [ -n "${SDK_BUILD_CMAKE_ARGS}" ]; then
        echo "export SDK_BUILD_CMAKE_ARGS=\"${SDK_BUILD_CMAKE_ARGS}\""
    fi
} > "${SDK_BUILD_ENV_FILE}"

EOF

RUN git clone https://github.com/microsoft/vcpkg.git
CMD ["/bin/sh", "-c", "\
        echo \"ARCH=$ARCH  DISTRO=$DISTRO  CMAKE_PRESET=$CMAKE_PRESET\" && \
        cat \"$SDK_BUILD_ENV_FILE\" && \
        . \"$SDK_BUILD_ENV_FILE\" && \
        cmake \
            --preset $CMAKE_PRESET \
            $SDK_BUILD_CMAKE_ARGS \
            -DVCPKG_ROOT=vcpkg \
            -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -S sdk \
            -B build && \
        cmake --build build -j $(nproc) \
    "]
