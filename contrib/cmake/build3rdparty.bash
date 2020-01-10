# This script is for getting and building the 3rd party libraries that the MEGA SDK uses (some are optional, and some are only needed only by MEGA apps too)
# 
# Your 3rdParty library builds should be outside the SDK repo.  We are moving to use vcpkg to build most of them. You can start it like this:
#
# mkdir 3rdParty
# cd 3rdParty
# git clone https://github.com/Microsoft/vcpkg.git
# cd vcpkg
# .\bootstrap-vcpkg.bat
#
# Note that our current toolset for XP compatibility (which we had but we are deprecating), is VS2015 with v140 or v140_xp.  
# The triplet files ending -mega set v140, please adjust to your requirements.
#
# Another thing you might want to consider for developing, is turning off checked iterators in MSVC builds, since those extra tests can cause big delays
# in debug (eg. deleting the node tree on logout etc).  You can do that by setting set(VCPKG_CXX_FLAGS "${VCPKG_CXX_FLAGS} -D_ITERATOR_DEBUG_LEVEL=0")
# in your triplet file, though every library used must have that same setting.
# 
# Another way is to edit the STL headers first, which can ensure that nested builds get the right flag.
# 
# Copy the folders in contrib/cmake/vcpkg_extra_ports to your 3rdParty/vcpkg/ports folder.
# Copy the folders in contrib/cmake/vcpkg_extra_triplets to your 3rdParty/vcpkg/triplets folder.
# 
# Comment out any libraries that you won't use.
# If using pdfium, follow the instructions below to get the source code
#
# On Mac, go to ports/libraw/CONTROL and comment out the freeglut dependency
# 
# Copy this script to your 3rdParty/vcpkg folder, and run it with the desired triplet as the parameter.  (usually x64-windows-mega or x86-windows-mega)

export TRIPLET=x64-linux

build_one ()
{
  ./vcpkg install --triplet $TRIPLET $1
  echo $? $1 $TRIPLET >> buildlog
}

build_one zlib
build_one cryptopp
build_one libsodium
build_one sqlite3
build_one openssl
build_one c-ares
build_one curl
build_one libevent
build_one libzen
build_one libmediainfo
build_one ffmpeg
build_one gtest

#REM freeimage is not needed for MEGASync (but might be for other projects)
#REM build_one freeimage

#REM MEGASync needs libuv and libraw
build_one libuv
build_one libraw

# MEGASync needs pdfium, and building it is quite tricky - we can build it statically though with its own CMakeLists.txt after getting the code per their instructions.  
# It in turn depends on these libs which are easier to build with vcpkg as part of our compatible static library set than as part of its own third_party dependencies
build_one icu
build_one lcms
build_one libjpeg-turbo
build_one openjpeg

# If building something that depends on MEGAchat you will also need libwebsockets:
build_one libwebsockets

# ------ building pdifum - this one needs some manual steps - these can be done before calling the script ---------------
# - Set up your Depot Tools (this can be one time, reuse it for other builds etc)
#      Follow these instructions to get the depot_tools: https://commondatastorage.googleapis.com/chrome-infra-docs/flat/depot_tools/docs/html/depot_tools_tutorial.html#_setting_up
# - Then in your 3rdParty/vcpkg folder, and run these commands in it to get the pdfium source:
#      export DEPOT_TOOLS=<<<<your depot_tools path>>>>
#      export PATH=$DEPOT_TOOLS;$PATH
#      mkdir pdfium
#      cd pdfium
#      gclient config --unmanaged https://pdfium.googlesource.com/pdfium.git
#      gclient sync
#      # branch 3710 is compatibile with the VS 2015 compiler and v140 toolset  (or if you want to use the latest, see below)
#      cd pdfium
#      git checkout chromium/3710
#      cd ..
#      gclient sync --force
# - If using the latest Pdfium, use at least VS2017 and skip the branch checkout above, and substitute the pdfium-masterbranch-CMakeLists.txt in vcpkg/ports/pdfium and make this one small patch (other changes may be needed if the master branch has changed):
#      in pdfium\core\fxcrt\fx_memory_wrappers.h(26)   comment out the static_assert (uint8_t counts as an arithmentic type)

build_one pdfium
build_one pdfium-freetype

