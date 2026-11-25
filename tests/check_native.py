#!/usr/bin/env python3
"""Execute new production peer/codec/queue and frontend lifetime code."""
from pathlib import Path
import json,os,platform,shutil,subprocess,tempfile
import check_platform
ROOT=Path(__file__).resolve().parents[1]

def run(source,output,cc=None,cxx=None):
    source,output=Path(source),Path(output);output.mkdir(parents=True,exist_ok=True)
    cc=cc or shutil.which('cc');cxx=cxx or shutil.which('c++')
    platform_abi=check_platform.run(source,output/'platform-abi',cc)
    include=source/'src/compat_rtw89';front=source/'src/front'
    runtime=(front/'OwlyNativeP2P.inc').read_text()
    # Keep every new production runtime function and its alias/guard helpers;
    # the subclass itself is checked by real kernel-header compilation.
    beginning=runtime[:runtime.index('class OwlyNativeP2PInterface')]
    body=runtime[runtime.index('bool RTLAirLinkFront::owlyNativeStart()'):]
    header=(front/'RTLAirLinkFront.hpp').read_text()
    declarations=header.split('private:\n#if defined(OWLY_NATIVE_PEER_DATA) && OWLY_NATIVE_PEER_DATA\n',1)[1].split('\n#endif',1)[0]
    env=os.environ.copy();env['UBSAN_OPTIONS']='halt_on_error=1'
    if platform.system()=='Linux':env['ASAN_OPTIONS']='detect_leaks=0'
    sdk=[]
    if platform.system()=='Darwin':sdk=['-isysroot',subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()]
    results={}
    with tempfile.TemporaryDirectory(prefix='owly-native-peer-') as folder:
        folder=Path(folder)
        (folder/'production_native_runtime.inc').write_text(beginning+body)
        (folder/'native_class_declarations.inc').write_text(declarations)
        for mode in ['normal','asan_ubsan']:
            flags=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'] if mode=='asan_ubsan' else ['-O2']
            common=[*sdk,*flags,'-DOWLY_NATIVE_PEER_DATA=1','-I'+str(include),'-I'+str(source/'src/front'),'-I'+str(folder)]
            def execute(command,label):
                p=subprocess.run(command,capture_output=True,text=True,env=env)
                (output/(mode+'-'+label+'.txt')).write_text('COMMAND: '+repr(command)+'\n'+p.stdout+p.stderr)
                if p.returncode:raise RuntimeError('Native production check failed: '+mode+'/'+label)
                return p.stdout
            core=[include/n for n in ['owly_native_link.c','owly_awdl_sync.c','owly_awdl_frame.c','owly_native_config.c']]
            execute([cc,'-std=gnu11',*common,*map(str,core),str(ROOT/'tests/native_link_test.c'),'-o',str(folder/'link')],'link-compile')
            results[mode+'_link']=execute([str(folder/'link')],'link-run').strip()
            execute([cc,'-std=gnu11',*common,'-c',str(include/'owly_awdl_sync.c'),'-o',str(folder/'sync.o')],'sync-object')
            execute([cxx,'-std=gnu++17',*common,'-pthread',str(ROOT/'tests/native_runtime_model.cpp'),str(folder/'sync.o'),'-o',str(folder/'runtime')],'runtime-compile')
            results[mode+'_runtime']=execute([str(folder/'runtime')],'runtime-run').strip()
    return {'production_peer_codec_queue_and_runtime':results,'platform_callback_abi':platform_abi,
            'kernel_apis_mocked_in_runtime':True,'kernel_boot_radio_airdrop_proven':False}

if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=ROOT/'Source-Build');p.add_argument('--out',type=Path,default=ROOT/'validation/native');a=p.parse_args()
    print(json.dumps(run(a.source,a.out),indent=2))
