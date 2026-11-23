# RTLAirLink

The first native AWDL/AirDrop implementation for Realtek on macOS Tahoe.

**Native AWDL research, design and implementation by Anh Tài (xnpty) in Owly/RTLAirLink.** [Credits and source history](CREDITS.md)

[![Support on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/taingo) · [Discord](https://discord.com/users/856029147750858793) · [Tiếng Việt](README_VI.md)

## Requirements

- macOS Tahoe 26 on x86_64 (Intel or AMD), a Realtek RTL8852BE, and a working OpenCore EFI.
- Python 3.9 or newer to run the installer.
- AirDrop discovery uses Bluetooth. The installer can set up a supported Realtek USB Bluetooth controller.

Wi-Fi uses `Lilu.kext` (1.7.3 or newer) and three legacy dependencies. [Versions and sources](docs/DEPENDENCIES.md)

- `IOSkywalkFamily.kext`
- `IO80211FamilyLegacy.kext`
- `AMFIPass.kext`

## Install v0.2.0

1. Download and extract [RTLAirLink-v0.2.0-Setup.zip](https://github.com/xnpty/RTLAirLink/releases/download/v0.2.0-setup/RTLAirLink-v0.2.0-Setup.zip).
2. Open `Install-RTLAirLink.command`. Check the EFI in the preview and enter `y` to back up and install.
3. Restart macOS.

The installer uses compatible kexts already on the EFI, downloads missing Wi-Fi dependencies, and adds the required OpenCore settings. [Load order and manual setup](docs/INSTALL.md)

To install from Terminal, run the following command.

~~~bash
curl -fLO https://github.com/xnpty/RTLAirLink/releases/download/v0.2.0-setup/install-rtlairlink-setup.sh && \
  bash install-rtlairlink-setup.sh
~~~

For a supported Realtek USB Bluetooth controller, run `bash install-rtlairlink-setup.sh --with-bluetooth`. [Bluetooth setup](docs/INSTALL.md#realtek-bluetooth-for-airdrop)

| Bundle | Role |
| --- | --- |
| RTLAirLink.kext | Realtek Wi-Fi driver with the RTLAirLinkFront.kext frontend inside |
| RTLAirBridge.kext | Lilu event bridge matched to the frontend UUID |
| BlueToolFixup.kext and RealtekBluetoothFirmware.kext | Bluetooth setup for supported Realtek USB controllers |

## Troubleshooting and reporting

- **Installer validation fails.** Run `bash install-rtlairlink-setup.sh --efi /Volumes/EFI --dry-run` with your EFI's mount path. If your OpenCore build has a different schema, pass `--ocvalidate` with the validator from that build. [Installation details](docs/INSTALL.md#preview-and-validate)
- **macOS fails to start.** Boot from a working backup OpenCore EFI and restore the affected files. [EFI recovery](docs/INSTALL.md#recover-the-efi)
- **Wi-Fi is missing after restart.** Verify which EFI booted and check whether the Wi-Fi kexts loaded.

~~~bash
kmutil showloaded --list-only | grep -Ei 'Lilu|IOSkywalkFamily|IO80211FamilyLegacy|AMFIPass|io\.github\.xnpty\.RTLAirLink|BlueToolFixup|RealtekBluetoothFirmware'
~~~

- **AirDrop is stuck.** Check Bluetooth, confirm `awdl0` has a link-local IPv6 address with `ifconfig awdl0`, and note the direction of the transfer and the stage where it stopped.

Include the macOS version and build (`sw_vers`), OpenCore version, Wi-Fi card and PCI ID, steps to reproduce, and the relevant installer or kext output. Remove device identifiers and private network details from any logs you share. [Open a GitHub issue](https://github.com/xnpty/RTLAirLink/issues/new)

## Status

The hardware test used an RTL8852BE on macOS build 25G241 with Bluetooth configured separately. The driver source underlying this release completed transfers both ways over IPv6 on awdl0. Six of eight Mac-to-iPhone sends succeeded, and eight iPhone-to-Mac transfer matched the original SHA-256. Two sends stalled during Companion discovery. The v0.2.0 binaries have build and installer checks. Their first hardware boot is pending. [Verification details](docs/STATUS.md)

## Build

~~~bash
git clone https://github.com/xnpty/RTLAirLink.git
cd RTLAirLink
bash build.sh
~~~

The builder runs the driver host tests, matches the bridge to the compiled frontend, signs the kexts, and creates the binary and source archives. [Build details](docs/BUILD.md)

## License

RTLAirLink's driver and bridge are distributed under GPLv2. Firmware and dependencies retain their own licenses. [Credits](CREDITS.md) · [License notices](LICENSES/)
