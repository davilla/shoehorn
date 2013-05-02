%define name shoehorn
%define version 3.4
%define release 1GL
%define builddir $RPM_BUILD_DIR/%{name}-%{version}
Name:		%{name}
Version:	%{version}
Release:	%{release}
Vendor:		Mike Touloumtzis <miket@bluemug.com>
Packager:	Guenther H. Leber (guenther@adcon.at, gleber@gams.at)
URL:		http://web.bluemug.com/~miket/arm/shoehorn/shoehorn-%{version}.tar.gz
Source:         %{name}-%{version}.tar.gz
#Patch0:		shoehorn-loaderinlib.patch
Group:		Development/Tools
Copyright:	Distributable
BuildRoot:	/var/tmp/%{name}-%{version}
Requires:	arm-linux-binutils, arm-linux-gcc
Summary:	Bootstrap loader for CL-PS7111 and EP7211

%description
The 7111 and 7211 have internal SRAM and bootstrap ROM.  This package
connects to the device in bootstrap mode over a serial connection, to load
and boot a Linux kernel with ramdisk filesystem.

%prep
%setup -n %{name}-%{version}
#%patch0 -p1 -b loaderinlib

%build
make EXTRAFLAGS="-DLOADERPATH=/usr/lib/shoehorn/"

%install
if [ -d $RPM_BUILD_ROOT ]; then rm -rf $RPM_BUILD_ROOT; fi
mkdir -p $RPM_BUILD_ROOT/usr/bin
mkdir -p $RPM_BUILD_ROOT/usr/lib/shoehorn
#install -c shoehorn $RPM_BUILD_ROOT/usr/bin
#install -c loader.bin $RPM_BUILD_ROOT/usr/lib/shoehorn
make install INSTALLPREFIX=$RPM_BUILD_ROOT BINDIR=/usr/bin LIBDIR=/usr/lib/shoehorn

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr (755, root, root, -)
%doc README COPYING
%defattr (4755, root, root, -)
/usr/bin/shoehorn
%defattr (755, root, root, -)
/usr/lib/shoehorn/loader.bin

%changelog 


* Fri Sep 29 2000	Günther H. Leber <gleber@gams.at>, <guenther@adcon.at>
- RPMised
- added dropping of root privs if setuid root
- set MAC for target's cs8900 if none yet set
- added a couple of initializations for edb7211
- wiped a couple of bugs (buffer sizes for files and alignment)
