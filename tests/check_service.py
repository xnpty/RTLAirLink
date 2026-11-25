#!/usr/bin/env python3
"""Exercise extracted production service dispatch and hardware-readiness code."""
from pathlib import Path
import json
import os
import platform
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def extract(text, name):
    start = text.index(name)
    begin = text.index('{', start)
    depth, pos = 1, begin + 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[start:pos]

def run(source, output, cxx=None):
    source, output = Path(source), Path(output)
    output.mkdir(parents=True, exist_ok=True)
    cxx = cxx or shutil.which('c++')
    if not cxx:
        raise RuntimeError('A C++ compiler is required')
    front = (source / 'src/front/RTLAirLinkFront.cpp').read_text()
    glue = (source / 'src/compat_rtw89/rtw89_glue.c').read_text()
    production_front = extract(front, 'bool RTLAirLinkFront::owlyServiceIoctl(') + '\n'
    production_front += extract(front, 'int RTLAirLinkFront::owlyServiceIoctlOnGate(') + '\n'
    production_ready = extract(glue, 'static struct ieee80211_channel *owly_sync_bound_channel(') + '\n'
    old_predicate = 'static struct ieee80211_channel *owly_sync_channel('
    if old_predicate in glue:
        production_ready += extract(glue, old_predicate) + '\n'
    production_ready += extract(glue, 'bool rtw89_glue_awdl_service_ready(') + '\n'
    sdk = []
    if platform.system() == 'Darwin':
        sdk = ['-isysroot', subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip()]
    env = os.environ.copy()
    env['UBSAN_OPTIONS'] = 'halt_on_error=1'
    if platform.system() == 'Linux':
        env['ASAN_OPTIONS'] = 'detect_leaks=0'
    with tempfile.TemporaryDirectory(prefix='owly-service-model-') as folder:
        temp = Path(folder)
        for name, body in [('front', production_front), ('ready', production_ready)]:
            for mode in ['normal', 'asan_ubsan', 'feature_off']:
                enabled = mode != 'feature_off'
                (temp / 'production.inc').write_text(body if enabled or name == 'ready' else '')
                flags = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if mode == 'asan_ubsan' else ['-O2']
                if mode == 'asan_ubsan' and platform.system() == 'Linux':
                    flags.append('-no-pie')
                command = [cxx, '-std=c++17', '-Wall', '-Wextra', '-Wno-unused-function', *sdk, *flags,
                           '-DOWLY_AWDL_SERVICE_LIFECYCLE=' + str(int(enabled)),
                           '-DOWLY_AWDL_SOFTWARE_SYNC=' + str(int(enabled)), '-I' + str(temp),
                           str(ROOT / 'tests' / ('test_service_' + name + '.cpp')), '-o', str(temp / 'test')]
                built = subprocess.run(command, capture_output=True, text=True)
                (output / (name + '-' + mode + '-compile.txt')).write_text('COMMAND: ' + repr(command) + '\n' + built.stdout + built.stderr)
                if built.returncode:
                    raise RuntimeError('Service model compile failed: ' + name + '/' + mode)
                tested = subprocess.run([str(temp / 'test')], capture_output=True, text=True, env=env, timeout=30)
                (output / (name + '-' + mode + '.txt')).write_text(tested.stdout + tested.stderr)
                if tested.returncode:
                    raise RuntimeError('Service model failed: ' + name + '/' + mode)
    return {'extracted_production_front_dispatch': True, 'extracted_scc_service_readiness_predicate': True,
            'modes': ['normal', 'asan_ubsan', 'feature_off'], 'native_kernel_rf_or_airdrop_proven': False}

if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, default=ROOT / 'Source-Build')
    parser.add_argument('--out', type=Path, default=ROOT / 'validation/service')
    args = parser.parse_args()
    print(json.dumps(run(args.source, args.out), indent=2))
