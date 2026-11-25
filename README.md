# RTLAirLink

Native Wi-Fi, AWDL/AirDrop (~59.2 MB/s wire two-way), and smooth iPhone Mirroring for Realtek RTL8852BE on macOS Tahoe (x86_64).

**Native AWDL research, design, and implementation by Anh Tài (xnpty) in Owly/RTLAirLink.** [Credits and source history](CREDITS.md)

[![Support on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/taingo) · [Discord](https://discord.com/users/856029147750858793) · [Tiếng Việt](README_VI.md)

## Supported Realtek PCIe Cards

RTLAirLink carries the same Realtek `rtw89` PCIe chip tables and firmware set as `AirPort_RTW89`. **Only `RTL8852BE` has been verified on real hardware.** All other cards are **untested**, and **AirDrop / AWDL may not work** on them because the native AWDL radio/CAM/BlockAck paths currently target `RTL8852B`.

| Card Model | PCI IDs (`10ec:xxxx`) | Wi-Fi Standard | Status | AirDrop / AWDL |
| --- | --- | --- | --- | --- |
| **RTL8852BE** | `10ec:b852`, `10ec:b85b` | Wi-Fi 6 (802.11ax, 2×2, 80 MHz) | **Tested & Verified** | **Supported** (~59.2 MB/s wire 2-way, iPhone Mirroring) |
| **RTL8851BE** | `10ec:b851` | Wi-Fi 6 (802.11ax, 1×1, 80 MHz) | **Untested** | **Untested — AirDrop may not work** |
| **RTL8852AE** | `10ec:8852`, `10ec:a85a` | Wi-Fi 6 (802.11ax, 2×2, 80 MHz) | **Untested** | **Untested — AirDrop may not work** |
| **RTL8852BTE** | `10ec:b520` | Wi-Fi 6 (802.11ax, 2×2, 80 MHz) | **Untested** | **Untested — AirDrop may not work** |
| **RTL8852CE** | `10ec:c852` | Wi-Fi 6E (802.11ax, 2×2, 160 MHz) | **Untested** | **Untested — AirDrop may not work** |
| **RTL8922AE** | `10ec:8922`, `10ec:892b` | Wi-Fi 7 (802.11be, 2×2, 160 MHz) | **Untested** | **Untested — AirDrop may not work** |
| **RTL8922DE** | `10ec:892d`, `10ec:882d`, `10ec:895d` | Wi-Fi 7 (802.11be, 2×2, 160 MHz) | **Untested** | **Untested — AirDrop may not work** |

## Requirements

- macOS Tahoe 26 on x86_64 (Intel or AMD) with a working OpenCore EFI.
- Python 3.9 or newer to run the automated installer.
- AirDrop discovery uses Bluetooth. The release bundles `BlueToolFixup.kext` and `RealtekBluetoothFirmware.kext` for supported Realtek USB Bluetooth controllers (`--with-bluetooth`).

## Install v0.4.0

### One-command Terminal install (recommended)

~~~bash
curl -fsSL https://github.com/xnpty/RTLAirLink/releases/download/v0.4.0/install-rtlairlink.sh | bash
~~~

To also install or replace the bundled Realtek USB Bluetooth kexts (`BlueToolFixup.kext` + `RealtekBluetoothFirmware.kext` + `-btlfxallowanyaddr` + Bluetooth NVRAM keys):

~~~bash
curl -fsSL https://github.com/xnpty/RTLAirLink/releases/download/v0.4.0/install-rtlairlink.sh | bash -s -- --with-bluetooth
~~~

### Offline / GUI install

1. Download and extract [RTLAirLink-v0.4.0-x86_64.zip](https://github.com/xnpty/RTLAirLink/releases/download/v0.4.0/RTLAirLink-v0.4.0-x86_64.zip).
2. Open `Install-RTLAirLink.command`. Check the EFI target and preview, then enter `y` to back up and install.
3. Restart macOS.

**What the v0.4.0 installer does automatically:**
- **Bundles all required verified kexts** (`Lilu.kext`, `RTLAirBridge.kext`, `IOSkywalkFamily.kext`, `IO80211FamilyLegacy.kext`, `AMFIPass.kext`, `RTLAirLink.kext` + `RTLAirLinkFront.kext`, plus optional Bluetooth kexts and `ocvalidate`) — no external downloads needed.
- **Installs or replaces** missing/older/unpatched Wi-Fi kexts on your EFI after saving a verified backup to `~/Downloads/RTLAirLink-Backups/`.
- **Updates `config.plist`** with the exact required `Kernel → Add` load order, `IOSkywalkFamily` block (`Exclude`, `MinKernel 23.0.0`), `SecureBootModel = Disabled`, and required `boot-args` (`-amfipassbeta`, plus `-btlfxallowanyaddr` with `--with-bluetooth`), then validates with `ocvalidate`.
- **Optionally installs `rtlairlink-rtgd`** (`/usr/local/libexec/rtlairlink-rtgd` LaunchDaemon) and `rtlairlinkctl` so iPhone Mirroring automatically locks 16/16 AWDL slots (`0xffff`) for smooth 60 fps playback.

[Full installation guide, flags, and manual setup](docs/INSTALL.md)

## What's New in v0.4.0

- **Symmetric ~59.2 MB/s wire AirDrop (Mac ↔ iPhone):** Fixed the TX timestamp lock race, upgraded TX pipeline to 4096-entry queue + proactive TXBD reclaim + lock-free TX status + bidirectional 3-subframe A-MSDU (`4900 B` over 64-MPDU hardware A-MPDU), bringing both directions to **59.2 MB/s wire (~49–50 MB/s end-to-end)** with `0` TX refused / `0` TX expired.
- **Smooth iPhone Mirroring (RTG Peer Cache + Auto-RTG Daemon):** Implemented `APPLE80211_IOC_PEER_CACHE_MAXIMUM_SIZE (130)`, `APPLE80211_IOC_PEER_CACHE_CONTROL (143)`, and expedited `0x33` data-path events in `RTLAirLinkFront`, plus the new lightweight `rtlairlink-rtgd` daemon that eliminates 196–262 ms off-channel stutters (`0x8e87` → `0xffff`) automatically while iPhone Mirroring is connected.
- **Standalone 5 GHz & Stability Fixes:** Fixed VN 5 GHz regulatory TX power limits, standalone 5 GHz bringup and `SET 120` `ENXIO` race, multichannel `ch149 ↔ ch6` ping-pong, mid-transfer background scan stalls, Port 0 CAM shadowing on RX BlockAck, and broken HW RTS on AWDL peers.
- **All-in-One Release & One-Command Installer:** Bundles all Tahoe-verified companion kexts and `ocvalidate` right inside the release ZIP so setup works cleanly in one command.

[Detailed release notes](docs/RELEASE_NOTES.md) · [Verification benchmarks](docs/STATUS.md)

## Troubleshooting

- **Preview without writing:** `curl -fsSL https://github.com/xnpty/RTLAirLink/releases/download/v0.4.0/install-rtlairlink.sh | bash -s -- --efi /Volumes/EFI --dry-run`
- **Verify loaded kexts after reboot:**
  ~~~bash
  kmutil showloaded --list-only | grep -Ei 'Lilu|IOSkywalkFamily|IO80211FamilyLegacy|AMFIPass|io\.github\.xnpty\.RTLAirLink|BlueToolFixup|RealtekBluetoothFirmware'
  ~~~
- **Check iPhone Mirroring RTG daemon:** `/usr/local/libexec/rtlairlink-rtgd --status`
- **Recovery:** Every install saves `config.plist` and replaced kexts under `~/Downloads/RTLAirLink-Backups/`. [Recovery guide](docs/INSTALL.md#recover-the-efi) · [Open an issue](https://github.com/xnpty/RTLAirLink/issues/new)

## Build from Source

~~~bash
git clone https://github.com/xnpty/RTLAirLink.git
cd RTLAirLink
bash build.sh
~~~

[Build details](docs/BUILD.md) · [License (GPLv2)](LICENSE) · [Third-party notices](LICENSES/)
