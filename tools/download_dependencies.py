#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Updated 2026-11-21 for RTLAirLink 0.2.0.
"""Download and verify the dependency archives pinned by RTLAirLink."""
import argparse
import hashlib
import json
import shutil
import stat
import subprocess
import tempfile
import zipfile
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / 'DEPENDENCIES.json').read_text())


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def verify(path, entry):
    if path.stat().st_size != entry['size'] or sha(path) != entry['sha256']:
        raise RuntimeError(f"Size or SHA-256 mismatch for {entry['filename']}")


def fetch(entry, directory):
    name = entry['filename']
    if Path(name).name != name or not name.endswith('.zip'):
        raise RuntimeError('Invalid dependency filename')
    directory.mkdir(parents=True, exist_ok=True)
    target = directory / name
    if target.is_file():
        verify(target, entry)
        return target
    print(f'Downloading {name}', flush=True)
    with tempfile.TemporaryDirectory(prefix='dependency-', dir=directory) as folder:
        partial = Path(folder) / name
        subprocess.run(['/usr/bin/curl', '--proto', '=https', '--proto-redir', '=https',
                        '-fsSL', '--retry', '2', '--max-time', '120',
                        '--max-filesize', str(entry['size']), '-o', str(partial),
                        entry['url']], check=True)
        verify(partial, entry)
        partial.replace(target)
    return target


def unpack_bundle(entry, cache, destination):
    bundle = entry['bundle']
    archive = fetch(entry, cache)
    with zipfile.ZipFile(archive) as file:
        members = [(info, PurePosixPath(info.filename)) for info in file.infolist()]
        matches = [name.parent.parent for _, name in members
                   if name.name == 'Info.plist' and name.parent.name == 'Contents'
                   and name.parent.parent.name == bundle]
        if len(matches) != 1:
            raise RuntimeError(f'Expected one {bundle} in {archive.name}')
        root = matches[0]
        for info, path in members:
            if not path.is_relative_to(root) or info.is_dir():
                continue
            relative = path.relative_to(root)
            mode = info.external_attr >> 16
            if path.is_absolute() or '..' in path.parts or stat.S_ISLNK(mode):
                raise RuntimeError(f'Unexpected ZIP member in {archive.name}')
            if path.parts[-1].startswith('._'):
                continue
            target = destination / bundle / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            with file.open(info) as src, target.open('xb') as dst:
                shutil.copyfileobj(src, dst)
            target.chmod(0o755 if mode & 0o111 or 'MacOS' in relative.parts else 0o644)
    return destination / bundle


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, default=Path.home() / 'Downloads/RTLAirLink-Dependencies')
    args = parser.parse_args()
    for entry in [*LOCK['dependencies'], LOCK['validator']]:
        path = fetch(entry, args.out)
        print(f'Verified {path.name}')
    print(f'Dependency archives are in {args.out}')


if __name__ == '__main__':
    main()
