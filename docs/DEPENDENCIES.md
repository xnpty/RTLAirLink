# Dependencies

The installer obtains Lilu 1.7.3, IOSkywalkFamily, IO80211FamilyLegacy and AMFIPass from the providers pinned in [DEPENDENCIES.json](../DEPENDENCIES.json). It verifies archive sizes, SHA-256 digests, bundle IDs and executable digests. An eligible Lilu already installed in the EFI can be reused. AirDrop discovery uses a working Bluetooth setup on the machine.

To pre-download the dependency archives for an offline installation, run Download-Dependencies.command from the source tree. The installer uses its cache in Downloads/RTLAirLink-Dependencies by default. Use --cache to point to another directory.
