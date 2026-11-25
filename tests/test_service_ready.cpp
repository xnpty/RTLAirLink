#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <initializer_list>
#define IOLog(...) ((void)0)
constexpr unsigned IEEE80211_CHAN_DISABLED=1, IEEE80211_CHAN_NO_IR=2, IEEE80211_CHAN_RADAR=4;
struct ieee80211_channel {unsigned flags=0;unsigned center_freq=5745;};
struct ieee80211_chanctx_conf {struct {ieee80211_channel *chan=nullptr;}def;};
struct Vif {struct {ieee80211_chanctx_conf *chanctx_conf=nullptr;}bss_conf;};
struct {bool active=true,probed=true,up=true,scanning=false;int cmd_lock=0;}glue;
struct {ieee80211_chanctx_conf*channel=nullptr;bool blocked=false;}owly_radio_state;
bool bound=true;Vif *owly_core04c_vif=nullptr;ieee80211_chanctx_conf *shared=nullptr;
bool owly_radio_is_bound(decltype(owly_radio_state)*){return bound;}
ieee80211_chanctx_conf *rtw89_mlme_awdl_shared_chanctx(){return shared;}
struct HW{void*wiphy=this;}hw;
HW*glue_hw(){return &hw;}
bool rtw89_cfg80211_awdl_5g_ok(void*,unsigned){return false;}
int locks=0, lock_calls=0;
void mutex_lock(int*){assert(locks++==0);++lock_calls;}
void mutex_unlock(int*){assert(--locks==0);}
void wiphy_lock(void*){assert(locks++==1);++lock_calls;}
void wiphy_unlock(void*){assert(--locks==1);}
#include "production.inc"
int main(){
    ieee80211_channel chan;ieee80211_chanctx_conf ctx,other;ctx.def.chan=&chan;
    Vif vif;vif.bss_conf.chanctx_conf=&ctx;
    auto reset=[&]{glue={};glue.active=glue.probed=glue.up=true;owly_radio_state={};owly_radio_state.channel=&ctx;
        bound=true;owly_core04c_vif=&vif;shared=&ctx;ctx.def.chan=&chan;vif.bss_conf.chanctx_conf=&ctx;
        chan.flags=0;chan.center_freq=5745;locks=lock_calls=0;};
    reset();
#if OWLY_AWDL_SOFTWARE_SYNC
    int groups=0;
    for(unsigned f:{2437u,5220u,5745u}){reset();chan.center_freq=f;assert(rtw89_glue_awdl_service_ready());assert(!locks && lock_calls==2);++groups;}
    /* Fix9 trace: SET120 arrived in a connected scan on 149. A real
     * surviving bind lets Apple handle the request without disabling AWDL. */
    reset();glue.scanning=true;
    assert(rtw89_glue_awdl_service_ready() && !locks && lock_calls==2);
    assert(glue.scanning);
    chan.flags=IEEE80211_CHAN_NO_IR;
    assert(!rtw89_glue_awdl_service_ready() && !locks);++groups;
    for(int which=0;which<14;++which){
        if(which==3)continue;
        reset();
        if(which==0)glue.active=false;if(which==1)glue.probed=false;if(which==2)glue.up=false;
        if(which==3)glue.scanning=true;if(which==4)owly_core04c_vif=nullptr;if(which==5)bound=false;
        if(which==6)owly_radio_state.blocked=true;if(which==7)shared=nullptr;if(which==8)shared=&other;
        if(which==9)vif.bss_conf.chanctx_conf=&other;if(which==10)ctx.def.chan=nullptr;
        if(which==11)chan.flags=IEEE80211_CHAN_DISABLED;if(which==12)chan.flags=IEEE80211_CHAN_NO_IR;
        if(which==13)chan.flags=IEEE80211_CHAN_RADAR;
        assert(!rtw89_glue_awdl_service_ready() && !locks);++groups;
    }
    for(unsigned f:{2412u,2462u,5180u,5775u,0u}){reset();chan.center_freq=f;assert(!rtw89_glue_awdl_service_ready() && !locks);++groups;}
    reset();for(int i=0;i<10000;++i)assert(rtw89_glue_awdl_service_ready());
    assert(glue.up && !glue.scanning && shared==&ctx && vif.bss_conf.chanctx_conf==&ctx && !locks);++groups;
    std::printf("%d readiness groups passed; production guard, no kernel/RF proof\n",groups);
#else
    assert(!rtw89_glue_awdl_service_ready() && !locks && !lock_calls);
    std::puts("readiness feature off: query returns false without touching driver locks");
#endif
}
