Name:           ghost-dl
Version:        %{_ghost_version}
Release:        1%{?dist}
Summary:        KHInsider game OST downloader
License:        GPL-3.0-or-later
URL:            https://github.com/TheElevatedOne/ghost-dl
Group:          Applications/Multimedia
BuildArch:      %{_ghost_arch}
AutoReqProv:    yes

%description
ghost-dl downloads albums from the Kingdom Hearts Insider game
soundtrack archive (https://downloads.khinsider.com).

It provides a command-line interface and an ncurses search TUI,
format selection, batch files, and parallel downloads.

%prep

%build

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}
cp -a %{_ghost_stage}/. %{buildroot}/

%files
%attr(0755,root,root) %{_bindir}/ghost-dl
%{_mandir}/man1/ghost-dl.1*
%doc %{_docdir}/ghost-dl/README.md
%license %{_datadir}/licenses/ghost-dl/LICENSE

%changelog
* Mon Sep 14 2026 Adam Mladý <admin@elevated.ovh> - %{_ghost_version}-1
- C rewrite with CLI, TUI, and native packages
