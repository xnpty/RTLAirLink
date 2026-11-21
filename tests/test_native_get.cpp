// SPDX-License-Identifier: GPL-2.0
// The included methods and wire declarations are extracted verbatim from
// production source. Only IOKit services, entitlement and copyout are mocked.
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <cerrno>
#include <cstdio>
#include <initializer_list>
#if defined(__APPLE__)
#include <sys/types.h>
#endif
#include "rtw89_glue.h"
#include "production_wire.inc"
#if !defined(__APPLE__)
using user_addr_t=uintptr_t;
#endif
static_assert(sizeof(user_addr_t)==sizeof(void*), "user address must match pointer width");
using SInt32=int32_t;
constexpr int kIOReturnSuccess=0,kIOReturnNoMemory=-1001,kIOReturnBadArgument=-1002,
 kIOReturnUnsupported=-1003,kIOReturnBusy=-1004,kIOReturnNotReady=-1005,
 kIOReturnNotFound=-1006,kIOReturnNoSpace=-1007,kIOReturnNotPrivileged=-1008;
static int version_major=25,kernel_task=1,task_now=1;
static int current_task(){return task_now;}
static bool authorize_now=true,oom=false,copy_fault=false;
static unsigned copied=0,enable_calls=0;
namespace OwlyEntitlement {struct Result{};static bool authorize(int,Result&){return authorize_now;}}
static void *IOMallocZero(size_t n){return oom?nullptr:std::calloc(1,n);}
static void IOFree(void *p,size_t){std::free(p);}
static int copyout(const void *p,user_addr_t dst,size_t n)
{++copied;if(copy_fault)return EFAULT;std::memcpy(reinterpret_cast<void*>(dst),p,n);return 0;}
static struct rtw89_glue_awdl_sync_status state;
static int status_error=0;
static int config_error=-EOPNOTSUPP;
static unsigned config_calls=0,last_config=0;
extern "C" int rtw89_glue_awdl_config(bool,unsigned n,void*){++config_calls;last_config=n;return config_error;}
extern "C" int rtw89_glue_awdl_sync_status(struct rtw89_glue_awdl_sync_status *s){*s=state;return status_error;}
extern "C" int rtw89_glue_awdl_sync_enable(bool on){++enable_calls;state.requested=on;return 0;}
extern "C" int rtw89_glue_awdl_sync_template(const uint8_t*,size_t){return 0;}
#if defined(OWLY_NATIVE_PEER_DATA) && OWLY_NATIVE_PEER_DATA
static unsigned mi_clears;
extern "C" int rtw89_glue_awdl_mi(bool automatic,const uint8_t *destination,
                                  const uint8_t *payload,size_t length)
{
    assert(automatic && !payload && !length && destination[0]==255);
    ++mi_clears;
    return 0;
}
#endif
constexpr unsigned RTW89_FRONT_ROLE_AIRLINK_OBSERVED=6;
struct RTLAirLink {
 struct NativeState {struct {void *cookie=nullptr;uint32_t role=6;bool enabled=false;} airlink;};
 NativeState * _ns=nullptr;
 bool _nativeClosing=false,_halting=false,_asleep=false;
 template<class T> bool setProperty(const char*,T,unsigned=0){return true;}
 int nativeAwdlSyncRequest(bool,int,void*);
 bool nativeAwdlGetEnvelope(void*,void*,unsigned long,void*,int*);
};
#include "production_methods.inc"
int main()
{
 RTLAirLink dev; RTLAirLink::NativeState ns;int cookie=0,other=0;
 ns.airlink.cookie=&cookie;dev._ns=&ns;
 apple80211_awdl_sync_enabled status={};
#ifdef OWLY_TEST_BASELINE
 apple80211_awdl_sync_channel_sequence seq={};
 assert(dev.nativeAwdlSyncRequest(false,129,&seq)==kIOReturnNotReady && !enable_calls);
 puts("baseline regression reproduced: inactive GET129 rejects configuration before activation");
 return 0;
#else
 unsigned groups=0;
 state={};state.requested=true;
 assert(dev.nativeAwdlSyncRequest(false,117,&status)==0 && status.version==1 && !status.unk1 && !status.enabled);++groups;
 std::memset(&status,0xa5,sizeof(status));state.enabled=state.active=true;
 assert(dev.nativeAwdlSyncRequest(false,117,&status)==0 && status.version==1 && status.enabled==1);++groups;
 status={};status.version=2;status.enabled=1;unsigned saved=enable_calls;
 assert(dev.nativeAwdlSyncRequest(true,117,&status)==kIOReturnBadArgument && saved==enable_calls);++groups;
 status.version=1;status.enabled=3;
 assert(dev.nativeAwdlSyncRequest(true,117,&status)==kIOReturnBadArgument && saved==enable_calls);++groups;
 status.enabled=1;task_now=2;authorize_now=false;
 assert(dev.nativeAwdlSyncRequest(true,117,&status)==kIOReturnNotPrivileged && saved==enable_calls);
 authorize_now=true;assert(dev.nativeAwdlSyncRequest(true,117,&status)==0 && enable_calls==saved+1);task_now=1;++groups;
#if defined(OWLY_NATIVE_PEER_DATA) && OWLY_NATIVE_PEER_DATA
 alignas(void*) uint8_t mi[56]={};
 std::memset(mi+4,255,6);
 mi[20]=6;
 task_now=2;authorize_now=false;
 assert(dev.nativeAwdlSyncRequest(true,225,mi)==kIOReturnNotPrivileged && !mi_clears);
 task_now=1;authorize_now=true;
 mi[20]=5;
 assert(dev.nativeAwdlSyncRequest(true,225,mi)==kIOReturnBadArgument && !mi_clears);
 mi[20]=6;
 assert(dev.nativeAwdlSyncRequest(false,225,mi)==kIOReturnUnsupported && !mi_clears);
 assert(dev.nativeAwdlSyncRequest(true,149,mi)==kIOReturnBadArgument && !mi_clears);
 const uint8_t *user_buffer=mi;
 std::memcpy(mi+48,&user_buffer,sizeof(user_buffer));
 mi[40]=1;
 assert(dev.nativeAwdlSyncRequest(true,225,mi)==kIOReturnBadArgument && !mi_clears);
 std::memset(mi+40,0,16);
 assert(dev.nativeAwdlSyncRequest(true,225,mi)==0 && mi_clears==1);
 ++groups;
#endif
 unsigned char bytes[404];std::memset(bytes,0xcc,sizeof(bytes));
 apple80211req req={};req.req_type=117;req.req_len=12;req.req_data=bytes;
 int result=999;copied=0;
 assert(!dev.nativeAwdlGetEnvelope(&other,&other,SIOCGA80211,&req,&result) && !copied);++groups;
 assert(!dev.nativeAwdlGetEnvelope(&other,nullptr,SIOCGA80211,&req,&result) && !copied);++groups;
 state.enabled=state.active=false;assert(dev.nativeAwdlGetEnvelope(nullptr,&cookie,SIOCGA80211,&req,&result));
 std::memcpy(&status,bytes,sizeof(status));
 assert(!result && copied==1 && status.version==1 && !status.enabled && bytes[12]==0xcc);++groups;
 copied=0;req.req_len=11;assert(dev.nativeAwdlGetEnvelope(nullptr,&cookie,SIOCGA80211,&req,&result) && result==EINVAL && !copied);
 req.req_len=13;assert(dev.nativeAwdlGetEnvelope(nullptr,&cookie,SIOCGA80211,&req,&result) && result==EINVAL && !copied);
 req.req_len=12;req.req_data=nullptr;assert(dev.nativeAwdlGetEnvelope(nullptr,&cookie,SIOCGA80211,&req,&result) && result==EFAULT && !copied);req.req_data=bytes;++groups;
 copy_fault=true;assert(dev.nativeAwdlGetEnvelope(nullptr,&cookie,SIOCGA80211,&req,&result) && result==EFAULT);copy_fault=false;++groups;
 copied=0;req.req_type=129;req.req_len=400;state.active=false;
 std::memset(bytes,0xcc,sizeof(bytes));unsigned unchanged_enables=enable_calls;
 auto unchanged_state=state;
 assert(dev.nativeAwdlGetEnvelope(nullptr,&cookie,SIOCGA80211,&req,&result) && !result && copied==1);
 assert(bytes[0]==1 && bytes[1]==0 && bytes[2]==0 && bytes[3]==0 && bytes[400]==0xcc);
 for(unsigned i=4;i<400;i++)assert(bytes[i]==0);
 assert(!std::memcmp(&state,&unchanged_state,sizeof(state)) && enable_calls==unchanged_enables);++groups;
 copied=0;
 state.enabled=state.active=true;state.channel=6;std::memset(bytes,0xcc,sizeof(bytes));
 assert(dev.nativeAwdlGetEnvelope(nullptr,&cookie,SIOCGA80211,&req,&result) && !result && copied==1);
 owly_cfg_sequence seq;std::memcpy(&seq,bytes,sizeof(seq));
 assert(seq.version==1 && seq.length==16 && seq.channels[0].channel==6 && seq.channels[15].channel==6 && seq.channels[0].flags==10 && bytes[400]==0xcc);++groups;
 copied=0;assert(!dev.nativeAwdlGetEnvelope(nullptr,&cookie,SIOCSA80211,&req,&result) && !copied);
 req.req_type=120;assert(!dev.nativeAwdlGetEnvelope(nullptr,&cookie,SIOCGA80211,&req,&result) && !copied);++groups;
 req.req_type=117;req.req_len=12;dev._halting=true;
 assert(dev.nativeAwdlGetEnvelope(nullptr,&cookie,SIOCGA80211,&req,&result) && result==ENXIO && !copied);dev._halting=false;++groups;
 oom=true;assert(dev.nativeAwdlGetEnvelope(nullptr,&cookie,SIOCGA80211,&req,&result) && result==ENOMEM && !copied);oom=false;++groups;
 assert(dev.nativeAwdlGetEnvelope(&cookie,nullptr,SIOCGA80211,&req,&result) && !result);++groups;
 req.req_type=129;req.req_len=400;copied=0;state={};status_error=-ENODEV;
 assert(dev.nativeAwdlGetEnvelope(nullptr,&cookie,SIOCGA80211,&req,&result) && result==ENODEV && !copied);status_error=0;++groups;
 for(unsigned phase=0;phase<3;phase++){
  state={};state.requested=phase!=0;state.has_template=phase==2;state.blocked=phase==2;
  auto prior=state;owly_cfg_sequence empty;std::memset(&empty,0xa5,sizeof(empty));
  unsigned before_enable=enable_calls;
  assert(dev.nativeAwdlSyncRequest(false,129,&empty)==0 && empty.version==1);
  const auto *p=reinterpret_cast<const unsigned char*>(&empty);
  for(unsigned i=4;i<sizeof(empty);i++)assert(!p[i]);
  assert(!std::memcmp(&state,&prior,sizeof(state)) && enable_calls==before_enable);
 }++groups;
 owly_cfg_sequence denied={};denied.version=1;
 assert(dev.nativeAwdlSyncRequest(true,129,&denied)==EOPNOTSUPP && last_config==129);++groups;
 state={};state.enabled=state.active=true;state.channel=149;owly_cfg_sequence active={};
 assert(dev.nativeAwdlSyncRequest(false,129,&active)==0 && active.version==1 && active.length==16);
 for(unsigned i=0;i<16;i++)assert(active.channels[i].channel==149 && active.channels[i].flags==18);++groups;
 state.active=false;state.scan_paused=true;status={};active={};
 assert(dev.nativeAwdlSyncRequest(false,117,&status)==0 && status.enabled==1);
 assert(dev.nativeAwdlSyncRequest(false,129,&active)==0 && active.length==16);
 assert(active.channels[0].channel==149 && active.channels[15].channel==149);++groups;
 state={};status={};
 assert(dev.nativeAwdlSyncRequest(false,129,&active)==0);
 assert(dev.nativeAwdlSyncRequest(false,117,&status)==0 && status.version==1 && !status.enabled && !state.requested && !state.active);++groups;
 status={};status.enabled=1;saved=enable_calls;
 assert(dev.nativeAwdlSyncRequest(true,117,&status)==0 && enable_calls==saved+1);++groups;
 alignas(8) unsigned char cfg[400]={};
 config_error=0;
 for(unsigned number: {116u,122u,125u,129u,132u,134u,135u,213u}) {
  unsigned before=config_calls;
  task_now=2;authorize_now=false;
  assert(dev.nativeAwdlSyncRequest(true,number,cfg)==kIOReturnNotPrivileged && config_calls==before);
  authorize_now=true;
  assert(dev.nativeAwdlSyncRequest(true,number,cfg)==0 && config_calls==before+1 && last_config==number);
  task_now=1;
 }++groups;
 cfg[0]=2;saved=config_calls;
 assert(dev.nativeAwdlSyncRequest(true,132,cfg)==kIOReturnBadArgument && config_calls==saved);++groups;
 std::printf("native GET production methods: %u groups passed; mocked kernel APIs, no RF proof\n",groups);
 return 0;
#endif
}
