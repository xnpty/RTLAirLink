# RTLAirLink 0.4.0 Verification

The v0.4.0 cohort was verified on real hardware with a **Realtek RTL8852BE (`10ec:b852`)** running **macOS Tahoe 26 (build 25G241)** and **OpenCore 1.0.9**, paired with an **iPhone 13 (`iPhone14,5`)**.

## AirDrop (`awdl0`, `transport: "awdl"`, ch 149 VHT80 2×2)

- **Mac → iPhone (186.1 MB wire / 182 MB payload):** **59.2 MB/s wire** (`3.142 s`, **49.1 MB/s** end-to-end including HTTP/3 open/close), 3-subframe A-MSDU active (`OwlyNativeTXAMSDUFallback = No`, `OwlyNativeTXMPDUsAMSDU = 46,873`, `OwlyNativeTXAMSDUSubframes = 134,229`), `OwlyNativeTXRefused = 0`, `OwlyNativeTXExpired = 0`.
- **iPhone → Mac (186.1 MB wire / 182 MB payload):** **59.2 MB/s wire** (`3.144 s`, **49.8 MB/s** end-to-end), `RTW89 RX Reorder Timeouts = 0`, matching Apple's native iPhone TX rate within 2 ms.
- **Note:** On VHT80 2SS MCS 9 (`866.7 Mbps` PHY = `108.3 MB/s` raw), `59.2 MB/s` wire represents ~85–92% of the practical real-world AWDL ceiling on a shared 5 GHz channel.

## iPhone Mirroring (AWDL RTG + `rtlairlink-rtgd`)

- `RTLAirLinkFront` (`OwlyNativePeerDataRevision = 8`) verified serving `GET 130` (`OwlyNativePeerCacheMaxSizeResult = 32`), `SET 143` (`OwlyNativePeerCacheNoted = OwlyNativePeerCacheReported = 25`, `0` dropped), and expedited `0x33` / UMI `0x45` indications (`OwlyNativeUMINoted = OwlyNativeUMIStatusReported = 2`, `0` dropped).
- `rtlairlink-rtgd` verified auto-detecting `iPhone Mirroring` TCP connections on `awdl0`, enabling AWDL RTG (`SIOCSA80211 type=164`, `MPRemoteCamera`, `0x40`), switching `shared_slots` from `0x8e87` to `0xffff` (16/16 slots) with iPhone TLV `31:17` acknowledgment in ~29 ms, and cleanly clearing RTG on disconnect with zero regression to AirDrop (`OwlyNativeTXRadioACK = 15,304 / 15,372`).

## Automated Host & ABI Gates

All release gates pass in normal and ASan/UBSan modes: `make hosttest` (RTL8852BE `00`/`ff`/fakechip, 160 MHz/EHT wide rounds, SAE kernel crypto, and all 11 other Realtek chip IDs), `Bridge-Source` host tests & lifecycle audits, `tests/check_raw_tx.py`, `tests/check_native.py` (including peer-cache + UMI indication tests), `tests/check_block_ack.py`, `tests/check_p2p_abi.py` (322 vtable words), `check_kpi.py`, `bind_bridge.py --check`, `codesign --verify --strict --deep`, and `rtlairlink-rtgd` ASan/UBSan unit tests.
