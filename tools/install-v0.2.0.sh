#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Use the updated OpenCore installer with the immutable v0.2.0 binary archive.
set -euo pipefail

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
download_verified 'https://raw.githubusercontent.com/xnpty/RTLAirLink/a0c1768bb6f9ce7fcb72fd9211be586502313387/tools/install_efi.py' \
    "$package/tools/install_efi.py" '63cb1d0f11410342506dd4fbfe4870344aa485d978ed22c378f1ff90b3d19d98'
/bin/bash "$package/Install-RTLAirLink.command" "$@"
