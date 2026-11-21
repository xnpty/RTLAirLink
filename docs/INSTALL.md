# Install RTLAirLink 0.2.0

Use this release on Intel macOS Tahoe with an existing OpenCore installation, Python 3.9 or newer and a Realtek RTL8852BE. Hardware testing used macOS build 25G241. The default installer validates the plan with OpenCore 1.0.9's ocvalidate. For a different OpenCore version, pass its corresponding executable with --ocvalidate.

~~~bash
curl -fL --proto '=https' --proto-redir '=https' -o install-rtlairlink.sh https://github.com/xnpty/RTLAirLink/releases/download/v0.2.0/install-rtlairlink.sh && bash install-rtlairlink.sh
~~~

The command verifies the signed kexts and the pinned dependency downloads. It detects the OpenCore EFI and displays the target, load order and boot arguments before applying the changes. Type INSTALL when the preview shows the intended volume. Restart macOS after the installation completes.

You can unpack the [release ZIP](https://github.com/xnpty/RTLAirLink/releases/tag/v0.2.0) and run Install-RTLAirLink.command directly. Use --dry-run to check a mounted EFI before any write.

~~~bash
bash Install-RTLAirLink.command --efi /Volumes/EFI --dry-run
~~~

The installer upgrades earlier RTLAirLink installations through their bundle IDs. It installs RTLAirLink.kext with the nested RTLAirLinkFront.kext, RTLAirBridge.kext, and the Wi-Fi stack. The bridge loads after Lilu and before the Wi-Fi family. The plan sets SecureBootModel to Disabled, configures the system Skywalk exclusion, and adds the Tahoe AMFIPass boot argument. AirDrop discovery relies on the Bluetooth configuration already present on your machine. [Dependency details](https://github.com/xnpty/RTLAirLink/blob/v0.2.0/docs/DEPENDENCIES.md)

Each install creates a folder in Downloads/RTLAirLink-Backups with the original config.plist and replaced kexts. If you need to recover, boot from your working OpenCore backup, restore the original config.plist and the files in the backup Kexts directory, then remove newly added kext directories absent from that backup. The installer rolls back changes if its post-install validation fails.
