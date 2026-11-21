#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Added 2026-11-21 for RTLAirLink 0.2.0.
set -euo pipefail
cd "$(dirname "$0")"
if ! command -v python3 >/dev/null 2>&1; then
    echo 'RTLAirLink requires Python 3.9 or newer.' >&2
    exit 1
fi
exec python3 tools/install_efi.py "$@"
