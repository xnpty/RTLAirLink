#!/usr/bin/env python3
"""Execute production self-address transaction and synthetic SYN-ACK admission."""
from pathlib import Path
import json,os,platform,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
def run(source,output,cc=None):
    source,output=Path(source),Path(output);output.mkdir(parents=True,exist_ok=True)
    inc=source/'src/compat_rtw89';cc=cc or shutil.which('cc');sdk=[]
    if platform.system()=='Darwin':sdk=['-isysroot',subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()]
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1');results={}
    with tempfile.TemporaryDirectory(prefix='owly-unicast-') as folder:
        folder=Path(folder)
        for mode,flags in [('normal',['-O2']),('asan_ubsan',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
            if mode=='asan_ubsan' and platform.system()=='Linux':flags+=['-no-pie']
            for name,files in [('self-address',[ROOT/'tests/unicast_self_addr_test.c']),
                               ('packet',[ROOT/'tests/unicast_packet_test.c',*[inc/n for n in ['owly_awdl_frame.c','owly_awdl_sync.c','owly_native_link.c','owly_native_config.c']]])]:
                executable=str(folder/(mode+'-'+name));cmd=[cc,*sdk,*flags,'-std=gnu11','-Wall','-Wextra','-Werror','-DOWLY_NATIVE_PEER_DATA=1','-I'+str(inc),*map(str,files),'-o',executable]
                for step,args in [('compile',cmd),('run',[executable])]:
                    p=subprocess.run(args,capture_output=True,text=True,env=env,timeout=60)
                    (output/(mode+'-'+name+'-'+step+'.txt')).write_text(p.stdout+p.stderr)
                    if p.returncode:raise RuntimeError('Unicast '+mode+'/'+name+'/'+step+': '+p.stderr[-2400:])
                results[mode+'_'+name]=p.stdout.strip()
    return {'production_core_cam_transaction':True,'production_parser_and_admission':True,'synthetic_tcp_fixture':True,'firmware_calls_mocked':True,'results':results,'real_syn_ack_observed':False,'file_transfer_proven':False}
if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=ROOT/'Source-Build');p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    print(json.dumps(run(a.source,a.out),indent=2))
