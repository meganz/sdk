# Dockerfile for cross-compiling for Android and its different architectures.
#
# Build the Docker image:
#   docker build -t android-build-env -f /path/to/your/sdk/contrib/dockerfile/android-cross-build.dockerfile .
#     -t : Tags the built container with a name
#     -f : Specify dockerfile to be build, replace /path/to/your/sdk with your local path to it
#
# Run the Docker container and build the project for a specific architecture:
#   docker run -v /path/to/your/sdk:/mega/sdk -e ARCH=[arm, arm64, x86, x64] -it android-build-env
#     -v : Mounts a local directory into the container, replace /path/to/your/sdk with your local path to it
#     -e : Sets an environment variable, `ARCH` environment variable is used to specify the target architecture
#     -it : Starts an interactive terminal session inside the container after the cmake project is configured and build

# Base image
FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    git \
    cmake \
    build-essential \
    curl \
    zip \
    unzip \
    autoconf \
    autoconf-archive \
    nasm \
    pkg-config \
    wget \
    python3 \
    python3-pip

# Download, extract and set the Android NDK
RUN mkdir -p /mega/android-ndk && \
    cd /mega/android-ndk && \
    wget https://dl.google.com/android/repository/android-ndk-r21d-linux-x86_64.zip && \
    unzip android-ndk-r21d-linux-x86_64.zip && \
    rm android-ndk-r21d-linux-x86_64.zip
ENV ANDROID_NDK_HOME=/mega/android-ndk/android-ndk-r21d
ENV PATH=$PATH:$ANDROID_NDK_HOME

# Clone vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git /mega/vcpkg

# Set up work directory
WORKDIR /mega

# Set default architecture
ARG ARCH=x64

# Configure and build CMake command
CMD arch=${ARCH} && \
    case ${arch} in \
      arm) \
        VCPKG_TRIPLET="armeabiv7a-android-mega" && \
        C_COMPILER="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi26-clang" && \
        CXX_COMPILER="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi26-clang++";; \
      arm64) \
        VCPKG_TRIPLET="arm64v8a-android-mega" && \
        C_COMPILER="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang" && \
        CXX_COMPILER="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android26-clang++";; \
      x86) \
        VCPKG_TRIPLET="x86-android-mega" && \
        C_COMPILER="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/i686-linux-android26-clang" && \
        CXX_COMPILER="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/i686-linux-android26-clang++";; \
      x64) \
        VCPKG_TRIPLET="x64-android-mega" && \
        C_COMPILER="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/x86_64-linux-android26-clang" && \
        CXX_COMPILER="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/x86_64-linux-android26-clang++";; \
      *) \
        echo "Unsupported architecture: ${arch}" && exit 1;; \
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
        -DCMAKE_C_COMPILER=${C_COMPILER} \
        -DCMAKE_CXX_COMPILER=${CXX_COMPILER} && \
    cmake --build buildAndroid