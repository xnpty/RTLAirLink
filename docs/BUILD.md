# Build RTLAirLink

Use x86_64 macOS with Xcode command-line tools and Python 3.9 or newer.

~~~bash
bash build.sh
~~~

To bundle verified companion kexts (e.g., from a working OpenCore EFI) directly into the release archive:

~~~bash
python3 build.py --bundle-kexts-dir /Volumes/EFI/EFI/OC/Kexts
~~~

Run `bash build.sh --checks-only` to run the driver, bridge, and raw-TX host test suites without creating a release archive.

The builder compiles `RTLAirLink.kext` (with `RTLAirLinkFront.kext`), `RTLAirBridge.kext` (bound to the compiled frontend UUID), `rtlairlinkctl`, and `rtlairlink-rtgd`, verifies kernel KPIs, the P2P vtable, Mach-O module descriptors, bundle versions, code signatures, and bundled kext hashes, and outputs `RTLAirLink-v0.4.0-x86_64.zip`, `install-rtlairlink.sh`, and `SHA256SUMS` under `build/`.

Commit all changes before creating a release build. [License and attribution](../CREDITS.md)
