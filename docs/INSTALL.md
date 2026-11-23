# Install RTLAirLink 0.3.0

Use this release on x86_64 macOS Tahoe with an existing OpenCore installation and a Realtek RTL8852BE. Hardware testing used macOS build 25G241. The automated installer runs on macOS with Python 3.9 or newer.

## Install with the setup package

1. Download and extract [RTLAirLink-v0.3.0-x86_64.zip](https://github.com/xnpty/RTLAirLink/releases/download/v0.3.0/RTLAirLink-v0.3.0-x86_64.zip).
2. Open `Install-RTLAirLink.command` and select your OpenCore EFI if prompted.
3. Review the target, kexts and boot settings. Enter `y` to back up and apply the changes, then restart macOS.

The installer reuses compatible kexts and downloads missing dependencies from the pinned providers. It verifies the published v0.3.0 kexts and backs up earlier driver and bridge bundles when migrating them. [Dependency versions and checksums](https://github.com/xnpty/RTLAirLink/blob/main/docs/DEPENDENCIES.md)

To install from Terminal, run the following command.

~~~bash
curl -fLO https://github.com/xnpty/RTLAirLink/releases/download/v0.3.0/install-rtlairlink.sh && \
  bash install-rtlairlink.sh
~~~

## Preview and validate

Use `--efi` with a mounted EFI volume, its `OC` folder, or the exact `config.plist`. Test the changes with `--dry-run`.

~~~bash
bash install-rtlairlink-setup.sh --efi /Volumes/EFI --dry-run
~~~

The built-in OpenCore 1.0.9 validator checks the standard settings on a temporary copy when it sees OpenCore MOD's `ACPI/Quirks/EnableForAll`, `Booter/Quirks/EnableForAll` or `Misc/Boot/SkipCustomEntryCheck`. The installed EFI retains those keys. For another schema, pass `--ocvalidate /path/to/ocvalidate` with the validator from your OpenCore build.

## Manual OpenCore setup

1. Back up the working EFI before editing it.
2. Copy the two RTLAirLink kexts from the ZIP and the [Wi-Fi dependency kexts](https://github.com/xnpty/RTLAirLink/blob/main/docs/DEPENDENCIES.md) into `EFI/OC/Kexts`.
3. Run OC Snapshot in ProperTree. If migrating, disable the earlier driver, frontend and bridge entries. Disable any `AirPortBrcmNIC.kext` entry under `IO80211FamilyLegacy.kext`.
4. Check the relative order below in `Kernel → Add`. Set `MinKernel` to `25.0.0` and `MaxKernel` to `25.99.99` for both RTLAirLink kexts and the frontend. Set `MinKernel` to `23.0.0` for both legacy Apple Wi-Fi kexts.
5. In `Kernel → Block`, enable `com.apple.iokit.IOSkywalkFamily` with strategy `Exclude`, architecture `x86_64` and minimum kernel `23.0.0`.
6. Set `Misc → Security → SecureBootModel` to `Disabled`. Add `-amfipassbeta` to your existing `boot-args` in `NVRAM → Add → 7C436110-AB2A-4BBB-A880-FE41995C9F82`. In `NVRAM → Delete`, include `boot-args` under that same GUID so OpenCore applies the new value.
7. Validate with the `ocvalidate` from your OpenCore build and restart.

The installer sets the same entries in dependency order.

1. `Lilu.kext`
2. `RTLAirBridge.kext`
3. `IOSkywalkFamily.kext`
4. `IO80211FamilyLegacy.kext`
5. `AMFIPass.kext`
6. `RTLAirLink.kext`
7. `RTLAirLink.kext/Contents/PlugIns/RTLAirLinkFront.kext`

## Realtek Bluetooth for AirDrop

Run `bash install-rtlairlink-setup.sh --with-bluetooth` on the target Mac. With a supported USB controller such as `0bda:4853`, the installer adds or reuses `BlueToolFixup.kext` and `RealtekBluetoothFirmware.kext` after the Wi-Fi kexts. It sets `bluetoothExternalDongleFailed` to one zero byte and `bluetoothInternalControllerInfo` to 14 zero bytes in OpenCore NVRAM.

If the preview shows a Wi-Fi-only plan, check the Bluetooth controller in System Information → USB. The USB port carrying internal Bluetooth must be enabled in that machine's USB map. [Bluetooth firmware setup](https://github.com/thegwchr/RealtekBluetoothFirmware/blob/v1.0.2/docs/Installation.md)

## Recover the EFI

An install with changes backs up the original `config.plist` and every replaced or removed bundle under `Downloads/RTLAirLink-Backups`. Save the preview's `Kexts to copy` list so you can identify additions during recovery.

Boot from your working OpenCore backup, restore the saved `config.plist`, and restore the bundles from the backup's `Kexts` directory. Remove a new kext directory only if it appears in the `Kexts to copy` list and is absent from that backup. The installer rolls back the transaction automatically if post-install validation fails. [Troubleshooting and reporting](https://github.com/xnpty/RTLAirLink#troubleshooting-and-reporting)
