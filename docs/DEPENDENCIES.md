# Dependencies

Starting with **v0.4.0**, `RTLAirLink-v0.4.0-x86_64.zip` bundles all required Tahoe-verified kexts and `ocvalidate` directly so installation works in one step and completely offline:

- `Kexts/Lilu.kext` (1.7.3, Acidanthera)
- `Kexts/IOSkywalkFamily.kext` (1.0 / v1.2.0, OCLP legacy Wi-Fi stack)
- `Kexts/IO80211FamilyLegacy.kext` (1200.12.2b1, OCLP legacy Wi-Fi stack with `AirPortBrcmNIC.kext` removed)
- `Kexts/AMFIPass.kext` (1.4.1, Tahoe-verified build)
- `Kexts-Bluetooth/BlueToolFixup.kext` (2.7.3, Acidanthera BrcmPatchRAM — optional via `--with-bluetooth`)
- `Kexts-Bluetooth/RealtekBluetoothFirmware.kext` (1.0.2, thegwchr — optional via `--with-bluetooth`)
- `tools/ocvalidate` (OpenCore 1.0.9 validator)

The installer verifies every bundled kext against `RELEASE_MANIFEST.json` before copying it to your EFI and backs up any replaced kexts under `~/Downloads/RTLAirLink-Backups/`.

When building from source without `--bundle-kexts-dir`, `build.py` and `tools/download_dependencies.py` continue to reference the pinned upstream archives in [DEPENDENCIES.json](../DEPENDENCIES.json). [Third-party credits](../CREDITS.md) · [Licenses](../LICENSES/)
