# Dependencies

The installer reuses Intel builds of `Lilu.kext` 1.7.3 or newer and `AMFIPass.kext` 1.4.1 or newer. It checks the bundle IDs, versions and executable digests of `IOSkywalkFamily.kext` and `IO80211FamilyLegacy.kext`. It obtains missing or incompatible dependencies from the pinned providers and verifies each archive's size and SHA-256 before extracting it. [Version and checksum lock](../DEPENDENCIES.json) · [Third-party credits](../CREDITS.md)

For manual Wi-Fi setup, download the matching archives from their providers.

- [Lilu 1.7.3](https://github.com/dortania/build-repo/releases/download/Lilu-0249c3a/Lilu-1.7.3-RELEASE.zip)
- [IOSkywalkFamily 1.2.0](https://raw.githubusercontent.com/dortania/OpenCore-Legacy-Patcher/d9604c36a432eaf243ea18659ff4d208187452d7/payloads/Kexts/Wifi/IOSkywalkFamily-v1.2.0.zip)
- [IO80211FamilyLegacy 1.0.0](https://raw.githubusercontent.com/dortania/OpenCore-Legacy-Patcher/d9604c36a432eaf243ea18659ff4d208187452d7/payloads/Kexts/Wifi/IO80211FamilyLegacy-v1.0.0.zip)
- [AMFIPass 1.4.1](https://raw.githubusercontent.com/dortania/OpenCore-Legacy-Patcher/d9604c36a432eaf243ea18659ff4d208187452d7/payloads/Kexts/Acidanthera/AMFIPass-v1.4.1-RELEASE.zip)

For supported Realtek USB Bluetooth controllers, `--with-bluetooth` adds `BlueToolFixup.kext` 2.7.3 from Dortania's build of Acidanthera BrcmPatchRAM and `RealtekBluetoothFirmware.kext` 1.0.2 from thegwchr. The installer reuses BlueToolFixup 2.7.2 or newer and the matching firmware bundle. [Bluetooth download versions and supported USB IDs](../DEPENDENCIES.json)

Use `./Download-Dependencies.command --with-bluetooth` from the source tree to pre-download all dependency archives. The installer uses `Downloads/RTLAirLink-Dependencies` by default. Use `--cache` to point to another directory.
