#!/usr/bin/env python3
"""Compile and execute the production scan request builder with mocked I/O."""
from pathlib import Path
import json,os,platform,shutil,subprocess,tempfile
from check_vif_restore import extract_function
ROOT=Path(__file__).resolve().parents[1]
def run(source,output,cc=None,cxx=None):
    source,output=Path(source),Path(output);output.mkdir(parents=True,exist_ok=True)
    cc=cc or shutil.which('cc');cxx=cxx or shutil.which('c++');inc=source/'src/compat_rtw89';sdk=[]
    if platform.system()=='Darwin':sdk=['-isysroot',subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()]
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1');results={}
    with tempfile.TemporaryDirectory(prefix='owly-native-scan-') as folder:
        folder=Path(folder)
        (folder/'production_scan_builder.inc').write_text(extract_function((inc/'rtw89_glue.c').read_text(),'glue_scan_locked'))
        native=(source/'src/kext/RTLAirLinkNative.cpp').read_text()
        start=native.index('    case APPLE80211_IOC_SCAN_REQ:\n')
        end=native.index('    case APPLE80211_IOC_CURRENT_NETWORK:',start)
        (folder/'production_native_scan_cases.inc').write_text(native[start:end])
        for mode,flags in [('normal',['-O2']),('asan_ubsan',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
            if mode=='asan_ubsan' and platform.system()=='Linux':flags+=['-no-pie']
            exe=str(folder/mode)
            cmd=[cc,*sdk,*flags,'-std=gnu11','-Wall','-Werror','-Wno-unused-function','-I'+str(folder),'-I'+str(inc),str(ROOT/'tests/native_scan_builder_test.c'),'-o',exe]
            for step,args in [('compile',cmd),('run',[exe])]:
                p=subprocess.run(args,capture_output=True,text=True,env=env,timeout=60)
                (output/(mode+'-'+step+'.txt')).write_text(p.stdout+p.stderr)
                if p.returncode:raise RuntimeError('Native scan '+mode+'/'+step+': '+p.stderr[-2000:])
            results[mode]=p.stdout.strip()
            dispatch=str(folder/(mode+'-dispatch'))
            cmd=[cxx,*sdk,*flags,'-std=gnu++17','-Wall','-Werror','-I'+str(folder),'-I'+str(inc),str(ROOT/'tests/native_scan_dispatch_test.cpp'),'-o',dispatch]
            for step,args in [('compile',cmd),('run',[dispatch])]:
                p=subprocess.run(args,capture_output=True,text=True,env=env,timeout=60)
                (output/(mode+'-dispatch-'+step+'.txt')).write_text(p.stdout+p.stderr)
                if p.returncode:raise RuntimeError('Native scan dispatch '+mode+'/'+step+': '+p.stderr[-2000:])
            results[mode+'_dispatch']=p.stdout.strip()
    return {'actual_production_scan_builder':True,'driver_calls_mocked':True,'results':results,'radio_file_transfer_proven':False}
if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=ROOT/'Source-Build');p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    print(json.dumps(run(a.source,a.out),indent=2))
