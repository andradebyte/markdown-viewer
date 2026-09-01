Name:           mdview
Version:        1.0.0
Release:        1%{?dist}
Summary:        Lightweight Markdown viewer/editor
%define debug_package %{nil}

License:        MIT
URL:            https://github.com/joaoigorandrade/mdview
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  pkgconfig(gtk+-3.0)
BuildRequires:  pkgconfig(webkit2gtk-4.1)

Requires:       webkit2gtk4.1

%description
A lightweight Markdown and text viewer/editor for Linux written in C with
GTK3 and WebKitGTK. Rendered preview, inline editing, tabs, dark theme,
zoom and session cache in a single native binary.

%prep
%setup -q

%build
make CFLAGS="%{optflags}"

%install
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_datadir}/applications
mkdir -p %{buildroot}%{_datadir}/icons/hicolor/scalable/apps
install -m 0755 mdview %{buildroot}%{_bindir}/mdview
sed 's|/home/joaoigorandrade/.local/bin/mdview|/usr/bin/mdview|' mdview.desktop \
    > %{buildroot}%{_datadir}/applications/mdview.desktop
install -m 0644 mdview.svg %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/mdview.svg

%post
update-desktop-database %{_datadir}/applications &> /dev/null || :
gtk-update-icon-cache -f %{_datadir}/icons/hicolor &> /dev/null || :

%files
%{_bindir}/mdview
%{_datadir}/applications/mdview.desktop
%{_datadir}/icons/hicolor/scalable/apps/mdview.svg

%changelog
* Tue Sep 01 2026 joaoigorandrade - 1.0.0-1
- Initial package
