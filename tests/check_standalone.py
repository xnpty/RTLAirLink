#!/usr/bin/env python3
"""Check the actual idle-channel code, not a second implementation."""
import json,os,platform,shutil,subprocess,tempfile
from pathlib import Path
from check_vif_restore import extract_function
ROOT=Path(__file__).resolve().parents[1]
def run(source,output,cc=None):
    source=Path(source);output=Path(output);output.mkdir(parents=True,exist_ok=True)
    inc=source/'src/compat_rtw89';compiler=cc or shutil.which('cc')
    sdk=[]
    if platform.system()=='Darwin':sdk=['-isysroot',subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()]
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1')
    results={}
    with tempfile.TemporaryDirectory(prefix='owly-standalone-') as folder:
        glue=(inc/'rtw89_glue.c').read_text()
        mlme=(inc/'rtw89_mlme.c').read_text()
        production=extract_function(mlme,'rtw89_mlme_awdl_shared_chanctx')+'\n'+extract_function(glue,'owly_radio_maybe_attach')
        (Path(folder)/'production_standalone_glue.inc').write_text(production)
        for mode,flags in [('normal',['-O2']),('asan_ubsan',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
            if mode=='asan_ubsan' and platform.system()=='Linux':flags+=['-no-pie']
            executable=str(Path(folder)/mode)
            command=[compiler,*sdk,*flags,'-std=gnu11','-Wall','-Werror','-Wno-misleading-indentation',
                     '-DOWLY_NATIVE_PEER_DATA=1','-I'+str(inc),'-I'+str(folder),str(ROOT/'tests/standalone_radio_test.c'),
                     *[str(inc/n) for n in ['owly_abi6_rx.c','owly_awdl_frame.c','owly_native_link.c','owly_awdl_sync.c','owly_native_config.c']],'-o',executable]
            for stage,args in [('compile',command),('run',[executable])]:
                result=subprocess.run(args,capture_output=True,text=True,env=env,timeout=60)
                (output/(mode+'-'+stage+'.txt')).write_text(result.stdout+result.stderr)
                if result.returncode:raise RuntimeError(mode+'/'+stage+': '+result.stderr[-2200:])
            results[mode]=result.stdout.strip()
    return {'production_functions_executed':True,'kernel_driver_calls_mocked':True,'native_airdrop_proven':False,'results':results}
if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=ROOT/'Source-Build');p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    print(json.dumps(run(a.source,a.out),indent=2))
