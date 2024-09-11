Name:           robdb
Version:        1.1
Release:        %{autorelease -n %{?dist}}
Summary:        Provide basic functions to search and read Berkeley Database records

License:        GPL-2.0-or-later
URL:            https://github.com/389ds/389-ds-base/lib/librobdb
Source0:         %{name}-%{version}.tar.bz2

BuildRequires:  gcc
# Requires:

%description


%package        devel
Summary:        Development files for %{name}
License:        GPL-2.0-or-later OR LGPL-2.1-or-later
Requires:       %{name}-libs%{?_isa} = %{version}-%{release}

%description    devel
The %{name}-devel package contains the library and the header file for
developing applications that use %{name}: A library derived from
rpm lib project (https://github.com/rpm-software-management) that
provides some basic functions to search and read Berkeley Database records


%package        libs
Summary:        Library for %{name}
License:        GPL-2.0-or-later OR LGPL-2.1-or-later

%description    libs
The %{name}-lib package contains a library derived from rpm lib
project (https://github.com/rpm-software-management) that provides
some basic functions to search and read Berkeley Database records


%prep
%autosetup

%build
%make_build

%install
%make_install

%{?ldconfig_scriptlets}


%files libs
%license COPYING COPYING.RPM
%doc %{_defaultdocdir}/%{name}-libs/README.md
%{_libdir}/*.so.*

%files devel
%license COPYING COPYING.RPM
%doc %{_defaultdocdir}/%{name}-devel/README.md
%{_libdir}/*.so
%{_includedir}/*


%changelog
%autochangelog

