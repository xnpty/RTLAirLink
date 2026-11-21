# Build RTLAirLink

Use Intel macOS with Xcode command-line tools and Python 3.9 or newer.

~~~bash
bash build.sh
~~~

The builder uses the Legacy Wi-Fi image pinned in DEPENDENCIES.json. Pass --legacy-image with its executable path to supply an existing local copy. The image must match the pinned SHA-256. Run bash build.sh --checks-only to run the driver, bridge and raw transmit host tests.

The release directory under build contains the paired binaries, source archive, installation script and checksums. The bridge is compiled with the newly built frontend UUID. The builder checks kernel imports, the P2P vtable, Mach-O module descriptors, bundle versions, code signatures and archive contents.

The source archive comes from the checked-out Git commit. Commit changes before creating a release build. [License and attribution](../CREDITS.md)
