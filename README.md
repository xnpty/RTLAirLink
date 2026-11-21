# RTLAirLink

Experimental Wi-Fi and native AWDL/AirDrop for Realtek RTL8852BE on Intel macOS Tahoe.

**Native AWDL research, design and implementation by Anh Tài (xnpty) in Owly/RTLAirLink.** [Credits and source history](CREDITS.md)

[![Support on Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/taingo) · [Discord](https://discord.com/users/856029147750858793) · [Tiếng Việt](README_VI.md)

## Install v0.2.0

Run on an Intel Mac with macOS Tahoe, Python 3.9 or newer, and an existing OpenCore EFI.

~~~bash
curl -fL --proto '=https' --proto-redir '=https' -o install-rtlairlink.sh https://github.com/xnpty/RTLAirLink/releases/download/v0.2.0/install-rtlairlink.sh && bash install-rtlairlink.sh
~~~

The installer downloads the release and pinned dependencies, previews the OpenCore changes, backs up the affected EFI files, and installs the cohort. The release ZIP also includes the double-clickable Install-RTLAirLink.command. [Installation details](docs/INSTALL.md)

| Bundle | Role |
| --- | --- |
| RTLAirLink.kext | Realtek Wi-Fi driver with the RTLAirLinkFront.kext frontend inside |
| RTLAirBridge.kext | Lilu event bridge matched to the frontend UUID |

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
