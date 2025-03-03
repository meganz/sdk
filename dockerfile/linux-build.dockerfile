# docker build --build-arg DISTRO=<DISTRO> -f /path/to/linux-build.dockerfile -t sdk-linux-build .
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
# docker run -v /path/to/sdk:/mega/sdk sdk-linux-build

ARG DISTRO=ubuntu:22.04

FROM $DISTRO

ARG DISTRO
ENV DISTRO=$DISTRO

WORKDIR /mega

RUN <<EOF
PACKAGES="autoconf autoconf-archive curl git nasm pkg-config python3 tar unzip zip"

if [ "$DISTRO" != "ubuntu:20.04" ]
then
    PACKAGES="$PACKAGES cmake"
fi

if [[ "$DISTRO" == opensuse* ]]
then
    PACKAGES="$PACKAGES automake awk gcc-c++"
    zypper install -y $PACKAGES
else
    PACKAGES="$PACKAGES build-essential"
    export DEBCONF_NOWARNINGS=yes
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y $PACKAGES
fi

if [ "$DISTRO" = "ubuntu:20.04" ]
then
    curl -L https://github.com/Kitware/CMake/releases/download/v3.18.0/cmake-3.18.0-Linux-x86_64.sh -o cmake.sh
    chmod u+x cmake.sh
    ./cmake.sh --prefix=/usr --exclude-subdir --skip-license
    rm cmake.sh
fi
EOF

CMD ["/bin/sh", "-c", "\
        git clone https://github.com/microsoft/vcpkg.git && \
        cmake \
            -DVCPKG_ROOT=vcpkg \
            -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -S sdk \
            -B build && \
        cmake --build build -j $(nproc) \
    "]
