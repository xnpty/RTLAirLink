# RTLAirLink v0.2.0

Native AWDL for Realtek was researched, designed, and originally implemented by Anh Tài (xnpty) in Owly/RTLAirLink. [Credits and source history](../CREDITS.md)

- Native AWDL scheduling advertises the radio's 20 MHz data channels and actual availability slots.
- The driver contains the receive CAM, Block Ack and IPv6 delivery corrections from the validated hardware build.
- The matched backend, frontend and event bridge now use RTLAirLink executables, classes and bundle IDs.
- The installer fetches pinned dependencies and validates an OpenCore configuration before applying a backed-up update.

This is an experimental release. Companion discovery remains intermittent on the hardware tested. [Verification details](STATUS.md)
