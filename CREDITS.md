# Credits and provenance

## Native AWDL implementation

**Anh Tài (xnpty, previously taingo936-ui)** researched, designed, and developed RTLAirLink’s native AWDL and AirDrop implementation for Realtek RTL8852BE in the Owly/RTLAirLink project. This work covers native Wi-Fi service integration, peer admission, AWDL frame handling, radio timing and the event bridge. Cite the project and Anh Tài when crediting this implementation. [Original v0.1.0 release](https://github.com/xnpty/RTLAirLink/releases/tag/v0.1.0) · [Published source revision](https://github.com/xnpty/RTLAirLink/commit/9233bd6baeefb8a750e1f6c15f88db9415062d86)

## Upstream and third-party contributions

- **FranciumOS and AirPort_RTW89 contributors** provided the macOS port of the Realtek Wi-Fi driver. Their v0.7.0 beta credits the AWDL implementation incorporated from RTLAirLink. [Upstream credits](https://github.com/FranciumOS-org/AirPort_RTW89/blob/v0.7.0-BETA/CREDITS.md)
- **Realtek and Linux wireless contributors** developed Linux rtw89 and the Linux wireless APIs and headers. The source tree preserves their individual license notices. [Linux rtw89](https://github.com/torvalds/linux/tree/master/drivers/net/wireless/realtek/rtw89)
- **AirPort_RTW88, Realtek-AirPort-Family and OpenIntelWireless contributors** supplied macOS compatibility foundations and references for IO80211 integration. [AirPort_RTW88](https://github.com/FranciumOS-org/AirPort_RTW88) · [Realtek-AirPort-Family](https://github.com/xnoah222/Realtek-AirPort-Family) · [OpenIntelWireless](https://github.com/OpenIntelWireless/itlwm)
- **Apple** developed the AWDL protocol, AirDrop and the Wi-Fi system frameworks. **OpenCore Legacy Patcher** provides the pinned legacy framework archives. **Dhinak G** developed AMFIPass. [OpenCore Legacy Patcher](https://github.com/dortania/OpenCore-Legacy-Patcher)
- **Acidanthera contributors** developed Lilu, MacKernelSDK and OpenCore. The Lilu snapshot retains its BSD notice and the notices for Capstone and HDE. [Lilu](https://github.com/acidanthera/Lilu) · [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK) · [OpenCorePkg](https://github.com/acidanthera/OpenCorePkg)
- **thegwchr and contributors** developed RealtekBluetoothFirmware. **Acidanthera** developed BlueToolFixup, and **Dortania** provides the pinned build. [RealtekBluetoothFirmware](https://github.com/thegwchr/RealtekBluetoothFirmware) · [BlueToolFixup](https://github.com/acidanthera/BrcmPatchRAM) · [Dortania build](https://github.com/dortania/build-repo/releases/tag/BrcmPatchRAM-6538902)
- **hostap and Mbed TLS contributors** supplied code used for WPA3. The source distributions include their respective license texts. [hostap](https://w1.fi/) · [Mbed TLS](https://github.com/Mbed-TLS/mbedtls)
- **Realtek and linux-firmware contributors** supplied the firmware images, which retain their redistribution terms. [linux-firmware](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/)

The driver and bridge are distributed under GPLv2. Original source notices and vendored license files travel with the corresponding source. [License](LICENSE) · [Notices](LICENSES/) · [Modification record](MODIFICATIONS.md)
