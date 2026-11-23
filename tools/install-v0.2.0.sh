#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# RTLAirLink release installer.
set -euo pipefail
scratch="$(/usr/bin/mktemp -d "${TMPDIR:-/tmp}/rtlairlink.XXXXXXXX")"
trap '/bin/rm -rf "$scratch"' EXIT
archive="$scratch/RTLAirLink-v0.2.0-Setup.zip"
/usr/bin/curl --proto '=https' --proto-redir '=https' -fsSL --retry 2 \
    --max-time 180 -o "$archive" 'https://github.com/xnpty/RTLAirLink/releases/download/v0.2.0-setup/RTLAirLink-v0.2.0-Setup.zip'
actual="$(/usr/bin/shasum -a 256 "$archive")"
if [[ "${actual%% *}" != '03b87090c8b0f4047c1f312c010477e04fb3cc34ef3384df86f2b00959fab385' ]]; then
    echo 'RTLAirLink download checksum mismatch.' >&2
    exit 1
fi
/usr/bin/ditto -x -k "$archive" "$scratch"
/bin/bash "$scratch/RTLAirLink-v0.2.0/Install-RTLAirLink.command" "$@"
