# Dependencies

The installer reuses `Lilu.kext` 1.7.3 or newer and copies of `IOSkywalkFamily.kext`, `IO80211FamilyLegacy.kext`, and `AMFIPass.kext` with the expected bundle IDs, versions, and executable digests. It obtains missing or incompatible dependencies from the pinned providers and verifies each archive's size and SHA-256 before extracting it. [Version and checksum lock](../DEPENDENCIES.json) · [Third-party credits](../CREDITS.md)

To pre-download the dependency archives for an offline installation, run Download-Dependencies.command from the source tree. The installer uses its cache in Downloads/RTLAirLink-Dependencies by default. Use --cache to point to another directory.
