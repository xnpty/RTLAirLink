#!/usr/bin/env python3
"""Exercise AWDL CAM, RX filters and TX port selection with mocked hardware I/O."""
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile

from check_vif_restore import extract_function

ROOT = Path(__file__).resolve().parents[1]


def run(source, output, cc=None):
    source, output = Path(source), Path(output)
    output.mkdir(parents=True, exist_ok=True)
    glue = (source/'src/compat_rtw89/rtw89_glue.c').read_text()
    vendor = source/'third_party/rtw89'
    mac80211 = (vendor/'mac80211.c').read_text()
    cam = (vendor/'cam.c').read_text()
    header = (source/'third_party/linux-include/net/mac80211.h').read_text()
    flags = ('FIF_ALLMULTI', 'FIF_FCSFAIL', 'FIF_BCN_PRBRESP_PROMISC', 'FIF_OTHER_BSS', 'FIF_PROBE_REQ')
    definitions = [f'#define {name} ({re.search(rf"\b{name}\s*=\s*([^,\n]+)", header)[1]})'
                   for name in flags]
    regs = (vendor/'reg.h').read_text()
    bits = ('B_AX_A_MC', 'B_AX_A_MC_LIST_CAM_MATCH',
            'B_AX_A_CRC32_ERR', 'B_AX_A_A1_MATCH', 'B_AX_A_BCN_CHK_EN',
            'B_AX_A_BC', 'B_AX_A_BC_CAM_MATCH', 'B_AX_A_UC_CAM_MATCH', 'B_AX_SNIFFER_MODE')
    for name in bits:
        value = re.search(r'^#define '+name+r'\s+(BIT\(\d+\))', regs, re.M)[1]
        definitions.append(f'#define {name} {value}')
    for name in ('B_AX_UID_FILTER_MASK', 'B_AX_RX_MPDU_MAX_LEN_MASK',
                 'B_AX_RX_FLTR_CFG_MASK'):
        value = re.search(r'^#define '+name+r'[ \t]+([^\n]+)', regs, re.M)[1]
        definitions.append(f'#define {name} {value}')
    definitions.append(re.search(r'^#define R_AX_RX_FLTR_OPT[ \t]+[^\n]+', regs, re.M)[0])
    txrx = (vendor/'txrx.h').read_text()
    for name, value in re.findall(r'^#define (RTW89_TXWD_INFO0_\w+)\s+([^\n]+)', txrx, re.M):
        definitions.append(f'#define {name} {value}')
    mac = (vendor/'mac.c').read_text()
    functions = [extract_function(mac, 'rtw89_mac_awdl_rx_fltr'),
                 extract_function(mac, 'rtw89_mac_set_rx_fltr'),
                 extract_function(mac, 'rx_fltr_init_ax'),
                 extract_function(mac80211, 'rtw89_ops_configure_filter'),
                 extract_function(cam, 'rtw89_cam_bssid_changed')]
    functions.extend(extract_function(glue, name) for name in (
        'owly_scc_filter_set', 'owly_aux_program_cam', 'owly_core04c_vif_register'))
    core = (vendor/'core.c').read_text()
    functions.extend(extract_function(core, name) for name in (
        'rtw89_core_tx_update_awdl_port', 'rtw89_build_txwd_info0',
        'rtw89_core_awdl_data_rx_immediate'))

    compiler = cc or shutil.which('cc')
    sdk = []
    if platform.system() == 'Darwin':
        sdk = ['-isysroot', subprocess.check_output(
            ['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip()]
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')
    results = {}
    with tempfile.TemporaryDirectory(prefix='owly-rx-filter-') as folder:
        temp = Path(folder)
        (temp/'production_unicast_rx.inc').write_text('\n\n'.join(definitions + functions)+'\n')
        for mode, options in (('normal', ['-O2']), ('asan_ubsan', [
            '-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
        ])):
            if mode == 'asan_ubsan' and platform.system() == 'Linux':
                options.append('-no-pie')
            executable = temp/mode
            command = [compiler, *sdk, *options, '-std=c11', '-Wall', '-Werror',
                       '-DRTW89_MACOS=1', '-DOWLY_NATIVE_PEER_DATA=1', '-I'+str(temp),
                       str(ROOT/'tests/unicast_rx_model.c'), '-o', str(executable)]
            for stage, command in (('compile', command), ('run', [str(executable)])):
                result = subprocess.run(command, capture_output=True, text=True, env=env, timeout=45)
                (output/f'{mode}-{stage}.txt').write_text(result.stdout + result.stderr)
                if result.returncode:
                    raise RuntimeError(f'AWDL unicast model {mode} {stage} failed: {result.stderr[-1600:]}')
            results[mode] = result.stdout.strip()
    return {'production_glue_and_vendor_functions': True, 'hardware_io_mocked': True, 'results': results}


if __name__ == '__main__':
    import argparse
    import json

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT/'Source-Build')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(run(args.source, args.out), indent=2))
