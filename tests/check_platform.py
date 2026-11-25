#!/usr/bin/env python3
"""Compile the real platform header in independent caller/callee TUs.

Every feature-flag combination must agree on the complete struct layout,
preserve existing callbacks, and zero an omitted native callback.
"""
from pathlib import Path
import json,os,platform,shutil,subprocess,tempfile

CALLER=r'''
#include <assert.h>
#include <string.h>
#include "rtw89_glue.h"
extern void accept_platform(const struct rtw89_glue_platform *);
extern size_t driver_size(void);
extern size_t driver_wake_offset(void);
static unsigned wakes,scans;
static void wake(void *ctx){assert(ctx==&wakes);++wakes;}
static void scanned(void *ctx,bool aborted){assert(ctx==&wakes && aborted);++scans;}
int main(void){
    struct rtw89_glue_platform p={0};
    p.ctx=&wakes;p.tx_wake=wake;p.scan_done=scanned;
    assert(sizeof(p)==driver_size());
    assert(offsetof(struct rtw89_glue_platform,tx_wake)==driver_wake_offset());
    accept_platform(&p);assert(wakes==1 && scans==1);return 0;
}
'''
CALLEE=r'''
#include <assert.h>
#include "rtw89_glue.h"
size_t driver_size(void){return sizeof(struct rtw89_glue_platform);}
size_t driver_wake_offset(void){return offsetof(struct rtw89_glue_platform,tx_wake);}
void accept_platform(const struct rtw89_glue_platform *p){
    struct rtw89_glue_platform copy=*p;
    assert(copy.native_rx==0 && copy.tx_wake && copy.scan_done);
    copy.tx_wake(copy.ctx);copy.scan_done(copy.ctx,true);
}
'''

def run(source,output,cc=None):
    source,output=Path(source),Path(output);output.mkdir(parents=True,exist_ok=True)
    cc=cc or shutil.which('cc');env=os.environ.copy();env['UBSAN_OPTIONS']='halt_on_error=1'
    if platform.system()=='Linux':env['ASAN_OPTIONS']='detect_leaks=0'
    sdk=[]
    if platform.system()=='Darwin':sdk=['-isysroot',subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()]
    cases=[]
    with tempfile.TemporaryDirectory(prefix='owly-platform-abi-') as folder:
        folder=Path(folder);(folder/'caller.c').write_text(CALLER);(folder/'callee.c').write_text(CALLEE)
        for caller in [0,1]:
            for callee in [0,1]:
                label=str(caller)+'-'+str(callee);log=[]
                common=[*sdk,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(source/'src/compat_rtw89')]
                commands=[]
                for name,flag in [('caller',caller),('callee',callee)]:
                    commands.append([cc,*common,'-DOWLY_NATIVE_PEER_DATA='+str(flag),'-c',str(folder/(name+'.c')),'-o',str(folder/(name+'.o'))])
                commands.append([cc,*common,str(folder/'caller.o'),str(folder/'callee.o'),'-o',str(folder/'probe')])
                commands.append([str(folder/'probe')])
                for cmd in commands:
                    p=subprocess.run(cmd,text=True,capture_output=True,env=env)
                    log.append('COMMAND: '+repr(cmd)+'\n'+p.stdout+p.stderr)
                    (output/(label+'.txt')).write_text('\n'.join(log))
                    if p.returncode:raise RuntimeError('Platform ABI caller/callee '+label+' failed')
                cases.append({'caller_native':caller,'driver_native':callee,'asan_ubsan':True,'callbacks_and_copy_passed':True})
    return {'production_header_cross_tu_cases':cases,'kernel_or_radio_proven':False}

if __name__=='__main__':
    import argparse
    root=Path(__file__).resolve().parents[1]
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=root/'Source-Build');p.add_argument('--out',type=Path,default=root/'validation/platform');a=p.parse_args()
    print(json.dumps(run(a.source,a.out),indent=2))
