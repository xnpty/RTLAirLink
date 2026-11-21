# RTLAirLink modifications

## 2026-11-21

- The developer's working driver snapshot was brought into RTLAirLink with AWDL receive filtering, Block Ack CAM checking, radio timing and channel-sequence corrections.
- The driver, frontend and bridge have consistent 0.2.0 module metadata, executable names, bundle IDs, IOKit class names and tool entry points. The bridge is rebound at build time to the new frontend UUID.
- The release builder and OpenCore installer assemble the matched cohort. The installer recognizes earlier module IDs, verifies dependency downloads, validates the destination configuration and saves a rollback copy of the affected files.

The 2026-11-15 experimental release and earlier modification records remain in the v0.1.0 source revision. [Source attribution](CREDITS.md)
