#!/usr/bin/env bash

set -euo pipefail

repository_url="https://github.com/Job-Bouwhuis/PersonalProductivityApp.git"
source_directory="${PRODUCTIVITY_SOURCE_DIR:-${HOME}/.local/src/PersonalProductivityApp}"

if ! command -v git >/dev/null 2>&1; then
    echo "bootstrap.sh: git is required to clone the repository." >&2
    exit 1
fi

if [[ -e "$source_directory" ]]; then
    printf 'bootstrap.sh: destination already exists: %s\n' "$source_directory" >&2
    echo "Set PRODUCTIVITY_SOURCE_DIR to a different path or remove the existing checkout first." >&2
    exit 1
fi

mkdir -p "$(dirname -- "$source_directory")"
printf 'Cloning %s into %s...\n' "$repository_url" "$source_directory"
if ! git clone -- "$repository_url" "$source_directory"; then
    printf 'bootstrap.sh: clone failed; setup was not run. Check the destination before retrying: %s\n' "$source_directory" >&2
    exit 1
fi

if ! "$source_directory/setup.sh"; then
    echo "bootstrap.sh: repository cloned, but setup failed. The checkout was preserved for inspection and retry." >&2
    exit 1
fi
