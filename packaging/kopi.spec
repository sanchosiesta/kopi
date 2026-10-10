%global appname kopi

Name:           kopi
Version:        1.0.0
Release:        1%{?dist}
Summary:        A graphical clipboard manager with vim keybindings

License:        MIT
URL:            https://github.com/sanchosiesta/kopi
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc-c++
BuildRequires:  cmake >= 3.21
BuildRequires:  make
BuildRequires:  SDL3-devel
BuildRequires:  libXtst-devel
BuildRequires:  libX11-devel

# Runtime shared libraries (SDL3, XTest, X11)
Requires:       SDL3
Requires:       libXtst
Requires:       libX11

%description
kopi is a graphical clipboard manager with vim keybindings, written in modern
C++23.  Summon it with Alt+8 on Linux, navigate with vim keys, and pick an
item — it pastes directly into your editor.  Runs as a background app with a
system-tray icon; supports rich (HTML/RTF/image) clipboard capture.

%prep
%setup -q

%build
%cmake
%cmake_build

%install
%cmake_install

%files
%{_bindir}/kopi

%changelog
* Thu Oct 09 2025 mrsipan <mrsipan@users.noreply.github.com>
- initial Fedora/RHEL packaging
