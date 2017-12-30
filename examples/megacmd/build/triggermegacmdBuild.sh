#!/bin/bash

##
 # @file examples/megacmd/build/triggermegacmdBuild.sh
 # @brief Triggers OBS compilation for configured repositories 
 #     suppossing project with tarball built at $PROJECT_PATH/    
 #     suposing oscrc configured with apiurl correctly:           
 #      nano ~/.oscrc                                             
 #	     apiurl=https://linux
 #
 # (c) 2013 by Mega Limited, Auckland, New Zealand
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

set -e

function printusage {
	echo "$0 [--home] [user@remoteobsserver] [PROJECTPATH [OSCFOLDER]]"
	echo "This scripts triggers recompilation of OBS packages using PROJECTPATH as the folder to look for files to deploy. "
	echo "Notice that the tarball must be created before calling this script (with create_tarball.sh)"
	echo " Use --home to only update home:Admin project. Otherwise DEB and RPM projects will be updated" 
}

if [[ $1 == "--help" ]]; then
	printusage
	exit 1
fi

if [[ $1 == "--home" ]]; then
	onlyhomeproject=1;
	shift
fi

sshpasscommand=""
if [[ $1 == *@* ]]; then
remote=$1
shift
echo -n $remote Password: 
read -s password
sshpasscommand="sshpass -p $password ssh $remote"
echo
fi

PROJECT_PATH=$1
NEWOSCFOLDER_PATH=$2

if [ -z "$PROJECT_PATH" ]; then
	PROJECT_PATH=/tmp/sdk
	echo "using default PROJECT_PATH: $PROJECT_PATH"
fi
if [ -z "$NEWOSCFOLDER_PATH" ]; then
	NEWOSCFOLDER_PATH=/datos/building/osc_projects/megacmd/`date +%Y%m%d%H%M%S`
	echo "using default NEWOSCFOLDER_PATH: $NEWOSCFOLDER_PATH"
fi

export EDITOR=nano

function copy
{
	if [ "$sshpasscommand" ]; then
		sshpass -p $password scp $1 $remote:$2
	else #linking will be enough
		ln -sf $1 $2
	fi
}

echo "creating folder with OBS projects..."
$sshpasscommand mkdir $NEWOSCFOLDER_PATH
$sshpasscommand cd $NEWOSCFOLDER_PATH

echo "checking out existing OBS projects..."
if [ "$onlyhomeproject" ]; then
	$sshpasscommand "cd $NEWOSCFOLDER_PATH && osc co home:Admin "
else
	$sshpasscommand "cd $NEWOSCFOLDER_PATH && osc co RPM "
	$sshpasscommand "cd $NEWOSCFOLDER_PATH && osc co DEB "
fi

for package in megacmd; do
	oldver=`$sshpasscommand cat $NEWOSCFOLDER_PATH/DEB/megacmd/PKGBUILD | grep  pkgver= | cut -d "=" -f2`
	oldrelease=`$sshpasscommand cat $NEWOSCFOLDER_PATH/DEB/megacmd/PKGBUILD | grep pkgrel= | cut -d "=" -f2`
	
	echo "deleting old files for package $package ... "
	
	$sshpasscommand rm $NEWOSCFOLDER_PATH/{DEB,RPM,home:Admin}/$package/* || :
	$sshpasscommand mkdir -p $NEWOSCFOLDER_PATH/{DEB,RPM,home:Admin}/$package || :
	
	$sshpasscommand cp $NEWOSCFOLDER_PATH/DEB/$package/{,old}PKGBUILD || :

	echo "replacing files with newly generated  (tarball, specs, dsc and so on) for package $package ..."
	copy $PROJECT_PATH/build/$package/*.spec $NEWOSCFOLDER_PATH/RPM/$package/
	copy $PROJECT_PATH/build/$package/*tar.gz $NEWOSCFOLDER_PATH/RPM/$package/

	if ls $PROJECT_PATH/build/$package/*changes 2>&1 > /dev/null ; then 
		copy $PROJECT_PATH/build/$package/*changes $NEWOSCFOLDER_PATH/RPM/$package/; 
	fi
	for i in $PROJECT_PATH/build/$package/{PKGBUILD,megacmd.install,*.dsc,*.tar.gz,debian.changelog,debian.control,debian.postinst,debian.postrm,debian.rules,debian.compat,debian.copyright} ; do 
		if [ -e $i ]; then
			copy $i $NEWOSCFOLDER_PATH/DEB/$package/; 
		fi
	done
	#link tar.gz from RPM into DEB
	$sshpasscommand ln -sf $NEWOSCFOLDER_PATH/RPM/$package/*tar.gz $NEWOSCFOLDER_PATH/DEB/$package/
	
	
	newver=`$sshpasscommand cat $NEWOSCFOLDER_PATH/DEB/megacmd/PKGBUILD | grep  pkgver= | cut -d "=" -f2`
	fixedrelease=`$sshpasscommand cat $NEWOSCFOLDER_PATH/DEB/megacmd/PKGBUILD | grep pkgrel= | cut -d "=" -f2`
	if [ "$newver" = "$oldver" ]; then
		((newrelease=oldrelease+1))
	else
		newrelease=1
	fi
	
	echo testing difference in PKGBUILD
	if ! $sshpasscommand cmp $NEWOSCFOLDER_PATH/DEB/megacmd/{,old}PKGBUILD >/dev/null 2>&1; then
		$sshpasscommand sed -i "s#pkgrel=$fixedrelease#pkgrel=$newrelease#g" $NEWOSCFOLDER_PATH/DEB/megacmd/PKGBUILD
	fi
	$sshpasscommand rm $NEWOSCFOLDER_PATH/DEB/megacmd/oldPKGBUILD || :

	#link everything on RPM & DEB projects into home:Admin project
	$sshpasscommand ln -sf $NEWOSCFOLDER_PATH/{RPM,DEB}/$package/* $NEWOSCFOLDER_PATH/home:Admin/$package/
done

echo "modifying files included/excluded in projects (to respond to e.g. tar.gz version changes)"
if [ "$onlyhomeproject" ]; then
$sshpasscommand osc addremove -r $NEWOSCFOLDER_PATH/home:Admin
else
$sshpasscommand osc addremove -r $NEWOSCFOLDER_PATH/DEB
$sshpasscommand osc addremove -r $NEWOSCFOLDER_PATH/RPM 
fi

echo "updating changed files and hence triggering rebuild in the OBS platform ...."
if [ "$onlyhomeproject" ]; then
	$sshpasscommand osc ci -n "$NEWOSCFOLDER_PATH/home:Admin"
else
	$sshpasscommand osc ci -n $NEWOSCFOLDER_PATH/DEB
	$sshpasscommand osc ci -n $NEWOSCFOLDER_PATH/RPM
fi


echo "All good: rebuild triggered"
