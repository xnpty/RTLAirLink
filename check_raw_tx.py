#!/usr/bin/env python3
"""Run the production AWDL submitter and injected descriptor builder in a host harness.

Only allocation, peer lookup and hardware I/O are mocked. This does not prove RF
delivery. The same tests can be pointed at Fix39 to reproduce the regressions.
"""
from pathlib import Path
import json, os, platform, re, shutil, subprocess, tempfile
from check_vif_restore import extract_function

ROOT = Path(__file__).resolve().parent


def run(source, output, cc=None):
    source, output = Path(source), Path(output)
    output.mkdir(parents=True, exist_ok=True)
    vendor = source / 'third_party/rtw89'
    core = re.sub(r'^static void\s*\n', 'static void ', (vendor / 'core.c').read_text(), flags=re.M)
    glue = (source / 'src/compat_rtw89/rtw89_glue.c').read_text()
    txrx = (vendor / 'txrx.h').read_text()
    definitions = []
    flags = (source / 'third_party/linux-include/net/mac80211.h').read_text()
    flag_defs = []
    for name in ('IEEE80211_TX_CTL_REQ_TX_STATUS', 'IEEE80211_TX_CTL_NO_ACK',
                 'IEEE80211_TX_CTL_ASSIGN_SEQ', 'IEEE80211_TX_CTL_SEND_AFTER_DTIM',
                 'IEEE80211_TX_CTL_INJECTED', 'IEEE80211_TX_INTFL_DONT_ENCRYPT',
                 'IEEE80211_TX_CTL_NO_CCK_RATE', 'IEEE80211_TX_RC_USE_RTS_CTS',
                 'IEEE80211_TX_RC_40_MHZ_WIDTH', 'IEEE80211_TX_RC_80_MHZ_WIDTH',
                 'IEEE80211_TX_RC_160_MHZ_WIDTH', 'IEEE80211_TX_RC_SHORT_GI',
                 'IEEE80211_TX_RC_VHT_MCS', 'IEEE80211_TX_RC_MCS'):
        value = re.search(r'\b' + name + r'\s*=\s*(BIT\(\d+\))', flags)
        if not value:
            raise RuntimeError('Production TX flag missing: ' + name)
        flag_defs.append('#define ' + name + ' ' + value[1])
    for name, value in re.findall(r'^#define (RTW89_TXWD_(?:INFO[014]|BODY[023])_\w+)\s+([^\n]+)', txrx, re.M):
        definitions.append(f'#define {name} {value}')
    functions = [extract_function(core, name) for name in (
        'rtw89_core_tx_update_injection', 'rtw89_core_tx_update_desc_info',
        'rtw89_build_txwd_info0', 'rtw89_build_txwd_info1',
        'rtw89_build_txwd_info4', 'rtw89_build_txwd_body0',
        'rtw89_build_txwd_body2', 'rtw89_build_txwd_body3')]
    submit = extract_function(glue, 'owly_sync_submit')
    if 'owly_awdl_away(' in submit:
        submit = extract_function(glue, 'owly_awdl_away') + '\n\n' + submit
    compiler = cc or shutil.which('cc')
    sdk = []
    if platform.system() == 'Darwin':
        sdk = ['-isysroot', subprocess.check_output(
            ['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip()]
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')
    results = {}
    with tempfile.TemporaryDirectory(prefix='owly-raw-tx-') as folder:
        temp = Path(folder)
        (temp / 'production_raw_flags.inc').write_text('\n'.join(flag_defs))
        (temp / 'production_raw_tx.inc').write_text('\n\n'.join(definitions + functions))
        (temp / 'production_submit.inc').write_text(submit)
        for mode, flags in [('normal', ['-O2']), ('asan_ubsan', [
                '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])]:
            if mode == 'asan_ubsan' and platform.system() == 'Linux':
                flags.append('-no-pie')
            executable = temp / mode
            commands = [('compile', [compiler, *sdk, *flags, '-UNDEBUG', '-std=gnu11',
                '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                '-DOWLY_NATIVE_PEER_DATA=1', '-I' + str(temp),
                str(ROOT / 'tests/raw_tx_model.c'), '-o', str(executable)]),
                ('run', [str(executable)])]
            for stage, command in commands:
                p = subprocess.run(command, capture_output=True, text=True, env=env, timeout=45)
                (output / (mode + '-' + stage + '.txt')).write_text(p.stdout + p.stderr)
                if p.returncode:
                    raise RuntimeError('Raw TX ' + mode + '/' + stage + ': ' + (p.stdout + p.stderr)[-3000:])
            results[mode] = p.stdout.strip()
    return {'actual_submitter_and_descriptor_functions': True, 'hardware_io_mocked': True,
            'results': results, 'radio_delivery_proven': False, 'airdrop_proven': False}


if __name__ == '__main__':
    import argparse
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, default=ROOT / 'Source-Build')
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    print(json.dumps(run(a.source, a.out), indent=2))
