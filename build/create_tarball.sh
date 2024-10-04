#!/bin/bash -x

##
 # @file build/create_tarball.sh
 # @brief Generates SDK tarballs and compilation scripts
 #
 # (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 #
 # This file is part of the MEGA SDK - Client Access Engine.
 #
 # SDK is distributed in the hope that it will be useful,
 # but WITHOUT ANY WARRANTY; without even the implied warranty of
 # MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 #
 # @copyright Simplified (2-clause) BSD License.
 #
 # You should have received a copy of the license along with this
 # program.
##

set -euo pipefail
IFS=$'\n\t'
BASEPATH=$(pwd)/../

# get current version
megasdk_VERSION=$(cat $BASEPATH/include/mega/version.h | grep -Po "MEGA_.*_VERSION [0-9]*"| awk '{print $2}' | paste -sd '.')
export megasdk_NAME=megasdk-$megasdk_VERSION
rm -rf $megasdk_NAME.tar.gz
rm -rf $megasdk_NAME

echo "sdk version: $megasdk_VERSION"

# delete previously generated files
rm -fr megasdk/megasdk*.dsc

# fix version number in template files and copy to appropriate directories
sed -e "s/megasdk_VERSION/$megasdk_VERSION/g" templates/megasdk/megasdk.spec | sed "s#^ *##g" > megasdk/megasdk.spec
sed -e "s/megasdk_VERSION/$megasdk_VERSION/g" templates/megasdk/megasdk.dsc > megasdk/megasdk.dsc
sed -e "s/megasdk_VERSION/$megasdk_VERSION/g" templates/megasdk/PKGBUILD > megasdk/PKGBUILD
for dscFile in `find templates/megasdk/ -name megasdk-xUbuntu_* -o -name megasdk-Debian_* -o -name megasdk-Raspbian_*`; do
    sed -e "s/megasdk_VERSION/$megasdk_VERSION/g" "${dscFile}" > megasdk/`basename ${dscFile}`
done

# read the last generated ChangeLog version
version_file="version"

if [ -s "$version_file" ]; then
    last_version=$(cat "$version_file")
else
    last_version="none"
fi

if [ "$last_version" != "$megasdk_VERSION" ]; then
    # add RPM ChangeLog entry
    changelog="megasdk/megasdk.changes"
    changelogold="megasdk/megasdk.changes.old"
    if [ -f $changelog ]; then
        mv $changelog $changelogold
    fi
    ./generate_rpm_changelog_entry.sh $megasdk_VERSION $BASEPATH/include/mega/version.h > $changelog #TODO: read this from somewhere
    if [ -f $changelogold ]; then
        cat $changelogold >> $changelog
        rm $changelogold
    fi

    # add DEB ChangeLog entry
    changelog="megasdk/debian.changelog"
    changelogold="megasdk/debian.changelog.old"
    if [ -f $changelog ]; then
        mv $changelog $changelogold
    fi
    ./generate_deb_changelog_entry.sh $megasdk_VERSION $BASEPATH/include/mega/version.h > $changelog #TODO: read this from somewhere
    if [ -f $changelogold ]; then
        cat $changelogold >> $changelog
        rm $changelogold
    fi

    # update version file
    echo $megasdk_VERSION > $version_file
fi

# create archive
mkdir $megasdk_NAME
ln -s ../megasdk/megasdk.spec $megasdk_NAME/megasdk.spec
ln -s ../megasdk/debian.postinst $megasdk_NAME/debian.postinst
ln -s ../megasdk/debian.prerm $megasdk_NAME/debian.prerm
ln -s ../megasdk/debian.postrm $megasdk_NAME/debian.postrm
ln -s ../megasdk/debian.copyright $megasdk_NAME/debian.copyright

ln -s $BASEPATH/src $megasdk_NAME/
ln -s $BASEPATH/include $megasdk_NAME
ln -s $BASEPATH/third_party $megasdk_NAME/
ln -s $BASEPATH/tests $megasdk_NAME/
ln -s $BASEPATH/CMakeLists.txt $megasdk_NAME/
ln -s $BASEPATH/vcpkg.json $megasdk_NAME/
mkdir $megasdk_NAME/examples
ln -s $BASEPATH/examples/CMakeLists.txt $megasdk_NAME/examples/
ln -s $BASEPATH/examples/megacli.cpp $megasdk_NAME/examples/
ln -s $BASEPATH/examples/megacli.h $megasdk_NAME/examples/

mkdir $megasdk_NAME/tools
ln -s $BASEPATH/tools/gfxworker $megasdk_NAME/tools/
mkdir $megasdk_NAME/contrib
ln -s $BASEPATH/contrib/cmake $megasdk_NAME/contrib/

tar czfh $megasdk_NAME.tar.gz --exclude-vcs $megasdk_NAME
rm -rf $megasdk_NAME

# delete any previous archive
rm -fr megasdk/megasdk_*.tar.gz
# transform arch name, to satisfy Debian requirements
mv $megasdk_NAME.tar.gz megasdk/megasdk_$megasdk_VERSION.tar.gz

#get md5sum and replace in PKGBUILD
MD5SUM=`md5sum megasdk/megasdk_$megasdk_VERSION.tar.gz | awk '{print $1}'`
sed "s/MD5SUM/$MD5SUM/g"  -i megasdk/PKGBUILD

######
######
