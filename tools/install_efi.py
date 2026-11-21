#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Added 2026-11-21 for RTLAirLink 0.2.0.
"""Install the verified RTLAirLink cohort into an existing OpenCore EFI."""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path, PurePosixPath
import plistlib
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

from download_dependencies import LOCK, ROOT, fetch, sha, unpack_bundle

VERSION = '0.2.0'
DRIVER = 'RTLAirLink.kext'
FRONT = f'{DRIVER}/Contents/PlugIns/RTLAirLinkFront.kext'
BRIDGE = 'RTLAirBridge.kext'
FAMILY = 'IO80211FamilyLegacy.kext'
LILU = 'Lilu.kext'
ORDER = (LILU, BRIDGE, 'IOSkywalkFamily.kext', FAMILY, 'AMFIPass.kext', DRIVER, FRONT)
NVRAM = '7C436110-AB2A-4BBB-A880-FE41995C9F82'
SKYWALK_ID = 'com.apple.iokit.IOSkywalkFamily'
RELEASE_IDS = {
    DRIVER: 'io.github.xnpty.RTLAirLink',
    FRONT: 'io.github.xnpty.RTLAirLink.front',
    BRIDGE: 'io.github.xnpty.RTLAirLink.bridge',
}
# Bundle IDs recognize pre-0.2.0 installations under any directory name.
MIGRATION_IDS = {'com.rtw89.driver', 'com.rtw89.front', 'com.owly.airlink.eventbridge'}
DEPENDENCY_IDS = {entry['bundle_identifier'] for entry in LOCK['dependencies']}
REMOVED_BOOT_ARGS = {'-owlyeb1', '-owlyeb1off', 'owlyebstage'}
BOOT_ARGS = ('-amfipassbeta',)


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
    return read_plist(info).get('CFBundleIdentifier') if info.is_file() else None


def executable_path(bundle, info):
    name = info['CFBundleExecutable']
    require(Path(name).name == name and name not in ('', '.', '..'), 'Invalid kext executable name')
    executable = bundle / 'Contents/MacOS' / name
    require(executable.is_file(), f'Missing executable in {bundle.name}')
    return executable


def digest_tree(directory):
    return {p.relative_to(directory).as_posix(): sha(p) for p in directory.rglob('*')
            if p.is_file() and not p.name.startswith('._')}


def validate_package(package):
    manifest = json.loads((package / 'RELEASE_MANIFEST.json').read_text())
    require(manifest['release_version'] == VERSION, 'Package version does not match this installer')
    for name, expected in RELEASE_IDS.items():
        bundle = package / name
        info = bundle_info(bundle)
        require(info['CFBundleIdentifier'] == expected, f'Unexpected bundle ID for {name}')
        require(info['CFBundleVersion'] == VERSION, f'Unexpected module version for {name}')
        image = executable_path(bundle, info)
        require(sha(image) == manifest['cohort'][name]['sha256'], f'Checksum mismatch for {name}')
    for name in (DRIVER, BRIDGE):
        require(digest_tree(package / name) == manifest['bundle_files'][name],
                f'Unexpected file contents in {name}')
        subprocess.run(['/usr/bin/codesign', '--verify', '--strict', '--deep', str(package/name)], check=True)


def validate_dependency(path, entry):
    info = bundle_info(path)
    require(info['CFBundleIdentifier'] == entry['bundle_identifier']
            and info['CFBundleVersion'] == entry['bundle_version']
            and sha(executable_path(path, info)) == entry['executable_sha256'],
            f"Unexpected dependency executable in {path.name}")


def version_tuple(value):
    parts = re.fullmatch(r'(\d+)\.(\d+)\.(\d+)', value or '')
    return tuple(map(int, parts.groups())) if parts else ()


def prepare(package, existing, cache, folder, custom_validator):
    for name in (DRIVER, BRIDGE):
        shutil.copytree(package / name, folder / name)
    for entry in LOCK['dependencies']:
        name = entry['bundle']
        if name == LILU:
            current = existing / LILU
            if bundle_id(current) == entry['bundle_identifier']:
                info = bundle_info(current)
                if version_tuple(info.get('CFBundleVersion')) >= version_tuple(entry['bundle_version']):
                    executable_path(current, info)
                    continue
        path = unpack_bundle(entry, cache, folder)
        validate_dependency(path, entry)
        if name == FAMILY:
            for plugin in (path / 'Contents/PlugIns').glob('*.kext'):
                if bundle_id(plugin) == 'com.apple.driver.AirPort.BrcmNIC' or plugin.name == 'AirPortBrcmNIC.kext':
                    shutil.rmtree(plugin)
    if custom_validator is not None:
        require(custom_validator.is_file() and os.access(custom_validator, os.X_OK), 'ocvalidate is not executable')
        return custom_validator
    spec = LOCK['validator']
    target = folder.parent / 'ocvalidate'
    with zipfile.ZipFile(fetch(spec, cache)) as file:
        target.write_bytes(file.read(spec['member']))
    target.chmod(0o700)
    return target


def kext_entry(name, folder, existing):
    bundle = (folder if (folder / name).is_dir() else existing) / name
    info = bundle_info(bundle)
    exe = executable_path(bundle, info)
    if name in RELEASE_IDS:
        minimum, maximum = '25.0.0', '25.99.99'
    elif name in ('IOSkywalkFamily.kext', FAMILY):
        minimum, maximum = '23.0.0', ''
    else:
        minimum, maximum = '', ''
    return {'Arch': 'x86_64', 'BundlePath': name, 'Comment': name.split('/')[-1],
            'Enabled': True, 'ExecutablePath': exe.relative_to(bundle).as_posix(),
            'MaxKernel': maximum, 'MinKernel': minimum, 'PlistPath': 'Contents/Info.plist'}


def configure(config, existing, staged):
    managed_ids = set(RELEASE_IDS.values()) | MIGRATION_IDS | DEPENDENCY_IDS
    managed_names = {name for name in ORDER if '/' not in name}
    installed = {p.name for p in existing.iterdir() if p.is_dir() and p.suffix.lower() == '.kext'
                 and (p.name in managed_names or bundle_id(p) in managed_ids)}
    entries = config['Kernel']['Add']
    retained = []
    preserved_lilu = None
    insert_at = None
    wifi_at = None
    for item in entries:
        path = PurePosixPath(item['BundlePath'])
        require(path.parts and not path.is_absolute() and '..' not in path.parts,
                f'Invalid BundlePath in config.plist: {path}')
        managed = path.parts[0] in installed | managed_names or bundle_id(existing / path) in managed_ids
        if managed:
            if str(path) == LILU:
                insert_at = len(retained)
                if not (staged / LILU).exists():
                    preserved_lilu = item.copy()
            elif wifi_at is None:
                wifi_at = len(retained)
            continue
        if item['Enabled']:
            require((existing / path).is_dir(), f'Enabled kext directory is missing: {path}')
        retained.append(item)
    new = [kext_entry(name, staged, existing) for name in ORDER]
    if preserved_lilu:
        preserved_lilu.update(Enabled=True, PlistPath=new[0]['PlistPath'],
                              ExecutablePath=new[0]['ExecutablePath'])
        require(preserved_lilu.get('Arch', 'Any') in ('Any', 'x86_64'),
                'The installed Lilu entry does not apply to x86_64')
        new[0] = preserved_lilu
    index = insert_at if insert_at is not None else 0
    wifi_index = max(index, wifi_at if wifi_at is not None else index)
    entries[:] = (retained[:index] + new[:1] + retained[index:wifi_index]
                  + new[1:] + retained[wifi_index:])

    blocks = config['Kernel']['Block']
    blocks[:] = [item for item in blocks if item['Identifier'] != SKYWALK_ID]
    blocks.append({'Arch': 'x86_64', 'Comment': 'RTLAirLink Wi-Fi stack',
                   'Enabled': True, 'Identifier': SKYWALK_ID, 'MaxKernel': '',
                   'MinKernel': '23.0.0', 'Strategy': 'Exclude'})

    config['Misc']['Security']['SecureBootModel'] = 'Disabled'
    nvram = config['NVRAM']
    args = nvram['Add'].setdefault(NVRAM, {})
    previous = args.get('boot-args', '').split()
    managed_args = set(BOOT_ARGS) | REMOVED_BOOT_ARGS
    args['boot-args'] = ' '.join([a for a in previous if a.split('=', 1)[0] not in managed_args] + list(BOOT_ARGS))
    deletes = nvram['Delete'].setdefault(NVRAM, [])
    if 'boot-args' not in deletes:
        deletes.append('boot-args')
    if not (staged / LILU).exists():
        installed.discard(LILU)
    return installed


def diskutil_info(target):
    result = subprocess.check_output(['/usr/sbin/diskutil', 'info', '-plist', str(target)])
    return plistlib.loads(result)


def find_oc(setting):
    if setting:
        volume = setting.resolve()
        oc = volume / 'EFI/OC'
        require((oc / 'config.plist').is_file(), f'OpenCore config.plist is missing on {volume}')
        return oc
    candidates = [p for p in Path('/Volumes').iterdir() if (p / 'EFI/OC/config.plist').is_file()]
    if len(candidates) == 1:
        return candidates[0] / 'EFI/OC'
    if not candidates:
        disks = plistlib.loads(subprocess.check_output(['/usr/sbin/diskutil', 'list', '-plist']))
        ids = [p['DeviceIdentifier'] for disk in disks['AllDisksAndPartitions']
               for p in disk.get('Partitions', []) if p.get('Content') == 'EFI']
        if ids:
            for index, device in enumerate(ids, 1):
                print(f'  {index}. EFI partition {device}')
            selected = select(len(ids), 'EFI partition')
            device = ids[selected]
            subprocess.run(['/usr/sbin/diskutil', 'mount', device], check=True)
            return find_oc(Path(diskutil_info(device)['MountPoint']))
    require(candidates, 'No OpenCore EFI found. Use --efi with the mounted EFI volume.')
    for index, volume in enumerate(candidates, 1):
        print(f'  {index}. {volume}')
    return candidates[select(len(candidates), 'OpenCore EFI')] / 'EFI/OC'


def select(count, label):
    if count == 1:
        return 0
    answer = input(f'Select {label} [1-{count}]: ').strip()
    require(answer.isdigit() and 1 <= int(answer) <= count, 'Invalid selection')
    return int(answer)-1


def validate_config(validator, config):
    subprocess.run(['/usr/bin/plutil', '-lint', str(config)], check=True)
    subprocess.run([str(validator), str(config)], check=True)


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


def install(oc, remove, staged, original, candidate, validator, backup_root):
    lock = oc / 'RTLAirLink-install.active'
    lock.mkdir()
    try:
        install_transaction(oc, remove, staged, original, candidate, validator, backup_root)
    finally:
        lock.rmdir()


def install_transaction(oc, remove, staged, original, candidate, validator, backup_root):
    live = oc / 'Kexts'
    config = oc / 'config.plist'
    require(config.read_bytes() == original, 'EFI changed after the installation preview')
    sources = sorted(staged.glob('*.kext'))
    names = remove | {p.name for p in sources if (live / p.name).exists()}
    require(all((live / name).is_dir() and not (live / name).is_symlink() for name in names),
            'An existing kext changed before installation')
    before = {name: digest_tree(live / name) for name in names}
    backup_root.mkdir(parents=True, exist_ok=True)
    backup = Path(tempfile.mkdtemp(prefix=datetime.now().strftime('RTLAirLink-%Y%m%d-%H%M%S-'), dir=backup_root))
    (backup / 'config.plist').write_bytes(original)
    (backup / 'Kexts').mkdir()
    for name, hashes in before.items():
        shutil.copytree(live / name, backup / 'Kexts' / name)
        require(digest_tree(backup / 'Kexts' / name) == hashes, f'Backup verification failed for {name}')
    print(f'Backup saved at {backup}', flush=True)

    transaction = Path(tempfile.mkdtemp(prefix='RTLAirLink-stage-', dir=oc))
    old, new = transaction / 'old', transaction / 'new'
    old.mkdir()
    new.mkdir()
    removed, placed = [], []
    try:
        checksums = {}
        for source in sources:
            shutil.copytree(source, new / source.name)
            checksums[source.name] = digest_tree(source)
            require(digest_tree(new / source.name) == checksums[source.name], f'Stage copy differs for {source.name}')
        require(config.read_bytes() == original and
                all(digest_tree(live / name) == hashes for name, hashes in before.items()),
                'EFI changed while staging the update')
        for name in sorted(names):
            (live / name).rename(old / name)
            removed.append(name)
        for source in sources:
            (new / source.name).rename(live / source.name)
            placed.append(source.name)
        require(all(digest_tree(live / name) == hashes for name, hashes in checksums.items()),
                'Installed kext files differ from the release')
        write_atomic(config, candidate)
        validate_config(validator, config)
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
    print(f'RTLAirLink {VERSION} installed. Restart macOS to load the new kexts.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--efi', type=Path, help='Mounted volume containing EFI/OC')
    parser.add_argument('--cache', type=Path, default=Path.home()/ 'Downloads/RTLAirLink-Dependencies')
    parser.add_argument('--ocvalidate', type=Path, help='Validator from your OpenCore release')
    parser.add_argument('--dry-run', action='store_true', help='Validate the plan without writing to the EFI')
    parser.add_argument('--yes', action='store_true', help='Apply to the explicitly selected --efi volume')
    args = parser.parse_args()
    require(platform.system() == 'Darwin' and platform.machine() == 'x86_64', 'RTLAirLink requires macOS on Intel')
    require(args.dry_run or platform.release().split('.')[0] == '25', 'This release targets macOS Tahoe')
    require(not args.yes or args.efi is not None, '--yes requires an explicit --efi volume')
    validate_package(ROOT)
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
    with tempfile.TemporaryDirectory(prefix='rtlairlink-install-') as path:
        stage = Path(path) / 'Kexts'
        stage.mkdir()
        validator = prepare(ROOT, existing, args.cache.expanduser(), stage,
                            args.ocvalidate.expanduser().resolve() if args.ocvalidate else None)
        remove = configure(config, existing, stage)
        candidate = Path(path) / 'config.plist'
        candidate.write_bytes(plistlib.dumps(config, sort_keys=False))
        validate_config(validator, candidate)
        print(f'OpenCore target {oc}')
        print('Load order ' + ', '.join(ORDER))
        print('Boot arguments ' + ' '.join(BOOT_ARGS))
        print('SecureBootModel Disabled')
        print('Kexts to replace ' + (', '.join(sorted(remove)) or 'none'))
        if args.dry_run:
            print('Dry run passed.')
            return
        if not args.yes:
            require(input('Type INSTALL to back up and apply this plan: ').strip() == 'INSTALL', 'Installation cancelled')
        install(oc, remove, stage, original, candidate.read_bytes(), validator,
                Path.home()/'Downloads'/'RTLAirLink-Backups')


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, OSError, ValueError, KeyError, subprocess.CalledProcessError,
            zipfile.BadZipFile, EOFError) as error:
        sys.exit(f'RTLAirLink: {error}')
