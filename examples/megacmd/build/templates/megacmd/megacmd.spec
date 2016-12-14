Name:		megacmd
Version:	megacmd_VERSION
Release:	1%{?dist}
Summary:	MEGA Command Line Interactive and Scriptable Application
License:	Freeware
Group:		Applications/Others
Url:		https://mega.nz
Source0:	megacmd_%{version}.tar.gz
Vendor:		MEGA Limited
Packager:	MEGA Linux Team <linux@mega.co.nz>

BuildRequires: openssl-devel, sqlite-devel, zlib-devel, autoconf, automake, libtool, gcc-c++
BuildRequires: hicolor-icon-theme, unzip, wget

%if 0%{?suse_version}
BuildRequires: libcares-devel
 
# disabling post-build-checks that ocassionally prevent opensuse rpms from being generated
# plus it speeds up building process
#BuildRequires: -post-build-checks

%if 0%{?suse_version} <= 1320
BuildRequires: libcryptopp-devel
%endif

%endif

%if 0%{?fedora}
BuildRequires: c-ares-devel, cryptopp-devel
%endif

%if 0%{?centos_version} || 0%{?scientificlinux_version}
BuildRequires: c-ares-devel,
%endif

%description
MegaCMD provides non UI access to MEGA services. 
It intends to offer all the functionality 
with your MEGA account via shell interaction. 
It features 2 modes of interaction:

  - interactive. A shell to query your actions
  - scriptable. A way to execute commands from a shell/an script/another program.

%prep
%setup -q

%build

%define flag_cryptopp %{nil}
%define with_cryptopp %{nil}

%if 0%{?centos_version} || 0%{?scientificlinux_version}
%define flag_cryptopp -q
%define with_cryptopp --with-cryptopp=deps
%endif

%if 0%{?rhel_version}
%define flag_cryptopp -q
%define with_cryptopp --with-cryptopp=deps
%endif

%if 0%{?suse_version} > 1320
%define with_cryptopp --with-cryptopp=deps
%endif


%define flag_disablezlib %{nil}
%define with_zlib --with-zlib=deps

%if 0%{?fedora_version} == 23
%define flag_disablezlib -z
%define with_zlib %{nil}
%endif

# Fedora uses system Crypto++ header files
%if 0%{?fedora}
rm -fr bindings/qt/3rdparty/include/cryptopp
%endif

./autogen.sh

#build dependencies into folder deps
mkdir deps
bash -x ./contrib/build_sdk.sh %{flag_cryptopp} -o archives -f \
  -g %{flag_disablezlib} -b -l -c -s -u -a -p deps/

./configure --disable-shared --enable-static --disable-silent-rules \
  --disable-curl-checks %{with_cryptopp} --with-sodium=deps \
  %{with_zlib} --with-sqlite=deps --with-cares=deps \
  --with-curl=deps --without-freeimage --with-readline=deps \
  --with-termcap=deps --prefix=$PWD/deps --disable-examples

make

%install

for i in examples/megacmd/client/mega-*; do install -D $i %{buildroot}%{_bindir}/${i/examples\/megacmd\/client\//}; done
install -D examples/megacmd/client/megacmd_completion.sh %{buildroot}%{_sysconfdir}/bash_completion.d/megacmd_completion.sh
install -D examples/mega-cmd %{buildroot}%{_bindir}/mega-cmd

%post
#source bash_completion?
### END of POSTINST

%postun
killall mega-cmd 2> /dev/null || true

%clean
%{?buildroot:%__rm -rf "%{buildroot}"}

%files
%defattr(-,root,root)
%{_bindir}/mega-attr
%{_bindir}/mega-cd
%{_bindir}/mega-confirm
%{_bindir}/mega-cp
%{_bindir}/mega-debug
%{_bindir}/mega-du
%{_bindir}/mega-exec
%{_bindir}/mega-export
%{_bindir}/mega-find
%{_bindir}/mega-get
%{_bindir}/mega-help
%{_bindir}/mega-history
%{_bindir}/mega-import
%{_bindir}/mega-invite
%{_bindir}/mega-ipc
%{_bindir}/mega-killsession
%{_bindir}/mega-lcd
%{_bindir}/mega-log
%{_bindir}/mega-login
%{_bindir}/mega-logout
%{_bindir}/mega-lpwd
%{_bindir}/mega-ls
%{_bindir}/mega-mkdir
%{_bindir}/mega-mount
%{_bindir}/mega-mv
%{_bindir}/mega-passwd
%{_bindir}/mega-preview
%{_bindir}/mega-put
%{_bindir}/mega-pwd
%{_bindir}/mega-quit
%{_bindir}/mega-reload
%{_bindir}/mega-rm
%{_bindir}/mega-session
%{_bindir}/mega-share
%{_bindir}/mega-showpcr
%{_bindir}/mega-signup
%{_bindir}/mega-speedlimit
%{_bindir}/mega-sync
%{_bindir}/mega-thumbnail
%{_bindir}/mega-userattr
%{_bindir}/mega-users
%{_bindir}/mega-version
%{_bindir}/mega-whoami
%{_bindir}/mega-cmd
%{_sysconfdir}/bash_completion.d/megacmd_completion.sh

%changelog
