# Dockerfile for cross-compiling for Android and its different architectures.
#
# Build the Docker image:
#   docker build -t android-build-env -f /path/to/your/sdk/dockerfile/android-cross-build.dockerfile .
#     -t : Tags the built container with a name
#     -f : Specify dockerfile to be build, replace /path/to/your/sdk with your local path to it
#
# Run the Docker container and build the project for a specific architecture:
#   docker run -v /path/to/your/sdk:/mega/sdk -v /path/to/your/vcpkg:/mega/vcpkg -e ARCH=[arm, arm64, x86, x64] [-e BUILD_SHARED_LIBS=ON] -it android-build-env
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
    libtool-bin \
    nasm \
    openjdk-21-jdk \
    pkg-config \
    python3 \
    python3-pip \
    swig \
    unzip \
    wget \
    zip \
    1> /dev/null

# Download, extract and set the Android NDK
RUN mkdir -p /mega/android-ndk && \
    chmod 777 /mega && \
    cd /mega/android-ndk && \
    wget https://dl.google.com/android/repository/android-ndk-r27b-linux.zip && \
    unzip android-ndk-r27b-linux.zip && \
    rm android-ndk-r27b-linux.zip
ENV ANDROID_NDK_HOME=/mega/android-ndk/android-ndk-r27b
ENV PATH=$PATH:$ANDROID_NDK_HOME
ENV JAVA_HOME=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64
ENV PATH=$PATH:$JAVA_HOME

# Set up work directory
WORKDIR /mega

# Set default architecture
ARG ARCH=x64

# Configure and build CMake command, this will be executed when running the container
CMD ["sh", "-c", "\
    owner_uid=$(stat -c '%u' /mega/sdk) && \
    owner_gid=$(stat -c '%g' /mega/sdk) && \
    groupadd -g $owner_gid me && \
    echo 'Adding \"me\" user...' && \
    useradd -r -M -u $owner_uid -g $owner_gid -d /mega -s /bin/bash me && \
    arch=${ARCH} && \
    case ${arch} in \
      arm) \
        export ANDROID_ARCH='armeabi-v7a';; \
      arm64) \
        export ANDROID_ARCH='arm64-v8a';; \
      x86) \
        export ANDROID_ARCH='x86';; \
      x64) \
        export ANDROID_ARCH='x86_64';; \
      *) \
        echo 'Unsupported architecture: ${arch}' && exit 1;; \
    esac && \
    case ${BUILD_SHARED_LIBS} in \
      ON) \
        export DEFINE_BUILD_SHARED_LIBS_ON=-DBUILD_SHARED_LIBS=ON;; \
      OFF|'') \
        ;; \
      *) \
        echo 'error: Unsupported value for BUILD_SHARED_LIBS:' ${BUILD_SHARED_LIBS} && \
        echo 'Valid values are: ON | OFF' && \
        echo 'Build stopped.' && exit 1;; \
    esac && \
    su - me -w 'ANDROID_NDK_HOME,PATH,JAVA_HOME,VCPKG_TRIPLET,ANDROID_ARCH,DEFINE_BUILD_SHARED_LIBS_ON' -c ' \
    cmake -B buildAndroid -S sdk \
        ${DEFINE_BUILD_SHARED_LIBS_ON} \
        -DVCPKG_ROOT=/mega/vcpkg \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_SYSTEM_NAME=Android \
        -DCMAKE_ANDROID_ARCH_ABI=${ANDROID_ARCH} \
        -DCMAKE_ANDROID_NDK=${ANDROID_NDK_HOME} && \
    cmake --build buildAndroid' && \
    exec /bin/bash"]
