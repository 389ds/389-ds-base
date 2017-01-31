%global srcname lib389
%global sum A library for accessing, testing, and configuring the 389 Directory Server
%global vers 1.0.3

Name: python-%{srcname}
Summary:%{sum}
Version: %{vers}
Release: 1%{?dist}
%global tarver %{version}-1
Source0: http://www.port389.org/binaries/%{name}-%{tarver}.tar.bz2
License: GPLv3+
Group: Development/Libraries
BuildArch: noarch
Url: http://port389.org/docs/389ds/FAQ/upstream-test-framework.html
%if 0%{?rhel}
BuildRequires: python-devel
BuildRequires: python-setuptools
%else
BuildRequires: python2-devel
BuildRequires: python2-setuptools
BuildRequires: python%{python3_pkgversion}-devel
BuildRequires: python%{python3_pkgversion}-setuptools
%endif
%description
This module contains tools and libraries for accessing, testing, 
and configuring the 389 Directory Server.


%package -n python2-%{srcname}
Summary:    %{sum}
Requires: python-ldap
Requires: krb5-workstation
Requires: krb5-server
# Conditional will need to change later.
%if 0%{?rhel}
Requires: pytest
Requires: python-six
Requires: python-pyasn1
Requires: python-pyasn1-modules
Requires: python-dateutil
%else
Requires: python2-pytest
Requires: python2-six
Requires: python2-pyasn1
Requires: python2-pyasn1-modules
Requires: python2-dateutil
%endif
%{?python_provide:%python_provide python2-%{srcname}}
%description -n python2-%{srcname}
This module contains tools and libraries for accessing, testing, 
and configuring the 389 Directory Server.

# Can't build on EL7! Python3 tooling is too broken :( 
# We have to use >= 8, because <= 7 doesn't work ....
%if 0%{?rhel} >= 8 || 0%{?fedora}
%package -n python%{python3_pkgversion}-%{srcname}
Summary:    %{sum}
Requires: python%{python3_pkgversion}-pytest
Requires: python%{python3_pkgversion}-pyldap
Requires: python%{python3_pkgversion}-six
Requires: python%{python3_pkgversion}-pyasn1
Requires: python%{python3_pkgversion}-pyasn1-modules
Requires: python%{python3_pkgversion}-dateutil
%{?python_provide:%python_provide python%{python3_pkgversion}-%{srcname}}
%description -n python%{python3_pkgversion}-%{srcname}
This module contains tools and libraries for accessing, testing, 
and configuring the 389 Directory Server.
%endif

%prep
%autosetup -n %{name}-%{tarver}

%build
# JFC you need epel only devel packages for this, python 3 is the worst
%if 0%{?rhel} >= 8 || 0%{?fedora}
%py2_build
%py3_build
%else
%{__python} setup.py build
%endif

%install
%if 0%{?rhel} >= 8 || 0%{?fedora}
%py2_install
%py3_install
%else
%{__python} setup.py install -O1 --skip-build --root %{buildroot}
%endif

%files -n python2-%{srcname}
%license LICENSE
%doc README
%doc %{_datadir}/%{srcname}/examples/*
%{python2_sitelib}/*
# We don't provide the cli tools for python2
%exclude %{_sbindir}/*

%if 0%{?rhel} >= 8 || 0%{?fedora}
%files -n python%{python3_pkgversion}-%{srcname}
%license LICENSE
%doc README
%doc %{_datadir}/%{srcname}/examples/*
%{python3_sitelib}/*
%{_sbindir}/*
%endif

%changelog
* Thu Sep 22 2016 William Brown <wibrown@redhat.com> - 1.0.3-1
- Bump version to 1.0.3 pre-release
- Ticket 48952 - Restart command needs a sleep
- Ticket 47957 - Update the replication "idle" status string
- Ticket 48949 - Fix ups for style and correctness
- Ticket 48951 - dsadm and dsconf base files
- Ticket 48951 - dsadm dsconfig status and plugin
- Ticket 48984 - Add lib389 paths module
- Ticket 48991 - Fix lib389 spec for python2 and python3
- Ticket 48949 - configparser fallback not python2 compatible
- Ticket 48949 - os.makedirs() exist_ok not python2 compatible, added try/except
- Ticket 48949 - change default file path generation - use os.path.join
- Ticket 48949 - added copying slapd-collations.conf


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

