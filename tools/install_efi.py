#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Updated 2026-11-25 for RTLAirLink 0.4.0.
"""Install or update RTLAirLink 0.4.0 and its required kexts into an OpenCore EFI."""
import argparse
from datetime import datetime
from functools import partial
import json
import os
from pathlib import Path, PurePosixPath
import plistlib
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import zipfile

from download_dependencies import LOCK, ROOT, dependency_entries, fetch, sha, unpack_bundle

VERSION = '0.4.0'
DRIVER = 'RTLAirLink.kext'
FRONT = f'{DRIVER}/Contents/PlugIns/RTLAirLinkFront.kext'
BRIDGE = 'RTLAirBridge.kext'
FAMILY = 'IO80211FamilyLegacy.kext'
LILU = 'Lilu.kext'
ORDER = (LILU, BRIDGE, 'IOSkywalkFamily.kext', FAMILY, 'AMFIPass.kext', DRIVER, FRONT)
BLUETOOTH_ORDER = tuple(entry['bundle'] for entry in LOCK['bluetooth']['dependencies'])
BLUETOOTH_NVRAM = {
    'bluetoothExternalDongleFailed': bytes(1),
    'bluetoothInternalControllerInfo': bytes(14),
}
NVRAM = '7C436110-AB2A-4BBB-A880-FE41995C9F82'
SKYWALK_ID = 'com.apple.iokit.IOSkywalkFamily'
RELEASE_IDS = {
    DRIVER: 'io.github.xnpty.RTLAirLink',
    FRONT: 'io.github.xnpty.RTLAirLink.front',
    BRIDGE: 'io.github.xnpty.RTLAirLink.bridge',
}
# Bundle IDs recognized when migrating from pre-0.2.0 or AirPort_RTW89 setups.
MIGRATION_IDS = {
    'com.rtw89.driver',
    'com.rtw89.front',
    'com.owly.airlink.eventbridge',
    'org.franciumos.AirPort_RTW89',
    'org.franciumos.AirPortRTW89',
    'org.franciumos.AirPort_RTW89.front',
    'org.franciumos.BroadTek',
}
REMOVED_BOOT_ARGS = {'-owlyeb1', '-owlyeb1off', 'owlyebstage', '-broadtekp2poff'}
BOOT_ARGS = ('-amfipassbeta',)
BLUETOOTH_BOOT_ARGS = ('-btlfxallowanyaddr',)
OPENCORE_MOD_KEYS = (
    ('ACPI', 'Quirks', 'EnableForAll'),
    ('Booter', 'Quirks', 'EnableForAll'),
    ('Misc', 'Boot', 'SkipCustomEntryCheck'),
)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def read_plist(path):
    with path.open('rb') as file:
        return plistlib.load(file)


def bundle_info(bundle):
    return read_plist(bundle / 'Contents/Info.plist')


def bundle_id(bundle):
    info = bundle / 'Contents/Info.plist'
    if not info.is_file():
        return None
    try:
        data = read_plist(info)
    except (plistlib.InvalidFileException, ValueError):
        return None
    identifier = data.get('CFBundleIdentifier') if isinstance(data, dict) else None
    return identifier if isinstance(identifier, str) else None


def executable_path(bundle, info):
    name = info['CFBundleExecutable']
    require(isinstance(name, str) and Path(name).name == name and name not in ('', '.', '..'),
            'Invalid kext executable name')
    executable = bundle / 'Contents/MacOS' / name
    require(executable.is_file(), f'Missing executable in {bundle.name}')
    return executable


def digest_tree(directory):
    return {p.relative_to(directory).as_posix(): sha(p) for p in directory.rglob('*')
            if p.is_file() and not p.name.startswith('._')}


def copy_clean_bundle(src, dst):
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns('._*', '.DS_Store'))


def package_kext(package, name):
    """Locate a kext bundle inside the release package (Kexts/, Kexts-Bluetooth/, or root)."""
    rel = PurePosixPath(name)
    for subdir in ('Kexts', 'Kexts-Bluetooth', ''):
        candidate = (package / subdir / rel) if subdir else (package / rel)
        if candidate.is_dir():
            return candidate
    return None


def validate_package(package):
    manifest = json.loads((package / 'RELEASE_MANIFEST.json').read_text())
    require(manifest['release_version'] == VERSION, 'Package version does not match this installer')
    for name, expected in RELEASE_IDS.items():
        bundle = package_kext(package, name)
        require(bundle is not None, f'Missing {name} in release package')
        info = bundle_info(bundle)
        require(info['CFBundleIdentifier'] == expected, f'Unexpected bundle ID for {name}')
        require(info['CFBundleVersion'] == VERSION, f'Unexpected module version for {name}')
        image = executable_path(bundle, info)
        require(sha(image) == manifest['cohort'][name]['sha256'], f'Checksum mismatch for {name}')
    for name in (DRIVER, BRIDGE):
        bundle = package_kext(package, name)
        require(digest_tree(bundle) == manifest['bundle_files'][name],
                f'Unexpected file contents in {name}')
        subprocess.run(['/usr/bin/codesign', '--verify', '--strict', '--deep', str(bundle)], check=True)
    for name, expected_files in manifest.get('bundled_dependencies', {}).items():
        bundle = package_kext(package, name)
        require(bundle is not None, f'Missing bundled dependency {name} in release package')
        require(digest_tree(bundle) == expected_files,
                f'Checksum mismatch for bundled dependency {name}')
    ocv = package / 'tools/ocvalidate'
    if 'ocvalidate_sha256' in manifest:
        require(ocv.is_file() and sha(ocv) == manifest['ocvalidate_sha256'],
                'Bundled ocvalidate checksum mismatch')
    return manifest


def validate_dependency(path, entry):
    info = bundle_info(path)
    require(info['CFBundleIdentifier'] == entry['bundle_identifier']
            and info['CFBundleVersion'] == entry['bundle_version']
            and sha(executable_path(path, info)) == entry['executable_sha256'],
            f"Unexpected dependency executable in {path.name}")


def version_tuple(value):
    parts = re.fullmatch(r'(\d+)\.(\d+)\.(\d+)', value) if isinstance(value, str) else None
    return tuple(map(int, parts.groups())) if parts else ()


def dependency_matches(bundle, entry):
    try:
        minimum = entry['bundle_version'] if entry['bundle'] == LILU else entry.get('minimum_reuse_version')
        if minimum:
            info = bundle_info(bundle)
            executable = executable_path(bundle, info)
            return (info['CFBundleIdentifier'] == entry['bundle_identifier']
                    and version_tuple(info.get('CFBundleVersion')) >= version_tuple(minimum)
                    and 'x86_64' in subprocess.check_output(['/usr/bin/file', '-b', str(executable)], text=True))
        validate_dependency(bundle, entry)
    except (RuntimeError, OSError, KeyError, ValueError, plistlib.InvalidFileException,
            subprocess.CalledProcessError):
        return False
    return True


def reusable_bundle(installed, name, identifier, matches):
    candidates = sorted(installed.get(identifier, []), key=lambda path: (path.name != name, path.name))
    return next((path.name for path in candidates if matches(path)), None)


def iter_kext_dirs(directory):
    for bundle in directory.iterdir():
        if (not bundle.name.startswith('._') and bundle.suffix.lower() == '.kext'
                and bundle.is_dir() and not bundle.is_symlink()):
            yield bundle


def prepare(package, manifest, existing, cache, folder, custom_validator,
            with_bluetooth=False, keep_existing_deps=False):
    installed = {}
    for bundle in iter_kext_dirs(existing):
        installed.setdefault(bundle_id(bundle), []).append(bundle)
    reused = {}
    for name in (DRIVER, BRIDGE):
        expected_files = manifest['bundle_files'][name]
        current = reusable_bundle(installed, name, RELEASE_IDS[name],
                                  lambda path: digest_tree(path) == expected_files)
        if current:
            reused[name] = current
        else:
            copy_clean_bundle(package_kext(package, name), folder / name)
    bundled_deps = manifest.get('bundled_dependencies', {})
    for entry in dependency_entries(with_bluetooth):
        name = entry['bundle']
        bundled_src = package_kext(package, name)
        bundled_hashes = bundled_deps.get(name)
        if bundled_src is not None and bundled_hashes is not None:
            current = reusable_bundle(installed, name, entry['bundle_identifier'],
                                      lambda path: digest_tree(path) == bundled_hashes)
            if not current and keep_existing_deps:
                current = reusable_bundle(installed, name, entry['bundle_identifier'],
                                          lambda path: dependency_matches(path, entry))
            if current:
                reused[name] = current
            else:
                copy_clean_bundle(bundled_src, folder / name)
            continue
        current = reusable_bundle(installed, name, entry['bundle_identifier'],
                                  lambda path: dependency_matches(path, entry))
        if current:
            reused[name] = current
            continue
        path = unpack_bundle(entry, cache, folder)
        validate_dependency(path, entry)
        if name == FAMILY:
            for plugin in (path / 'Contents/PlugIns').glob('*.kext'):
                if bundle_id(plugin) == 'com.apple.driver.AirPort.BrcmNIC' or plugin.name == 'AirPortBrcmNIC.kext':
                    shutil.rmtree(plugin)
    if custom_validator is not None:
        require(custom_validator.is_file() and os.access(custom_validator, os.X_OK), 'ocvalidate is not executable')
        return custom_validator, reused
    bundled_ocv = package / 'tools/ocvalidate'
    if bundled_ocv.is_file():
        target = folder.parent / 'ocvalidate'
        shutil.copy2(bundled_ocv, target)
        target.chmod(0o700)
        return target, reused
    spec = LOCK['validator']
    target = folder.parent / 'ocvalidate'
    with zipfile.ZipFile(fetch(spec, cache)) as file:
        target.write_bytes(file.read(spec['member']))
    target.chmod(0o700)
    return target, reused


def kext_entry(name, folder, existing, reused):
    path = PurePosixPath(name)
    current = reused.get(path.parts[0])
    if current is not None:
        path = PurePosixPath(current, *path.parts[1:])
    bundle = (existing if current is not None else folder) / path
    info = bundle_info(bundle)
    exe = executable_path(bundle, info)
    if name in RELEASE_IDS:
        minimum, maximum = '25.0.0', '25.99.99'
    elif name in ('IOSkywalkFamily.kext', FAMILY):
        minimum, maximum = '23.0.0', ''
    elif name == 'BlueToolFixup.kext':
        minimum, maximum = '21.0.0', ''
    else:
        minimum, maximum = '', ''
    return {'Arch': 'x86_64', 'BundlePath': path.as_posix(), 'Comment': path.name,
            'Enabled': True, 'ExecutablePath': exe.relative_to(bundle).as_posix(),
            'MaxKernel': maximum, 'MinKernel': minimum, 'PlistPath': 'Contents/Info.plist'}


def configure(config, existing, staged, reused, with_bluetooth=False, no_wpa3=False):
    order = ORDER + (BLUETOOTH_ORDER if with_bluetooth else ())
    dependency_ids = {entry['bundle_identifier'] for entry in dependency_entries(with_bluetooth)}
    managed_ids = set(RELEASE_IDS.values()) | MIGRATION_IDS | dependency_ids
    managed_names = {name for name in order if '/' not in name}
    bundles = {path.name: bundle_id(path) for path in iter_kext_dirs(existing)}
    installed = {name for name, identifier in bundles.items()
                 if name in managed_names or identifier in managed_ids}
    entries = config['Kernel']['Add']
    retained = []
    preserve_paths = {reused.get(name) for name in (LILU, *BLUETOOTH_ORDER)}
    preserved = {}
    for item in entries:
        path = PurePosixPath(item['BundlePath'])
        require(path.parts and not path.is_absolute() and '..' not in path.parts,
                f'Invalid BundlePath in config.plist: {path}')
        managed = path.parts[0] in installed | managed_names or bundle_id(existing / path) in managed_ids
        if managed:
            if str(path) in preserve_paths:
                preserved.setdefault(str(path), item)
            continue
        if item['Enabled']:
            require((existing / path).is_dir(), f'Enabled kext directory is missing: {path}')
        retained.append(item)
    new = [kext_entry(name, staged, existing, reused) for name in order]
    kernel = version_tuple(platform.release())
    if not kernel or kernel[0] != 25:
        kernel = (25, 0, 0)
    for index, entry in enumerate(new):
        current = preserved.get(entry['BundlePath'])
        if not current:
            continue
        minimum = version_tuple(current.get('MinKernel'))
        maximum = version_tuple(current.get('MaxKernel'))
        if (current.get('Arch') in ('Any', 'x86_64')
                and (not minimum or minimum <= kernel) and (not maximum or kernel <= maximum)):
            new[index] = dict(current, Enabled=True, PlistPath=entry['PlistPath'],
                              ExecutablePath=entry['ExecutablePath'])
    # Place Lilu, RTLAirBridge, legacy Apple Wi-Fi stack, AMFIPass, RTLAirLink,
    # RTLAirLinkFront (and Bluetooth when requested) first in exact dependency order.
    entries[:] = new + retained

    blocks = config['Kernel'].setdefault('Block', [])
    blocks[:] = [item for item in blocks if item['Identifier'] != SKYWALK_ID]
    blocks.append({'Arch': 'x86_64', 'Comment': 'RTLAirLink Wi-Fi stack',
                   'Enabled': True, 'Identifier': SKYWALK_ID, 'MaxKernel': '',
                   'MinKernel': '23.0.0', 'Strategy': 'Exclude'})

    config['Misc']['Security']['SecureBootModel'] = 'Disabled'
    nvram = config['NVRAM']
    args = nvram['Add'].setdefault(NVRAM, {})
    required_args = list(BOOT_ARGS)
    if with_bluetooth:
        required_args.extend(BLUETOOTH_BOOT_ARGS)
    if no_wpa3:
        required_args.append('-rtw89nowpa3')
    previous = args.get('boot-args', '').split()
    updated = []
    for arg in previous:
        name = arg.partition('=')[0]
        if name in REMOVED_BOOT_ARGS:
            continue
        if name in required_args:
            if name in updated:
                continue
            arg = name
        updated.append(arg)
    updated.extend(arg for arg in required_args if arg not in updated)
    if updated != previous:
        args['boot-args'] = ' '.join(updated)
    deletes = nvram['Delete'].setdefault(NVRAM, [])
    if 'boot-args' not in deletes:
        deletes.append('boot-args')
    if with_bluetooth:
        args.update(BLUETOOTH_NVRAM)
        deletes.extend(name for name in BLUETOOTH_NVRAM if name not in deletes)
    return installed.difference(reused.values()), [item['BundlePath'] for item in new], required_args


def detect_realtek_bluetooth():
    try:
        registry = subprocess.check_output([
            '/usr/sbin/ioreg', '-a', '-p', 'IOUSB', '-r', '-c', 'IOUSBHostDevice', '-d', '1', '-w0',
        ])
    except subprocess.CalledProcessError:
        return []
    supported = LOCK['bluetooth']['usb_devices']
    matches = set()
    for device in plistlib.loads(registry):
        vendor, product = device.get('idVendor'), device.get('idProduct')
        if isinstance(vendor, int) and isinstance(product, int):
            vid, pid = f'{vendor:04x}', f'{product:04x}'
            if pid in supported.get(vid, ()):
                matches.add(f'{vid}:{pid}')
    return sorted(matches)


def diskutil_info(target):
    result = subprocess.check_output(['/usr/sbin/diskutil', 'info', '-plist', str(target)])
    return plistlib.loads(result)


def oc_from_path(setting):
    path = setting.expanduser().resolve()
    if path.name.lower() == 'config.plist' and path.is_file():
        path = path.parent
    for oc in (path, path / 'OC', path / 'EFI/OC'):
        if (oc / 'config.plist').is_file() and (oc / 'Kexts').is_dir():
            return oc
    return None


def find_oc(setting):
    if setting is not None:
        oc = oc_from_path(setting)
        require(oc, f'OpenCore config.plist and Kexts were not found at {setting}')
        return oc

    mounted = sorted({oc for volume in Path('/Volumes').iterdir() if (oc := oc_from_path(volume))})
    if mounted:
        if len(mounted) > 1:
            for index, oc in enumerate(mounted, 1):
                print(f'  {index}. {oc}')
        return mounted[select(len(mounted), 'OpenCore EFI')]

    disks = plistlib.loads(subprocess.check_output(['/usr/sbin/diskutil', 'list', '-plist']))
    partitions = [part for disk in disks['AllDisksAndPartitions'] for part in disk.get('Partitions', [])
                  if part.get('Content') == 'EFI' or part.get('VolumeName', '').upper() == 'EFI']
    while partitions:
        for index, part in enumerate(partitions, 1):
            label = part.get('VolumeName') or part['Content']
            print(f"  {index}. {part['DeviceIdentifier']} ({label})")
        part = partitions.pop(select(len(partitions), 'EFI partition'))
        device = part['DeviceIdentifier']
        result = subprocess.run(['/usr/sbin/diskutil', 'mount', device])
        if result.returncode == 0:
            mount_point = diskutil_info(device).get('MountPoint')
            if mount_point and (oc := oc_from_path(Path(mount_point))):
                return oc
            print(f'OpenCore config.plist and Kexts were not found on {device}.')
    answer = input('Drag the mounted EFI, OC folder, or config.plist here and press Return: ').strip()
    require(answer, 'Select your mounted OpenCore EFI with --efi /path/to/EFI')
    if oc := oc_from_path(Path(answer)):
        return oc
    paths = shlex.split(answer)
    require(len(paths) == 1, 'Select your mounted OpenCore EFI with --efi /path/to/EFI')
    return find_oc(Path(paths[0]))


def select(count, label):
    if count == 1:
        return 0
    answer = input(f'Select {label} [1-{count}]: ').strip()
    require(answer.isdigit() and 1 <= int(answer) <= count, 'Invalid selection')
    return int(answer) - 1


def validate_config(validator, config, stock_validator=False):
    subprocess.run(['/usr/bin/plutil', '-lint', str(config)], check=True)
    if not stock_validator:
        subprocess.run([str(validator), str(config)], check=True)
        return

    # Stock ocvalidate cannot parse these extra OpenCore MOD booleans.
    # Keep them in the installed plist and validate the stock schema on a copy.
    standard = read_plist(config)
    mod_keys = []
    for section, subsection, key in OPENCORE_MOD_KEYS:
        options = standard.get(section, {}).get(subsection, {})
        if key in options:
            require(isinstance(options[key], bool), f'{section}/{subsection}/{key} must be a boolean')
            del options[key]
            mod_keys.append(f'{section}/{subsection}/{key}')
    with tempfile.TemporaryDirectory(prefix='rtlairlink-ocvalidate-') as folder:
        target = config
        if mod_keys:
            target = Path(folder) / 'config.plist'
            target.write_bytes(plistlib.dumps(standard, sort_keys=False))
            print('Preserving OpenCore MOD keys: ' + ', '.join(mod_keys), flush=True)
        result = subprocess.run([str(validator), str(target)])
    require(result.returncode == 0,
            'OpenCore validation failed. Review the errors, or use --ocvalidate with the validator from your OpenCore build.')


def write_atomic(target, content):
    pending = target.with_name(f'.{target.name}.{os.getpid()}.tmp')
    try:
        with pending.open('xb') as file:
            file.write(content)
            file.flush()
            os.fsync(file.fileno())
        os.replace(pending, target)
    finally:
        pending.unlink(missing_ok=True)


def install(oc, remove, staged, original, candidate, validate, backup_root, reused_files):
    lock = oc / 'RTLAirLink-install.active'
    lock.mkdir()
    try:
        install_transaction(oc, remove, staged, original, candidate, validate, backup_root, reused_files)
    finally:
        lock.rmdir()


def install_transaction(oc, remove, staged, original, candidate, validate, backup_root, reused_files):
    live = oc / 'Kexts'
    config = oc / 'config.plist'
    require(config.read_bytes() == original, 'EFI changed after the installation preview')
    sources = sorted(p for p in staged.glob('*.kext') if not p.name.startswith('._'))
    names = remove | {p.name for p in sources if (live / p.name).exists()}
    require(all((live / name).is_dir() and not (live / name).is_symlink() for name in names),
            'An existing kext changed before installation')
    require(all(digest_tree(live / name) == hashes for name, hashes in reused_files.items()),
            'A kext selected for reuse changed after the installation preview')
    before = {name: digest_tree(live / name) for name in names}
    backup_root.mkdir(parents=True, exist_ok=True)
    backup = Path(tempfile.mkdtemp(prefix=datetime.now().strftime('RTLAirLink-%Y%m%d-%H%M%S-'), dir=backup_root))
    (backup / 'config.plist').write_bytes(original)
    (backup / 'Kexts').mkdir()
    for name, hashes in before.items():
        copy_clean_bundle(live / name, backup / 'Kexts' / name)
        require(digest_tree(backup / 'Kexts' / name) == hashes, f'Backup verification failed for {name}')
    print(f'Backup saved at {backup}', flush=True)

    transaction = Path(tempfile.mkdtemp(prefix='RTLAirLink-stage-', dir=oc))
    old, new = transaction / 'old', transaction / 'new'
    old.mkdir()
    new.mkdir()
    removed, placed = [], []
    try:
        checksums = reused_files.copy()
        for source in sources:
            copy_clean_bundle(source, new / source.name)
            checksums[source.name] = digest_tree(source)
            require(digest_tree(new / source.name) == checksums[source.name], f'Stage copy differs for {source.name}')
        require(config.read_bytes() == original and
                all(digest_tree(live / name) == hashes for name, hashes in (before | reused_files).items()),
                'EFI changed while staging the update')
        for name in sorted(names):
            (live / name).rename(old / name)
            removed.append(name)
        for source in sources:
            (new / source.name).rename(live / source.name)
            placed.append(source.name)
        require(all(digest_tree(live / name) == hashes for name, hashes in checksums.items()),
                'Installed kext files differ from the release')
        if candidate != original:
            write_atomic(config, candidate)
        validate(config)
    except BaseException:
        if candidate != original and config.read_bytes() == candidate:
            write_atomic(config, original)
        for name in reversed(placed):
            (live / name).rename(new / name)
        for name in reversed(removed):
            (old / name).rename(live / name)
        shutil.rmtree(transaction)
        raise
    shutil.rmtree(transaction)
    print(f'RTLAirLink {VERSION} installed to EFI.')


def maybe_install_rtgd(args):
    installer = ROOT / 'tools/install-rtgd.sh'
    binary = ROOT / 'tools/rtlairlink-rtgd'
    if args.dry_run or args.no_rtgd or not installer.is_file() or not binary.is_file():
        return
    if not args.with_rtgd:
        if args.yes:
            return
        answer = input('Install rtlairlink-rtgd service (smooth iPhone Mirroring) & rtlairlinkctl? [Y/n] ').strip().lower()
        if answer not in ('', 'y', 'yes'):
            print('Skipped rtlairlink-rtgd. You can install it later with: sudo bash tools/install-rtgd.sh')
            return
    cmd = ['/bin/bash', str(installer)] if os.geteuid() == 0 else ['/usr/bin/sudo', '/bin/bash', str(installer)]
    result = subprocess.run(cmd)
    if result.returncode != 0:
        print('Warning: rtlairlink-rtgd installation did not complete. Run sudo bash tools/install-rtgd.sh manually.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--efi', type=Path, help='Mounted EFI volume, OpenCore folder, or config.plist')
    parser.add_argument('--cache', type=Path, default=Path.home() / 'Downloads/RTLAirLink-Dependencies')
    parser.add_argument('--ocvalidate', type=Path, help='Validator from your OpenCore release')
    parser.add_argument('--with-bluetooth', action='store_true',
                        help='Install or update bundled Realtek USB Bluetooth kexts (BlueToolFixup + RealtekBluetoothFirmware)')
    parser.add_argument('--no-wpa3', action='store_true',
                        help='Add -rtw89nowpa3 boot-arg to disable WPA3/SAE if your router has WPA3 issues')
    parser.add_argument('--keep-existing-deps', action='store_true',
                        help='Keep compatible Lilu/AMFIPass/Bluetooth kexts on EFI instead of replacing with bundled verified copies')
    parser.add_argument('--with-rtgd', action='store_true',
                        help='Install rtlairlink-rtgd background service (for smooth iPhone Mirroring) without prompting')
    parser.add_argument('--no-rtgd', action='store_true',
                        help='Skip installing rtlairlink-rtgd background service')
    parser.add_argument('--dry-run', action='store_true', help='Validate the plan without writing to the EFI')
    parser.add_argument('--yes', action='store_true', help='Apply to the explicitly selected --efi target')
    args = parser.parse_args()
    require(platform.system() == 'Darwin' and platform.machine() == 'x86_64',
            'The installer requires x86_64 macOS (Intel or AMD)')
    require(args.dry_run or platform.release().split('.')[0] == '25', 'This release targets macOS Tahoe')
    require(not args.yes or args.efi is not None, '--yes requires an explicit --efi volume')
    manifest = validate_package(ROOT)
    oc = find_oc(args.efi)
    existing = oc / 'Kexts'
    config_path = oc / 'config.plist'
    original = config_path.read_bytes()
    require(existing.is_dir(), 'The OpenCore Kexts directory is missing')
    config = plistlib.loads(original)
    require(config['Misc']['Security'].get('Vault') == 'Optional', 'OpenCore Vault must be re-signed after configuration changes')
    if not args.dry_run:
        volume = oc.parents[1]
        disk = diskutil_info(volume)
        require(disk.get('FilesystemType') == 'msdos' and disk.get('PartitionMapPartition')
                and Path(disk.get('MountPoint', '/')) == volume,
                'Select a mounted FAT partition containing OpenCore')
        require(os.access(oc, os.W_OK) and os.access(existing, os.W_OK),
                'The EFI is read-only. Remount it with write access and run the installer again.')
    bluetooth_controllers = detect_realtek_bluetooth() if args.with_bluetooth else []
    with_bluetooth = bool(args.with_bluetooth)
    with tempfile.TemporaryDirectory(prefix='rtlairlink-install-') as path:
        stage = Path(path) / 'Kexts'
        stage.mkdir()
        custom_validator = args.ocvalidate.expanduser().resolve() if args.ocvalidate else None
        validator, reused = prepare(ROOT, manifest, existing, args.cache.expanduser(), stage,
                                    custom_validator, with_bluetooth, args.keep_existing_deps)
        validate = partial(validate_config, validator, stock_validator=custom_validator is None)
        reused_files = {name: digest_tree(existing / name) for name in reused.values()}
        remove, load_order, required_args = configure(config, existing, stage, reused, with_bluetooth, args.no_wpa3)
        changes_config = config != plistlib.loads(original)
        planned = plistlib.dumps(config, sort_keys=False) if changes_config else original
        candidate = Path(path) / 'config.plist'
        candidate.write_bytes(planned)
        validate(candidate)
        print(f'OpenCore target: {oc}')
        print('Load order: ' + ' -> '.join(load_order))
        print('Boot arguments: ' + ' '.join(required_args))
        print('Kernel Block: ' + SKYWALK_ID + ' (Exclude, MinKernel 23.0.0)')
        print('SecureBootModel: Disabled')
        if with_bluetooth:
            if bluetooth_controllers:
                print('Realtek Bluetooth USB detected: ' + ', '.join(bluetooth_controllers))
            else:
                print('Realtek Bluetooth USB: not currently detected (installing kexts as requested; verify your USB map)')
            print('Bluetooth NVRAM: ' + ', '.join(BLUETOOTH_NVRAM))
        copy = sorted(p.name for p in stage.iterdir() if not p.name.startswith('._'))
        print('Kexts to install/replace: ' + (', '.join(copy) or 'none'))
        print('Existing kexts already matching (reused): ' + (', '.join(sorted(reused.values())) or 'none'))
        print('Existing kexts to back up & replace/remove: ' + (', '.join(sorted(remove)) or 'none'))
        if args.dry_run:
            print('Dry run passed.')
            return
        if not copy and not remove and not changes_config:
            print(f'RTLAirLink {VERSION} and its dependencies are already up to date on EFI.')
            maybe_install_rtgd(args)
            return
        if not args.yes:
            answer = input('Back up and install to this EFI? [y/N] ').strip().lower()
            require(answer in ('y', 'yes', 'install'), 'Installation cancelled')
        install(oc, remove, stage, original, planned, validate,
                Path.home() / 'Downloads' / 'RTLAirLink-Backups', reused_files)
    maybe_install_rtgd(args)
    print(f'Done! Restart macOS to load RTLAirLink {VERSION}.')


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, ValueError, KeyError, subprocess.CalledProcessError,
            zipfile.BadZipFile, EOFError) as error:
        sys.exit(f'RTLAirLink: {error}')
