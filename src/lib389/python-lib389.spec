Summary: A library for accessing, testing, and configuring the 389 Directory Server
Name: python-lib389
Version: 1.0.1
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
Requires: python-nss

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
* Mon Dec 7 2015 Mark Reynolds <mreynolds@redhat.com> - 1.0.1-1
- Removed downloaded dependencies, and added python_provide macro
- Fixed Source0 URL in spec file
 
* Fri Dec 4 2015 Mark Reynolds <mreynolds@redhat.com> - 1.0.1-1
- Renamed package to python-lib389, and simplified the spec file

* Tue Dec 1 2015 Mark Reynolds <mreynolds@redhat.com> - 1.0.1-1
- Bugzilla 1287846 - Submit lib389 python module to access the 389 DS

