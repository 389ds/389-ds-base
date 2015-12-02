%{!?__python2: %global __python2 %__python}
%{!?python2_sitelib: %global python2_sitelib %(%{__python2} -c "from distutils.sysconfig import get_python_lib; print(get_python_lib())")}

%define name lib389
%define version 1.0.1
%define prerel 1

Summary: A library for accessing, testing, and configuring the 389 Directory Server
Name: %{name}
Version: %{version}
Release: %{prerel}%{?dist}
Source0: http://port389.org/binaries/%{name}-%{version}-%{prerel}.tar.bz2
License: GPLv3+
Group: Development/Libraries
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)
Prefix: %{_prefix}
BuildArch: noarch
Vendor: Red Hat Inc. <389-devel@lists.fedoraproject.org>
Url: http://port389.org/wiki/Upstream_test_framework
Requires: python-ldap pytest python-krbV

# Currently python-ldap is not python3 compatible, so lib389 only works with 
# python 2.7

%description
This repository contains tools and libraries for accessing, testing, and
configuring the 389 Directory Server.

%prep
%setup -qc
mv %{name}-%{version}-%{prerel} python2

%build
pushd python2
# Remove CFLAGS=... for noarch packages (unneeded)
CFLAGS="$RPM_OPT_FLAGS" %{__python2} setup.py build
popd

%install
rm -rf $RPM_BUILD_ROOT
pushd python2
%{__python2} setup.py install -O1 --skip-build --root $RPM_BUILD_ROOT
popd
for file in $RPM_BUILD_ROOT%{python2_sitelib}/lib389/clitools/*.py; do
    if [ "$file" != "__init__.py" ]; then
        chmod a+x $file
    fi
done

%check
pushd python2
%{__python2} setup.py test
popd

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
%doc python2/LICENSE
%{python2_sitelib}/*

%changelog
* Tue Dec 1 2015 Mark Reynolds <mreynolds@redhat.com> - 1.0.1-1
- Initial Fedora Package




