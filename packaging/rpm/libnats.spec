%global source_name nats.c
%define debug_package %{nil}

Name:          libnats
Version:       3.1.1
Release:       1%{?dist}
Summary:       NATS & NATS Streaming - C Client
License:       ASL 2.0
URL:           https://github.com/nats-io/nats.c
Source0:       https://github.com/nats-io/nats.c/archive/v%{version}/%{source_name}-v%{version}.tar.gz
Requires:      openssl-libs
BuildRequires: cmake
BuildRequires: openssl-devel
BuildRequires: protobuf-c-devel

%description
A C client for the NATS messaging system.
Go here for the online documentation, and check the frequently asked questions.
This NATS Client implementation is heavily based on the NATS GO Client.
There is support for Mac OS/X, Linux and Windows (although we don't have specific platform support matrix).

%package devel
Summary:       Development files for %{name}
Requires:      %{name}%{?_isa} = %{version}-%{release}

%description devel
The %{name}-devel package contains libraries and header files for
developing applications that use %{source_name}.

%prep
%setup -q -n %{source_name}-%{version}

%build
%{cmake}
%{cmake_build} -j 1

%install
%{cmake_install}
find %{buildroot} -name '*.a' -delete

%files
%{_libdir}/%{name}.so*

%files devel
%dir %{_includedir}/nats
%dir %{_includedir}/nats/adapters
%{_includedir}/nats.h
%{_includedir}/nats/*.h
%{_includedir}/nats/adapters/*.h
%{_libdir}/pkgconfig/%{name}.pc
%{_prefix}/lib/cmake/cnats/*.cmake

%changelog
* Sat Nov 06 2021 Sergey Safarov <s.safarov@gmail.com> 3.1.1
- initial packaging
