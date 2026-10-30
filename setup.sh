#!/usr/bin/env bash

set -euo pipefail

repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
minimum_cmake_version=3.25
minimum_qt_version=6.12
qt_install_root="${HOME}/.local/opt/Qt"
qt_installer_tmpdir=""

cleanup_setup() {
    if [[ -n "$qt_installer_tmpdir" ]]; then
        rm -rf -- "$qt_installer_tmpdir"
    fi
}
trap cleanup_setup EXIT

if [[ ! -r /etc/os-release ]]; then
    echo "setup.sh: cannot identify this Linux distribution (/etc/os-release is unavailable)." >&2
    exit 1
fi

# shellcheck disable=SC1091
. /etc/os-release

distro_id=${ID:-unknown}
distro_name=${PRETTY_NAME:-${NAME:-$distro_id}}
distro_families="${distro_id} ${ID_LIKE:-}"

has_command() {
    command -v "$1" >/dev/null 2>&1
}

package_manager=""
case " ${distro_families,,} " in
    *" debian "*|*" ubuntu "*)
        has_command apt-get && package_manager=apt-get
        ;;
    *" fedora "*|*" rhel "*|*" centos "*)
        if has_command dnf; then
            package_manager=dnf
        elif has_command yum; then
            package_manager=yum
        fi
        ;;
    *" arch "*|*" manjaro "*)
        has_command pacman && package_manager=pacman
        ;;
    *" opensuse "*|*" suse "*)
        has_command zypper && package_manager=zypper
        ;;
    *" alpine "*)
        has_command apk && package_manager=apk
        ;;
    *" gentoo "*)
        has_command emerge && package_manager=emerge
        ;;
    *" void "*)
        has_command xbps-install && package_manager=xbps-install
        ;;
esac

if [[ -z "$package_manager" ]]; then
    for candidate in apt-get dnf yum pacman zypper apk emerge xbps-install; do
        if has_command "$candidate"; then
            package_manager=$candidate
            break
        fi
    done
fi

if [[ -z "$package_manager" ]]; then
    printf 'setup.sh: detected %s, but no supported package manager was found.\n' "$distro_name" >&2
    exit 1
fi

printf 'Detected distribution: %s (%s)\n' "$distro_name" "$distro_id"
printf 'Detected package manager: %s\n' "$package_manager"

run_as_root() {
    if ((EUID == 0)); then
        "$@"
    elif has_command sudo; then
        sudo "$@"
    else
        echo "setup.sh: installing system build tools requires root or sudo." >&2
        return 1
    fi
}

install_build_tools() {
    echo "Installing CMake, a C++ compiler, pkg-config, OpenGL and libxml2 development files, and curl..."
    case "$package_manager" in
        apt-get)
            run_as_root apt-get update
            run_as_root apt-get install -y cmake build-essential pkg-config libgl1-mesa-dev libxml2-dev curl
            ;;
        dnf|yum)
            run_as_root "$package_manager" install -y cmake gcc-c++ pkgconf-pkg-config mesa-libGL-devel libxml2-devel curl
            ;;
        pacman)
            run_as_root pacman -S --needed --noconfirm cmake base-devel pkgconf mesa libxml2 curl
            ;;
        zypper)
            run_as_root zypper --non-interactive install cmake gcc-c++ pkg-config Mesa-libGL-devel libxml2-devel curl
            ;;
        apk)
            run_as_root apk add cmake build-base pkgconf mesa-gl-dev libxml2-dev curl
            ;;
        emerge)
            run_as_root emerge --ask=n dev-build/cmake sys-devel/gcc virtual/pkgconfig media-libs/mesa dev-libs/libxml2 net-misc/curl
            ;;
        xbps-install)
            run_as_root xbps-install -Sy cmake gcc pkg-config mesa-devel libxml2-devel curl
            ;;
        *)
            printf 'setup.sh: automatic build-tool installation is not configured for %s.\n' "$package_manager" >&2
            return 1
            ;;
    esac
}

install_secret_service_client() {
    if has_command secret-tool; then
        return
    fi
    echo "Installing the Secret Service command-line client used to store Outlook tokens in the desktop credential store..."
    case "$package_manager" in
        apt-get)
            run_as_root apt-get update
            run_as_root apt-get install -y libsecret-tools
            ;;
        dnf|yum)
            run_as_root "$package_manager" install -y libsecret
            ;;
        pacman)
            run_as_root pacman -S --needed --noconfirm libsecret
            ;;
        zypper)
            run_as_root zypper --non-interactive install libsecret-tools
            ;;
        apk)
            run_as_root apk add libsecret
            ;;
        emerge)
            run_as_root emerge --ask=n app-crypt/libsecret
            ;;
        xbps-install)
            run_as_root xbps-install -Sy libsecret
            ;;
        *)
            printf 'setup.sh: automatic Secret Service client installation is not configured for %s. Install secret-tool before using Outlook authentication.\n' "$package_manager" >&2
            return 1
            ;;
    esac
    if ! has_command secret-tool; then
        echo "setup.sh: package installation completed, but secret-tool is still unavailable." >&2
        return 1
    fi
}

version_at_least() {
    local required=$1
    local installed=$2
    local first
    first=$(printf '%s\n%s\n' "$required" "$installed" | sort -V | sed -n '1p')
    [[ "$first" == "$required" ]]
}

discover_qt() {
    local search_root config_file config_dir qt_prefix qmake_candidate detected_version
    local -a search_roots=()
    local -a config_dirs=()
    local -A seen_config_dirs=()

    if [[ -n "${Qt6_DIR:-}" ]]; then
        search_roots+=("$Qt6_DIR")
    fi
    if [[ -n "${CMAKE_PREFIX_PATH:-}" ]]; then
        local old_ifs=$IFS
        IFS=:
        read -r -a cmake_prefixes <<< "$CMAKE_PREFIX_PATH"
        IFS=$old_ifs
        for search_root in "${cmake_prefixes[@]}"; do
            [[ -n "$search_root" ]] && search_roots+=("$search_root")
        done
    fi
    search_roots+=(
        "${HOME}/Qt"
        "${HOME}/.local/opt/Qt"
        "${HOME}/.local/share/Qt"
        /opt/Qt
        /usr/local/Qt
        /usr/lib64/cmake
        /usr/lib/x86_64-linux-gnu/cmake
        /usr/lib/cmake
    )

    for search_root in "${search_roots[@]}"; do
        [[ -d "$search_root" ]] || continue
        while IFS= read -r config_file; do
            config_dir=${config_file%/*}
            if [[ -z "${seen_config_dirs[$config_dir]+present}" ]]; then
                seen_config_dirs[$config_dir]=1
                config_dirs+=("$config_dir")
            fi
        done < <(find "$search_root" -type f -path '*/lib/cmake/Qt6/Qt6Config.cmake' -print 2>/dev/null)
        while IFS= read -r config_file; do
            config_dir=${config_file%/*}
            if [[ -z "${seen_config_dirs[$config_dir]+present}" ]]; then
                seen_config_dirs[$config_dir]=1
                config_dirs+=("$config_dir")
            fi
        done < <(find "$search_root" -type f -path '*/lib64/cmake/Qt6/Qt6Config.cmake' -print 2>/dev/null)
        if [[ -f "$search_root/Qt6Config.cmake" && -z "${seen_config_dirs[$search_root]+present}" ]]; then
            seen_config_dirs[$search_root]=1
            config_dirs+=("$search_root")
        fi
    done

    for config_dir in "${config_dirs[@]}"; do
        qt_prefix=${config_dir%/lib/cmake/Qt6}
        qt_prefix=${qt_prefix%/lib64/cmake/Qt6}
        detected_version=""
        for qmake_candidate in "$qt_prefix/bin/qmake" "$qt_prefix/bin/qmake6"; do
            if [[ -x "$qmake_candidate" ]]; then
                detected_version=$("$qmake_candidate" -query QT_VERSION 2>/dev/null || true)
                [[ -n "$detected_version" ]] && break
            fi
        done
        if [[ -z "$detected_version" && -x "$qt_prefix/bin/qtpaths6" ]]; then
            detected_version=$("$qt_prefix/bin/qtpaths6" --query QT_VERSION 2>/dev/null || true)
        fi
        if [[ -z "$detected_version" ]]; then
            local pkg_config_paths="$qt_prefix/lib/pkgconfig:$qt_prefix/lib64/pkgconfig"
            detected_version=$(PKG_CONFIG_PATH="$pkg_config_paths${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}" \
                pkg-config --modversion Qt6Widgets 2>/dev/null || true)
        fi

        if [[ -n "$detected_version" ]] && version_at_least "$minimum_qt_version" "$detected_version"; then
            qt6_config_dir=$config_dir
            qt6_prefix=$qt_prefix
            qt6_version=$detected_version
            return 0
        fi
    done

    return 1
}

install_qt() {
    local qt_installer
    if [[ ! -t 0 ]]; then
        echo "setup.sh: Qt 6.12 is not installed and Qt's installer needs an interactive terminal for account and license prompts." >&2
        return 1
    fi
    if [[ "$(uname -m)" != x86_64 ]]; then
        echo "setup.sh: automatic Qt installation currently supports x86_64 Linux only." >&2
        return 1
    fi
    if ! has_command curl; then
        echo "setup.sh: curl is required to download Qt's official installer." >&2
        return 1
    fi

    qt_installer_tmpdir=$(mktemp -d)
    qt_installer="$qt_installer_tmpdir/qt-online-installer.run"
    echo "Downloading Qt's official Linux installer..."
    curl --fail --location --silent --show-error \
        https://download.qt.io/official_releases/online_installers/qt-online-installer-linux-x64-online.run \
        --output "$qt_installer"
    chmod +x "$qt_installer"
    echo "Installing Qt 6.12 and Qt SVG under $qt_install_root. The installer will ask for Qt account and license confirmation."
    "$qt_installer" --root "$qt_install_root" install qt6.12.0-essentials-dev qt.qt6.6120.addons.qtsvg
}

if ! has_command cmake || ! has_command c++ || ! has_command pkg-config || \
    ! pkg-config --exists gl || \
    ! version_at_least "$minimum_cmake_version" "$(cmake --version 2>/dev/null | awk 'NR == 1 { print $3 }' || true)"; then
    install_build_tools
fi

if ! discover_qt; then
    echo "Qt Widgets ${minimum_qt_version}+ was not found in the standard or user Qt installation paths."
    install_qt
    if ! discover_qt; then
        echo "setup.sh: Qt installation finished, but a usable Qt ${minimum_qt_version}+ CMake package was not found." >&2
        exit 1
    fi
fi

install_secret_service_client

cmake_version=$(cmake --version | awk 'NR == 1 { print $3 }')
if ! version_at_least "$minimum_cmake_version" "$cmake_version"; then
    printf 'setup.sh: CMake %s is too old; Qt 6.12 requires CMake %s or newer.\n' "$cmake_version" "$minimum_cmake_version" >&2
    exit 1
fi

printf 'CMake: %s (OK)\n' "$cmake_version"
printf 'C++ compiler: %s (OK)\n' "$(command -v c++)"
printf 'Qt Widgets: %s at %s\n' "$qt6_version" "$qt6_prefix"
printf 'Configuring and building with this Qt installation...\n'
cmake -S "$repo_dir" -B "$repo_dir/build" -DQt6_DIR="$qt6_config_dir"
cmake --build "$repo_dir/build" --parallel

install_prefix=${PRODUCTIVITY_INSTALL_PREFIX:-/usr/local}
printf 'Installing the application to %s...\n' "$install_prefix"
install_prefix_parent=$(dirname -- "$install_prefix")
if [[ -w "$install_prefix" || -w "$install_prefix_parent" ]] || ((EUID == 0)); then
    cmake --install "$repo_dir/build" --prefix "$install_prefix"
else
    run_as_root cmake --install "$repo_dir/build" --prefix "$install_prefix"
fi

autostart_directory="${XDG_CONFIG_HOME:-${HOME}/.config}/autostart"
install -D -m 0644 \
    "$repo_dir/resources/linux/personal-productivity-app-autostart.desktop" \
    "$autostart_directory/personal-productivity-app.desktop"
printf 'Configured background startup in %s.\n' "$autostart_directory"
