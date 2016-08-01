Summary: A library for accessing, testing, and configuring the 389 Directory Server
Name: python-lib389
Version: 1.0.2
Release: 1%{?dist}
%global tarver %{version}-1
Source0: http://www.port389.org/binaries/%{name}-%{tarver}.tar.bz2
License: GPLv3+
Group: Development/Libraries
BuildArch: noarch
Url: http://port389.org/docs/389ds/FAQ/upstream-test-framework.html
BuildRequires: python2-devel
BuildRequires: python-ldap
BuildRequires: krb5-devel
BuildRequires: python-setuptools
Requires: pytest
Requires: python-ldap
Requires: python-six
Requires: python-pyasn1
Requires: python-pyasn1-modules
Requires: python2-dateutil

%{?python_provide:%python_provide python2-lib389}

%description
This module contains tools and libraries for accessing, testing, 
and configuring the 389 Directory Server.

%prep
%setup -q -n %{name}-%{tarver}

%build
CFLAGS="$RPM_OPT_FLAGS" %{__python2} setup.py build

%install
%{__python2} setup.py install -O1 --skip-build --root $RPM_BUILD_ROOT
for file in $RPM_BUILD_ROOT%{python2_sitelib}/lib389/clitools/*.py; do
        chmod a+x $file
done

%check
%{__python2} setup.py test

%files
%license LICENSE
%doc README
%{python2_sitelib}/*
%exclude %{_sbindir}/*

%changelog
* Mon Aug 1 2016 Mark Reynolds <mreynolds@redhat.com> - 1.0.2-1
- Bump version to 1.0.2
- Ticket 48946 - openConnection should not fully popluate DirSrv object
- Ticket 48832 - Add DirSrvTools.getLocalhost() function
- Ticket 48382 - Fix serverCmd to get sbin dir properly
- Bug 1347760 - Information disclosure via repeated use of LDAP ADD operation, etc.
- Ticket 48937 - Cleanup valgrind wrapper script
- Ticket 48923 - Fix additional issue with serverCmd
- Ticket 48923 - serverCmd timeout not working as expected
- Ticket 48917 - Attribute presence
- Ticket 48911 - Plugin improvements for lib389
- Ticket 48911 - Improve plugin support based on new mapped objects
- Ticket 48910 - Fixes for backend tests and lib389 reliability.
- Ticket 48860 - Add replication tools
- Ticket 48888 - Correction to create of dsldapobject
- Ticket 48886 - Fix NSS SSL library in lib389
- Ticket 48885 - Fix spec file requires
- Ticket 48884 - Bugfixes for mapped object and new connections
- Ticket 48878 - better style for backend in backend_test.py
- Ticket 48878 - pep8 fixes part 2
- Ticket 48878 - pep8 fixes and fix rpm to build
- Ticket 48853 - Prerelease installer
- Ticket 48820 - Begin to test compatability with py.test3, and the new orm
- Ticket 48434 - Fix for negative tz offsets
- Ticket 48857 - Remove python-krbV from lib389
- Ticket 48820 - Move Encryption and RSA to the new object types
- Ticket 48431 - lib389 integrate ldclt
- Ticket 48434 - lib389 logging tools
- Ticket 48796 - add function to remove logs
- Ticket 48771 - lib389 - get ns-slapd version
- Ticket 48830 - Convert lib389 to ip route tools
- Ticket 48763 - backup should run regardless of existing backups.
- Ticket 48434 - lib389 logging tools
- Ticket 48798 - EL6 compat for lib389 tests for DH params
- Ticket 48798 - lib389 add ability to create nss ca and certificate
- Ticket 48433 - Aci linting tools
- Ticket 48791 - format args in server tools
- Ticket 48399 - Helper makefile is missing mkdir dist
- Ticket 48399 - Helper makefile is missing mkdir dist
- Ticket 48794 - lib389 build requires are on a single line
- Ticket 48660 - Add function to convert binary values in an entry to base64
- Ticket 48764 - Fix mit krb password to be random.
- Ticket 48765 - Change default ports for standalone topology
- Ticket 48750 - Clean up logging to improve command experience
- Ticket 48751 - Improve lib389 ldapi support
- Ticket 48399 - Add helper makefile to lib389 to build and install
- Ticket 48661 - Agreement test suite fails at the test_changes case
- Ticket 48407 - Add test coverage module for lib389 repo
- Ticket 48357 - clitools should standarise their args
- Ticket 48560 - Make verbose handling consistent
- Ticket 48419 - getadminport() should not a be a static method
- Ticket 48415 - Add default domain parameter
- Ticket 48408 - RFE escaped default suffix for tests
- Ticket 48405 - python-lib389 in rawhide is missing dependencies
- Ticket 48401 - Revert typecheck
- Ticket 48401 - lib389 Entry hasAttr returs dict instead of false
- Ticket 48390 - RFE Improvements to lib389 monitor features for rest389
- Ticket 48358 - Add new spec file
- Ticket 48371 - weaker host check on localhost.localdomain

* Mon Dec 7 2015 Mark Reynolds <mreynolds@redhat.com> - 1.0.1-1
- Removed downloaded dependencies, and added python_provide macro
- Fixed Source0 URL in spec file
 
* Fri Dec 4 2015 Mark Reynolds <mreynolds@redhat.com> - 1.0.1-1
- Renamed package to python-lib389, and simplified the spec file

* Tue Dec 1 2015 Mark Reynolds <mreynolds@redhat.com> - 1.0.1-1
- Bugzilla 1287846 - Submit lib389 python module to access the 389 DS

