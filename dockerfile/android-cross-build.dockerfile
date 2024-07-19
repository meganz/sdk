# Dockerfile for cross-compiling for Android and its different architectures.
#
# Build the Docker image:
#   docker build -t android-build-env -f /path/to/your/sdk/dockerfile/android-cross-build.dockerfile .
#     -t : Tags the built container with a name
#     -f : Specify dockerfile to be build, replace /path/to/your/sdk with your local path to it
#
# Run the Docker container and build the project for a specific architecture:
#   docker run -v /path/to/your/sdk:/mega/sdk -v /path/to/your/vcpkg:/mega/vcpkg -e ARCH=[arm, arm64, x86, x64] -it android-build-env
#     -v : Mounts a local directory into the container, replace /path/to/your/sdk and /path/to/your/vcpkg with your local paths
#     -e : Sets an environment variable, `ARCH` environment variable is used to specify the target architecture
#     -it : Starts an interactive terminal session inside the container after the cmake project is configured and build

# Base image
FROM ubuntu:22.04

# Install dependencies
RUN apt-get --quiet=2 update && DEBCONF_NOWARNINGS=yes apt-get --quiet=2 install \
    autoconf \
    autoconf-archive \
    build-essential \
    cmake \
    curl \
    git \
    nasm \
    pkg-config \
    python3 \
    python3-pip \
    unzip \
    wget \
    zip \
    1> /dev/null

# Download, extract and set the Android NDK
RUN mkdir -p /mega/android-ndk && \
    cd /mega/android-ndk && \
    wget https://dl.google.com/android/repository/android-ndk-r21d-linux-x86_64.zip && \
    unzip android-ndk-r21d-linux-x86_64.zip && \
    rm android-ndk-r21d-linux-x86_64.zip
ENV ANDROID_NDK_HOME=/mega/android-ndk/android-ndk-r21d
ENV PATH=$PATH:$ANDROID_NDK_HOME

# Set up work directory
WORKDIR /mega

# Set default architecture
ARG ARCH=x64

# Configure and build CMake command, this will be executed when running the container
CMD ["sh", "-c", "\
    arch=${ARCH} && \
    case ${arch} in \
      arm) \
        VCPKG_TRIPLET='arm-android-mega' && \
        ANDROID_ARCH='armeabi-v7a';; \
      arm64) \
        VCPKG_TRIPLET='arm64-android-mega' && \
        ANDROID_ARCH='arm64-v8a';; \
      x86) \
        VCPKG_TRIPLET='x86-android-mega' && \
        ANDROID_ARCH='x86';; \
      x64) \
        VCPKG_TRIPLET='x64-android-mega' && \
        ANDROID_ARCH='x86_64';; \
      *) \
        echo 'Unsupported architecture: ${arch}' && exit 1;; \
    esac && \
    cmake -B buildAndroid -S sdk \
        -DVCPKG_ROOT=/mega/vcpkg \
        -DCMAKE_BUILD_TYPE=Debug \
        -DVCPKG_TARGET_TRIPLET=${VCPKG_TRIPLET} \
        -DENABLE_SDKLIB_EXAMPLES=OFF \
        -DENABLE_SDKLIB_TESTS=OFF \
        -DUSE_FREEIMAGE=OFF \
        -DUSE_FFMPEG=OFF \
        -DUSE_LIBUV=ON \
        -DUSE_PDFIUM=OFF \
        -DUSE_READLINE=OFF \
        -DCMAKE_SYSTEM_NAME=Android \
        -DCMAKE_ANDROID_API=26 \
        -DCMAKE_ANDROID_ARCH_ABI=${ANDROID_ARCH} \
        -DCMAKE_ANDROID_NDK=${ANDROID_NDK_HOME} && \
    cmake --build buildAndroid && \
    exec /bin/bash"]