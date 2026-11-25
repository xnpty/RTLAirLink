/* SPDX-License-Identifier: GPL-2.0 */
/* Execute production MLME idle transactions and RX policy with driver I/O
 * substituted. No firmware, native Apple peer, or file success is simulated. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "owly_awdl_radio_lifecycle.h"
#include "owly_abi6_rx.h"
#include "owly_native_link.h"
#include "owly_scc_rx_policy.h"
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define GFP_KERNEL 0
#define RTW89_MLME_IDLE 0
#define RTW89_MLME_CONNECTED 4
#define IEEE80211_CHAN_DISABLED 1
#define IEEE80211_CHAN_NO_IR 2
#define IEEE80211_CHAN_RADAR 4
#define NL80211_CHAN_NO_HT 0
#define NL80211_CHAN_WIDTH_80 3
#define NL80211_BAND_5GHZ 1
#define mlme_info(...) ((void)0)
struct ieee80211_supported_band {
 struct { bool vht_supported; } vht_cap;
 struct { bool ht_supported; struct { u8 rx_mask[2]; } mcs; } ht_cap;
};
struct ieee80211_channel { unsigned center_freq, flags, band; };
struct cfg80211_chan_def { struct ieee80211_channel *chan; unsigned width, center_freq1, center_freq2; };
struct ieee80211_chanctx_conf { struct cfg80211_chan_def def,min_def; unsigned rx_chains_static,rx_chains_dynamic; };
struct ieee80211_bss_conf { struct ieee80211_chanctx_conf *chanctx_conf; struct { struct cfg80211_chan_def oper; } chanreq; };
struct ieee80211_vif { struct ieee80211_bss_conf bss_conf; bool auxiliary; u8 addr[6]; };
struct wiphy { struct ieee80211_supported_band *bands[2]; bool locked; };
struct ieee80211_hw { struct wiphy *wiphy; unsigned chanctx_data_size; };
struct ieee80211_ops {
 int (*add_chanctx)(struct ieee80211_hw *,struct ieee80211_chanctx_conf *);
 void (*remove_chanctx)(struct ieee80211_hw *,struct ieee80211_chanctx_conf *);
 int (*assign_vif_chanctx)(struct ieee80211_hw *,struct ieee80211_vif *,struct ieee80211_bss_conf *,struct ieee80211_chanctx_conf *);
 void (*unassign_vif_chanctx)(struct ieee80211_hw *,struct ieee80211_vif *,struct ieee80211_bss_conf *,struct ieee80211_chanctx_conf *);
};
struct local { const struct ieee80211_ops *ops; };
static struct { bool running,awdl_chanctx,chanctx_assigned,awdl_self_addr; int state,awdl_self_addr_error;
 struct ieee80211_hw *hw;struct ieee80211_vif *vif;struct local *local;
 struct ieee80211_chanctx_conf *chanctx;struct ieee80211_channel *chan;
 void *sband; struct cfg80211_chan_def chandef;
} mlme;
static void (*owly_core04d_before_release)(struct ieee80211_chanctx_conf *);
static struct ieee80211_channel channels[3]={{2437,0,0},{5220,0,1},{5745,0,1}};
static int allocs,frees,added,removed,assigned,unassigned,fail_alloc,fail_add,fail_assign;
static char order[32];static unsigned order_len;
static void event(char c) {assert(order_len+1<sizeof(order));order[order_len++]=c;}
#define lockdep_assert_wiphy(w) assert((w)->locked)
static void *kzalloc(size_t n,int flags) {(void)flags;if(fail_alloc)return NULL;allocs++;return calloc(1,n);}
static void kfree(void *p) {if(p){frees++;free(p);}}
static bool awdl_5g_allowed;
static bool rtw89_cfg80211_awdl_5g_ok(struct wiphy *w, unsigned freq) {
 lockdep_assert_wiphy(w); return awdl_5g_allowed && (freq == 5220u || freq == 5745u);
}
static struct ieee80211_channel *ieee80211_get_channel(struct wiphy *w,unsigned freq) {
 lockdep_assert_wiphy(w);for(unsigned i=0;i<3;i++)if(channels[i].center_freq==freq)return &channels[i];return NULL;
}
static void cfg80211_chandef_create(struct cfg80211_chan_def *d,struct ieee80211_channel *c,int mode) {
 (void)mode;d->chan=c;d->width=0;d->center_freq1=c->center_freq;d->center_freq2=0;
}
static int add(struct ieee80211_hw *h,struct ieee80211_chanctx_conf *c) {
 lockdep_assert_wiphy(h->wiphy);assert(c->def.chan);event('a');added++;return fail_add ? -EIO : 0;
}
static void remove_ctx(struct ieee80211_hw *h,struct ieee80211_chanctx_conf *c) {
 lockdep_assert_wiphy(h->wiphy);assert(c);event('r');removed++;
}
static int assign(struct ieee80211_hw *h,struct ieee80211_vif *v,struct ieee80211_bss_conf *b,struct ieee80211_chanctx_conf *c) {
 lockdep_assert_wiphy(h->wiphy);assert(b->chanreq.oper.chan==c->def.chan);event(v->auxiliary?'A':'s');assigned++;
 return fail_assign ? -EIO : 0;
}
static void unassign(struct ieee80211_hw *h,struct ieee80211_vif *v,struct ieee80211_bss_conf *b,struct ieee80211_chanctx_conf *c) {
 lockdep_assert_wiphy(h->wiphy);assert(b->chanctx_conf==c);event(v->auxiliary?'U':'u');unassigned++;
}
static unsigned self_writes,restores;static int self_error;
static u8 hardware_self[6];
int rtw89_compat_awdl_self_addr(struct ieee80211_vif *v,const u8 *addr,bool force) {
 assert(v==mlme.vif && mlme.hw->wiphy->locked);
 if(self_error)return self_error;
 if(force){assert(!memcmp(addr,v->addr,6));restores++;event('m');}
 if(!force && !memcmp(hardware_self,addr,6))return 0;
 memcpy(hardware_self,addr,6);self_writes++;return 1;
}
#include "owly_awdl_idle.inc"
static const struct ieee80211_ops operations={add,remove_ctx,assign,unassign};
static struct wiphy wiphy={.locked=true};static struct ieee80211_hw hw={&wiphy,16};
static struct ieee80211_vif vif,aux;static struct local local={&operations};
static struct owly_radio_lease lease;
static int aux_assign(void *p,void *v,void *c) {
 (void)p;struct ieee80211_vif *a=v;struct ieee80211_chanctx_conf *ctx=c;
 a->bss_conf.chanreq.oper=ctx->def;int rc=assign(&hw,a,&a->bss_conf,ctx);
 if(!rc)a->bss_conf.chanctx_conf=ctx;return rc;
}
static void aux_unassign(void *p,void *v,void *c) {
 (void)p;struct ieee80211_vif *a=v;unassign(&hw,a,&a->bss_conf,c);a->bss_conf.chanctx_conf=NULL;
}
static const struct owly_radio_ops lease_ops={aux_assign,aux_unassign};
static void before_release(struct ieee80211_chanctx_conf *ctx) {
 assert(!mlme.awdl_chanctx); /* RX withdrawn before context release */
 assert(owly_radio_unbind(&lease,ctx,&lease_ops,NULL)==OWLY_RADIO_OK);
}
/* Compile the production coordinator and the real shared-context predicate. */
static void IOLog(const char *format,...) {(void)format;}
#define owly_radio_state lease
static const struct owly_radio_ops owly_radio_driver_ops={aux_assign,aux_unassign};
static struct {bool up,scanning;} glue;
static struct ieee80211_vif *owly_core04c_vif=&aux;
static bool owly_scc_aux_ready=true,filter_enabled,owly_link_lock;
static unsigned filter_freq;
static struct {bool explicitly_disabled;} owly_sync_state;
static struct owly_native_link owly_link_state;
static uint64_t clock_us,owly_idle_move_us,owly_idle_heard_us;
static bool owly_aux_cam_ready(struct ieee80211_hw *h, struct ieee80211_vif *v) { (void)h; (void)v; return false; }
struct rtw89_mlme_status {int state;bool awdl_channel_owned;unsigned freq;};
static void rtw89_mlme_get_status(struct rtw89_mlme_status *s) {
    s->state=mlme.state;s->awdl_channel_owned=mlme.awdl_chanctx;
    s->freq=mlme.chan ? mlme.chan->center_freq : 0;
}
static struct ieee80211_hw *glue_hw(void) {return &hw;}
static uint64_t glue_now_us(void) {return clock_us;}
static void spin_lock(bool *lock) {assert(!*lock);*lock=true;}
static void spin_unlock(bool *lock) {assert(*lock);*lock=false;}
static void owly_scc_filter_set(bool enabled,unsigned freq) {filter_enabled=enabled;filter_freq=freq;}
#include "production_standalone_glue.inc"
static void reset(void) {
 memset(&mlme,0,sizeof(mlme));memset(&vif,0,sizeof(vif));memset(&aux,0,sizeof(aux));memset(&lease,0,sizeof(lease));
 self_writes=restores=0;self_error=0;memset(hardware_self,0,sizeof(hardware_self));
 vif.addr[0]=2;vif.addr[5]=1;aux.addr[0]=2;aux.addr[5]=2;memcpy(hardware_self,vif.addr,6);
 mlme.running=true;mlme.hw=&hw;mlme.vif=&vif;mlme.local=&local;aux.auxiliary=true;
 allocs=frees=added=removed=assigned=unassigned=fail_alloc=fail_add=fail_assign=0;
 order_len=0;memset(order,0,sizeof(order));owly_core04d_before_release=NULL;
 for(unsigned i=0;i<3;i++)channels[i].flags=0;
 glue.up=true;glue.scanning=false;owly_sync_state.explicitly_disabled=false;awdl_5g_allowed=false;
 memset(&owly_link_state,0,sizeof(owly_link_state));clock_us=1000000;owly_idle_move_us=0;owly_idle_heard_us=0;filter_enabled=false;
}
static void test_owned_lifetime(void) {
 reset();assert(rtw89_mlme_awdl_hold_channel(2437)==0);
 assert(mlme.state==RTW89_MLME_IDLE && mlme.awdl_chanctx && mlme.chanctx_assigned);
 assert(mlme.chandef.chan==&channels[0] && mlme.chanctx->def.chan==&channels[0]);
 assert(rtw89_mlme_awdl_hold_channel(2437)==0 && added==1);
 assert(owly_radio_bind(&lease,&aux,mlme.chanctx,true,&lease_ops,NULL)==OWLY_RADIO_OK);
 owly_core04d_before_release=before_release;
 order_len=0;memset(order,0,sizeof(order));
 rtw89_mlme_awdl_release_channel();assert(strcmp(order,"Uur")==0);
 assert(!lease.channel && !mlme.chanctx && !mlme.chan && !mlme.awdl_chanctx);
 assert(allocs==frees && removed==1 && unassigned==2);
 rtw89_mlme_awdl_release_channel();assert(removed==1);
}
static void test_failures_and_join_priority(void) {
 reset();mlme.state=RTW89_MLME_CONNECTED;assert(rtw89_mlme_awdl_hold_channel(2437)==-EBUSY && !allocs);
 reset();mlme.running=false;assert(rtw89_mlme_awdl_hold_channel(2437)==-ENETDOWN && !allocs);
 reset();assert(rtw89_mlme_awdl_hold_channel(2412)==-EINVAL && !allocs);
 for(unsigned flag=1;flag<=4;flag*=2) {reset();channels[0].flags=flag;assert(rtw89_mlme_awdl_hold_channel(2437)==-EACCES && !added);}
 reset();fail_alloc=1;assert(rtw89_mlme_awdl_hold_channel(2437)==-ENOMEM && !added);
 reset();fail_add=1;assert(rtw89_mlme_awdl_hold_channel(2437)==-EIO && allocs==frees && !assigned && !mlme.chanctx);
 reset();fail_assign=1;assert(rtw89_mlme_awdl_hold_channel(2437)==-EIO && allocs==frees && removed==1 && !mlme.chanctx);
 reset();assert(!rtw89_mlme_awdl_hold_channel(2437));assert(!rtw89_mlme_awdl_hold_channel(5745));
 assert(mlme.chan==&channels[2] && added==2 && removed==1);rtw89_mlme_awdl_release_channel();assert(allocs==frees);
 reset();struct ieee80211_ops missing=operations;struct local broken={&missing};missing.assign_vif_chanctx=NULL;mlme.local=&broken;
 assert(rtw89_mlme_awdl_hold_channel(2437)==-EOPNOTSUPP && !added);
}
static void test_rx_and_channel_policy(void) {
 uint8_t frame[24]={0};struct owly_abi6_rx_context c={.rx_freq_mhz=2437,.sta_freq_mhz=2437,
 .sta_center_freq_mhz=2437,.sta_width_mhz=20,.crc_ok=true,.aux_present=true};
 assert(owly_abi6_demux(frame,sizeof(frame),&c,NULL).kind==OWLY_ABI6_RX_NOT_ELIGIBLE);
 c.standalone_channel=true;assert(owly_abi6_demux(frame,sizeof(frame),&c,NULL).kind==OWLY_ABI6_RX_NOT_AWDL);
 c.scanning=true;assert(owly_abi6_demux(frame,sizeof(frame),&c,NULL).kind==OWLY_ABI6_RX_NOT_ELIGIBLE);c.scanning=false;
 c.rx_freq_mhz=5745;assert(owly_abi6_demux(frame,sizeof(frame),&c,NULL).kind==OWLY_ABI6_RX_NOT_ELIGIBLE);c.rx_freq_mhz=2437;
 c.sta_width_mhz=80;assert(owly_abi6_demux(frame,sizeof(frame),&c,NULL).kind==OWLY_ABI6_RX_NOT_AWDL);
 c.sta_width_mhz=160;assert(owly_abi6_demux(frame,sizeof(frame),&c,NULL).kind==OWLY_ABI6_RX_NOT_ELIGIBLE);
 struct owly_native_link link={0};uint8_t mac[6]={2,1,2,3,4,5};owly_link_reconcile(&link,true,mac,6,1000000);
 assert(owly_link_follow_channel(&link,1000000)==6);
 struct owly_link_peer *p=&link.peers[0];p->used=p->clock_valid=true;p->last_rx_us=p->clock_rx_us=1000000;
 p->mac[0]=2;p->mac[5]=10;memcpy(p->master,p->mac,6);
 for(unsigned i=0;i<16;i++)p->channels[i]=i<9 ? 149 : 6;
 assert(owly_link_follow_channel(&link,1001000)==149);
 for(unsigned i=0;i<16;i++)p->channels[i]=i<8 ? 149 : 6;
 assert(owly_link_follow_channel(&link,1001000)==6);
 assert(owly_link_follow_channel(&link,4000001)==6);
}
static void test_actual_radio_coordinator(void) {
 reset();owly_core04d_before_release=before_release;
 /* When 5 GHz is NO_IR without AWDL 5G allowance, initial standalone starts on ch 6. */
 channels[1].flags=IEEE80211_CHAN_NO_IR;channels[2].flags=IEEE80211_CHAN_NO_IR;
 glue.scanning=true;owly_radio_maybe_attach();assert(!added && !lease.channel);
 glue.scanning=false;owly_radio_maybe_attach();
 assert(mlme.state==RTW89_MLME_IDLE && mlme.awdl_chanctx && lease.channel==mlme.chanctx);
 assert(filter_enabled && filter_freq==2437);
 assert(mlme.awdl_self_addr && self_writes==1 && !memcmp(hardware_self,aux.addr,6));
 owly_radio_maybe_attach();assert(added==1 && assigned==2);
 /* Multicast mDNS RX increments data_rx_admitted without marking unicast busy;
  * once 5 GHz is permitted, ch 6 must upgrade immediately to ch 149. */
 channels[2].flags=0;
 uint8_t mac[6]={2,1,2,3,4,5};owly_link_reconcile(&owly_link_state,true,mac,6,clock_us);
 owly_link_state.sync_enabled=true;
 struct owly_link_peer *p=&owly_link_state.peers[0];p->used=p->clock_valid=true;
 p->last_rx_us=p->clock_rx_us=clock_us;p->mac[0]=2;p->mac[5]=10;
 for(unsigned i=0;i<16;i++)p->channels[i]=i<9?149:6;
 owly_link_state.data_rx_admitted=42;
 owly_radio_maybe_attach();assert(mlme.chan==&channels[2] && lease.channel==mlme.chanctx && filter_freq==5745);
 rtw89_mlme_awdl_release_channel();assert(!lease.channel && allocs==frees);
 /* Active unicast traffic MUST prevent mid-transfer channel switch. */
 reset();owly_core04d_before_release=before_release;
 channels[1].flags=IEEE80211_CHAN_NO_IR;channels[2].flags=IEEE80211_CHAN_NO_IR;
 owly_radio_maybe_attach();assert(filter_freq==2437);
 channels[2].flags=0;
 owly_link_reconcile(&owly_link_state,true,mac,6,clock_us);
 owly_link_state.sync_enabled=true;
 owly_link_state.last_unicast_activity_us=clock_us;
 p=&owly_link_state.peers[0];p->used=p->clock_valid=true;
 p->last_rx_us=p->clock_rx_us=clock_us;p->mac[0]=2;p->mac[5]=10;
 for(unsigned i=0;i<16;i++)p->channels[i]=i<9?149:6;
 owly_radio_maybe_attach();assert(mlme.chan==&channels[0] && filter_freq==2437);
 rtw89_mlme_awdl_release_channel();assert(!lease.channel && allocs==frees);
 /* When 5 GHz ch 149 is allowed, standalone starts immediately on ch 149. */
 reset();owly_core04d_before_release=before_release;
 owly_radio_maybe_attach();
 assert(mlme.chan==&channels[2] && lease.channel==mlme.chanctx && filter_freq==5745);
 rtw89_mlme_awdl_release_channel();assert(!lease.channel && allocs==frees);
 /* Also starts on ch 149 when NO_IR is cleared by rtw89_cfg80211_awdl_5g_ok. */
 reset();owly_core04d_before_release=before_release;
 channels[1].flags=IEEE80211_CHAN_NO_IR;channels[2].flags=IEEE80211_CHAN_NO_IR;
 awdl_5g_allowed=true;
 owly_radio_maybe_attach();
 assert(mlme.chan==&channels[2] && lease.channel==mlme.chanctx && filter_freq==5745);
 rtw89_mlme_awdl_release_channel();assert(!lease.channel && allocs==frees);
 reset();owly_sync_state.explicitly_disabled=true;owly_radio_maybe_attach();assert(!added && !lease.channel);
 reset();mlme.state=RTW89_MLME_CONNECTED;owly_radio_maybe_attach();assert(!added && !mlme.awdl_chanctx);
 reset();fail_assign=1;owly_radio_maybe_attach();assert(lease.blocked && allocs==frees && !lease.channel);
}
static void test_self_address_lifetime(void) {
 reset();assert(rtw89_mlme_awdl_set_self_addr(aux.addr)==-EBUSY && !self_writes);
 assert(!rtw89_mlme_awdl_hold_channel(2437));
 assert(!rtw89_mlme_awdl_set_self_addr(aux.addr));assert(mlme.awdl_self_addr && self_writes==1);
 assert(!rtw89_mlme_awdl_set_self_addr(aux.addr) && self_writes==1);
 memcpy(hardware_self,vif.addr,6); // firmware scan complete restored station MAC
 assert(!rtw89_mlme_awdl_set_self_addr(aux.addr) && self_writes==2);
 mlme.state=RTW89_MLME_CONNECTED;
 assert(rtw89_mlme_awdl_set_self_addr(aux.addr)==-EBUSY && self_writes==2);
 mlme.state=RTW89_MLME_IDLE;order_len=0;memset(order,0,sizeof(order));
 rtw89_mlme_awdl_release_channel();assert(!mlme.awdl_self_addr && restores==1);
 assert(order[0]=='m' && !memcmp(hardware_self,vif.addr,6) && allocs==frees);
 reset();assert(!rtw89_mlme_awdl_hold_channel(2437));self_error=-EIO;
 assert(rtw89_mlme_awdl_set_self_addr(aux.addr)==-EIO && mlme.awdl_self_addr);
 rtw89_mlme_awdl_release_channel();assert(mlme.awdl_self_addr && mlme.awdl_self_addr_error==-EIO);
 self_error=0;assert(!rtw89_mlme_awdl_set_self_addr(NULL));assert(!mlme.awdl_self_addr && restores==1);
 reset();self_error=-EIO;owly_radio_maybe_attach();assert(lease.blocked && lease.last_error==-EIO);
 self_error=0;rtw89_mlme_awdl_release_channel();assert(!mlme.awdl_self_addr && allocs==frees);
}
int main(void) {test_self_address_lifetime();test_owned_lifetime();test_failures_and_join_priority();test_rx_and_channel_policy();test_actual_radio_coordinator();puts("standalone: production radio coordinator, idle-channel lifetime, failure rollback, RX eligibility and master-channel policy passed");}
