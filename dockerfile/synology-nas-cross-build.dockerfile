# Dockerfile for cross-compiling for Synology in its different architectures.
#
# Build the Docker image:
#   docker build -t synology-build-env -f /path/to/your/sdk/dockerfile/synology-nas-cross-build.dockerfile /path/to/your/sdk/dockerfile/ --build-arg ARCH=architechtureName
#     -t : Tags the built container with a name
#     -f : Specify dockerfile to be build, replace /path/to/your/sdk with your local path to the sdk
#     --build-arg : adds an argument to the build, you should select one of the possible architectures
#
# Run the Docker container and build the project for a specific architecture:
#   docker run -v /path/to/your/sdk:/mega/sdk -v /path/to/your/vcpkg:/mega/vcpkg -it synology-build-env
#     -v : Mounts a local directory into the container, replace /path/to/your/sdk and /path/to/your/vcpkg with your local paths
#     -it : Starts an interactive terminal session inside the container after the cmake project is configured and build
#
#     Possible architechtures are: [alpine alpine4k apollolake armada37xx armada38x avoton broadwell broadwellnk broadwellnkv2 broadwellntbap bromolow braswell denverton epyc7002 geminilake grantley kvmcloud kvmx64 monaco purley r1000 rtd1296 rtd1619b v1000]

# Base image
FROM ubuntu:22.04

# Set default architecture
ARG ARCH=alpine
ENV ARCH=${ARCH}

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
RUN git clone https://github.com/SynologyOpenSource/pkgscripts-ng.git pkgscripts \
    && git -C ./pkgscripts checkout e1d9f52

# Copy the shell script and configuration files
COPY synology-toolchain.sh /mega/
COPY synology-toolchains.conf /mega/

# Make the helper shell script executable
RUN chmod +x /mega/synology-toolchain.sh

# Run the shell script to set up the environment
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