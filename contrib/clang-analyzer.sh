##
# @file clang-analyzer.sh
# @brief check SDK project with clang static analyzer
#
# (c) 2013-2014 by Mega Limited, Wellsford, New Zealand
#
# This file is part of the MEGA SDK - Client Access Engine.
#
# Applications using the MEGA API must present a valid application key
# and comply with the the rules set forth in the Terms of Service.
#
# The MEGA SDK is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#
# @copyright Simplified (2-clause) BSD License.
#
# You should have received a copy of the license along with this
# program.
##

if [ "$#" -ne 2 ] ; then
  echo "Usage: $0 [clang] [output dir]"
  echo "    [clang] path to clang++ executable"
  echo "    [output dir] directory to place output html files"
  exit 1
fi

if ! type "scan-build" > /dev/null; then
    echo "scan-build is not found. Please install clang package!"
    exit 1
fi

CLANG_PATH=$1
OUT_DIR=$2

CXX=$CLANG_PATH CXXFLAGS="-std=c++11 -stdlib=libc++" ./configure --enable-debug --enable-examples --disable-silent-rules && scan-build --use-c++=$CLANG_PATH -o $OUT_DIR -k -v -v -v  make -j9
