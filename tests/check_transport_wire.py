#!/usr/bin/env python3
"""Exercise actual production election and SCC datapath wire fields."""
from pathlib import Path
import json,os,platform,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
def run(source,output,cc=None):
    source,output=Path(source),Path(output);output.mkdir(parents=True,exist_ok=True)
    cc=cc or shutil.which('cc');inc=source/'src/compat_rtw89'
    env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=0';env['UBSAN_OPTIONS']='halt_on_error=1'
    sdk=['-isysroot',subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()] if platform.system()=='Darwin' else []
    result={}
    with tempfile.TemporaryDirectory(prefix='owly-transport-wire-') as folder:
        for mode in ['normal','asan_ubsan']:
            flags=['-O2'] if mode=='normal' else ['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
            if mode=='asan_ubsan' and platform.system()=='Linux':flags.append('-no-pie')
            exe=Path(folder)/mode
            command=[cc,*sdk,*flags,'-std=gnu11','-Wall','-Wextra','-Werror','-DOWLY_NATIVE_PEER_DATA=1','-I'+str(inc),str(inc/'owly_native_link.c'),str(inc/'owly_awdl_sync.c'),str(ROOT/'tests/transport_wire_test.c'),'-o',str(exe)]
            p=subprocess.run(command,capture_output=True,text=True,env=env);(output/(mode+'-compile.txt')).write_text(p.stdout+p.stderr)
            if p.returncode:raise RuntimeError('Transport wire compile failed '+mode+': '+p.stderr[-4000:])
            p=subprocess.run([str(exe)],capture_output=True,text=True,env=env);(output/(mode+'-run.txt')).write_text(p.stdout+p.stderr)
            if p.returncode:raise RuntimeError('Transport wire test failed '+mode+': '+p.stderr[-4000:])
            result[mode]=p.stdout.strip()
    return {'actual_production_code':True,'results':result,'radio_or_file_transfer_proven':False}
if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=ROOT/'Source-Build');p.add_argument('--out',type=Path,required=True);a=p.parse_args();print(json.dumps(run(a.source,a.out),indent=2))
