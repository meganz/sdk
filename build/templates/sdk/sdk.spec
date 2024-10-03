Name:		sdk
Version:	sdk_VERSION
Release:	%(cat MEGA_BUILD_ID || echo "1").1
Summary:	MEGA SDK - Client Access Engine
License:	https://github.com/meganz/megacmd/blob/master/LICENSE
Group:		Unspecified
Url:		https://mega.io/developers
Source0:	sdk_%{version}.tar.gz
Vendor:		MEGA Limited
Packager:	MEGA Linux Team <linux@mega.co.nz>


BuildRequires: autoconf, autoconf-archive, automake, libtool, gcc-c++
BuildRequires: hicolor-icon-theme, zip, unzip, nasm, cmake, perl

%if 0%{?fedora_version} >= 40
    BuildRequires: wget2, wget2-wget
%else
    BuildRequires: wget
%endif

%if 0%{?suse_version} || 0%{?sle_version}
    %if 0%{?suse_version} > 1500
        BuildRequires: pkgconf-pkg-config
    %else
        BuildRequires: pkg-config
    %endif
%endif
%if 0%{?fedora}
    BuildRequires: pkgconf-pkg-config
%endif


#OpenSUSE
%if 0%{?suse_version} || 0%{?sle_version}
    # disabling post-build-checks that ocassionally prevent opensuse rpms from being generated
    # plus it speeds up building process
    #!BuildIgnore: post-build-checks

    # OpenSUSE leap features too old compiler and python 3.10 by default:
    %if 0%{?suse_version} && 0%{?suse_version} <= 1500
        BuildRequires: gcc13 gcc13-c++
        BuildRequires: python311
    %endif
%endif

#Fedora specific
%if 0%{?fedora}
    # allowing for rpaths (taken as invalid, as if they were not absolute paths when they are)
    %if 0%{?fedora_version} >= 35
        %define __brp_check_rpaths QA_RPATHS=0x0002 /usr/lib/rpm/check-rpaths
    %endif
%endif

%description
This SDK brings you all the power of our client applications and let you create
your own or analyze the security of our products. 

%prep
%global debug_package %{nil}
%setup -q

if [ -f /opt/vcpkg.tar.gz ]; then
    export VCPKG_DEFAULT_BINARY_CACHE=/opt/persistent/vcpkg_cache
    mkdir -p ${VCPKG_DEFAULT_BINARY_CACHE}
    tar xzf /opt/vcpkg.tar.gz
    vcpkg_root="-DVCPKG_ROOT=vcpkg"
fi

# use a custom cmake if required/available:
if [ -f /opt/cmake.tar.gz ]; then
    echo "8dc99be7ba94ad6e14256b049e396b40  /opt/cmake.tar.gz" | md5sum -c -
    tar xzf /opt/cmake.tar.gz
    ln -s cmake-*-Linux* cmake_inst
    export PATH="${PWD}/cmake_inst/bin:${PATH}"
fi

# OpenSuse Leap 15.x defaults to gcc7.
# Python>=10 needed for VCPKG pkgconf
%if 0%{?suse_version} && 0%{?suse_version} <= 1500
    export CC=gcc-13
    export CXX=g++-13
    mkdir python311
    ln -sf /usr/bin/python3.11 python311/python3
    export PATH=$PWD/python311:$PATH
%endif

cmake --version
cmake ${vcpkg_root} -DCMAKE_VERBOSE_MAKEFILE=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo -S . -B %{_builddir}/build_dir

%build

if [ -f /opt/cmake.tar.gz ]; then
    export PATH="${PWD}/cmake_inst/bin:${PATH}"
fi

cmake --build %{_builddir}/build_dir %{?_smp_mflags}

%install


if [ -f /opt/cmake.tar.gz ]; then
    export PATH="${PWD}/cmake_inst/bin:${PATH}"
fi

cmake --install %{_builddir}/build_dir --prefix %{buildroot}
ls -l %{buildroot}
pwd

%post


%preun


%postun


%posttrans


%clean


%{?buildroot:%__rm -rf "%{buildroot}"}


%files
/cmake/sdklibConfig.cmake
/cmake/sdklibTargets-relwithdebinfo.cmake
/cmake/sdklibTargets.cmake
/include/megaapi.h
/lib64/libSDKlib.a
/pkgconfig/sdklib.pc

%defattr(-,root,root)


%changelog
