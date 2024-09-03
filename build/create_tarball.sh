#!/bin/bash -x

##
 # @file build/create_tarball.sh
 # @brief Generates SDK tarballs and compilation scripts
 #
 # (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 #
 # This file is part of the SDK.
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
sdk_VERSION=$(cat $BASEPATH/include/mega/version.h | grep -Po "MEGA_.*_VERSION [0-9]*"| awk '{print $2}' | paste -sd '.')
export sdk_NAME=sdk-$sdk_VERSION
rm -rf $sdk_NAME.tar.gz
rm -rf $sdk_NAME

echo "sdk version: $sdk_VERSION"

# delete previously generated files
rm -fr sdk/sdk*.dsc

# fix version number in template files and copy to appropriate directories
sed -e "s/sdk_VERSION/$sdk_VERSION/g" templates/sdk/sdk.spec | sed "s#^ *##g" > sdk/sdk.spec
sed -e "s/sdk_VERSION/$sdk_VERSION/g" templates/sdk/sdk.dsc > sdk/sdk.dsc
sed -e "s/sdk_VERSION/$sdk_VERSION/g" templates/sdk/PKGBUILD > sdk/PKGBUILD
for dscFile in `find templates/sdk/ -name sdk-xUbuntu_* -o -name sdk-Debian_* -o -name sdk-Raspbian_*`; do
    sed -e "s/sdk_VERSION/$sdk_VERSION/g" "${dscFile}" > sdk/`basename ${dscFile}`
done

# read the last generated ChangeLog version
version_file="version"

if [ -s "$version_file" ]; then
    last_version=$(cat "$version_file")
else
    last_version="none"
fi

if [ "$last_version" != "$sdk_VERSION" ]; then
    # add RPM ChangeLog entry
    changelog="sdk/sdk.changes"
    changelogold="sdk/sdk.changes.old"
    if [ -f $changelog ]; then
        mv $changelog $changelogold
    fi
    ./generate_rpm_changelog_entry.sh $sdk_VERSION $BASEPATH/include/mega/version.h > $changelog #TODO: read this from somewhere
    if [ -f $changelogold ]; then
        cat $changelogold >> $changelog
        rm $changelogold
    fi

    # add DEB ChangeLog entry
    changelog="sdk/debian.changelog"
    changelogold="sdk/debian.changelog.old"
    if [ -f $changelog ]; then
        mv $changelog $changelogold
    fi
    ./generate_deb_changelog_entry.sh $sdk_VERSION $BASEPATH/include/mega/version.h > $changelog #TODO: read this from somewhere
    if [ -f $changelogold ]; then
        cat $changelogold >> $changelog
        rm $changelogold
    fi

    # update version file
    echo $sdk_VERSION > $version_file
fi

# create archive
mkdir $sdk_NAME
ln -s ../sdk/sdk.spec $sdk_NAME/sdk.spec
ln -s ../sdk/debian.postinst $sdk_NAME/debian.postinst
ln -s ../sdk/debian.prerm $sdk_NAME/debian.prerm
ln -s ../sdk/debian.postrm $sdk_NAME/debian.postrm
ln -s ../sdk/debian.copyright $sdk_NAME/debian.copyright

ln -s $BASEPATH/src $sdk_NAME/
ln -s $BASEPATH/include $sdk_NAME
ln -s $BASEPATH/third_party $sdk_NAME/
ln -s $BASEPATH/tests $sdk_NAME/
ln -s $BASEPATH/CMakeLists.txt $sdk_NAME/
ln -s $BASEPATH/vcpkg.json $sdk_NAME/
mkdir $sdk_NAME/examples
ln -s $BASEPATH/examples/CMakeLists.txt $sdk_NAME/examples/
ln -s $BASEPATH/examples/megacli.cpp $sdk_NAME/examples/
ln -s $BASEPATH/examples/megacli.h $sdk_NAME/examples/

mkdir $sdk_NAME/tools
ln -s $BASEPATH/tools/gfxworker $sdk_NAME/tools/
mkdir $sdk_NAME/contrib
ln -s $BASEPATH/contrib/cmake $sdk_NAME/contrib/

tar czfh $sdk_NAME.tar.gz --exclude-vcs $sdk_NAME
rm -rf $sdk_NAME

# delete any previous archive
rm -fr sdk/sdk_*.tar.gz
# transform arch name, to satisfy Debian requirements
mv $sdk_NAME.tar.gz sdk/sdk_$sdk_VERSION.tar.gz

#get md5sum and replace in PKGBUILD
MD5SUM=`md5sum sdk/sdk_$sdk_VERSION.tar.gz | awk '{print $1}'`
sed "s/MD5SUM/$MD5SUM/g"  -i sdk/PKGBUILD

######
######
