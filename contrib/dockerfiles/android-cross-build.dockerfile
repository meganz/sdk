# This dockerfile configures and compiles an Android build from Ubuntu
# The dockerfile should be run in a directory where the sdk repo is present

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
ENV CC=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/clang
ENV CXX=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/clang++

# Clone vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git /mega/vcpkg

# Set up work directory and copy the SDK
WORKDIR /mega
COPY ./sdk ./sdk

# Configure project
RUN cmake -B buildAndroid -S sdk \
    -DVCPKG_ROOT=/mega/vcpkg \
    -DCMAKE_BUILD_TYPE=Debug \
    -DVCPKG_TARGET_TRIPLET=x64-android-mega \
    -DENABLE_SDKLIB_EXAMPLES=OFF \
    -DENABLE_SDKLIB_TESTS=OFF \
    -DUSE_FREEIMAGE=OFF \
    -DUSE_FFMPEG=OFF \
    -DUSE_LIBUV=ON \
    -DUSE_PDFIUM=OFF \
    -DUSE_READLINE=OFF

# Build project
RUN cmake --build buildAndroid