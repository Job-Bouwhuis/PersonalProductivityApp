#!/usr/bin/env bash

set -euo pipefail

repo_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
install_prefix=${PRODUCTIVITY_INSTALL_PREFIX:-/usr/local}
manifest="$repo_dir/build/install_manifest.txt"

if [[ ! -f "$manifest" ]]; then
    printf 'uninstall.sh: no CMake install manifest found at %s. Nothing was removed.\n' "$manifest" >&2
    exit 1
fi

if [[ "$install_prefix" != /* ]]; then
    echo "uninstall.sh: PRODUCTIVITY_INSTALL_PREFIX must be an absolute path." >&2
    exit 1
fi

run_as_root() {
    if ((EUID == 0)); then
        "$@"
    elif command -v sudo >/dev/null 2>&1; then
        sudo "$@"
    else
        echo "uninstall.sh: removing installed system files requires root or sudo." >&2
        return 1
    fi
}

removed_count=0
while IFS= read -r installed_path; do
    [[ -n "$installed_path" ]] || continue
    case "$installed_path" in
        "$install_prefix"/*)
            if [[ -f "$installed_path" || -L "$installed_path" ]]; then
                if [[ -w "$(dirname -- "$installed_path")" ]] || ((EUID == 0)); then
                    rm -f -- "$installed_path"
                else
                    run_as_root rm -f -- "$installed_path"
                fi
                ((removed_count += 1))
            fi
            ;;
        *)
            printf 'uninstall.sh: refusing to remove manifest entry outside %s: %s\n' "$install_prefix" "$installed_path" >&2
            exit 1
            ;;
    esac
done < "$manifest"

for runtime_asset_path in \
    "$install_prefix/lib/personal-productivity-app" \
    "$install_prefix/share/personal-productivity-app" \
    "$install_prefix/bin/qt.conf"; do
    if [[ -e "$runtime_asset_path" || -L "$runtime_asset_path" ]]; then
        if [[ -w "$(dirname -- "$runtime_asset_path")" ]] || ((EUID == 0)); then
            rm -rf -- "$runtime_asset_path"
        else
            run_as_root rm -rf -- "$runtime_asset_path"
        fi
    fi
done

autostart_file="${XDG_CONFIG_HOME:-${HOME}/.config}/autostart/personal-productivity-app.desktop"
if [[ -f "$autostart_file" ]]; then
    rm -f -- "$autostart_file"
fi

printf 'Removed %d installed application files from %s. User data was left untouched.\n' "$removed_count" "$install_prefix"
