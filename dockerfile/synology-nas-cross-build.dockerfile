# Dockerfile for cross-compiling for Synology in its different architectures.
#
# Build the Docker image:
#   docker build -t synology-build-env -f /path/to/your/sdk/dockerfile/synology-nas-cross-build.dockerfile . --build-arg ARCH=architechtureName
#     -t : Tags the built container with a name
#     -f : Specify dockerfile to be build, replace /path/to/your/sdk with your local path to the sdk
#     --build-arg : adds an argument to the build, you should select one of the possible architectures
#
# Run the Docker container and build the project for a specific architecture:
#   docker run -v /path/to/your/sdk:/mega/sdk -v /path/to/your/vcpkg:/mega/vcpkg -e ARCH=architechtureName -it synology-build-env
#     -v : Mounts a local directory into the container, replace /path/to/your/sdk and /path/to/your/vcpkg with your local paths
#     -e : Sets an environment variable, `ARCH` environment variable is used to specify the target architecture
#     -it : Starts an interactive terminal session inside the container after the cmake project is configured and build
#
#     Possible architechtures are: [alpine alpine4k apollolake armada37xx armada38x avoton broadwell broadwellnk broadwellnkv2 broadwellntbap bromolow braswell denverton epyc7002 geminilake grantley kvmcloud kvmx64 monaco purley r1000 rtd1296 rtd1619b v1000]

# Base image
FROM ubuntu:22.04

# Environment variables and default values
## Architectures from: https://github.com/SynologyOpenSource/pkgscripts-ng/blob/e1d9f527f33cf1dbbf9e792becec9214e453eeaf/include/platforms
ENV ARCH_LIST="alpine alpine4k apollolake armada37xx armada38x avoton broadwell broadwellnk broadwellnkv2 broadwellntbap bromolow braswell denverton epyc7002 geminilake grantley kvmcloud kvmx64 monaco purley r1000 rtd1296 rtd1619b v1000"
## Toolchain
ENV TOOLCHAIN_BASE_URL="https://archive.synology.com/download/ToolChain/toolchain/7.2-72746"
## Set default architecture
ARG ARCH=alpine

# Install dependencies
RUN apt-get --quiet=2 update && DEBCONF_NOWARNINGS=yes apt-get --quiet=2 install \
    aria2 \
    autoconf \
    autoconf-archive \
    build-essential \
    cmake \
    curl \
    fakeroot \
    git \
    nasm \
    pkg-config \
    python3 \
    tar \
    unzip \
    wget \
    xz-utils \
    zip \
    1> /dev/null

# Set up work directory
WORKDIR /mega

# Clone and checkout known pkgscripts baseline
RUN git clone https://github.com/SynologyOpenSource/pkgscripts-ng.git pkgscripts
WORKDIR /mega/pkgscripts
RUN git checkout e1d9f52

# Set up correct workdir again
WORKDIR /mega

# Download and extract toolchain from Synology
RUN target_arch=${ARCH} && \
    case ${target_arch} in \
      alpine) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-72746/Annapurna%20Alpine%20Linux%203.10.108/alpine-gcc1220_glibc236_hard-GPL.txz && \
        TOOLCHAIN_FILE=alpine-gcc1220_glibc236_hard-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      alpine4k) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-72746/Annapurna%20Alpine%20Linux%203.10.108/alpine4k-gcc1220_glibc236_hard-GPL.txz && \
        TOOLCHAIN_FILE=alpine4k-gcc1220_glibc236_hard-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      apollolake) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-72746/Intel%20x86%20Linux%204.4.180%20%28Apollolake%29/apollolake-gcc1220_glibc236_x86_64-GPL.txz \
        TOOLCHAIN_FILE=apollolake-gcc1220_glibc236_x86_64-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      armada37xx) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-72746/Marvell%20Armada%2037xx%20Linux%204.4.302/armada37xx-gcc1220_glibc236_armv8-GPL.txz \
        TOOLCHAIN_FILE=armada37xx-gcc1220_glibc236_armv8-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      armada38x) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-72746/Marvell%20Armada%2038x%20Linux%203.10.108/armada38x-gcc1220_glibc236_hard-GPL.txz \
        TOOLCHAIN_FILE=armada38x-gcc1220_glibc236_hard-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      avoton) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-72746/Intel%20x86%20Linux%203.10.108%20%28Avoton%29/avoton-gcc1220_glibc236_x86_64-GPL.txz \
        TOOLCHAIN_FILE=avoton-gcc1220_glibc236_x86_64-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      broadwell) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-72746/Intel%20x86%20Linux%204.4.180%20%28Broadwell%29/broadwell-gcc1220_glibc236_x86_64-GPL.txz \
        TOOLCHAIN_FILE=broadwell-gcc1220_glibc236_x86_64-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      broadwellnk) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-72746/Intel%20x86%20Linux%204.4.302%20%28Broadwellnk%29/broadwellnk-gcc1220_glibc236_x86_64-GPL.txz \
        TOOLCHAIN_FILE=broadwellnk-gcc1220_glibc236_x86_64-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      broadwellnkv2) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-72746/Intel%20x86%20Linux%204.4.302%20%28Broadwellnkv2%29/broadwellnkv2-gcc1220_glibc236_x86_64-GPL.txz \
        TOOLCHAIN_FILE=broadwellnkv2-gcc1220_glibc236_x86_64-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      broadwellntbap) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-72746/Intel%20x86%20Linux%204.4.302%20%28Broadwellntbap%29/broadwellntbap-gcc1220_glibc236_x86_64-GPL.txz \
        TOOLCHAIN_FILE=broadwellntbap-gcc1220_glibc236_x86_64-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      bromolow) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-72746/Intel%20x86%20linux%203.10.108%20%28Bromolow%29/bromolow-gcc1220_glibc236_x86_64-GPL.txz \
        TOOLCHAIN_FILE=bromolow-gcc1220_glibc236_x86_64-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      braswell) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-72746/Intel%20x86%20Linux%203.10.108%20%28Braswell%29/braswell-gcc1220_glibc236_x86_64-GPL.txz \
        TOOLCHAIN_FILE=braswell-gcc1220_glibc236_x86_64-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      denverton) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-72746/Intel%20x86%20Linux%204.4.302%20%28Denverton%29/denverton-gcc1220_glibc236_x86_64-GPL.txz \
        TOOLCHAIN_FILE=denverton-gcc1220_glibc236_x86_64-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      epyc7002) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-72746/AMD%20x86%20Linux%20Linux%205.10.55%20%28epyc7002%29/epyc7002-gcc1220_glibc236_x86_64-GPL.txz \
        TOOLCHAIN_FILE=epyc7002-gcc1220_glibc236_x86_64-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      geminilake) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-72746/Intel%20x86%20Linux%204.4.302%20%28GeminiLake%29/geminilake-gcc1220_glibc236_x86_64-GPL.txz \
        TOOLCHAIN_FILE=geminilake-gcc1220_glibc236_x86_64-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      grantley) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-72746/Intel%20x86%20Linux%203.10.108%20%28Grantley%29/grantley-gcc1220_glibc236_x86_64-GPL.txz \
        TOOLCHAIN_FILE=grantley-gcc1220_glibc236_x86_64-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      kvmcloud) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-63134/Intel%20x86%20Linux%204.4.302%20%28Kvmcloud%29/kvmcloud-gcc1220_glibc236_x86_64-GPL.txz \
        TOOLCHAIN_FILE=kvmcloud-gcc1220_glibc236_x86_64-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      kvmx64) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-72746/Intel%20x86%20Linux%204.4.302%20%28Kvmx64%29/kvmx64-gcc1220_glibc236_x86_64-GPL.txz \
        TOOLCHAIN_FILE=kvmx64-gcc1220_glibc236_x86_64-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      monaco) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-72746/STMicroelectronics%20Monaco%20Linux%203.10.108/monaco-gcc1220_glibc236_hard-GPL.txz \
        TOOLCHAIN_FILE=monaco-gcc1220_glibc236_hard-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      purley) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-72746/Intel%20x86%20Linux%204.4.302%20%28Purley%29/purley-gcc1220_glibc236_x86_64-GPL.txz \
        TOOLCHAIN_FILE=purley-gcc1220_glibc236_x86_64-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      r1000) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-72746/AMD%20x86%20Linux%204.4.302%20%28r1000%29/r1000-gcc1220_glibc236_x86_64-GPL.txz \
        TOOLCHAIN_FILE=r1000-gcc1220_glibc236_x86_64-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      rtd1296) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-72746/Realtek%20RTD129x%20Linux%204.4.302/rtd1296-gcc1220_glibc236_armv8-GPL.txz \
        TOOLCHAIN_FILE=rtd1296-gcc1220_glibc236_armv8-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      rtd1619b) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-72746/Realtek%20RTD16xxb%20Linux%205.10.55/rtd1619b-gcc1220_glibc236_armv8-GPL.txz \
        TOOLCHAIN_FILE=rtd1619b-gcc1220_glibc236_armv8-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      v1000) \
        TOOLCHAIN_URL=https://global.synologydownload.com/download/ToolChain/toolchain/7.2-72746/Intel%20x86%20Linux%204.4.302%20%28V1000%29/v1000-gcc1220_glibc236_x86_64-GPL.txz \
        TOOLCHAIN_FILE=v1000-gcc1220_glibc236_x86_64-GPL && \
        TOOLCHAIN_TAR="${TOOLCHAIN_FILE}.txz";; \
      *) \
        echo 'Unsupported architecture: ${target_arch}' && exit 1;; \
    esac && \
    wget ${TOOLCHAIN_URL} && \
    mkdir /mega/toolchain && \
    tar -xf ${TOOLCHAIN_TAR} -C /mega/toolchain && \
    rm ${TOOLCHAIN_TAR}

# # Generate vcpkg chainload toolchain and vcpkg triplet files
## Copy the shell script
COPY ./sdk/dockerfile/synology-toolchain.sh /mega/
## Make the helper shell script executable
RUN chmod +x /mega/synology-toolchain.sh
# ## Source the helper shell script and call the synology_toolchain_files function
RUN bash -c '/mega/synology-toolchain.sh ${ARCH}'

# Configure and build CMake command
CMD ["sh", "-c", "\
    cmake -B buildSynology -S sdk \
        -DVCPKG_ROOT=/mega/vcpkg \
        -DCMAKE_BUILD_TYPE=Debug \
        -DENABLE_SDKLIB_EXAMPLES=OFF \
        -DENABLE_SDKLIB_TESTS=OFF \
        -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=/mega/${ARCH}.toolchain.cmake \
        -DVCPKG_OVERLAY_TRIPLETS=/mega \
        -DVCPKG_TARGET_TRIPLET=${ARCH} && \
    cmake --build buildSynology && \
    exec /bin/bash"]