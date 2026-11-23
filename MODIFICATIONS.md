# RTLAirLink modifications

## 2026-11-23

- Native AWDL unicast data can use firmware rate control when a 20 MHz peer rate table is available. The raw descriptor preserves the frame, sequence and retry limit. Management and multicast frames retain the 6 Mb/s rate.
- The experimental HT20 profile uses the peer's advertised first-stream receive MCS mask. Its firmware mask also retains the peer's legacy OFDM fallback rates. The peer rate table is refreshed in place when that advertisement changes. [AWDL capability format](https://www.wireshark.org/docs/dfref/a/awdl_data.html)

## 2026-11-21

- The developer's working driver snapshot was brought into RTLAirLink with AWDL receive filtering, Block Ack CAM checking, radio timing and channel-sequence corrections.
- The driver, frontend and bridge have consistent 0.2.0 module metadata, executable names, bundle IDs, IOKit class names and tool entry points. The bridge is rebound at build time to the new frontend UUID.
- The release builder and OpenCore installer assemble the matched cohort. The installer recognizes earlier module IDs, verifies dependency downloads, validates the destination configuration and saves a rollback copy of the affected files.

The 2026-11-15 experimental release and earlier modification records remain in the v0.1.0 source revision. [Source attribution](CREDITS.md)
