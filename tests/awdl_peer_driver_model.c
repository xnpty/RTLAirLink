/* SPDX-License-Identifier: GPL-2.0 */
/* Executes the production include; only kernel and firmware calls are mocked. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "owly_native_link.h"
#include "owly_awdl_ba.h"
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;
#define FCS_LEN 4
#define BIT(n) (1u<<(n))
#define RTL8852B 1
#define NL80211_BAND_2GHZ 0
#define NL80211_BAND_5GHZ 1
#define NL80211_CHAN_WIDTH_20 1
#define NL80211_CHAN_WIDTH_40 2
#define NL80211_CHAN_WIDTH_80 3
#define NL80211_CHAN_WIDTH_160 4
#define IEEE80211_STA_RX_BW_20 0
#define IEEE80211_STA_RX_BW_40 1
#define IEEE80211_STA_RX_BW_80 2
#define IEEE80211_HT_CAP_LDPC_CODING 0x0001
#define IEEE80211_HT_CAP_SUP_WIDTH_20_40 0x0002
#define IEEE80211_HT_CAP_SGI_20 0x0020
#define IEEE80211_HT_CAP_SGI_40 0x0040
#define IEEE80211_HT_CAP_TX_STBC 0x0080
#define IEEE80211_HT_CAP_RX_STBC 0x0300
#define IEEE80211_VHT_CAP_RXLDPC 0x00000010
#define IEEE80211_VHT_CAP_SHORT_GI_80 0x00000020
#define IEEE80211_VHT_CAP_TXSTBC 0x00000080
#define IEEE80211_VHT_CAP_RXSTBC_MASK 0x00000700
#define IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK 0x03800000
#define IEEE80211_QOS_CTL_ACK_POLICY_MASK 0x60
#define IEEE80211_QOS_CTL_ACK_POLICY_NOACK 0x20
#define MAX_RSSI 110
#define RSSI_FACTOR 1
#define WRITE_ONCE(field,value) ((field)=(value))
#define cpu_to_le16(x) ((uint16_t)(x))
#define le16_to_cpu(x) ((uint16_t)(x))
enum ieee80211_ampdu_mlme_action {
    IEEE80211_AMPDU_RX_START, IEEE80211_AMPDU_RX_STOP,
    IEEE80211_AMPDU_TX_START, IEEE80211_AMPDU_TX_STOP_CONT,
    IEEE80211_AMPDU_TX_STOP_FLUSH, IEEE80211_AMPDU_TX_STOP_FLUSH_CONT,
    IEEE80211_AMPDU_TX_OPERATIONAL
};
typedef unsigned spinlock_t;
static void spin_lock_init(spinlock_t *l){*l=0;}
static void spin_lock(spinlock_t *l){assert(!*l);*l=1;}
static void spin_unlock(spinlock_t *l){assert(*l);*l=0;}
static void spin_lock_destroy(spinlock_t *l){assert(!*l);}
struct work_struct {void (*func)(struct work_struct *);bool pending;};
struct delayed_work {struct work_struct work;};
struct workqueue_struct {unsigned unused;};
static struct workqueue_struct receive_queue;
#define INIT_WORK(w,f) do {(w)->func=(f);(w)->pending=false;} while(0)
#define INIT_DELAYED_WORK(w,f) INIT_WORK(&(w)->work,(f))
static unsigned usecs_to_jiffies(unsigned us){return us;}
static bool queue_work(struct workqueue_struct *q,struct work_struct *w){assert(q==&receive_queue);w->pending=true;return true;}
static bool queue_delayed_work(struct workqueue_struct *q,struct delayed_work *w,unsigned d){(void)d;return queue_work(q,&w->work);}
static bool cancel_work_sync(struct work_struct *w){w->pending=false;return true;}
static bool cancel_delayed_work_sync(struct delayed_work *w){return cancel_work_sync(&w->work);}
struct list_head {struct list_head *next,*prev;};
#define INIT_LIST_HEAD(h) ((h)->next=(h)->prev=(h))
static bool list_empty(struct list_head *h){return h->next==h;}
static void list_add_tail(struct list_head *n,struct list_head *h){n->prev=h->prev;n->next=h;h->prev->next=n;h->prev=n;}
static void list_del(struct list_head *n){n->prev->next=n->next;n->next->prev=n->prev;n->next=n->prev=n;}
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define list_first_entry(h,t,m) container_of((h)->next,t,m)
#define list_for_each_entry(e,h,m) for(struct list_head *_n=(h)->next;_n!=(h) && ((e)=container_of(_n,__typeof__(*(e)),m),1);_n=_n->next)
enum ieee80211_sta_state {IEEE80211_STA_NOTEXIST,IEEE80211_STA_NONE,IEEE80211_STA_AUTH,IEEE80211_STA_ASSOC,IEEE80211_STA_AUTHORIZED};
struct rtw89_sta;struct ieee80211_sta;
struct rtw89_vif_link {unsigned unused;};
struct rtw89_ba_cam_entry {struct list_head list;unsigned tid;};
struct rtw89_sta_link {
    struct rtw89_vif_link *rtwvif_link;
    struct list_head ba_cam_list;
    unsigned mac_id;
    struct {unsigned addr_cam_idx;} addr_cam;
    bool ra_configured;
    uint8_t owly_awdl_action_rssi;
    struct {u32 bit_rate; bool might_fallback_legacy;} ra_report;
};
struct rtw89_sta {bool owly_awdl;struct rtw89_sta_link *links[1];struct rtw89_sta_link link;struct rtw89_ba_cam_entry entries[16];};
struct ieee80211_sta {
    bool wme,uploaded;
    struct {
        unsigned bandwidth,rx_nss,link_id;
        uint32_t supp_rates[3];
        struct {bool ht_supported;uint16_t cap;uint8_t ampdu_factor,ampdu_density;struct {uint8_t rx_mask[10];} mcs;} ht_cap;
        struct {bool vht_supported;uint32_t cap;struct {uint16_t rx_mcs_map,tx_mcs_map;} vht_mcs;} vht_cap;
    } deflink;
    unsigned max_rx_aggregation_subframes;
    struct rtw89_sta private;
};
static struct rtw89_sta *sta_to_rtwsta(struct ieee80211_sta *s){return &s->private;}
struct ieee80211_channel {unsigned band;};
struct cfg80211_chan_def {struct ieee80211_channel *chan;unsigned width;};
struct ieee80211_vif {uint8_t addr[6];struct {struct {struct cfg80211_chan_def oper;} chanreq;} bss_conf;};
struct ieee80211_supported_band {
    unsigned n_bitrates;struct {unsigned bitrate;} bitrates[4];
    struct {bool ht_supported;uint16_t cap;uint8_t ampdu_factor,ampdu_density;struct {uint8_t rx_mask[10];} mcs;} ht_cap;
    struct {bool vht_supported;uint32_t cap;struct {uint16_t rx_mcs_map,tx_mcs_map;} vht_mcs;} vht_cap;
};
struct wiphy {bool held;struct ieee80211_supported_band *bands[3];};
struct ieee80211_hw {struct wiphy *wiphy;void *priv;unsigned queues;};
struct rtw89_dev {struct {unsigned chip_id,bacam_dynamic_num;} *chip;struct {uint8_t tx_nss,rx_nss;} hal;};
struct ieee80211_ampdu_params {enum ieee80211_ampdu_mlme_action action;struct ieee80211_sta *sta;u16 tid,ssn,buf_size;bool amsdu;};
struct sk_buff {uint8_t data[128];size_t len;struct list_head list;};
static struct ieee80211_channel channel;
static struct ieee80211_supported_band band={4,{{10},{20},{60},{120}},{true,0x01e3,3,4,{{0xff,0xff}}},{true,0x038001b0,{0xfffa,0xfffa}}};
static struct wiphy phy={true,{&band,&band,NULL}};
static __typeof__(*((struct rtw89_dev *)0)->chip) chip={RTL8852B,4};
static struct rtw89_dev device={&chip,{2,2}};static struct ieee80211_hw hardware={&phy,&device,4};
static struct ieee80211_vif aux={{2,1,2,3,4,5},{{{&channel,NL80211_CHAN_WIDTH_20}}}},*owly_core04c_vif=&aux;
static struct {bool scanning,up;struct workqueue_struct *awdl_rx_wq;} glue;
static struct owly_native_link owly_link_state;static spinlock_t owly_link_lock;
static unsigned allocated,freed,delivered,wakes,flushes,sta_changes,fw_starts,fw_stops,stats,txs,packet_alloc,packet_free;
static unsigned delivered_sn[256];
static uint8_t signal_at_assoc;
static int fail_state=-1,fw_result,undo_result,submit_result;
static bool rx_context,no_entry,window_open=true,idle_query_open=true,leave_window_during_h2c;
static uint64_t current=1000000;static uint8_t last_response[33];
static struct ieee80211_hw *glue_hw(void){return &hardware;}
static uint64_t glue_now_us(void){return current;}
#define lockdep_assert_wiphy(w) assert((w)->held && !rx_context)
#define IOLog(...) do {if(0)printf(__VA_ARGS__);} while(0)
static struct ieee80211_sta *rtw89_m80211_sta_alloc(struct ieee80211_vif *v,const uint8_t *mac)
{assert(v==&aux && mac[0]==2);struct ieee80211_sta *s=calloc(1,sizeof(*s));assert(s);++allocated;
 s->private.links[0]=&s->private.link;INIT_LIST_HEAD(&s->private.link.ba_cam_list);return s;}
static void rtw89_m80211_sta_free(struct ieee80211_sta *s){free(s);++freed;}
static void rtw89_m80211_sta_set_uploaded(struct ieee80211_sta *s,bool v){s->uploaded=v;}
static void rtw89_m80211_sta_purge_txqs(struct ieee80211_sta *s){assert(s);}
static void rtw89_phy_ra_assoc(struct rtw89_dev *dev,struct rtw89_sta_link *link)
{
    assert(dev==&device);
    signal_at_assoc=link->owly_awdl_action_rssi;
    link->ra_configured=true;
}
static int sta_state(struct ieee80211_hw *h,struct ieee80211_vif *v,struct ieee80211_sta *s,enum ieee80211_sta_state old,enum ieee80211_sta_state next)
{assert(h==&hardware && v==&aux && s->private.owly_awdl && phy.held && !rx_context);
 assert((int)next==(int)old+1 || (int)next==(int)old-1);++sta_changes;
 if((int)next==fail_state && next>old)return -EIO;
 if(old==IEEE80211_STA_AUTH && next==IEEE80211_STA_ASSOC)rtw89_phy_ra_assoc(&device,&s->private.link);
 return 0;}
static void flush(struct ieee80211_hw *h,struct ieee80211_vif *v,unsigned mask,bool discard)
{assert(h==&hardware && v==&aux && mask==15 && !discard && !rx_context);++flushes;}
static int ampdu_action_cb(struct ieee80211_hw *h,struct ieee80211_vif *v,struct ieee80211_ampdu_params *a)
{assert(h==&hardware && v==&aux && a && a->sta && phy.held && !rx_context);return 0;}
static const struct {int (*sta_state)(struct ieee80211_hw *,struct ieee80211_vif *,struct ieee80211_sta *,enum ieee80211_sta_state,enum ieee80211_sta_state);
 void (*flush)(struct ieee80211_hw *,struct ieee80211_vif *,unsigned,bool);
 int (*ampdu_action)(struct ieee80211_hw *,struct ieee80211_vif *,struct ieee80211_ampdu_params *);} ops={sta_state,flush,ampdu_action_cb};
struct rtw89_m80211_local {__typeof__(&ops) ops;};static struct rtw89_m80211_local local={&ops};
static struct rtw89_m80211_local *hw_to_local(struct ieee80211_hw *h){assert(h==&hardware);return &local;}
static int rtw89_fw_h2c_ba_cam(struct rtw89_dev *d,struct rtw89_vif_link *v,struct rtw89_sta_link *sl,bool start,struct ieee80211_ampdu_params *a)
{assert(d==&device && v==sl->rtwvif_link && !rx_context && a->tid<16);
 struct rtw89_sta *s=&a->sta->private;struct rtw89_ba_cam_entry *e=&s->entries[a->tid];
 if(start){++fw_starts;if(!no_entry){e->tid=a->tid;list_add_tail(&e->list,&sl->ba_cam_list);}
  if(leave_window_during_h2c){current+=20000;window_open=false;}return fw_result;}
 ++fw_stops;if(e->list.next && e->list.next!=&e->list)list_del(&e->list);return undo_result;}
static void rtw89_core_tid_rx_stats_ctrl(struct rtw89_dev *d,struct rtw89_sta *s,struct ieee80211_ampdu_params *a,bool start)
{(void)d;(void)s;(void)a;(void)start;++stats;}
static void kfree_skb(struct sk_buff *s){++packet_free;free(s);}
static void owly_awdl_deliver_rx(struct sk_buff *s)
{assert(!phy.held && !rx_context && delivered<256);delivered_sn[delivered++]=owly_ba_le16(s->data+22)>>4;kfree_skb(s);}
static struct {unsigned unused;} owly_sync_work;
static void wiphy_delayed_work_queue(struct wiphy *w,void *work,unsigned delay)
{assert(w==&phy && work==&owly_sync_work && !delay);++wakes;}
static struct ieee80211_channel *owly_sync_bound_channel(void){return &channel;}
bool owly_link_unicast_action_tx_ready(struct owly_native_link *s,const uint8_t mac[6],uint64_t now)
{assert(s==&owly_link_state && mac && now==current);return window_open && idle_query_open;}
bool owly_link_unicast_reply_tx_ready(struct owly_native_link *s,const uint8_t mac[6],uint64_t now)
{assert(s==&owly_link_state && mac && now==current);return window_open;}
static int owly_sync_submit(struct ieee80211_channel *ch,const uint8_t *p,size_t n,bool data)
{assert(ch==&channel && n==33 && !data && !rx_context);memcpy(last_response,p,33);++txs;return submit_result;}
#include "owly_awdl_peer.inc"
static void drain(void){assert(!rx_context);bool held=phy.held;phy.held=false;
 if(owly_peer_delivery_work.pending){owly_peer_delivery_work.pending=false;owly_peer_delivery_run(&owly_peer_delivery_work);}phy.held=held;}
static bool receive_data(struct sk_buff *s){rx_context=true;bool owned=owly_peer_data_rx(s);rx_context=false;drain();return owned;}
static const uint8_t remote[6]={2,6,7,8,9,10};
static struct sk_buff *request(unsigned token,unsigned tid){
 struct sk_buff *s=calloc(1,sizeof(*s));assert(s);++packet_alloc;s->len=37;s->data[0]=0xd0;
 memcpy(s->data+4,aux.addr,6);memcpy(s->data+10,remote,6);memcpy(s->data+16,(uint8_t[]){0,0x25,0,0xff,0x94,0x73},6);
 s->data[24]=3;s->data[26]=(uint8_t)token;owly_ba_put16(s->data+27,(uint16_t)(2|(tid<<2)|(64<<6)));owly_ba_put16(s->data+31,100<<4);return s;
}
static struct sk_buff *qos(unsigned sn,unsigned tid){
 struct sk_buff *s=request(0,tid);s->len=66;s->data[0]=0x88;owly_ba_put16(s->data+22,(uint16_t)(sn<<4));s->data[24]=(uint8_t)tid;return s;
}
static void receive(struct sk_buff *s){rx_context=true;assert(owly_peer_control_rx(s));rx_context=false;}
static void reset(void){
 if(allocated!=freed)owly_peer_close_all();
 assert(allocated==freed);owly_peer_init();memset(&owly_link_state,0,sizeof(owly_link_state));
 owly_link_state.active=true;owly_link_state.generation=7;memcpy(owly_link_state.local,aux.addr,6);
 owly_link_state.peers[0].used=true;memcpy(owly_link_state.peers[0].mac,remote,6);owly_link_state.peers[0].last_rx_us=current;
 glue.scanning=false;glue.up=true;glue.awdl_rx_wq=&receive_queue;
 fw_result=undo_result=submit_result=0;no_entry=false;window_open=idle_query_open=true;chip.bacam_dynamic_num=4;fail_state=-1;leave_window_during_h2c=false;
}
static void open_peer(void)
{
    owly_peer_tick(&channel);
    struct owly_awdl_radio_peer *peer=owly_peer_find(remote);
    assert(peer);
    rtw89_awdl_note_rx_cam(remote,aux.addr,sta_to_rtwsta(peer->sta)->link.mac_id);
}
static void action_rssi(void)
{
    reset();
    struct owly_link_peer *observed=&owly_link_state.peers[0];
    observed->rssi=-42;
    open_peer();
    struct rtw89_sta_link *link=&owly_peer_find(remote)->sta->private.link;
    assert(signal_at_assoc==136 && link->owly_awdl_action_rssi==136);

    current+=200000;
    observed->last_rx_us=current;
    observed->rssi=-65;
    owly_peer_tick(&channel);
    assert(signal_at_assoc==136 && link->owly_awdl_action_rssi==90);

    owly_peer_use_action_rssi=false;
    owly_peer_check_us=0;
    owly_peer_tick(&channel);
    assert(link->owly_awdl_action_rssi==0);

    owly_peer_use_action_rssi=true;
    current+=OWLY_LINK_PEER_TTL_US+1;
    observed->last_data_rx_us=current;
    owly_peer_tick(&channel);
    assert(owly_peer_find(remote) && link->owly_awdl_action_rssi==0);
    owly_peer_close_all();
    assert(allocated==freed);
}
static void late_control_reply(void){
 reset();open_peer();const unsigned before=txs;
 /* The confirmed peer slot is still open after proactive query wake time. */
 idle_query_open=false;receive(request(77,0));owly_peer_tick(&channel);
 assert(txs==before+1 && !owly_control_count && !owly_ba_le16(last_response+27));
 owly_peer_close_all();assert(allocated==freed && packet_alloc==packet_free);
}
static void dynamic_fallback(void){
 reset();open_peer();no_entry=true;
 const unsigned stopped=fw_stops,previous_stats=stats,before=delivered;
 receive(request(78,1));owly_peer_tick(&channel);
 struct owly_awdl_radio_peer *p=owly_peer_find(remote);
 assert(p && !p->blocked && p->ba_map==(1u<<1));
 assert(!owly_ba_le16(last_response+27) && fw_stops==stopped && stats==previous_stats+1);
 assert(receive_data(qos(102,1)));assert(receive_data(qos(100,1)));assert(receive_data(qos(101,1)));
 assert(delivered==before+3);
 for(unsigned i=0;i<3;i++)assert(delivered_sn[before+i]==100+i);
 /* A real H2C error must still decline, even on a dynamic-capable chip. */
 fw_result=-EIO;receive(request(79,1));owly_peer_tick(&channel);
 assert(owly_ba_le16(last_response+27)==37 && !p->ba_map && fw_stops>stopped);
 owly_peer_close_all();assert(allocated==freed && packet_alloc==packet_free);
}
static void tx_amsdu_and_idle_ba(void)
{
    reset();
    struct owly_link_peer *observed = &owly_link_state.peers[0];
    observed->ht_rx_mcs = 0xffffu;
    observed->ht_cap_info = 0x09efu;
    observed->max_channel_width = 80u;
    observed->has_vht_cap = true;
    observed->vht_cap_info = 0x038001b1u; /* max MPDU 7991 */
    observed->vht_rx_mcs_map = 0xfffa;
    open_peer();
    struct owly_awdl_radio_peer *p = owly_peer_find(remote);
    assert(p && p->sta);
    /* Simulate operational TX BA with A-MSDU enabled. */
    p->tx_ba_state = OWLY_TX_BA_OPERATIONAL;
    p->tx_ba_buf_size = 64;
    p->tx_ba_amsdu = true;
    p->tx_ba_amsdu_max = 4900u;
    p->tx_qos_sn = 42u;
    p->tx_last_unicast_us = current;

    struct owly_link_tx_qos_peer q[OWLY_RADIO_PEERS];
    /* Initial bit_rate=0 uses full 4900 / 3 subframes. */
    assert(owly_peer_fill_tx_qos(q) == 1 && q[0].max_amsdu == 4900u && q[0].max_subframes == 3u && q[0].qos_sn == 42u);
    /* High rate (866.7 Mbps) keeps 4900 / 3 subframes. */
    p->sta->private.link.ra_report.bit_rate = 8667u;
    assert(owly_peer_fill_tx_qos(q) == 1 && q[0].max_amsdu == 4900u && q[0].max_subframes == 3u);
    /* Medium rate (520 Mbps) caps to 3500 / 2 subframes. */
    p->sta->private.link.ra_report.bit_rate = 5200u;
    assert(owly_peer_fill_tx_qos(q) == 1 && q[0].max_amsdu == 3500u && q[0].max_subframes == 2u);
    /* Low rate (200 Mbps) disables TX A-MSDU (single-MSDU QoS). */
    p->sta->private.link.ra_report.bit_rate = 2000u;
    assert(owly_peer_fill_tx_qos(q) == 1 && q[0].max_amsdu == 0u && q[0].max_subframes == 1u);
    /* Legacy fallback also disables TX A-MSDU even at high bit_rate. */
    p->sta->private.link.ra_report.bit_rate = 8667u;
    p->sta->private.link.ra_report.might_fallback_legacy = true;
    assert(owly_peer_fill_tx_qos(q) == 1 && q[0].max_amsdu == 0u && q[0].max_subframes == 1u);
    p->sta->private.link.ra_report.might_fallback_legacy = false;

    uint8_t ba_st = 0; bool ba_amsdu = false, fb = false; uint16_t mbps = 0;
    owly_peer_tx_diag(&ba_st, &ba_amsdu, &fb, &mbps);
    assert(ba_st == OWLY_TX_BA_OPERATIONAL && ba_amsdu && !fb && mbps == 866u);

    /* 6 unACKed initial A-MSDUs trigger permanent fallback until next ADDBA. */
    for (unsigned k = 0; k < 6u; ++k) owly_peer_tx_amsdu_note(false);
    assert(owly_peer_fill_tx_qos(q) == 1 && q[0].max_amsdu == 0u && q[0].max_subframes == 1u);
    owly_peer_tx_diag(&ba_st, &ba_amsdu, &fb, &mbps);
    assert(fb);

    /* >10s idle expires operational TX BA inside owly_peer_tick(). */
    current += 10000001ull;
    observed->last_rx_us = current;
    owly_peer_tick(&channel);
    assert(p->tx_ba_state == OWLY_TX_BA_NONE && !p->tx_ba_amsdu && owly_peer_fill_tx_qos(q) == 0);
    owly_peer_close_all();
    assert(allocated == freed && packet_alloc == packet_free);
}
int main(int argc,char **argv){
 if(argc==2){
  if(!strcmp(argv[1],"late-reply"))late_control_reply();
  else if(!strcmp(argv[1],"dynamic-ba"))dynamic_fallback();
  else return 2;
  puts("regression passed (firmware mocked)");return 0;
 }

 reset();open_peer();assert(allocated==1 && !freed);
 struct owly_awdl_radio_peer *p=owly_peer_find(remote);assert(p && p->sta->uploaded && owly_peer_tx_station(remote)==p->sta);
 receive(request(19,7));assert(!fw_starts && wakes);
 glue.scanning=true;owly_peer_tick(&channel);assert(!fw_starts && owly_control_count==1);
 glue.scanning=false;window_open=false;owly_peer_tick(&channel);assert(!fw_starts && !txs);
 window_open=true;owly_peer_tick(&channel);assert(fw_starts==1 && txs==1 && !owly_control_count);
 assert(p->ba_map==(1u<<7) && p->reorder[7].active && !owly_ba_le16(last_response+27));
 assert(receive_data(qos(102,7)) && !delivered);assert(receive_data(qos(101,7)) && !delivered);
 rx_context=true;assert(owly_peer_data_rx(qos(100,7)));rx_context=false;
 assert(!delivered && owly_peer_ready_count==3);drain();assert(delivered==3);
 for(unsigned i=0;i<3;i++)assert(delivered_sn[i]==100+i);
 assert(receive_data(qos(101,7)) && delivered==3);
 struct sk_buff *wrong=qos(103,7);wrong->data[4]^=4;assert(!receive_data(wrong));kfree_skb(wrong);
 assert(receive_data(qos(105,7)) && delivered==3);
 struct sk_buff *bar=calloc(1,sizeof(*bar));assert(bar);++packet_alloc;bar->len=24;bar->data[0]=0x84;
 memcpy(bar->data+4,aux.addr,6);memcpy(bar->data+10,remote,6);owly_ba_put16(bar->data+16,(uint16_t)((7u<<12)|4));owly_ba_put16(bar->data+18,105<<4);
 rx_context=true;assert(owly_peer_bar_rx(bar));rx_context=false;drain();assert(delivered==4 && delivered_sn[3]==105);
 receive(request(19,7));owly_peer_tick(&channel);assert(fw_starts==1 && txs==2);
 fw_result=-EIO;receive(request(20,7));owly_peer_tick(&channel);assert(fw_starts==2 && !p->ba_map && owly_ba_le16(last_response+27)==37 && fw_stops>=2);
 reset();open_peer();no_entry=true;chip.bacam_dynamic_num=0;receive(request(1,0));owly_peer_tick(&channel);
 assert(owly_ba_le16(last_response+27)==37 && !owly_peer_find(remote)->ba_map);
 reset();open_peer();submit_result=-ENOSPC;receive(request(2,0));owly_peer_tick(&channel);assert(owly_control_count==1);
 unsigned started=fw_starts;submit_result=0;owly_peer_tick(&channel);assert(!owly_control_count && fw_starts==started);
 reset();open_peer();leave_window_during_h2c=true;unsigned sent=txs;
 receive(request(22,0));owly_peer_tick(&channel);started=fw_starts;assert(owly_control_count==1 && txs==sent && owly_peer_find(remote)->ba_map==1);
 leave_window_during_h2c=false;window_open=true;owly_peer_tick(&channel);assert(!owly_control_count && txs==sent+1 && fw_starts==started);
 receive(request(3,1));owly_link_state.generation++;owly_peer_tick(&channel);assert(!owly_control_count);
 reset();fail_state=IEEE80211_STA_ASSOC;owly_peer_tick(&channel);assert(!owly_peer_find(remote) && allocated==freed);
 reset();open_peer();receive(request(4,15));owly_peer_close_all();assert(!owly_control_count && allocated==freed && !owly_peer_tx_station(remote));
 reset();open_peer();receive(request(5,15));owly_peer_tick(&channel);assert(owly_peer_find(remote)->ba_map==(1u<<15));
 current+=OWLY_LINK_PEER_TTL_US+1;owly_peer_tick(&channel);assert(!owly_peer_find(remote) && allocated==freed);
 reset();open_peer();receive(request(6,0));owly_peer_tick(&channel);assert(receive_data(qos(101,0)));
 unsigned before=delivered;owly_peer_close_all();assert(delivered==before && allocated==freed && packet_alloc==packet_free);
 reset();open_peer();for(unsigned i=0;i<32;i++)receive(request(i,1));assert(owly_control_count==8);
 owly_peer_tick(&channel);assert(!owly_control_count);owly_peer_close_all();assert(packet_alloc==packet_free && allocated==freed);
 reset();open_peer();receive(request(55,0));owly_peer_tick(&channel);
 rx_context=true;assert(owly_peer_data_rx(qos(100,0)));rx_context=false;assert(owly_peer_ready_count==1);
 before=delivered;owly_peer_close_all();drain();assert(delivered==before && packet_alloc==packet_free && allocated==freed);
 /* Ready queue is bounded even when the ordinary RX worker is stalled. */
 reset();open_peer();receive(request(56,0));owly_peer_tick(&channel);
 rx_context=true;for(unsigned sn=100;sn<100u+OWLY_PEER_READY_SIZE+13u;sn++)assert(owly_peer_data_rx(qos(sn,0)));rx_context=false;
 assert(owly_peer_ready_count==OWLY_PEER_READY_SIZE);owly_peer_close_all();drain();assert(packet_alloc==packet_free && allocated==freed);
 late_control_reply();dynamic_fallback();action_rssi();tx_amsdu_and_idle_ba();
 puts("production peer glue: RSSI seeding and expiry, deferred RX, scan/window/H2C errors, duplicate retries, reorder/BAR and teardown passed (firmware mocked)");
}
