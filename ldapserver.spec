# BEGIN COPYRIGHT BLOCK
# Copyright (C) 2005 Red Hat, Inc.
# All rights reserved.
# END COPYRIGHT BLOCK
Summary: Directory Server
Name: ldapserver
Version: 7.1
Release: 0
License: GPL
Group: System Environment/Daemons
URL: http://www.redhat.com
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_builddir}/%{name}-root
BuildPreReq: perl, fileutils, make
# Without Autoreq: 0, rpmbuild finds all sorts of crazy
# dependencies that we don't care about, and refuses to install
Autoreq: 0
# Without Requires: something, rpmbuild will abort!
Requires: perl
Prefix: /opt/%{name}

%description
ldapserver is an LDAPv3 compliant server.

# prep and setup expect there to be a Source0 file
# in the SOURCES directory - it will be unpacked
# in the _builddir (not BuildRoot)
%prep
%setup

%build
# This will do a regular make build and make pkg
# including grabbing the admin server, setup, etc.
# The resultant zip files and setup program will
# be in ldapserver/pkg
# INSTDIR is relative to ldap/cm
# build the file structure to package under ldapserver/pkg
# instead of MM.DD/platform
# remove BUILD_DEBUG=optimize to build the debug version
make BUILD_JAVA_CODE=1 BUILD_DEBUG=optimize NO_INSTALLER_TAR_FILES=1 INSTDIR=../../pkg

%install
# all we do here is run setup -b to unpack the binaries
# into the BuildRoot
rm -rf $RPM_BUILD_ROOT
cd pkg
# hack hack hack
# hack for unbundled jre - please fix!!!!!!
export NSJRE=/share/builds/components/jdk/1.4.2/Linux/jre
mkdir tmp
cd tmp
mkdir -p bin/base/jre
cp -r $NSJRE/bin bin/base/jre
cp -r $NSJRE/lib bin/base/jre
zip -q -r ../base/nsjre.zip bin
cd ..
rm -rf tmp
echo yes | ./setup -b $RPM_BUILD_ROOT/%{prefix}
# this is our setup script that sets up the initial
# server instances after installation
cd ..
cp ldap/cm/newinst/setup $RPM_BUILD_ROOT/%{prefix}/setup

%clean
rm -rf $RPM_BUILD_ROOT

%files
# rather than listing individual files, we just package (and own)
# the entire ldapserver directory - if we change this to put
# files in different places, we won't be able to do this anymore
%defattr(-,root,root,-)
%{prefix}

%post
echo ""
echo "Please cd " %{prefix} " and run ./setup/setup"

%changelog
* Tue Mar  8 2005 Richard Megginson <rich@localhost.localdomain> 7.1-0
- use ${prefix} instead of /opt/ldapserver - prefix is defined as /opt/%{name}

* Thu Jan 20 2005 Richard Megginson <rmeggins@redhat.com>
- Initial build.


