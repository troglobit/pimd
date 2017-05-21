# https://fedoraproject.org/wiki/How_to_create_an_RPM_package
Name:           pimd
Version:        2.4.0
Release:        2%{?dist}
Summary:        pimd, the PIM-SM/SSM v2 multicast daemon

Group:          System Environment/Daemons
License:        BSD
URL:            https://github.com/troglobit/%{name}/releases/download/%{version}/%{name}-%{version}.tar.gz
Source0:        %{name}-%{version}.tar.gz
Source1:        %{name}.init
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}

BuildRequires:  make gcc

%description
This is pimd, a lightweight, stand-alone implementation of Protocol Independent
Multicast-Sparse Mode that may be freely distributed and/or deployed under the
BSD license.  The project implements the full PIM-SM specification according to
RFC 2362 with a few noted exceptions (see the RELEASE.NOTES for details).


%prep
%setup -q -n %{name}-master


%build
[ -x configure ] || ./autogen.sh
%configure
%make_build


%install
%{__mkdir_p} %{buildroot}%{_sysconfdir}
%{__install} -m 0644 pimd.conf %{buildroot}%{_sysconfdir}/
%{__mkdir_p} %{buildroot}%{_initrddir}
%{__install} -m 0755 %{SOURCE1} %{buildroot}%{_initrddir}/%{name}
%{__mkdir_p} %{buildroot}%{_sbindir}
%{__install} -m 0755 %{name} %{buildroot}%{_sbindir}/%{name}
%{__mkdir_p} %{buildroot}%{_mandir}/man8
%{__install} -m 0644 pimd.8 %{buildroot}%{_mandir}/man8/pimd.8


%clean
%{__rm} -rf %{buildroot}


%files
%defattr(-,root,root,-)
%doc AUTHORS CREDITS LICENSE LICENSE.mrouted RELEASE.NOTES
%config %{_sysconfdir}/pimd.conf
%{_initrddir}/%{name}
%{_sbindir}/%{name}
%{_mandir}/man8/pimd.8.gz


%post
chkconfig --add %{name}


%changelog
* Sun May 21 2017 troglobit@gmail.com - 2.4.0

* Mon Jan 27 2014 timeos@zssos.sk - 2.1.8

  fix for - pimd segfaults in igmp_read() (https://github.com/troglobit/pimd/issues/29)
* Thu Dec 31 2013 timeos@zssos.sk - 2.1.8

  Initial - Built from upstream version.
