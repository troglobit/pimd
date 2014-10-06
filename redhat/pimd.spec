Name:           pimd
Version:        2.1.8
Release:        2%{?dist}
Summary:        pimd, the PIM-SM v2 multicast daemon

Group:          System Environment/Daemons
License:        BSD
URL:            https://github.com/troglobit/%{name}/archive/master.zip
Source0:        %{name}-master.zip
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
./configure
make %{?_smp_mflags}


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
%doc AUTHORS CREDITS FAQ INSTALL LICENSE LICENSE.mrouted README README.config README.config.jp README.debug RELEASE.NOTES TODO
%config %{_sysconfdir}/pimd.conf
%{_initrddir}/%{name}
%{_sbindir}/%{name}
%{_mandir}/man8/pimd.8.gz


%post
chkconfig --add %{name}


%changelog
* Mon Jan 27 2014 timeos@zssos.sk - 2.1.8

  fix for - pimd segfaults in igmp_read() (https://github.com/troglobit/pimd/issues/29)
* Thu Dec 31 2013 timeos@zssos.sk - 2.1.8

  Initial - Built from upstream version.
