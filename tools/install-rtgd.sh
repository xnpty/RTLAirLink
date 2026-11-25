#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Install or uninstall rtlairlink-rtgd (automatic RTG daemon for smooth iPhone Mirroring).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_BIN="$SCRIPT_DIR/rtlairlink-rtgd"
SRC_CTL="$SCRIPT_DIR/rtlairlinkctl"
DST_DIR="/usr/local/libexec"
DST_BIN="$DST_DIR/rtlairlink-rtgd"
DST_CTL="/usr/local/bin/rtlairlinkctl"
LABEL="io.github.xnpty.rtlairlink.rtgd"
PLIST="/Library/LaunchDaemons/${LABEL}.plist"
LOG="/var/log/rtlairlink-rtgd.log"

case "${1:-}" in
    --status)
        if [[ -x "$DST_BIN" ]]; then
            "$DST_BIN" --status
        else
            echo "rtlairlink-rtgd is not installed ($DST_BIN not found)."
            exit 1
        fi
        exit 0
        ;;
    --uninstall)
        if [[ "$(id -u)" -ne 0 ]]; then
            echo "Run with sudo: sudo bash $0 --uninstall" >&2
            exit 1
        fi
        launchctl bootout "system/${LABEL}" 2>/dev/null || true
        rm -f "$PLIST" "$DST_BIN"
        echo "Removed ${LABEL} and ${DST_BIN}."
        exit 0
        ;;
    ""|--install)
        ;;
    *)
        echo "Usage: sudo bash $0 [--install | --uninstall | --status]" >&2
        exit 2
        ;;
esac

if [[ "$(id -u)" -ne 0 ]]; then
    echo "Run with sudo: sudo bash $0" >&2
    exit 1
fi

if [[ ! -x "$SRC_BIN" ]]; then
    echo "ERROR: rtlairlink-rtgd binary not found at $SRC_BIN" >&2
    exit 1
fi

mkdir -p "$DST_DIR"
launchctl bootout "system/${LABEL}" 2>/dev/null || true
cp "$SRC_BIN" "$DST_BIN"
chown root:wheel "$DST_BIN"
chmod 755 "$DST_BIN"
/usr/bin/codesign --force --sign - --timestamp=none "$DST_BIN" >/dev/null 2>&1 || true

if [[ -x "$SRC_CTL" ]]; then
    mkdir -p /usr/local/bin
    cp "$SRC_CTL" "$DST_CTL"
    chown root:wheel "$DST_CTL"
    chmod 755 "$DST_CTL"
    /usr/bin/codesign --force --sign - --timestamp=none "$DST_CTL" >/dev/null 2>&1 || true
fi

cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>${LABEL}</string>
    <key>ProgramArguments</key>
    <array>
        <string>${DST_BIN}</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>StandardErrorPath</key>
    <string>${LOG}</string>
    <key>StandardOutPath</key>
    <string>${LOG}</string>
</dict>
</plist>
EOF

chown root:wheel "$PLIST"
chmod 644 "$PLIST"
/usr/bin/plutil -lint "$PLIST" >/dev/null

launchctl bootstrap system "$PLIST"
launchctl kickstart -k "system/${LABEL}"
echo "Installed ${LABEL} (${DST_BIN})."
if [[ -x "$DST_CTL" ]]; then
    echo "Installed CLI tool (${DST_CTL})."
fi
