#!/usr/bin/env python3
"""Execute production BA parser/reorder and radio peer glue; hardware is mocked."""
from pathlib import Path
import json,os,platform,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
def production_function(text,name):
    start=text.index('int '+name+'(')
    opening=text.index('{',start);depth=1;at=opening+1
    while depth:
        if text[at]=='{':depth+=1
        elif text[at]=='}':depth-=1
        at+=1
    return text[start:at]
def run(source,output,cc=None):
    source,output=Path(source),Path(output);output.mkdir(parents=True,exist_ok=True)
    cc=cc or shutil.which('cc');inc=source/'src/compat_rtw89';sdk=[]
    if platform.system()=='Darwin':sdk=['-isysroot',subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()]
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1');result={}
    with tempfile.TemporaryDirectory(prefix='owly-block-ack-') as t:
        core=(source/'third_party/rtw89/core.c').read_text()
        fw=(source/'third_party/rtw89/fw.c').read_text()
        extracted='\n'.join(production_function(core,n) for n in
                            ['rtw89_core_acquire_sta_ba_entry','rtw89_core_release_sta_ba_entry'])
        extracted+='\n'+production_function(fw,'rtw89_fw_h2c_ba_cam')
        (Path(t)/'production_ba_firmware.inc').write_text(extracted.replace('skb->data','skb_data(skb)'))
        for mode,flags in [('normal',['-O2']),('asan_ubsan',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
            if mode=='asan_ubsan' and platform.system()=='Linux':flags+=['-no-pie']
            for name in ['awdl_block_ack_test','awdl_peer_driver_model','awdl_ba_firmware_contract']:
                exe=Path(t)/(name+'-'+mode)
                command=[cc,*sdk,*flags,'-UNDEBUG','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function',
                         '-I'+str(inc),'-I'+t,str(ROOT/'tests'/(name+'.c')),'-o',str(exe)]
                for step,cmd in [('compile',command),('run',[str(exe)])]:
                    p=subprocess.run(cmd,capture_output=True,text=True,env=env,timeout=60)
                    (output/(name+'-'+mode+'-'+step+'.txt')).write_text(p.stdout+p.stderr)
                    if p.returncode:raise RuntimeError('BlockAck '+name+'/'+mode+'/'+step+': '+p.stderr[-3000:])
                result[name+'_'+mode]=p.stdout.strip()
    return {'production_parser_reorder_and_glue_executed':True,'pinned_upstream_ba_allocator_h2c_executed':True,
            'firmware_and_kernel_calls_mocked':True,'results':result,
            'real_peer_addba_observed':False,'real_syn_ack_observed':False,'file_transfer_proven':False}
if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=ROOT/'Source-Build');p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    print(json.dumps(run(a.source,a.out),indent=2))
