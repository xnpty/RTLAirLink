#!/usr/bin/env python3
"""Check the actual compiled P2P vtable against the exact Legacy4C image."""
from pathlib import Path
import hashlib,json,re,struct
ROOT=Path(__file__).resolve().parents[1]
VTABLE='__ZTV22OwlyNativeP2PInterface'

def table(path):
    data=Path(path).read_bytes()
    if len(data)<32 or struct.unpack_from('<I',data)[0]!=0xfeedfacf:raise ValueError('Expected Mach-O64')
    cpu,_,kind,ncmds,cmdsize=struct.unpack_from('<5I',data,4)
    if cpu!=0x1000007 or kind not in (1,11):raise ValueError('Expected x86_64 object or kext')
    at=32;st=dt=None;sections=[]
    for _ in range(ncmds):
        command,size=struct.unpack_from('<II',data,at)
        if size<8 or at+size>32+cmdsize:raise ValueError('Invalid command')
        if command==2:st=struct.unpack_from('<4I',data,at+8)
        if command==11:dt=struct.unpack_from('<18I',data,at+8)
        if command==25:
            for i in range(struct.unpack_from('<I',data,at+64)[0]):
                off=at+72+80*i
                _,_,addr,length,fileoff,align,ro,rn=struct.unpack_from('<16s16sQQ4I',data,off)
                sections.append((addr,length,fileoff,ro,rn))
        at+=size
    if not st:raise ValueError('Missing symbols')
    symoff,count,stroff,strsize=st;symbols=[];by_name={};by_addr={}
    for i in range(count):
        index,typ,section,desc,value=struct.unpack_from('<IBBHQ',data,symoff+16*i)
        name=data[stroff+index:data.index(0,stroff+index,stroff+strsize)].decode() if index else ''
        symbol=(name,value,typ,section);symbols.append(symbol);by_name[name]=symbol
        if value and typ&0xe and not typ&0xe0:by_addr[value]=name
    if VTABLE not in by_name:raise ValueError('Native subclass vtable absent')
    _,start,typ,section=by_name[VTABLE]
    addr,length,fileoff,ro,rn=sections[section-1]
    candidates=[v for _,v,t,s in symbols if s==section and v>start and not t&0xe0]
    end=min(candidates) if candidates else addr+length
    relocs={}
    if kind==1:
        for i in range(rn):
            relative,word=struct.unpack_from('<iI',data,ro+8*i)
            if word>>27&1:relocs[addr+relative]=symbols[word&0xffffff][0]
    elif dt:
        for i in range(dt[15]):
            relative,word=struct.unpack_from('<iI',data,dt[14]+8*i)
            if word>>27&1:relocs[relative]=symbols[word&0xffffff][0]
    out=[]
    for pos in range(start,end,8):
        value=struct.unpack_from('<Q',data,fileoff+pos-addr)[0]
        out.append(relocs.get(pos,by_addr.get(value,hex(value)) if value else '0x0'))
    return out

def method(symbol):
    match=re.match(r'(__ZNK?)([0-9]+)',symbol)
    if not match:raise ValueError('Not an instance method: '+symbol)
    length=int(match[2]);start=match.end();owner=symbol[start:start+length]
    if owner not in ('OwlyNativeP2PInterface','IO80211VirtualInterface','IO80211P2PInterface'):raise ValueError('Unexpected vtable owner '+owner)
    return match[1],symbol[start+length:]

def validate(slots,pin):
    # At most one alignment word after the 322-word pinned table.
    if len(slots) not in (322,323) or len(slots)==323 and slots[322]!='0x0':raise ValueError('P2P vtable length differs: '+str(len(slots)))
    for record in pin['slots']:
        i=record['word'];expected=record['symbol'];actual=slots[i]
        if method(actual)!=method(expected):raise ValueError('P2P vtable word'+str(i)+' expected '+expected+'; got '+actual)
    return {'legacy_uuid':pin['legacy_uuid'],'legacy_sha256':pin['legacy_sha256'],
            'actual_compiled_vtable_words':len(slots),'checked_words':[268,321],
            'signal_output_thread_word':280,'p2p_set_unit_word':286,'p2p_reserved0_word':306,
            'native_peer_rx_tx_retained':True,'native_boot_or_airdrop_proven':False}

def run(path,output=None,pin_path=None):
    pin=json.loads(Path(pin_path or ROOT/'evidence/P2P_VTABLE_LEGACY4C.json').read_text())
    result=validate(table(path),pin)
    result['input_sha256']=hashlib.sha256(Path(path).read_bytes()).hexdigest()
    if output:Path(output).write_text(json.dumps(result,indent=2)+'\n')
    return result

if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('macho',type=Path);p.add_argument('--out',type=Path);a=p.parse_args()
    print(json.dumps(run(a.macho,a.out),indent=2))
