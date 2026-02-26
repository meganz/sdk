# docker build --build-arg DISTRO=<DISTRO> --build-arg ARCH=<ARCH> -f /path/to/linux-build.dockerfile -t sdk-linux-build .
#
# Where <DISTRO> is one of the following:
# - debian:11
# - debian:12
# - debian:testing
# - opensuse/tumbleweed
# - ubuntu:20.04
# - ubuntu:22.04
# - ubuntu:24.04
# - ubuntu:24.10
#
# Where <ARCH> is one of the following:
# - x86
# - x64
# - arm64
#
# docker run -v /path/to/sdk:/mega/sdk sdk-linux-build

# Define global args only visible to "FROM" directive(s)
ARG DISTRO=ubuntu:22.04

FROM $DISTRO

# Define args visible in current "FROM" directive
# Redeclaring DISTRO without a value inherits the global default
ARG DISTRO
ARG ARCH=x64
ENV ARCH=$ARCH

WORKDIR /mega

RUN <<EOF
echo 'ARCH' ${ARCH} 'for DISTRO' $DISTRO
PACKAGES="autoconf autoconf-archive curl dh-autoreconf git nasm pkg-config python3 tar unzip zip"

if [ "$DISTRO" != "ubuntu:20.04" ]
then
    PACKAGES="$PACKAGES cmake"
fi

if [ "$DISTRO" = opensuse* ]
then
    PACKAGES="$PACKAGES automake awk gcc-c++"
    zypper install -y --force-resolution $PACKAGES
    if [ "$ARCH" = "arm64" ]
    then
        echo 'error: Unsupported ARCH' ${ARCH} 'for DISTRO' $DISTRO
        exit 1
    fi
else
    if [ "$ARCH" = "arm64" ]
    then
        PACKAGES="$PACKAGES gcc-aarch64-linux-gnu g++-aarch64-linux-gnu"
    fi
    PACKAGES="$PACKAGES build-essential"
    export DEBCONF_NOWARNINGS=yes
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y $PACKAGES
fi

if [ "$DISTRO" = "ubuntu:20.04" ]
then
    curl -L https://github.com/Kitware/CMake/releases/download/v3.19.8/cmake-3.19.8-Linux-x86_64.sh -o cmake.sh
    echo "aa5a0e0dd5594b7fd7c107a001a2bfb5f83d9b5d89cf4acabf423c5d977863ad cmake.sh" | sha256sum -c || exit 1
    chmod u+x cmake.sh
    ./cmake.sh --prefix=/usr --exclude-subdir --skip-license
    rm cmake.sh
fi
EOF

RUN git clone https://github.com/microsoft/vcpkg.git
CMD ["/bin/sh", "-c", "\
        echo 'ARCH =' $ARCH && \
        if [ $ARCH = arm64 ]; then export CROSSBUILD_OPTIONS='-DVCPKG_TARGET_TRIPLET=arm64-linux-mega -DCMAKE_SYSTEM_PROCESSOR=aarch64 -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++'; fi && \
        echo 'CROSSBUILD_OPTIONS =' $CROSSBUILD_OPTIONS && \
        cmake \
            $CROSSBUILD_OPTIONS \
            -DVCPKG_ROOT=vcpkg \
            -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -S sdk \
            -B build && \
        cmake --build build -j $(nproc) ; \
        exec /bin/bash \
    "]

