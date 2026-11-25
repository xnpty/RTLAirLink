# RTLAirLink v0.4.0

Native AWDL research, design, and implementation by Anh Tài (xnpty) in Owly/RTLAirLink. [Credits and source history](../CREDITS.md)

## What's New

- **Symmetric ~59.2 MB/s Wire AirDrop (Mac ↔ iPhone):**
  - Expanded native AWDL TX queue to **4096 frames**, added proactive PCI TXBD/RPP reclamation, lock-free TX status accounting, 256-MPDU burst draining with batched PCI doorbells, and coalesced TX wakeups.
  - Added adaptive **bidirectional 3-subframe A-MSDU** (`4900 B` max MPDU over 64-MPDU hardware A-MPDU) with automatic ACK verification and rate-aware fallback.
  - Both directions now hit **59.2 MB/s wire (~49–50 MB/s end-to-end)** on RTL8852BE ↔ iPhone 13 with **`0` TX refused** and **`0` TX expired**.
- **Smooth iPhone Mirroring (AWDL RTG Peer Cache + `rtlairlink-rtgd`):**
  - Implemented `APPLE80211_IOC_PEER_CACHE_MAXIMUM_SIZE (130)` (`{1, 32}`), `APPLE80211_IOC_PEER_CACHE_CONTROL (143)` (`ADD_IND=1` / `DELETE_IND=3`), and expedited async `reportDataPathEvents(0x33)` in `RTLAirLinkFront` (`OwlyNativePeerDataRevision = 8`), fixing `SIOCSA80211 type=164` (`EIO`) failures during AWDL traffic registration.
  - Added the lightweight C daemon **`rtlairlink-rtgd`** (`/usr/local/libexec/rtlairlink-rtgd` + LaunchDaemon) that monitors active `iPhone Mirroring` sockets on `awdl0`, enables AWDL RTG (`shared_slots = 0xffff`, 16/16 slots; iPhone replies TLV `31:17` in ~29 ms) to eliminate 196–262 ms off-channel FPS stutters (`0x8e87`), and cleanly releases RTG when Mirroring closes.
- **All-in-One Release & One-Command OpenCore Installer:**
  - `RTLAirLink-v0.4.0-x86_64.zip` now bundles all required Tahoe-verified kexts (`Lilu.kext`, `RTLAirBridge.kext`, `IOSkywalkFamily.kext`, `IO80211FamilyLegacy.kext`, `AMFIPass.kext`, `RTLAirLink.kext` + `RTLAirLinkFront.kext`, optional Realtek USB Bluetooth kexts), `ocvalidate`, `rtlairlinkctl`, and `rtlairlink-rtgd`.
  - One-command installer (`curl -fsSL .../install-rtlairlink.sh | bash`) backs up your EFI, installs or replaces required kexts, sorts `Kernel → Add` into exact load order, configures `Kernel → Block`, `SecureBootModel`, and `boot-args`, validates with `ocvalidate`, and optionally installs `rtlairlink-rtgd`.

## Bug Fixes (since v0.3.0)

- **Fixed TX timestamp lock race wiping active peers:** Sampled `tx_now` strictly inside `owly_link_lock` so concurrent `owly_link_enqueue()` calls cannot advance `last_now_us` ahead of the drain loop and trigger backward-clock peer/queue resets.
- **Fixed standalone 5 GHz AWDL bringup & `SET 120` `ENXIO` race:** Fixed standalone 5 GHz VHT80 activation, multicast port/RX handling, and standalone BTC coexistence so AirDrop works reliably even without an associated AP.
- **Fixed VN 5 GHz regulatory TX power & disabled broken HW RTS on AWDL peers:** Overrode restrictive firmware VN 5 GHz regd limits and turned off hardware RTS/CTS for AWDL NoDS/AD_HOC peers while keeping 12 HW retries with firmware rate fallback.
- **Fixed multichannel `ch149 ↔ ch6` ping-pong & mid-transfer background scans:** Prevented unnecessary channel bounces and deferred background scans during active unicast AWDL transfers.
- **Fixed RX BlockAck CAM shadowing & RX DMA reorder stalls:** Prevented standalone Port 0 CAM entries from shadowing per-peer RX BlockAck, tuned RX DMA sync/ring + BA hole timeout, added active-RX slot continuation, and improved TX ADDBA recovery + UMI `0x45` reporting.

## Supported Realtek Cards

- **RTL8852BE** (`10ec:b852`, `10ec:b85b`): **Tested & Verified** on macOS Tahoe 25G241 (Wi-Fi 6 + AirDrop + iPhone Mirroring).
- **RTL8851BE** (`10ec:b851`), **RTL8852AE** (`10ec:8852`, `10ec:a85a`), **RTL8852BTE** (`10ec:b520`), **RTL8852CE** (`10ec:c852`), **RTL8922AE** (`10ec:8922`, `10ec:892b`), **RTL8922DE** (`10ec:892d`, `10ec:882d`, `10ec:895d`): **Untested — Wi-Fi is untested and AirDrop / AWDL may not work.**

## Live Benchmarks (`awdl0` on RTL8852BE, macOS Tahoe 25G241)

| Test (RTL8852BE ↔ iPhone 13) | v0.3.0 | **v0.4.0** |
| --- | --- | --- |
| AirDrop Mac → iPhone (186 MB wire) | 50.77 MB/s | **59.2 MB/s wire** (~49.1 MB/s e2e, `3.142 s`, `0` refused/expired) |
| AirDrop iPhone → Mac (186 MB wire) | 16.95 MB/s | **59.2 MB/s wire** (~49.8 MB/s e2e, `3.144 s`, `0` reorder timeouts) |
| iPhone Mirroring online video | Periodic 196–262 ms stutter (`0x8e87`) | **Smooth 60 fps** (`shared_slots=0xffff` 16/16 via auto-RTG) |

[Verification details](STATUS.md) · [Installation guide](INSTALL.md) · [Hướng dẫn tiếng Việt](../README_VI.md)
