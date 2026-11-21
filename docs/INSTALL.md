# Install RTLAirLink 0.2.0

Use this release on Intel macOS Tahoe with an existing OpenCore installation, Python 3.9 or newer and a Realtek RTL8852BE. Hardware testing used macOS build 25G241. The installer uses OpenCore 1.0.9's `ocvalidate` by default. For a different OpenCore version, pass its corresponding validator with `--ocvalidate /path/to/ocvalidate`.

~~~bash
curl -fL --proto '=https' --proto-redir '=https' -o install-rtlairlink.sh \
  https://raw.githubusercontent.com/xnpty/RTLAirLink/7541fd08ef2482a774b02e44ed5fa798a04ccd98/tools/install-v0.2.0.sh && \
  bash install-rtlairlink.sh
~~~

The command verifies the signed v0.2.0 kexts and dependency downloads. It reuses compatible installed bundles, including bundles whose folders were renamed. It obtains missing kexts and replaces incompatible versions with the verified builds. The preview shows the target EFI, load order, boot flags and which bundles it will copy or reuse. Type `INSTALL` when the preview shows the intended volume, then restart macOS after the installation completes. [Compatibility checks and pinned dependencies](DEPENDENCIES.md)

Use `--dry-run` to validate the plan for a specific EFI before any write. The ZIP includes the original `Install-RTLAirLink.command`. Use the terminal command above to reuse existing kexts. [v0.2.0 release](https://github.com/xnpty/RTLAirLink/releases/tag/v0.2.0)

~~~bash
bash install-rtlairlink.sh --efi /Volumes/EFI --dry-run
~~~

## OpenCore load order

The installer enables these entries in `Kernel → Add` in dependency order.

1. `Lilu.kext`
2. `RTLAirBridge.kext`
3. `IOSkywalkFamily.kext`
4. `IO80211FamilyLegacy.kext`
5. `AMFIPass.kext`
6. `RTLAirLink.kext`
7. `RTLAirLink.kext/Contents/PlugIns/RTLAirLinkFront.kext`

It adds `-amfipassbeta` to OpenCore `boot-args` if missing, sets `SecureBootModel` to `Disabled`, and configures `Kernel → Block` to exclude the system `com.apple.iokit.IOSkywalkFamily` on Darwin 23 and newer. AirDrop discovery uses the machine's existing Bluetooth setup.

## Recover the EFI

An install with changes backs up the original `config.plist` and every replaced or removed bundle under `Downloads/RTLAirLink-Backups`. Save the preview's `Kexts to copy` list so you can identify additions during recovery.

Boot from your working OpenCore backup, restore the saved `config.plist`, and restore the bundles from the backup's `Kexts` directory. Remove a new kext directory only if it appears in the `Kexts to copy` list and is absent from that backup. The installer rolls back the transaction automatically if post-install validation fails. [Troubleshooting and reporting](../README.md#troubleshooting-and-reporting)
