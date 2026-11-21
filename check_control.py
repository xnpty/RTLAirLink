#!/usr/bin/env python3
from pathlib import Path
import os, platform, re, shutil, subprocess, tempfile
ROOT=Path(__file__).resolve().parent
def extract(text,name):
    start=text.index(name);begin=text.index('{',start);depth=1;i=begin+1
    # The selected declarations have no unmatched braces in literals/comments.
    while depth:
        depth+=(text[i]=='{')-(text[i]=='}');i+=1
    while i<len(text) and text[i] in ' \t':i+=1
    if text[i:].startswith('__attribute__'):i=text.index(';',i)+1
    if i<len(text) and text[i]==';':i+=1
    return text[start:i]
def run(source,output,compiler=None):
    source=Path(source);output=Path(output);output.mkdir(parents=True,exist_ok=True)
    compiler=compiler or shutil.which('c++');sdk=[]
    if platform.system()=='Darwin':
        selected=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
        if not Path(selected).is_dir():raise RuntimeError('macOS SDK unavailable')
        sdk=['-isysroot',selected];(output/'sdk.txt').write_text(selected+'\n')
    text=(source/'src/kext/RTLAirLinkNative.cpp').read_text()
    old=(ROOT/'baseline/src/kext/RTLAirLinkNative.cpp').read_text()
    sdktext=(source/'third_party/MacKernelSDK/Headers/IOKit/80211/apple80211_ioctl.h').read_text()
    wire='#define IFNAMSIZ 16\nusing u_int32_t=uint32_t;\n'
    for name in ['SIOCSA80211','SIOCGA80211','APPLE80211_VERSION','APPLE80211_AWDL_MAX_CHANNELS_ABI',
                 'APPLE80211_IOC_AWDL_OOB_REQUEST','APPLE80211_IOC_AWDL_OOB_AUTO_REQUEST']:
        wire+=re.search(r'^#define\s+'+name+r'\s+[^\n]+',sdktext,re.M).group(0)+'\n'
    for name,value in [('APPLE80211_IOC_AWDL_SYNC_ENABLED',117),('APPLE80211_IOC_AWDL_SYNC_FRAME_TEMPLATE',151),('APPLE80211_IOC_AWDL_SYNCHRONIZATION_CHANNEL_SEQUENCE',129)]:wire+=f'#define {name} {value}\n'
    for name in ['apple80211req','apple80211_awdl_sync_enabled','apple80211_awdl_sync_frame_template','apple80211_channel_sequence','apple80211_awdl_sync_channel_sequence']:
        wire+=extract(sdktext,'struct '+name+' ' if 'struct '+name+' ' in sdktext else 'struct '+name+'\n')+'\n'
    with tempfile.TemporaryDirectory(prefix='owly-get-control-') as d:
        temp=Path(d);(temp/'production_wire.inc').write_text(wire)
        env=os.environ.copy();env['UBSAN_OPTIONS']='halt_on_error=1'
        if platform.system()=='Linux':env['ASAN_OPTIONS']='detect_leaks=0'
        for mode in ['baseline_repro','normal','asan_ubsan']:
            code=old if mode=='baseline_repro' else text
            methods=extract(code,'struct Scratch')+';\n'+extract(code,'static int errnoOf(')+'\n'+extract(code,'int RTLAirLink::nativeAwdlSyncRequest(')+'\n'
            methods+=extract(code,'bool RTLAirLink::nativeAwdlGetEnvelope(')+'\n'
            (temp/'production_methods.inc').write_text(methods)
            flags=['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'] if mode=='asan_ubsan' else ['-O2']
            if mode=='asan_ubsan' and platform.system()=='Linux':flags.append('-no-pie')
            cmd=[compiler,'-std=c++17','-Wall','-Wextra',*sdk,*flags,'-DOWLY_AWDL_SOFTWARE_SYNC=1',
                 '-I'+str(temp),'-I'+str(source/'src/compat_rtw89')]
            if mode=='baseline_repro':cmd+=['-DOWLY_TEST_BASELINE=1','-Wno-unused-function','-Wno-unused-variable']
            else:cmd+=['-DOWLY_NATIVE_PEER_DATA=1']
            cmd+=[str(ROOT/'tests/test_native_get.cpp'),'-o',str(temp/'test')]
            p=subprocess.run(cmd,capture_output=True,text=True);(output/(mode+'-compile.txt')).write_text('COMMAND: '+repr(cmd)+'\n'+p.stdout+p.stderr)
            if p.returncode:raise RuntimeError('GET production model compilation failed: '+mode)
            p=subprocess.run([str(temp/'test')],capture_output=True,text=True,env=env,timeout=30);(output/(mode+'.txt')).write_text(p.stdout+p.stderr)
            if p.returncode or (mode!='baseline_repro' and '26 groups passed' not in p.stdout):raise RuntimeError('GET production model failed: '+mode)
    return {'baseline_inactive_get129_rejection_reproduced':True,'production_method_groups':26,'modes':['normal','asan_ubsan'],'hardware_or_rf_proven':False}
if __name__=='__main__':
    import argparse,json
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();print(json.dumps(run(a.source,a.out),indent=2))
