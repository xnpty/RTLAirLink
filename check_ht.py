#!/usr/bin/env python3
"""Verify the production HT capability getter and scheduler with mocked hardware."""
from pathlib import Path
import json
import os
import platform
import shutil
import subprocess
import tempfile
from check_vif_restore import extract_function

ROOT = Path(__file__).resolve().parent


def run(source, output, cc=None):
    source, output = Path(source), Path(output)
    output.mkdir(parents=True, exist_ok=True)
    include = source / 'src/compat_rtw89'
    body = extract_function((include / 'rtw89_glue.c').read_text(),
                            'rtw89_glue_awdl_ht_capability')
    compiler = cc or shutil.which('cc')
    cxx = shutil.which('c++')
    sdk = []
    if platform.system() == 'Darwin':
        sdk = ['-isysroot', subprocess.check_output(
            ['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip()]
    results = {}
    with tempfile.TemporaryDirectory(prefix='owly-ht-') as folder:
        folder = Path(folder)
        (folder / 'production_ht_glue.inc').write_text(body)
        native = (source / 'src/kext/RTLAirLinkNative.cpp').read_text()
        at = native.index('    case APPLE80211_IOC_HT_CAPABILITY: {')
        end = native.index('    case APPLE80211_IOC_PHY_MODE:', at)
        (folder / 'production_native_ht_case.inc').write_text(native[at:end])
        front = (source / 'src/front/RTLAirLinkFront.cpp').read_text()
        at = front.index('    if (type==SIOCGA80211 &&\n        (number==APPLE80211_IOC_POWER')
        end = front.index('    const bool syncRequest', at)
        (folder / 'production_front_ht_dispatch.inc').write_text(front[at:end])
        for mode in ('normal', 'asan_ubsan', 'feature_off'):
            flags = ['-O2'] if mode != 'asan_ubsan' else [
                '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            command = [compiler, *sdk, *flags, '-std=gnu11', '-UNDEBUG',
                       '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                       '-Wno-unused-variable', '-DOWLY_NATIVE_PEER_DATA=' +
                       str(int(mode != 'feature_off')), '-I' + str(include),
                       '-I' + str(folder), str(ROOT / 'tests/awdl_ht_test.c'),
                       str(include / 'owly_awdl_sync.c'),
                       str(include / 'owly_native_config.c'), '-o', str(folder / 'test')]
            for stage, args in (('compile', command), ('run', [str(folder / 'test')])):
                proc = subprocess.run(args, capture_output=True, text=True, timeout=45,
                    env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1'))
                (output / f'{mode}-{stage}.txt').write_text(proc.stdout + proc.stderr)
                if proc.returncode:
                    raise RuntimeError(f'HT {mode}/{stage}: ' + (proc.stdout + proc.stderr)[-3000:])
            results[mode] = proc.stdout.strip()
            if mode == 'feature_off':
                continue
            command = [cxx, *sdk, *flags, '-std=c++17', '-UNDEBUG', '-Wall', '-Wextra',
                       '-Werror', '-I' + str(folder),
                       str(ROOT / 'tests/native_ht_dispatch_test.cpp'), '-o', str(folder / 'dispatch')]
            for stage, args in (('compile', command), ('run', [str(folder / 'dispatch')])):
                proc = subprocess.run(args, capture_output=True, text=True, timeout=45,
                    env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1'))
                (output / f'{mode}-dispatch-{stage}.txt').write_text(proc.stdout + proc.stderr)
                if proc.returncode:
                    raise RuntimeError(f'HT dispatch {mode}/{stage}: ' + (proc.stdout + proc.stderr)[-3000:])
            results[mode + '_dispatch'] = proc.stdout.strip()
    return {'production_getter_and_scheduler': True, 'hardware_mocked': True,
            'native_apple_extraction_model_only': True, 'results': results,
            'file_transfer_proven': False}


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(run(ROOT / 'Source-Build', args.out), indent=2))
