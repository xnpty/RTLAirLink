# Dependencies

The installer reuses Intel builds of `Lilu.kext` 1.7.3 or newer and `AMFIPass.kext` 1.4.1 or newer. It checks the bundle IDs, versions and executable digests of `IOSkywalkFamily.kext` and `IO80211FamilyLegacy.kext`. It obtains missing or incompatible dependencies from the pinned providers and verifies each archive's size and SHA-256 before extracting it. [Version and checksum lock](../DEPENDENCIES.json) · [Third-party credits](../CREDITS.md)

For supported Realtek USB Bluetooth controllers, `--with-bluetooth` adds `BlueToolFixup.kext` 2.7.3 from Dortania's build of Acidanthera BrcmPatchRAM and `RealtekBluetoothFirmware.kext` 1.0.2 from thegwchr. The installer reuses BlueToolFixup 2.7.2 or newer and the matching firmware bundle. [Bluetooth download versions and supported USB IDs](../DEPENDENCIES.json)

Use `./Download-Dependencies.command --with-bluetooth` from the source tree to pre-download all dependency archives. The installer uses `Downloads/RTLAirLink-Dependencies` by default. Use `--cache` to point to another directory.
