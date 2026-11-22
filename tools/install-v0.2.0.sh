#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Use the updated OpenCore installer with the immutable v0.2.0 binary archive.
set -euo pipefail

if ! python3 -c 'import sys; sys.exit(sys.version_info < (3, 9))' >/dev/null 2>&1; then
    echo 'RTLAirLink requires Python 3.9 or newer.' >&2
    exit 1
fi

scratch="$(/usr/bin/mktemp -d "${TMPDIR:-/tmp}/rtlairlink.XXXXXXXX")"
trap '/bin/rm -rf "$scratch"' EXIT

download_verified() {
    local url="$1" target="$2" expected="$3" actual
    /usr/bin/curl --proto '=https' --proto-redir '=https' -fsSL --retry 2 \
        --max-time 180 -o "$target" "$url"
    actual="$(/usr/bin/shasum -a 256 "$target")"
    if [[ "${actual%% *}" != "$expected" ]]; then
        echo "RTLAirLink checksum mismatch for ${target##*/}." >&2
        exit 1
    fi
}

archive="$scratch/RTLAirLink-v0.2.0-x86_64.zip"
download_verified 'https://github.com/xnpty/RTLAirLink/releases/download/v0.2.0/RTLAirLink-v0.2.0-x86_64.zip' \
    "$archive" 'c565bb78a0f7694b8f17d1866b38c2dbb8a7488fec17706c3b64660ee4134757'
/usr/bin/ditto -x -k "$archive" "$scratch"
package="$scratch/RTLAirLink-v0.2.0"
source='https://raw.githubusercontent.com/xnpty/RTLAirLink/6c6ca29821689d3951d62ba6e3f627d256a7a3ff'
download_verified "$source/tools/install_efi.py" \
    "$package/tools/install_efi.py" '58623fc3981499ebd6051f85ac3f31048054af14d01d9e5be9f6f857995d4e56'
download_verified "$source/tools/download_dependencies.py" \
    "$package/tools/download_dependencies.py" '1f171ea240dee1e802ec9b84c713f314061cfca3ed0fb2096082685b7d558d2f'
download_verified "$source/DEPENDENCIES.json" \
    "$package/DEPENDENCIES.json" 'ebd214566531ba1c74b60557776632a63fa6b4150028eaeb8251d2c880a5e8e8'
/bin/bash "$package/Install-RTLAirLink.command" "$@"
