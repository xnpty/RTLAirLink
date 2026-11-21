#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cerrno>
#include <functional>
#include <string>
#include <map>
using IOReturn = int;
using ifnet_t = void *;
constexpr int kIOReturnSuccess = 0;
constexpr unsigned long SIOCSA80211 = 0x802869c8;
constexpr int APPLE80211_IOC_AWDL_PEER_SERVICE_REQUEST = 120;
constexpr int RTW89_FRONT_ROLE_AIRLINK_OBSERVED = 6;
constexpr int RTW89_FRONT_VIF_SERVICE_READY = 5;
constexpr int kIO80211NetworkLinkUp = 2, kIO80211NetworkLinkDown = 1;
struct OSObject { int refs = 1; void retain(){++refs;} void release(){--refs; assert(refs >= 1);} };
struct IO80211Interface {};
struct apple80211req { int req_type=120; unsigned req_len=41; void *req_data=nullptr; };
int lock_depth = 0;
void IOLockLock(void *) {assert(lock_depth++ == 0);}
void IOLockUnlock(void *) {assert(--lock_depth == 0);}
int task = 1, kernel_task = 0;
int current_task(){return task;}
bool authorized = true;
namespace OwlyEntitlement {struct Result{};bool authorize(int,Result&){return authorized;}}
struct IO80211VirtualInterface:OSObject {
    int state=1, up_calls=0, down_calls=0;
    bool system_enabled=false;
    int enable_calls=0;
    std::function<void()> on_state;
    void setEnabledBySystem(bool on) {
        assert(lock_depth==0);system_enabled=on;++enable_calls;
    }
    void setLinkState(int s,unsigned) {
        if(s==2)assert(system_enabled && enable_calls>0);
        assert(lock_depth == 0); state=s;
        if(s==2)++up_calls;else ++down_calls;
        if(on_state)on_state();
    }
};
struct IOWorkLoop:OSObject {
    bool gated=false, fail=false;
    bool inGate(){return gated;}
    IOReturn runAction(IOReturn (*f)(OSObject*,void*,void*,void*,void*),OSObject*o,void*p){
        if(fail)return 99;bool old=gated;gated=true;int r=f(o,p,nullptr,nullptr,nullptr);gated=old;return r;
    }
};
struct IO80211Controller:OSObject {
    int parent_calls=0,parent_error=6;
    std::function<void()> on_parent;
    int apple80211_ioctl(IO80211Interface*,IO80211VirtualInterface*,ifnet_t,unsigned long,void*) {
        assert(lock_depth==0);++parent_calls;if(on_parent)on_parent();return parent_error;
    }
};
struct RTLAirLinkFront:IO80211Controller {
    struct OwlyServiceCall {IO80211Interface*interface;IO80211VirtualInterface*vif;ifnet_t net;unsigned long command;void*envelope;int result;};
    void *_owlyVifLock=this;
    IOWorkLoop *_workLoop=nullptr;
    IO80211VirtualInterface *_owlyAttachedAWDL=nullptr;
    bool _owlyAWDLPendingBSD=false,_owlyAWDLStopping=false,_owlyAWDLQuarantined=false;
    bool _owlyAWDLRequestBusy=false,_owlyVifBusy=false,_owlyVifMethodBusy=false;
    struct {void*cookie=nullptr;unsigned role=6;} _owlyAirlink;
    struct {void*ctx=nullptr;bool(*vif_event)(void*,void*,uint32_t,uint32_t,const uint8_t*)=nullptr;} _back;
    bool backend_available=true;
    int back_depth=0;
    std::map<std::string,uint64_t> props;
    bool backEnter(){++back_depth;return backend_available;}
    void backLeave(){assert(back_depth>0);--back_depth;}
    void setProperty(const char*k,unsigned long long v,unsigned){props[k]=v;}
    bool owlyServiceIoctl(IO80211Interface*,IO80211VirtualInterface*,ifnet_t,unsigned long,void*,int*);
    int owlyServiceIoctlOnGate(OwlyServiceCall*);
};
#if OWLY_AWDL_SERVICE_LIFECYCLE
#include "production.inc"
#endif
struct World {
    RTLAirLinkFront front;
    IOWorkLoop loop;
    IO80211VirtualInterface vif,other;
    apple80211req req;
    bool ready=true;
    int probe_calls=0;
    std::function<void()> on_probe;
    World(){
        task=1;authorized=true;
        front._workLoop=&loop;front._owlyAttachedAWDL=&vif;front._owlyAirlink.cookie=&vif;
        front._back.ctx=this;front._back.vif_event=[](void*p,void*v,uint32_t role,uint32_t event,const uint8_t*mac){
            auto&w=*static_cast<World*>(p);assert(v==&w.vif && role==6 && event==5 && !mac);
            assert(w.front._owlyVifMethodBusy && lock_depth==0);++w.probe_calls;
            if(w.on_probe)w.on_probe();return w.ready;
        };
    }
    void balanced(){assert(front.refs==1 && loop.refs==1 && vif.refs==1 && other.refs==1 && !lock_depth && !front.back_depth && !front._owlyVifMethodBusy);}
    int call(){
#if OWLY_AWDL_SERVICE_LIFECYCLE
        int result=-99;bool handled=front.owlyServiceIoctl(nullptr,&vif,nullptr,SIOCSA80211,&req,&result);assert(handled);balanced();return result;
#else
        return -1;
#endif
    }
};
int main(){
#if OWLY_AWDL_SERVICE_LIFECYCLE
    int groups=0;
    {World w;assert(w.call()==6 && w.vif.state==2 && w.front.parent_calls==1);++groups;}
    {World w;w.front.parent_error=0;assert(w.call()==0 && w.probe_calls==1 && w.vif.up_calls==1 && w.vif.system_enabled && w.vif.enable_calls==1);++groups;}
    {World w;w.front.parent_error=EIO;assert(w.call()==EIO && w.front.props["OwlyServiceAppleResultLast"]==EIO);++groups;}
    {World w;w.ready=false;assert(w.call()==6 && !w.vif.system_enabled && !w.vif.up_calls && w.vif.down_calls==1 && w.front.parent_calls==1);++groups;}
    {World w;authorized=false;assert(w.call()==EPERM && !w.probe_calls && !w.front.parent_calls && !w.vif.up_calls && !w.vif.enable_calls);++groups;}
    {World w;task=kernel_task;authorized=false;assert(w.call()==6 && w.probe_calls==1);++groups;}
    {World w;w.front.backend_available=false;assert(w.call()==6 && !w.probe_calls && !w.vif.up_calls);++groups;}
    {World w;w.front._back.vif_event=nullptr;assert(w.call()==6 && !w.probe_calls && !w.vif.up_calls);++groups;}
    {World w;w.loop.fail=true;assert(w.call()==ENXIO && !w.front.parent_calls && !w.probe_calls);++groups;}
    {World w;w.front._owlyAWDLStopping=true;assert(w.call()==ENXIO && !w.probe_calls && !w.vif.up_calls);++groups;}
    {World w;w.front._owlyAWDLQuarantined=true;assert(w.call()==ENXIO && !w.probe_calls);++groups;}
    {World w;w.front._owlyAWDLRequestBusy=true;assert(w.call()==ENXIO && !w.probe_calls);++groups;}
    {World w;w.front._owlyVifBusy=true;assert(w.call()==ENXIO && !w.probe_calls);++groups;}
    {World w;w.front._owlyAirlink.cookie=&w.other;assert(w.call()==ENXIO && !w.probe_calls);++groups;}
    {World w;w.front._owlyAirlink.role=7;assert(w.call()==ENXIO && !w.probe_calls);++groups;}
    {World w;w.on_probe=[&]{w.front._owlyAWDLStopping=true;};assert(w.call()==ENXIO && !w.vif.up_calls && !w.front.parent_calls);++groups;}
    {World w;w.vif.on_state=[&]{w.front._owlyAWDLStopping=true;};assert(w.call()==ENXIO && w.vif.state==1 && !w.vif.system_enabled && !w.front.parent_calls);++groups;}
    {World w;w.front.on_parent=[&]{assert(w.front._owlyVifMethodBusy && w.vif.refs==2);w.front._owlyAWDLStopping=true;};w.front.parent_error=EIO;assert(w.call()==EIO);++groups;}
    {World w;w.front.on_parent=[&]{int r=-1;assert(w.front.owlyServiceIoctl(nullptr,&w.vif,nullptr,SIOCSA80211,&w.req,&r));assert(r==ENXIO);};assert(w.call()==6 && w.probe_calls==1 && w.front.parent_calls==1);++groups;}
    for(int which=0;which<8;++which){
        World w;int r=-99;auto* v=&w.vif;void*data=&w.req;unsigned long cmd=SIOCSA80211;
        if(which==0)cmd=0;if(which==1)v=nullptr;if(which==2)data=nullptr;
        if(which==3)v=&w.other;if(which==4)w.req.req_type=117;
        if(which==5)w.req.req_len=19;if(which==6)w.req.req_len=4097;
        if(which==7)w.front._owlyAWDLPendingBSD=true;
        assert(!w.front.owlyServiceIoctl(nullptr,v,nullptr,cmd,data,&r));
        assert(r==-99 && !w.probe_calls && !w.front.parent_calls && !w.vif.up_calls);w.balanced();++groups;
    }
    std::printf("%d front dispatch groups passed; mocked Apple callbacks, no kernel/RF proof\n",groups);
#else
    std::puts("service feature off: production helper omitted");
#endif
}
