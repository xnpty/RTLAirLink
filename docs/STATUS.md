# RTLAirLink 0.2.0 verification

The working source was tested on RTL8852BE with macOS Tahoe 25G241 and OpenCore 1.0.9, with Bluetooth configured separately.

- Mac to iPhone completed six of eight AirDrop sends through IPv6 on awdl0. Five of those sends completed the Companion exchanges on AWDL.
- iPhone to Mac received a 922,428-byte image over QUIC on awdl0. The saved image matched the original SHA-256.
- Two outbound attempts stopped at missingEndpoint during Companion discovery.

The release build renames the kexts, bundle identifiers, IOKit classes, and the backend and frontend diagnostics. The bridge is linked to the new frontend UUID. Build checks and EFI installer validation cover the release package. A boot of the renamed cohort and continued testing of intermittent discovery remain open.

Six standalone v0.1.0 fixtures need updating for the current driver. They cover Block Ack, receive filters, VIF restoration, AWDL wire and clock behavior, and scanning. The current driver's scan-request checks, kernel import checks, P2P ABI check, raw transmit tests and bridge tests are part of the release build.
