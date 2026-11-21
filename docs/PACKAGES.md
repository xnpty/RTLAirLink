# GitHub Packages

The release files are also published as an OCI artifact in GitHub Packages:

```text
ghcr.io/xnpty/rtlairlink:v0.1.0
```

Package page: <https://github.com/users/xnpty/packages/container/package/rtlairlink>.

This artifact contains the exact five files from the matching GitHub Release:
the paired kext ZIP, corresponding source ZIP, build report, release manifest
and `SHA256SUMS`. The artifact is a file distribution package. Install the kexts
on the supported Intel Mac using [INSTALL.md](INSTALL.md).

## Download with ORAS

Install [ORAS](https://oras.land/docs/installation), then pull the files:

```bash
oras pull ghcr.io/xnpty/rtlairlink:v0.1.0 --output RTLAirLink-v0.1.0
cd RTLAirLink-v0.1.0
shasum -a 256 -c SHA256SUMS
```

For a private package, first authenticate with an account that has package access.
Use a GitHub personal access token (classic) with `read:packages` and supply it to
`oras login ghcr.io --username YOUR_GITHUB_USERNAME --password-stdin` through stdin.
The newly created package is private and linked to this repository. Access is
inherited from the repository under GitHub's default package settings.

You can also download the ZIP directly from [Releases](https://github.com/xnpty/RTLAirLink/releases).

## Publish an existing release

Run **Actions → Publish release to GitHub Packages → Run workflow**, and enter
the existing release tag. The workflow uses the repository's temporary
`GITHUB_TOKEN` with `contents: read` and `packages: write`; no long-lived registry
secret is stored. It verifies all release file hashes, publishes the OCI package,
then pulls it by immutable digest and compares every file with the originals.
The verified digest is recorded in the workflow summary. Dependencies are still
provided through the release's dependency downloader and are not repackaged here.
