# Dockerfile for cross-compiling for DMS in its different architectures.
#
# Build the Docker image:
#   docker build -t dms-build-env -f /path/to/your/sdk/dockerfile/dms-cross-build.dockerfile /path/to/your/sdk/dockerfile/ --build-arg PLATFORM=platformName
#     -t : Tags the built container with a name
#     -f : Specify dockerfile to be build, replace /path/to/your/sdk with your local path to the sdk
#     --build-arg : adds an argument to the build, you should select one of the possible architectures
#
# Run the Docker container and build the project for a specific architecture:
#   docker run -v /path/to/your/sdk:/mega/sdk -v /path/to/your/vcpkg:/mega/vcpkg -it dms-build-env
#     -v : Mounts a local directory into the container, replace /path/to/your/sdk and /path/to/your/vcpkg with your local paths
#     -it : Starts an interactive terminal session inside the container after the cmake project is configured and build
#
#     Possible architechtures are: [alpine alpine4k apollolake armada37xx armada38x avoton broadwell broadwellnk broadwellnkv2 broadwellntbap bromolow braswell denverton epyc7002 geminilake grantley kvmx64 monaco purley r1000 rtd1296 rtd1619b v1000]

# Base image
FROM ubuntu:22.04

# Set default architecture
ARG PLATFORM=alpine
ENV PLATFORM=${PLATFORM}

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

RUN chmod 777 /mega

# Clone and checkout known pkgscripts baseline
RUN git clone https://github.com/SynologyOpenSource/pkgscripts-ng.git pkgscripts \
    && git -C ./pkgscripts checkout e1d9f52

# Copy the shell script and configuration files
COPY dms-toolchain.sh /mega/
COPY dms-toolchains.conf /mega/

# Make the helper shell script executable
RUN chmod +x /mega/dms-toolchain.sh

# Configure and build CMake command
CMD ["sh", "-c", "\
    owner_uid=$(stat -c '%u' /mega/sdk) && \
    owner_gid=$(stat -c '%g' /mega/sdk) && \
    groupadd -g $owner_gid me && \
    echo 'Adding \"me\" user...' && \
    useradd -r -M -u $owner_uid -g $owner_gid -d /mega -s /bin/bash me && \
    export PLATFORM=${PLATFORM} && \
    su - me -w 'PLATFORM' -c ' \
    /mega/dms-toolchain.sh ${PLATFORM} && \
    cmake -B buildDMS -S sdk \
        -DVCPKG_ROOT=/mega/vcpkg \
        -DCMAKE_BUILD_TYPE=Debug \
        -DENABLE_SDKLIB_EXAMPLES=OFF \
        -DENABLE_SDKLIB_TESTS=OFF \
        -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=/mega/${PLATFORM}.toolchain.cmake \
        -DVCPKG_OVERLAY_TRIPLETS=/mega \
        -DVCPKG_TARGET_TRIPLET=${PLATFORM} && \
    cmake --build buildDMS' && \
    exec /bin/bash"]