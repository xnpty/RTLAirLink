#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Added 2026-11-21 for RTLAirLink 0.2.0.
set -euo pipefail
cd "$(dirname "$0")"
for python in "$(command -v python3 || true)" /usr/local/bin/python3 /opt/local/bin/python3 /usr/bin/python3; do
    if [[ -x "$python" ]] && "$python" -c 'import sys; sys.exit(sys.version_info < (3, 9))' >/dev/null 2>&1; then
        exec "$python" tools/install_efi.py "$@"
    fi
done
echo 'Install Python 3.9 or newer from https://www.python.org/downloads/macos/ and reopen Install-RTLAirLink.command.' >&2
exit 1
