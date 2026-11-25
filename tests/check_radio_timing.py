#!/usr/bin/env python3
"""Exercise production radio sequence/time helpers, including delayed RX."""
from pathlib import Path
import json,os,platform,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
def run(source,output,cc=None):
    source,output=Path(source),Path(output);output.mkdir(parents=True,exist_ok=True)
    cc=cc or shutil.which('cc');sdk=[];result={}
    if platform.system()=='Darwin':
        sdk=['-isysroot',subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()]
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1')
    with tempfile.TemporaryDirectory(prefix='owly-radio-timing-') as t:
        for mode,flags in [('normal',['-O2']),('asan_ubsan',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
            if mode=='asan_ubsan' and platform.system()=='Linux':flags+=['-no-pie']
            exe=Path(t)/mode
            command=[cc,*sdk,*flags,'-UNDEBUG','-std=c11','-Wall','-Wextra','-Werror',
                     '-I'+str(source/'src/compat_rtw89'),str(ROOT/'tests/radio_timing_test.c'),'-o',str(exe)]
            for step,cmd in [('compile',command),('run',[str(exe)])]:
                p=subprocess.run(cmd,capture_output=True,text=True,env=env,timeout=45)
                (output/(mode+'-'+step+'.txt')).write_text(p.stdout+p.stderr)
                if p.returncode:raise RuntimeError('Radio timing '+mode+'/'+step+': '+p.stderr[-2000:])
            result[mode]=p.stdout.strip()
            exe=Path(t)/(mode+'-snapshot')
            inc=source/'src/compat_rtw89'
            command=[cc,*sdk,*flags,'-UNDEBUG','-std=gnu11','-DOWLY_NATIVE_PEER_DATA=1',
                     '-I'+str(inc),str(ROOT/'tests/snapshot_age_test.c'),
                     str(inc/'owly_awdl_sync.c'),str(inc/'owly_native_config.c'),'-o',str(exe)]
            for step,cmd in [('compile',command),('run',[str(exe)])]:
                p=subprocess.run(cmd,capture_output=True,text=True,env=env,timeout=45)
                (output/(mode+'-snapshot-'+step+'.txt')).write_text(p.stdout+p.stderr)
                if p.returncode:raise RuntimeError('Production window projection '+mode+'/'+step+': '+p.stderr[-2000:])
            result[mode+'_snapshot']=p.stdout.strip()
    return {'production_helpers_executed':True,'results':result,'hardware_time_calibration_proven':False,'file_transfer_proven':False}
if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=ROOT/'Source-Build');p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();print(json.dumps(run(a.source,a.out),indent=2))
