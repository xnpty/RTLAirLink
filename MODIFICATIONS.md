# RTLAirLink modifications

## 2026-11-25 (v0.4.0)

- **Symmetric ~59.2 MB/s wire AirDrop pipeline:** Fixed the TX timestamp lock race (`owly_link_lock`) that triggered backward-clock peer wipes during bursts, expanded the native AWDL TX queue to 4096 frames, added proactive PCI TXBD/RPP reclamation, lock-free TX status accounting, 256-MPDU burst draining with batched doorbells, coalesced TX wakeups, and adaptive bidirectional 3-subframe A-MSDU (`4900 B` max MPDU) with initial-ACK verification and rate-aware fallback.
- **AWDL RTG Peer Cache & iPhone Mirroring (`rtlairlink-rtgd`):** Implemented `APPLE80211_IOC_PEER_CACHE_MAXIMUM_SIZE (130)`, `APPLE80211_IOC_PEER_CACHE_CONTROL (143)` (`ADD_IND=1` / `DELETE_IND=3`), and expedited async `reportDataPathEvents(0x33)` in `RTLAirLinkFront` (`OwlyNativePeerDataRevision = 8`). Added `Source-Build/tools/rtlairlink_rtgd.c` (`rtlairlink-rtgd`) to automatically toggle AWDL Real-Time Guided (`MPRemoteCamera`, `0x40`, `shared_slots=0xffff`) while `iPhone Mirroring` is connected over `awdl0`.
- **Radio & standalone stability:** Added standalone 5 GHz VHT80 bringup and `SET 120` `ENXIO` race fix, VN 5 GHz regulatory TX power override, standalone BTC coexistence fix, active-RX slot continuation, Port 0 CAM shadow protection for RX BlockAck, RX DMA sync/ring + BA hole timeout tuning, UMI `0x45` reporting, mid-transfer background scan deferral, multichannel `ch149 ↔ ch6` ping-pong fix, and disabled broken HW RTS on AWDL NoDS/AD_HOC peers.
- **All-in-one v0.4.0 release packaging & installer:** Updated `build.py`, `tools/install_efi.py`, `tools/install-rtgd.sh`, and `tools/install-bootstrap.sh.in` to bundle Tahoe-verified companion kexts and `ocvalidate` inside the release archive, support one-command piped Terminal install, install/replace required kexts cleanly with rollback backups, enforce exact OpenCore load order and `boot-args`, and optionally set up `rtlairlink-rtgd`.

## 2026-11-23 (v0.3.0)

- Added VHT 80 MHz / 2SS / SGI / LDPC + 64 KB RX A-MPDU capability negotiation (`GET 154` HT + `GET 214` VHT -> TLV 17, 80 MHz opclass 128 channel sequence), hardware TX A-MPDU via QoS Data (`0x88`) + TID 0 ADDBA Request/Response negotiation, high-index Address CAM placement for RTL8852B auxiliary AWDL VIF, NAPI RPQ budget reservation, and `rtlairlinkctl awdl-turbo` / `awdl-rssi` runtime toggles.

## 2026-11-21 (v0.2.0)

- Consolidated driver, frontend, and bridge under unified `RTLAirLink` bundle IDs, build-time bridge UUID binding, and OpenCore EFI installer.

Earlier 2026-11-15 modification records remain in the v0.1.0 source revision. [Source attribution](CREDITS.md)
