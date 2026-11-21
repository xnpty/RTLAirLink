#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Updated 2026-11-21 for the RTLAirLink 0.2.0 cohort.
"""Build, verify and package RTLAirLink on macOS x86_64."""
import argparse
import datetime
import json
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

from check_p2p_abi import run as check_p2p_abi

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'tools'))
from download_dependencies import LOCK, sha, unpack_bundle
from install_efi import (BRIDGE, DRIVER, FAMILY, FRONT, RELEASE_IDS,
                         VERSION, bundle_info, digest_tree, executable_path,
                         read_plist, require, validate_package)

DRIVER_SOURCE = ROOT / 'Source-Build'
BRIDGE_SOURCE = ROOT / 'Bridge-Source'
REPOSITORY = 'xnpty/RTLAirLink'
TAG = 'v' + VERSION
PLISTS = {
    DRIVER: DRIVER_SOURCE / 'src/kext/Info.plist',
    FRONT: DRIVER_SOURCE / 'src/front/Info.plist',
    BRIDGE: BRIDGE_SOURCE / 'Info.plist',
}


def output(*command):
    return subprocess.check_output(command, cwd=ROOT, text=True).strip()


def run(command, log, cwd=ROOT):
    print(f'Running {log.name}', flush=True)
    with log.open('w') as stream:
        result = subprocess.run(command, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT)
    require(result.returncode == 0, f'{log.name} failed\n' + log.read_text(errors='replace')[-2400:])


def uuid(image):
    identity = re.findall(r'UUID: ([A-F0-9-]+) \(x86_64\)', output('xcrun', 'dwarfdump', '--uuid', image))
    require(len(identity) == 1, f'Expected Intel Mach-O UUID in {image.name}')
    return identity[0]


def verify_versions():
    for name, path in PLISTS.items():
        info = read_plist(path)
        require(info['CFBundleIdentifier'] == RELEASE_IDS[name]
                and info['CFBundleShortVersionString'] == info['CFBundleVersion'] == VERSION,
                f'Version or module ID mismatch in {path}')
    for header in (DRIVER_SOURCE / 'src/rtw89_release.h', BRIDGE_SOURCE / 'src/ReleaseVersion.hpp'):
        versions = re.findall(r'^#define RTLAIRLINK_VERSION "([^"]+)"$', header.read_text(), re.M)
        require(versions == [VERSION], f'Display version mismatch in {header}')


def check_bridge(out):
    run([sys.executable, 'tools/verify_source.py'], out/'bridge-source.log', BRIDGE_SOURCE)
    run([sys.executable, 'tools/audit_lifecycle.py'], out/'bridge-lifecycle.log', BRIDGE_SOURCE)
    run(['make', 'hosttest-sanitize', 'bootstrap-hosttest', 'target-hosttest',
         'entitlement-hosttest', 'native-adapter-hosttest'], out/'bridge-host.log', BRIDGE_SOURCE)


def resolve_legacy(args, workspace):
    entry = next(item for item in LOCK['dependencies'] if item['bundle'] == FAMILY)
    if args.legacy_image:
        image = args.legacy_image.resolve()
    else:
        bundle = unpack_bundle(entry, args.dependency_cache, workspace)
        image = executable_path(bundle, bundle_info(bundle))
    require(sha(image) == entry['executable_sha256'], 'IO80211FamilyLegacy dependency checksum differs')
    return image


def sign_and_manifest(package, out):
    for name in (DRIVER, BRIDGE):
        run(['/usr/bin/codesign', '--force', '--sign', '-', '--timestamp=none', str(package/name)],
            out/f'sign-{name}.log')
    cohort = {}
    for name, identifier in RELEASE_IDS.items():
        bundle = package / name
        info = bundle_info(bundle)
        require(info['CFBundleIdentifier'] == identifier and info['CFBundleVersion'] == VERSION,
                f'Unexpected built identity for {name}')
        image = executable_path(bundle, info)
        data = image.read_bytes()
        descriptor = identifier.encode().ljust(64, b'\0') + VERSION.encode().ljust(64, b'\0')
        require(data.count(descriptor) == 1, f'Executable module descriptor mismatch for {name}')
        cohort[name] = {'identifier': identifier, 'executable': info['CFBundleExecutable'],
                        'version': VERSION, 'uuid': uuid(image), 'sha256': sha(image)}
    front_uuid = bytes.fromhex(cohort[FRONT]['uuid'].replace('-', ''))
    bridge = package / BRIDGE
    require(executable_path(bridge, bundle_info(bridge)).read_bytes().count(front_uuid) == 1,
            'Bridge does not target this frontend UUID')
    revision = output('git', 'rev-parse', 'HEAD')
    manifest = {
        'repository': REPOSITORY, 'release': TAG, 'release_version': VERSION,
        'source_commit': revision,
        'binary_archive': f'RTLAirLink-{TAG}-x86_64.zip',
        'source_archive': f'RTLAirLink-{TAG}-source.zip',
        'cohort': cohort,
        'bundle_files': {name: digest_tree(package/name) for name in (DRIVER, BRIDGE)},
    }
    (package/'RELEASE_MANIFEST.json').write_text(json.dumps(manifest, indent=2)+'\n')
    validate_package(package)
    return manifest


def package_release(out):
    package = out/f'RTLAirLink-{TAG}'
    package.mkdir()
    shutil.copytree(DRIVER_SOURCE/'build/out/bundle'/DRIVER, package/DRIVER)
    shutil.copytree(DRIVER_SOURCE/'build/out'/BRIDGE, package/BRIDGE)
    tools = package/'tools'
    tools.mkdir()
    for name in ('install_efi.py', 'download_dependencies.py'):
        shutil.copy2(ROOT/'tools'/name, tools/name)
    shutil.copy2(DRIVER_SOURCE/'build/out/rtlairlinkctl', tools/'rtlairlinkctl')
    for name in ('LICENSE', 'CREDITS.md', 'MODIFICATIONS.md', 'DEPENDENCIES.json',
                 'Install-RTLAirLink.command', 'docs/INSTALL.md'):
        shutil.copy2(ROOT/name, package/Path(name).name)
    shutil.copytree(ROOT/'LICENSES', package/'LICENSES')
    manifest = sign_and_manifest(package, out)
    front = package / FRONT
    p2p_abi = check_p2p_abi(executable_path(front, bundle_info(front)))

    report = {
        'release_version': VERSION, 'source_commit': manifest['source_commit'],
        'macos_build': output('/usr/bin/sw_vers', '-buildVersion'), 'architecture': 'x86_64',
        'backend_host_tests': 'passed', 'bridge_host_tests': 'passed',
        'raw_tx_host_tests': 'passed',
        'module_metadata_verified': True, 'bridge_frontend_uuid_verified': True,
        'p2p_abi': p2p_abi,
        'renamed_cohort_boot_verified': False, 'cohort': manifest['cohort'],
    }
    report_text = json.dumps(report, indent=2)+'\n'
    (package/'BUILD_REPORT.json').write_text(report_text)
    (out/'BUILD_REPORT.json').write_text(report_text)
    binary_zip = out/manifest['binary_archive']
    with zipfile.ZipFile(binary_zip, 'w', zipfile.ZIP_DEFLATED) as file:
        for path in sorted(package.rglob('*')):
            if path.is_file():
                file.write(path, path.relative_to(out).as_posix())
    with zipfile.ZipFile(binary_zip) as file:
        require(file.testzip() is None, 'Binary archive CRC check failed')

    source_zip = out/manifest['source_archive']
    subprocess.run(['git', 'archive', '--format=zip', f'--prefix=RTLAirLink-{TAG}-source/',
                    '-o', str(source_zip), manifest['source_commit']], cwd=ROOT, check=True)
    bootstrap = out/'install-rtlairlink.sh'
    template = (ROOT/'tools/install-bootstrap.sh.in').read_text()
    bootstrap.write_text(template.replace('@ARCHIVE@', binary_zip.name)
                         .replace('@RELEASE_URL@', f'https://github.com/{REPOSITORY}/releases/download/{TAG}')
                         .replace('@DIGEST@', sha(binary_zip)).replace('@TAG@', TAG))
    bootstrap.chmod(0o755)
    subprocess.run(['/bin/bash', '-n', str(bootstrap)], check=True)
    manifest.update(binary_archive_sha256=sha(binary_zip), source_archive_sha256=sha(source_zip),
                    installer_sha256=sha(bootstrap))
    (out/'RELEASE_MANIFEST.json').write_text(json.dumps(manifest, indent=2)+'\n')
    files = (binary_zip.name, source_zip.name, bootstrap.name, 'BUILD_REPORT.json', 'RELEASE_MANIFEST.json')
    (out/'SHA256SUMS').write_text(''.join(f'{sha(out/name)}  {name}\n' for name in files))
    print(f'Release files in {out}', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--checks-only', action='store_true')
    parser.add_argument('--legacy-image', type=Path, help='Exact Legacy dependency executable')
    parser.add_argument('--dependency-cache', type=Path, default=ROOT/'build/dependency-cache')
    args = parser.parse_args()
    require(platform.system() == 'Darwin' and platform.machine() == 'x86_64', 'Use Intel macOS with Xcode tools')
    verify_versions()
    folder = ROOT/'build'
    folder.mkdir(exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix=datetime.datetime.now().strftime('%Y%m%d-%H%M%S-'), dir=folder))
    check_bridge(out)
    run([sys.executable, 'check_raw_tx.py', '--out', str(out/'raw-tx')], out/'raw-tx.log')
    if args.checks_only:
        run(['make', 'hosttest'], out/'driver-host.log', DRIVER_SOURCE)
    else:
        require(not output('git', 'status', '--porcelain'), 'Commit the source before building release archives')
        with tempfile.TemporaryDirectory(prefix='rtlairlink-legacy-') as path:
            family = resolve_legacy(args, Path(path))
            run(['make', 'bundle', f'FAMILY_BIN={family}'], out/'driver-build.log', DRIVER_SOURCE)
    if not args.checks_only:
        package_release(out)


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
