# Install RTLAirLink 0.4.0

Use this release on x86_64 macOS Tahoe 26 with an existing OpenCore EFI. Hardware verification was performed on **Realtek RTL8852BE** (`macOS Tahoe 25G241`, `OpenCore 1.0.9`). Other Realtek PCIe cards listed in the README are **untested**, and AirDrop/AWDL may not work on them.

## Option 1: One-command Terminal install (recommended)

~~~bash
curl -fsSL https://github.com/xnpty/RTLAirLink/releases/download/0.4.0/install-rtlairlink.sh | bash
~~~

With bundled Realtek USB Bluetooth kexts (`BlueToolFixup.kext` + `RealtekBluetoothFirmware.kext`):

~~~bash
curl -fsSL https://github.com/xnpty/RTLAirLink/releases/download/0.4.0/install-rtlairlink.sh | bash -s -- --with-bluetooth
~~~

## Option 2: Download ZIP and run locally (works offline)

1. Download and extract [RTLAirLink-v0.4.0-x86_64.zip](https://github.com/xnpty/RTLAirLink/releases/download/0.4.0/RTLAirLink-v0.4.0-x86_64.zip).
2. Double-click `Install-RTLAirLink.command` (or run `bash Install-RTLAirLink.command` in Terminal).
3. Review the preview (target EFI, kexts to install/replace, load order, and boot-args), enter `y` to back up and apply, and optionally enter `y` to install the `rtlairlink-rtgd` background service for smooth iPhone Mirroring.
4. Restart macOS.

## What is bundled in v0.4.0

All required Tahoe-verified kexts and `ocvalidate` are now bundled directly inside `RTLAirLink-v0.4.0-x86_64.zip`:

- `Kexts/Lilu.kext` (1.7.3)
- `Kexts/RTLAirBridge.kext` (0.4.0, UUID-bound to `RTLAirLinkFront.kext`)
- `Kexts/IOSkywalkFamily.kext` (1.0 / v1.2.0)
- `Kexts/IO80211FamilyLegacy.kext` (1200.12.2b1, clean with `AirPortBrcmNIC.kext` removed)
- `Kexts/AMFIPass.kext` (1.4.1, Tahoe-verified build)
- `Kexts/RTLAirLink.kext` (0.4.0, containing `Contents/PlugIns/RTLAirLinkFront.kext`)
- `Kexts-Bluetooth/BlueToolFixup.kext` (2.7.3) & `RealtekBluetoothFirmware.kext` (1.0.2)
- `tools/rtlairlinkctl`, `tools/rtlairlink-rtgd`, `tools/install-rtgd.sh`, `tools/ocvalidate`

By default, the installer **replaces** any differing or older copies of these kexts on your EFI (after saving a verified backup under `~/Downloads/RTLAirLink-Backups/`) so that patched/verified Tahoe builds are guaranteed to match.

## Useful installer flags

| Flag | Effect |
| --- | --- |
| `--efi /Volumes/EFI` | Target a specific mounted EFI volume, `OC` folder, or `config.plist` |
| `--dry-run` | Preview and validate the full plan without modifying the EFI |
| `--with-bluetooth` | Also install/replace bundled `BlueToolFixup.kext` + `RealtekBluetoothFirmware.kext`, add `-btlfxallowanyaddr`, and set Bluetooth NVRAM keys |
| `--no-wpa3` | Add `-rtw89nowpa3` to `boot-args` if your router has WPA3/SAE issues |
| `--with-rtgd` / `--no-rtgd` | Install or skip the `rtlairlink-rtgd` iPhone Mirroring RTG daemon without prompting |
| `--keep-existing-deps` | Reuse compatible existing `Lilu.kext` / `AMFIPass.kext` / Bluetooth kexts instead of replacing with bundled copies |
| `--ocvalidate /path/to/ocvalidate` | Validate `config.plist` with a custom OpenCore validator |
| `--yes` | Apply non-interactively (requires `--efi`) |

Example dry run:

~~~bash
curl -fsSL https://github.com/xnpty/RTLAirLink/releases/download/0.4.0/install-rtlairlink.sh | bash -s -- --efi /Volumes/EFI --dry-run
~~~

## Exact OpenCore Load Order and Settings

The installer enforces the following `Kernel → Add` order at the top of your `config.plist` (preserving all of your other kexts underneath):

1. `Lilu.kext`
2. `RTLAirBridge.kext` (`MinKernel: 25.0.0`, `MaxKernel: 25.99.99`)
3. `IOSkywalkFamily.kext` (`MinKernel: 23.0.0`)
4. `IO80211FamilyLegacy.kext` (`MinKernel: 23.0.0`)
5. `AMFIPass.kext`
6. `RTLAirLink.kext` (`MinKernel: 25.0.0`, `MaxKernel: 25.99.99`)
7. `RTLAirLink.kext/Contents/PlugIns/RTLAirLinkFront.kext` (`MinKernel: 25.0.0`, `MaxKernel: 25.99.99`)
8. *(Optional `--with-bluetooth`)* `BlueToolFixup.kext` (`MinKernel: 21.0.0`)
9. *(Optional `--with-bluetooth`)* `RealtekBluetoothFirmware.kext`

Additional OpenCore settings configured automatically:
- **`Kernel → Block`:** Enable `com.apple.iokit.IOSkywalkFamily` (`Strategy: Exclude`, `Arch: x86_64`, `MinKernel: 23.0.0`).
- **`Misc → Security → SecureBootModel`:** `Disabled`.
- **`NVRAM → Add → 7C436110-AB2A-4BBB-A880-FE41995C9F82 → boot-args`:** Adds `-amfipassbeta` (plus `-btlfxallowanyaddr` with `--with-bluetooth` and `-rtw89nowpa3` with `--no-wpa3`), removes obsolete `-owlyeb1*` / `-broadtekp2poff` flags, and adds `boot-args` to `NVRAM → Delete` under the same GUID.
- **OpenCore MOD compatibility:** Automatically preserves `ACPI/Quirks/EnableForAll`, `Booter/Quirks/EnableForAll`, and `Misc/Boot/SkipCustomEntryCheck` when validating with stock `ocvalidate`.

## iPhone Mirroring RTG Daemon (`rtlairlink-rtgd`)

> **Note:** Enabling Apple's iPhone Mirroring on an Intel Hackintosh requires **[Mirra](https://github.com/eiyuumiru/Mirra)** (credits to **eiyuumiru** and **Dino-VN**). RTLAirLink's AWDL RTG support + `rtlairlink-rtgd` handles the wireless side so video streaming stays smooth.

`rtlairlink-rtgd` runs as a lightweight LaunchDaemon (`/Library/LaunchDaemons/io.github.xnpty.rtlairlink.rtgd.plist`). Whenever **iPhone Mirroring** establishes an active connection over `awdl0`, it automatically registers AWDL Real-Time Guided (`MPRemoteCamera`, `0x40`) so AWDL holds 16/16 slots (`shared_slots=0xffff`) instead of dropping to `0x8e87` every 262 ms, and cleanly clears RTG when Mirroring closes.

Manage it manually from the extracted ZIP folder at any time:

~~~bash
sudo bash tools/install-rtgd.sh              # Install / reinstall daemon + rtlairlinkctl
bash tools/install-rtgd.sh --status          # Check status
sudo bash tools/install-rtgd.sh --uninstall  # Remove daemon
~~~

## Recover the EFI

Every install with changes backs up the original `config.plist` and every replaced or removed kext to `~/Downloads/RTLAirLink-Backups/RTLAirLink-YYYYMMDD-HHMMSS-.../`. If post-install validation fails, the installer automatically rolls back all changes. To recover manually, boot from a backup OpenCore EFI and restore `config.plist` and the saved kexts from that backup folder.
