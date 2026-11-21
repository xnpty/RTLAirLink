# RTLAirLink

The First-ever native AWDL/AirDrop for Realtek RTL8852BE on Intel macOS Tahoe.

**Native AWDL research, design and implementation by Anh Tài (xnpty) in Owly/RTLAirLink.** [Credits and source history](CREDITS.md)

[![Support on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/taingo) · [Discord](https://discord.com/users/856029147750858793) · [Tiếng Việt](README_VI.md)

## Requirements

- Intel macOS Tahoe 26, a Realtek RTL8852BE, and a working OpenCore EFI.
- Python 3.9 or newer. The installer defaults to OpenCore 1.0.9's `ocvalidate`. [Using a different OpenCore version](docs/INSTALL.md)
- Working Bluetooth for AirDrop discovery.

Wi-Fi uses `Lilu.kext` (1.7.3 or newer) and three legacy dependencies. [Versions and sources](docs/DEPENDENCIES.md)

- `IOSkywalkFamily.kext`
- `IO80211FamilyLegacy.kext`
- `AMFIPass.kext`

## Install v0.2.0

~~~bash
curl -fL --proto '=https' --proto-redir '=https' -o install-rtlairlink.sh \
  https://raw.githubusercontent.com/xnpty/RTLAirLink/7541fd08ef2482a774b02e44ed5fa798a04ccd98/tools/install-v0.2.0.sh && \
  bash install-rtlairlink.sh
~~~

The installer reuses compatible kexts, downloads missing dependencies, and enables the required kexts in order. It adds `-amfipassbeta` on Tahoe if needed. Check the EFI in the preview, type `INSTALL` to back up and apply the changes, then restart macOS. [Load order and installation details](docs/INSTALL.md)

| Bundle | Role |
| --- | --- |
| RTLAirLink.kext | Realtek Wi-Fi driver with the RTLAirLinkFront.kext frontend inside |
| RTLAirBridge.kext | Lilu event bridge matched to the frontend UUID |

## Troubleshooting and reporting

- **Installer validation fails.** Run `bash install-rtlairlink.sh --efi /Volumes/EFI --dry-run` with your EFI's mount path. For OpenCore versions other than 1.0.9, pass `--ocvalidate` with the matching validator. [Installation details](docs/INSTALL.md)
- **macOS fails to start.** Boot from a working backup OpenCore EFI and restore the affected files. [EFI recovery](docs/INSTALL.md#recover-the-efi)
- **Wi-Fi is missing after restart.** Verify which EFI booted and check whether the Wi-Fi kexts loaded.

~~~bash
kmutil showloaded --list-only | grep -Ei 'Lilu|IOSkywalkFamily|IO80211FamilyLegacy|AMFIPass|io\.github\.xnpty\.RTLAirLink'
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
