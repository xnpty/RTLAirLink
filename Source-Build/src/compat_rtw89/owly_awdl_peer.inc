/* SPDX-License-Identifier: GPL-2.0 */ /* Modified 2026-11-15: BlockAck reply/dynamic-CAM fixes; see MODIFICATIONS.md. */
/* RTLAirLink integration and release changes dated 2026-11-21. */
/* Real observed AWDL radio peers. Wiphy owns CAM/firmware; RX only queues
 * control requests or packet ownership. No AP join, TDLS, synthetic TCP/TLS. */
#include "owly_awdl_ba.h"
#define OWLY_RADIO_PEERS 4u
#define OWLY_CONTROL_QUEUE 8u
#define OWLY_TX_BA_NONE 0u
#define OWLY_TX_BA_WAIT 1u
#define OWLY_TX_BA_OPERATIONAL 2u
#define OWLY_TX_BA_DECLINED 3u
#define OWLY_TX_BA_TIMEOUT_US 1000000ull
#define OWLY_TX_BA_MAX_TRIES 3u
struct owly_peer_caps {
 uint16_t ht_rx_mcs, ht_cap_info;
 uint8_t ht_ampdu_params, max_channel_width;
 bool has_vht_cap;
 uint32_t vht_cap_info;
 uint16_t vht_rx_mcs_map;
};
struct owly_awdl_radio_peer {
 struct ieee80211_sta *sta;enum ieee80211_sta_state state;bool blocked;
 uint8_t mac[6],own[6];uint16_t ba_map;
 bool rx_cam_seen;
 uint8_t rx_cam_mac_id;
 struct owly_ba_request requests[16];struct owly_ba_ring reorder[16];
 /* Applied PHY rate state for change detection. */
 uint8_t applied_bw, applied_nss;
 bool applied_vht, applied_turbo;
 uint16_t applied_ht_mcs, applied_ht_cap, applied_vht_mcs;
 uint32_t applied_vht_cap;
 /* TX BlockAck (TID 0) state machine. */
 uint8_t tx_ba_state, tx_ba_token, tx_ba_attempts;
 uint16_t tx_ba_buf_size, tx_qos_sn;
 bool tx_ba_needed;
 uint64_t tx_ba_sent_us, tx_last_unicast_us;
};
struct owly_awdl_control {uint8_t bytes[64];size_t length;uint64_t received,generation;};
static struct owly_awdl_radio_peer owly_radio_peers[4];
static struct owly_awdl_control owly_controls[8];
static unsigned owly_control_head,owly_control_count;
static spinlock_t owly_peer_lock;
static uint64_t owly_peer_generation,owly_peer_check_us;
static uint16_t owly_control_sequence;
static uint8_t owly_tx_ba_dialog;
static bool owly_peer_fault,owly_peer_accepting;
static bool owly_peer_use_action_rssi = true;
static bool owly_peer_turbo = true;
static struct work_struct owly_peer_delivery_work;
#define OWLY_PEER_READY_SIZE 1024u
static struct {struct sk_buff *skb;uint64_t generation;} owly_peer_ready[OWLY_PEER_READY_SIZE];
static unsigned owly_peer_ready_head,owly_peer_ready_count;
static uint64_t owly_peer_ready_drops;
static void owly_peer_delivery_run(struct work_struct *work);
static void owly_awdl_deliver_rx(struct sk_buff *skb);
static struct ieee80211_channel *owly_sync_bound_channel(void);
static int owly_sync_submit(struct ieee80211_channel *ch,const uint8_t *p,size_t n,bool data);
static void owly_peer_init(void)
{
 spin_lock_init(&owly_peer_lock);memset(owly_radio_peers,0,sizeof(owly_radio_peers));
 owly_control_head=owly_control_count=0;owly_peer_generation=owly_peer_check_us=0;
 owly_peer_fault=owly_peer_accepting=false;owly_control_sequence=0;owly_tx_ba_dialog=0;
 owly_peer_ready_head=owly_peer_ready_count=0;owly_peer_ready_drops=0;
 INIT_WORK(&owly_peer_delivery_work,owly_peer_delivery_run);
}
struct owly_peer_packet_queues {
 struct list_head drop;
 bool scheduled_delivery;
};

/* The reorder state and the ready queue share owly_peer_lock. Publish each
 * released packet before unlocking so another RX cannot overtake it or move
 * it into a new peer generation. Wake the worker after releasing the lock. */
static void owly_peer_good(void *ctx,void *packet)
{
 struct owly_peer_packet_queues *queues=ctx;
 struct sk_buff *skb=packet;
 if(!glue.up || !glue.awdl_rx_wq || !owly_peer_accepting || owly_peer_ready_count==OWLY_PEER_READY_SIZE){
  if(owly_peer_ready_count==OWLY_PEER_READY_SIZE) {
   const uint64_t drops = ++owly_peer_ready_drops;
   if(drops<=4ull || !(drops&(drops-1ull)))
    IOLog("OWLY peer RX ready drop: drops=%llu\n",(unsigned long long)drops);
  }
  list_add_tail(&skb->list,&queues->drop);
  return;
 }
 const unsigned slot=(owly_peer_ready_head+owly_peer_ready_count)%OWLY_PEER_READY_SIZE;
 owly_peer_ready[slot].skb=skb;
 owly_peer_ready[slot].generation=owly_peer_generation;
 ++owly_peer_ready_count;
 queues->scheduled_delivery=true;
}

static void owly_peer_bad(void *ctx,void *packet)
{list_add_tail(&((struct sk_buff *)packet)->list,&((struct owly_peer_packet_queues *)ctx)->drop);}
static void owly_peer_queues_init(struct owly_peer_packet_queues *q)
{
 INIT_LIST_HEAD(&q->drop);
 q->scheduled_delivery=false;
}
static void owly_peer_queues_finish(struct owly_peer_packet_queues *q)
{
 if(q->scheduled_delivery){
  q->scheduled_delivery=false;
  queue_work(glue.awdl_rx_wq,&owly_peer_delivery_work);
 }
 while(!list_empty(&q->drop)){
  struct sk_buff *skb=list_first_entry(&q->drop,struct sk_buff,list);
  list_del(&skb->list);
  kfree_skb(skb);
 }
}
static void owly_peer_delivery_run(struct work_struct *work)
{
 (void)work;
 /* Ordinary RX work: Apple's input may call back into cmd/wiphy. */
 struct {struct sk_buff *skb;bool deliver;} batch[64];
 for(;;){
  unsigned n=0;
  spin_lock(&owly_peer_lock);
  while(n<64u && owly_peer_ready_count){
   const unsigned at=owly_peer_ready_head;
   batch[n].skb=owly_peer_ready[at].skb;
   batch[n].deliver=glue.up && owly_peer_accepting && owly_peer_ready[at].generation==owly_peer_generation;
   owly_peer_ready_head=(at+1u)%OWLY_PEER_READY_SIZE;--owly_peer_ready_count;
   ++n;
  }
  spin_unlock(&owly_peer_lock);
  if(!n)break;
  for(unsigned i=0;i<n;++i){
   if(batch[i].deliver)owly_awdl_deliver_rx(batch[i].skb);
   else kfree_skb(batch[i].skb);
  }
 }
}
static void owly_peer_clear_delivery(void)
{
 struct owly_peer_packet_queues q;owly_peer_queues_init(&q);spin_lock(&owly_peer_lock);owly_peer_accepting=false;
 while(owly_peer_ready_count){owly_peer_bad(&q,owly_peer_ready[owly_peer_ready_head].skb);owly_peer_ready_head=(owly_peer_ready_head+1u)%OWLY_PEER_READY_SIZE;--owly_peer_ready_count;}
 spin_unlock(&owly_peer_lock);owly_peer_queues_finish(&q);
}
static void owly_peer_drain_rx(void)
{ /* Only after up=false, outside command/wiphy locks. */
 owly_peer_clear_delivery();cancel_work_sync(&owly_peer_delivery_work);
}
static void owly_peer_fini(void){spin_lock_destroy(&owly_peer_lock);}
static struct owly_awdl_radio_peer *owly_peer_find(const uint8_t mac[6])
{
 for(unsigned i=0;i<4;i++)if(owly_radio_peers[i].sta && !memcmp(owly_radio_peers[i].mac,mac,6))return &owly_radio_peers[i];
 return NULL;
}

/* The PCI RX descriptor reports the context selected by the hardware.
 * An AWDL data frame can hit the auxiliary VIF entry before the peer entry. */
void rtw89_awdl_note_rx_cam(const uint8_t source[6], const uint8_t destination[6],
                           uint8_t mac_id)
{
 spin_lock(&owly_peer_lock);
 struct owly_awdl_radio_peer *p = owly_peer_find(source);
 if (p && !memcmp(destination, p->own, sizeof(p->own))) {
  p->rx_cam_seen = true;
  p->rx_cam_mac_id = mac_id;
 }
 spin_unlock(&owly_peer_lock);
}

static struct ieee80211_sta *owly_peer_tx_station(const uint8_t destination[6])
{
 struct owly_awdl_radio_peer *p=owly_peer_find(destination);
 return p && !p->blocked && p->state>=IEEE80211_STA_ASSOC ? p->sta:NULL;
}
static uint8_t owly_peer_bound_width(void)
{
 if (!owly_peer_turbo || !owly_core04c_vif)
  return 20u;
#ifdef OWLY_MC_BUILT
 if (owly_mc.enabled && !owly_mc.home)
  return owly_mc_channel_width(glue_hw()->wiphy, owly_mc.channel);
#endif
 const struct cfg80211_chan_def *def = &owly_core04c_vif->bss_conf.chanreq.oper;
 if (!def->chan || def->chan->band != NL80211_BAND_5GHZ)
  return 20u;
 if (def->width == NL80211_CHAN_WIDTH_80 || def->width == NL80211_CHAN_WIDTH_160)
  return 80u;
 if (def->width == NL80211_CHAN_WIDTH_40)
  return 40u;
 return 20u;
}
static void owly_peer_tx_ba_stop(struct owly_awdl_radio_peer *p, bool destroy)
{
 struct ieee80211_hw *hw = glue_hw();
 struct rtw89_m80211_local *local = hw_to_local(hw);
 if (!p || !p->sta) return;
 if (p->tx_ba_state == OWLY_TX_BA_OPERATIONAL && local->ops->ampdu_action && owly_core04c_vif) {
  struct ieee80211_ampdu_params a = {
   .action = destroy ? IEEE80211_AMPDU_TX_STOP_FLUSH : IEEE80211_AMPDU_TX_STOP_CONT,
   .sta = p->sta, .tid = 0, .ssn = 0, .buf_size = 0, .amsdu = false
  };
  (void)local->ops->ampdu_action(hw, owly_core04c_vif, &a);
 }
 p->tx_ba_state = OWLY_TX_BA_NONE;
 p->tx_ba_buf_size = 0;
}
static void owly_peer_note_unicast_tx(const uint8_t destination[6])
{
 struct owly_awdl_radio_peer *p = owly_peer_find(destination);
 if (!p || p->blocked || p->state < IEEE80211_STA_ASSOC)
  return;
 const uint64_t now = glue_now_us();
 if (p->tx_ba_state == OWLY_TX_BA_OPERATIONAL && p->tx_last_unicast_us &&
     now > p->tx_last_unicast_us && now - p->tx_last_unicast_us > 2000000ull) {
  owly_peer_tx_ba_stop(p, false);
  p->tx_ba_attempts = 0;
 }
 p->tx_last_unicast_us = now;
 p->tx_ba_needed = true;
}
static bool owly_peer_tx_ampdu_peek(const uint8_t destination[6], uint16_t *qos_sn)
{
 struct owly_awdl_radio_peer *p = owly_peer_find(destination);
 if (!owly_peer_turbo || !p || p->blocked || p->state < IEEE80211_STA_ASSOC ||
     p->tx_ba_state != OWLY_TX_BA_OPERATIONAL)
  return false;
 if (qos_sn) *qos_sn = p->tx_qos_sn;
 return true;
}
static void owly_peer_tx_ampdu_commit(const uint8_t destination[6])
{
 struct owly_awdl_radio_peer *p = owly_peer_find(destination);
 if (p) p->tx_qos_sn = (uint16_t)((p->tx_qos_sn + 1u) & 4095u);
}
static int owly_peer_ba_firmware(struct owly_awdl_radio_peer *p,unsigned tid,bool start,uint16_t ssn,uint16_t window)
{
 struct rtw89_sta *rs=sta_to_rtwsta(p->sta);struct rtw89_sta_link *sl=rs->links[p->sta->deflink.link_id];
 struct rtw89_dev *dev=glue_hw()->priv;
 struct ieee80211_ampdu_params a={.sta=p->sta,.tid=(u16)tid,.ssn=ssn,.buf_size=window,.amsdu=false};
 struct rtw89_ba_cam_entry *entry;bool found=false;int rc;lockdep_assert_wiphy(glue_hw()->wiphy);
 if(!sl || tid>=16 || dev->chip->chip_id!=RTL8852B)return -EOPNOTSUPP;
 /* Preserve H2C errors. Upstream intentionally returns success when static
  * BA CAM is full: RTL8852B can create dynamic entries automatically.
  * A missing static entry is only an error without that hardware fallback. */
 rc=rtw89_fw_h2c_ba_cam(dev,sl->rtwvif_link,sl,start,&a);
 if(!rc && start){list_for_each_entry(entry,&sl->ba_cam_list,list)if(entry->tid==tid){found=true;break;}if(!found && !dev->chip->bacam_dynamic_num)rc=-ENOSPC;}
 if(rc && start){const int undo=rtw89_fw_h2c_ba_cam(dev,sl->rtwvif_link,sl,false,&a);if(undo)p->blocked=true;}
 if(rc && !start)p->blocked=true;
 if(!rc)rtw89_core_tid_rx_stats_ctrl(dev,rs,&a,start);
 return rc;
}
static void owly_peer_drop(struct owly_awdl_radio_peer *p)
{
 struct ieee80211_hw *hw=glue_hw();struct rtw89_m80211_local *local=hw_to_local(hw);
 struct owly_peer_packet_queues q;owly_peer_queues_init(&q);if(!p->sta)return;
 rtw89_m80211_sta_set_uploaded(p->sta,false);spin_lock(&owly_peer_lock);
 const uint16_t active=p->ba_map;p->ba_map=0;
 for(unsigned t=0;t<16;t++)owly_ba_clear(&p->reorder[t],owly_peer_bad,&q);
 spin_unlock(&owly_peer_lock);owly_peer_queues_finish(&q);
 for(unsigned t=0;t<16;t++)if(active&(1u<<t))if(owly_peer_ba_firmware(p,t,false,0,0))owly_peer_fault=true;
 owly_peer_tx_ba_stop(p, true);
 rtw89_m80211_sta_purge_txqs(p->sta);
 if(local->ops->flush)local->ops->flush(hw,owly_core04c_vif,BIT(hw->queues)-1,false);
 while(p->state>IEEE80211_STA_NOTEXIST){
  enum ieee80211_sta_state next=(enum ieee80211_sta_state)(p->state-1);
  const int rc=local->ops->sta_state(hw,owly_core04c_vif,p->sta,p->state,next);
  if(rc){owly_peer_fault=true;IOLog("OWLY radio peer remove: state=%u rc=%d\n",(unsigned)p->state,rc);}p->state=next;
 }
 spin_lock(&owly_peer_lock);struct ieee80211_sta *sta=p->sta;memset(p,0,sizeof(*p));spin_unlock(&owly_peer_lock);
 rtw89_m80211_sta_free(sta);
}
static uint8_t owly_vht_map_nss(uint16_t map, uint8_t max_nss)
{
 uint8_t nss = 0;
 for (uint8_t s = 0; s < max_nss && s < 8u; ++s) {
  if (((map >> (s * 2u)) & 3u) != 3u)
   nss = (uint8_t)(s + 1u);
 }
 return nss ? nss : 1u;
}
static uint16_t owly_vht_mask_mcs_map(uint16_t peer_rx, uint16_t local_tx, uint8_t max_nss)
{
 uint16_t out = 0xffffu;
 for (unsigned s = 0; s < 8u; ++s) {
  if (s >= max_nss) continue;
  const uint16_t p = (uint16_t)((peer_rx >> (s * 2u)) & 3u);
  const uint16_t l = (uint16_t)((local_tx >> (s * 2u)) & 3u);
  if (p == 3u || l == 3u) continue;
  const uint16_t m = p < l ? p : l;
  out = (uint16_t)((out & ~(3u << (s * 2u))) | (m << (s * 2u)));
 }
 return out;
}
static void owly_peer_apply_rates(struct owly_awdl_radio_peer *p,
                                  struct ieee80211_sta *sta,
                                  const struct owly_peer_caps *caps,
                                  struct ieee80211_channel *ch)
{
 struct ieee80211_hw *hw = glue_hw();
 struct rtw89_dev *rtwdev = hw->priv;
 const struct ieee80211_supported_band *band = ch ? hw->wiphy->bands[ch->band] : NULL;
 const uint8_t local_tx_nss = rtwdev->hal.tx_nss >= 2u ? 2u : 1u;
 const uint8_t bound_bw = owly_peer_bound_width();

 memset(&sta->deflink.ht_cap, 0, sizeof(sta->deflink.ht_cap));
 memset(&sta->deflink.vht_cap, 0, sizeof(sta->deflink.vht_cap));

 if (!owly_peer_turbo || !band || !band->ht_cap.ht_supported) {
  const uint8_t mcs0 = (uint8_t)(caps->ht_rx_mcs &
      (band ? band->ht_cap.mcs.rx_mask[0] : 0xffu));
  sta->deflink.bandwidth = IEEE80211_STA_RX_BW_20;
  sta->deflink.rx_nss = 1;
  sta->deflink.ht_cap.ht_supported = mcs0 != 0;
  sta->deflink.ht_cap.mcs.rx_mask[0] = mcs0;
  sta->deflink.ht_cap.ampdu_factor = caps->ht_ampdu_params & 3u;
  sta->deflink.ht_cap.ampdu_density = (caps->ht_ampdu_params >> 2) & 7u;
  p->applied_bw = 20u; p->applied_nss = 1u; p->applied_vht = false;
  p->applied_turbo = owly_peer_turbo;
  p->applied_ht_mcs = mcs0; p->applied_ht_cap = 0;
  p->applied_vht_cap = 0; p->applied_vht_mcs = 0xffffu;
  return;
 }

 /* Intersect HT capabilities with local band (up to 2 streams). */
 const uint8_t mcs0 = (uint8_t)(caps->ht_rx_mcs & band->ht_cap.mcs.rx_mask[0]);
 const uint8_t mcs1 = (local_tx_nss >= 2u && mcs0) ?
     (uint8_t)((caps->ht_rx_mcs >> 8) & band->ht_cap.mcs.rx_mask[1]) : 0u;
 uint8_t peer_bw = caps->max_channel_width ? caps->max_channel_width : 20u;
 uint8_t eff_bw = bound_bw < peer_bw ? bound_bw : peer_bw;
 if (ch->band != NL80211_BAND_5GHZ) eff_bw = 20u;

 const bool allow_vht = (ch->band == NL80211_BAND_5GHZ) &&
     band->vht_cap.vht_supported && caps->has_vht_cap && mcs0;
 uint16_t vht_rx_map = 0xffffu;
 uint32_t vht_cap = 0;
 if (allow_vht) {
  vht_rx_map = owly_vht_mask_mcs_map(caps->vht_rx_mcs_map,
                                     le16_to_cpu(band->vht_cap.vht_mcs.tx_mcs_map),
                                     local_tx_nss);
  if (vht_rx_map != 0xffffu) {
   const u32 local_vcap = band->vht_cap.cap;
   if ((caps->vht_cap_info & IEEE80211_VHT_CAP_RXLDPC) && (local_vcap & IEEE80211_VHT_CAP_RXLDPC))
    vht_cap |= IEEE80211_VHT_CAP_RXLDPC;
   if ((caps->vht_cap_info & IEEE80211_VHT_CAP_SHORT_GI_80) && (local_vcap & IEEE80211_VHT_CAP_SHORT_GI_80))
    vht_cap |= IEEE80211_VHT_CAP_SHORT_GI_80;
   if ((caps->vht_cap_info & IEEE80211_VHT_CAP_RXSTBC_MASK) && (local_vcap & IEEE80211_VHT_CAP_TXSTBC))
    vht_cap |= (caps->vht_cap_info & IEEE80211_VHT_CAP_RXSTBC_MASK);
   vht_cap |= (caps->vht_cap_info & IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK);
   sta->deflink.vht_cap.vht_supported = true;
   sta->deflink.vht_cap.cap = vht_cap;
   sta->deflink.vht_cap.vht_mcs.rx_mcs_map = cpu_to_le16(vht_rx_map);
   sta->deflink.vht_cap.vht_mcs.tx_mcs_map = cpu_to_le16(vht_rx_map);
  }
 }

 if (!sta->deflink.vht_cap.vht_supported && eff_bw > 40u)
  eff_bw = 40u;
 if (eff_bw == 40u &&
     !((caps->ht_cap_info & IEEE80211_HT_CAP_SUP_WIDTH_20_40) &&
       (band->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40)))
  eff_bw = 20u;

 uint16_t ht_cap = 0;
 const u16 local_hcap = band->ht_cap.cap;
 if ((caps->ht_cap_info & IEEE80211_HT_CAP_LDPC_CODING) && (local_hcap & IEEE80211_HT_CAP_LDPC_CODING))
  ht_cap |= IEEE80211_HT_CAP_LDPC_CODING;
 if (eff_bw >= 40u && (caps->ht_cap_info & IEEE80211_HT_CAP_SUP_WIDTH_20_40) &&
     (local_hcap & IEEE80211_HT_CAP_SUP_WIDTH_20_40))
  ht_cap |= IEEE80211_HT_CAP_SUP_WIDTH_20_40;
 if ((caps->ht_cap_info & IEEE80211_HT_CAP_SGI_20) && (local_hcap & IEEE80211_HT_CAP_SGI_20))
  ht_cap |= IEEE80211_HT_CAP_SGI_20;
 if (eff_bw >= 40u && (caps->ht_cap_info & IEEE80211_HT_CAP_SGI_40) &&
     (local_hcap & IEEE80211_HT_CAP_SGI_40))
  ht_cap |= IEEE80211_HT_CAP_SGI_40;
 if ((caps->ht_cap_info & IEEE80211_HT_CAP_RX_STBC) && (local_hcap & IEEE80211_HT_CAP_TX_STBC))
  ht_cap |= (caps->ht_cap_info & IEEE80211_HT_CAP_RX_STBC);

 sta->deflink.ht_cap.ht_supported = mcs0 != 0;
 sta->deflink.ht_cap.cap = ht_cap;
 sta->deflink.ht_cap.mcs.rx_mask[0] = mcs0;
 sta->deflink.ht_cap.mcs.rx_mask[1] = mcs1;
 sta->deflink.ht_cap.ampdu_factor = caps->ht_ampdu_params & 3u;
 sta->deflink.ht_cap.ampdu_density = (caps->ht_ampdu_params >> 2) & 7u;

 sta->deflink.bandwidth = eff_bw >= 80u ? IEEE80211_STA_RX_BW_80 :
                          eff_bw >= 40u ? IEEE80211_STA_RX_BW_40 :
                                          IEEE80211_STA_RX_BW_20;
 const uint8_t nss = sta->deflink.vht_cap.vht_supported ?
     owly_vht_map_nss(vht_rx_map, local_tx_nss) : (mcs1 ? 2u : 1u);
 sta->deflink.rx_nss = nss;

 p->applied_bw = eff_bw;
 p->applied_nss = nss;
 p->applied_vht = sta->deflink.vht_cap.vht_supported;
 p->applied_turbo = owly_peer_turbo;
 p->applied_ht_mcs = (uint16_t)(mcs0 | ((uint16_t)mcs1 << 8));
 p->applied_ht_cap = ht_cap;
 p->applied_vht_cap = vht_cap;
 p->applied_vht_mcs = vht_rx_map;
}

static uint8_t owly_peer_rate_rssi(int8_t signal)
{
    if (!owly_peer_use_action_rssi || signal >= 0 || signal <= -MAX_RSSI)
        return 0;
    return (uint8_t)((MAX_RSSI + signal) << RSSI_FACTOR);
}

static void owly_peer_refresh_rates(struct owly_awdl_radio_peer *p,
                                    const struct owly_peer_caps *caps,
                                    int8_t signal, struct ieee80211_channel *ch)
{
 if(p->blocked || p->state<IEEE80211_STA_ASSOC)return;
 struct rtw89_sta_link *link=sta_to_rtwsta(p->sta)->links[p->sta->deflink.link_id];
 if(!link)return;
 WRITE_ONCE(link->owly_awdl_action_rssi,owly_peer_rate_rssi(signal));
 if (!owly_peer_turbo && p->tx_ba_state != OWLY_TX_BA_NONE)
  owly_peer_tx_ba_stop(p, false);
 const uint8_t prev_bw = p->applied_bw, prev_nss = p->applied_nss;
 const bool prev_vht = p->applied_vht, prev_turbo = p->applied_turbo;
 const uint16_t prev_ht_mcs = p->applied_ht_mcs, prev_ht_cap = p->applied_ht_cap;
 const uint16_t prev_vht_mcs = p->applied_vht_mcs;
 const uint32_t prev_vht_cap = p->applied_vht_cap;
 owly_peer_apply_rates(p, p->sta, caps, ch);
 if (prev_bw == p->applied_bw && prev_nss == p->applied_nss &&
     prev_vht == p->applied_vht && prev_turbo == p->applied_turbo &&
     prev_ht_mcs == p->applied_ht_mcs && prev_ht_cap == p->applied_ht_cap &&
     prev_vht_mcs == p->applied_vht_mcs && prev_vht_cap == p->applied_vht_cap)
  return;
 rtw89_phy_ra_assoc(glue_hw()->priv,link);
 IOLog("RTLAirLink AWDL peer rate update: %s bw=%u nss=%u ht_mcs=0x%04x vht_mcs=0x%04x sent=%d\n",
       p->applied_vht ? "VHT" : "HT", (unsigned)p->applied_bw, (unsigned)p->applied_nss,
       (unsigned)p->applied_ht_mcs, (unsigned)p->applied_vht_mcs, link->ra_configured);
}

static int owly_peer_open(struct owly_awdl_radio_peer *p,const uint8_t mac[6],
                          const struct owly_peer_caps *caps,int8_t signal)
{
 struct ieee80211_hw *hw=glue_hw();struct rtw89_m80211_local *local=hw_to_local(hw);
 struct ieee80211_sta *sta=rtw89_m80211_sta_alloc(owly_core04c_vif,mac);if(!sta)return -ENOMEM;
 /* Radio context readiness only. Never ieee80211_sta.tdls or an AP join. */
 sta_to_rtwsta(sta)->owly_awdl=true;sta->wme=true;
 sta->max_rx_aggregation_subframes=64;struct ieee80211_channel *ch=owly_sync_bound_channel();
 owly_peer_apply_rates(p, sta, caps, ch);
 if(ch){
  struct ieee80211_supported_band *band=hw->wiphy->bands[ch->band];
  /* Use only the mandatory OFDM rates for the AWDL peer's rate table. */
  for(unsigned i=0;band && i<band->n_bitrates && i<32;i++){
   const unsigned bitrate=band->bitrates[i].bitrate;
   if(bitrate==60 || bitrate==120 || bitrate==240)
    sta->deflink.supp_rates[ch->band]|=BIT(i);
  }
 }
 spin_lock(&owly_peer_lock);memcpy(p->mac,mac,6);memcpy(p->own,owly_core04c_vif->addr,6);
 p->state=IEEE80211_STA_NOTEXIST;p->sta=sta;spin_unlock(&owly_peer_lock);
 while(p->state<IEEE80211_STA_ASSOC){
  enum ieee80211_sta_state next=(enum ieee80211_sta_state)(p->state+1);
  const int rc=local->ops->sta_state(hw,owly_core04c_vif,sta,p->state,next);
  if(rc){
   owly_peer_fault=true;
   if(p->state==IEEE80211_STA_AUTH){const int undo=local->ops->sta_state(hw,owly_core04c_vif,sta,IEEE80211_STA_AUTH,IEEE80211_STA_NONE);if(undo)IOLog("OWLY radio peer rollback refused: rc=%d\n",undo);p->state=IEEE80211_STA_NONE;}
   owly_peer_drop(p);return rc;
  }
  /* The link exists before the AUTH -> ASSOC transition programs its
   * first rate table. */
  if(next==IEEE80211_STA_AUTH){
   struct rtw89_sta_link *link=sta_to_rtwsta(sta)->links[sta->deflink.link_id];
   if(link)WRITE_ONCE(link->owly_awdl_action_rssi,owly_peer_rate_rssi(signal));
  }
  p->state=next;
 }
 rtw89_m80211_sta_set_uploaded(sta,true);struct rtw89_sta_link *sl=sta_to_rtwsta(sta)->links[sta->deflink.link_id];
 IOLog("RTLAirLink AWDL peer rate: %s bw=%u nss=%u ht_mcs=0x%04x vht_mcs=0x%04x sent=%d action_rssi=%u\n",
       p->applied_vht ? "VHT" : "HT", (unsigned)p->applied_bw, (unsigned)p->applied_nss,
       (unsigned)p->applied_ht_mcs, (unsigned)p->applied_vht_mcs,
       sl->ra_configured,(unsigned)sl->owly_awdl_action_rssi);
 IOLog("OWLY radio peer ready: peer=%02x:%02x:%02x:%02x:%02x:%02x macid=%u cam=%u (not IP/TLS proof)\n",
  mac[0],mac[1],mac[2],mac[3],mac[4],mac[5],(unsigned)sl->mac_id,(unsigned)sl->addr_cam.addr_cam_idx);return 0;
}
static void owly_peer_close_all(void)
{
 lockdep_assert_wiphy(glue_hw()->wiphy);owly_peer_clear_delivery();
 spin_lock(&owly_peer_lock);owly_control_head=owly_control_count=0;spin_unlock(&owly_peer_lock);
 for(unsigned i=0;i<4;i++)owly_peer_drop(&owly_radio_peers[i]);
 owly_peer_check_us=0;spin_lock(&owly_peer_lock);owly_peer_generation=0;spin_unlock(&owly_peer_lock);
}
static bool owly_peer_known_locked(const uint8_t mac[6],uint64_t now)
{
 if(!owly_link_state.active)return false;
 for(unsigned i=0;i<OWLY_LINK_PEERS;i++)if(!memcmp(owly_link_state.peers[i].mac,mac,6) && owly_link_peer_alive(&owly_link_state.peers[i],now))return true;
 return false;
}
static bool owly_peer_reply_due(const struct owly_awdl_control *c)
{
 spin_lock(&owly_link_lock);
 const uint64_t now=glue_now_us();
 const bool ready=c->generation==owly_link_state.generation && now>=c->received && now-c->received<=OWLY_LINK_PEER_TTL_US &&
  owly_peer_known_locked(c->bytes+10,now) && owly_link_unicast_reply_tx_ready(&owly_link_state,c->bytes+10,now);
 spin_unlock(&owly_link_lock);return ready;
}
static bool owly_peer_control_rx(struct sk_buff *skb)
{
 uint8_t own[6];uint64_t generation;bool valid;struct owly_ba_request request;
 const size_t n=skb->len-FCS_LEN;const uint8_t *raw=skb->data;
 if(n<26 || raw[0]!=0xd0 || raw[24]!=3)return false;
 spin_lock(&owly_link_lock);
 const uint64_t now=glue_now_us();
 memcpy(own,owly_link_state.local,6);generation=owly_link_state.generation;
 valid=owly_peer_known_locked(raw+10,now);spin_unlock(&owly_link_lock);
 if(!valid || !owly_ba_parse(raw,n,own,&request))return false;
 spin_lock(&owly_peer_lock);
 if(owly_control_count<8){struct owly_awdl_control *c=&owly_controls[(owly_control_head+owly_control_count)%8];
  c->length=n<64?n:64;memcpy(c->bytes,raw,c->length);c->received=now;c->generation=generation;++owly_control_count;}
 spin_unlock(&owly_peer_lock);wiphy_delayed_work_queue(glue_hw()->wiphy,&owly_sync_work,0);kfree_skb(skb);return true;
}
static bool owly_peer_data_rx(struct sk_buff *skb)
{
 const uint8_t *raw=skb->data;const size_t n=skb->len-FCS_LEN;
 if(n<26 || raw[0]!=0x88 || (raw[1]&0x47) || (raw[4]&1u) || (owly_ba_le16(raw+22)&15u))return false;
 if((raw[24]&IEEE80211_QOS_CTL_ACK_POLICY_MASK)==IEEE80211_QOS_CTL_ACK_POLICY_NOACK)return false;
 static const uint8_t b[6]={0,0x25,0,0xff,0x94,0x73};if(memcmp(raw+16,b,6))return false;
 struct owly_peer_packet_queues q;owly_peer_queues_init(&q);bool consumed=false;spin_lock(&owly_peer_lock);
 struct owly_awdl_radio_peer *p=owly_peer_find(raw+10);const unsigned tid=raw[24]&15u;
 if(p && !memcmp(raw+4,p->own,6) && (p->ba_map&(1u<<tid))){
  owly_ba_push(&p->reorder[tid],owly_ba_le16(raw+22)>>4,skb,glue_now_us(),owly_peer_good,owly_peer_bad,&q);consumed=true;}
 spin_unlock(&owly_peer_lock);owly_peer_queues_finish(&q);return consumed;
}
static bool owly_peer_bar_rx(struct sk_buff *skb)
{
 const uint8_t *raw=skb->data;const size_t n=skb->len-FCS_LEN;if(n<20 || raw[0]!=0x84 || (raw[1]&0x47))return false;
 struct owly_peer_packet_queues q;owly_peer_queues_init(&q);bool consumed=false;spin_lock(&owly_peer_lock);
 struct owly_awdl_radio_peer *p=owly_peer_find(raw+10);const uint16_t control=owly_ba_le16(raw+16);const unsigned tid=control>>12;
 if(p && (p->ba_map&(1u<<tid)) && !(control&2u) && !memcmp(raw+4,p->own,6)){
  owly_ba_bar(&p->reorder[tid],owly_ba_le16(raw+18)>>4,owly_peer_good,&q);consumed=true;}
 spin_unlock(&owly_peer_lock);owly_peer_queues_finish(&q);if(consumed)kfree_skb(skb);
 return consumed;
}
static void owly_peer_tick(struct ieee80211_channel *ch)
{
 lockdep_assert_wiphy(glue_hw()->wiphy);if(!ch || glue.scanning || !owly_core04c_vif || !glue.awdl_rx_wq)return;
 struct {uint8_t mac[6];struct owly_peer_caps caps;int8_t action_rssi;} observed[OWLY_LINK_PEERS];
 uint8_t own[6];unsigned count=0;uint64_t generation;
 spin_lock(&owly_link_lock);
 const uint64_t now=glue_now_us();
 generation=owly_link_state.generation;memcpy(own,owly_link_state.local,6);
 if(owly_link_state.active)for(unsigned i=0;i<OWLY_LINK_PEERS;i++){
  const struct owly_link_peer *p=&owly_link_state.peers[i];
  if(!owly_link_peer_alive(p,now))continue;
  memcpy(observed[count].mac,p->mac,6);
  observed[count].caps.ht_rx_mcs=p->ht_rx_mcs;
  observed[count].caps.ht_cap_info=p->ht_cap_info;
  observed[count].caps.ht_ampdu_params=p->ht_ampdu_params;
  observed[count].caps.max_channel_width=p->max_channel_width;
  observed[count].caps.has_vht_cap=p->has_vht_cap;
  observed[count].caps.vht_cap_info=p->vht_cap_info;
  observed[count].caps.vht_rx_mcs_map=p->vht_rx_mcs_map;
  observed[count++].action_rssi=owly_link_peer_action_rssi(p,now);
 }
 spin_unlock(&owly_link_lock);
 if(generation!=owly_peer_generation){owly_peer_close_all();spin_lock(&owly_peer_lock);owly_peer_generation=generation;spin_unlock(&owly_peer_lock);}
 spin_lock(&owly_peer_lock);owly_peer_accepting=true;const bool control_pending=owly_control_count!=0;spin_unlock(&owly_peer_lock);
 if(control_pending || now<owly_peer_check_us || now-owly_peer_check_us>=200000u){
  owly_peer_check_us=now;
  for(unsigned i=0;i<4;i++)if(owly_radio_peers[i].sta){bool found=false;
   for(unsigned j=0;j<count;j++)if(!memcmp(observed[j].mac,owly_radio_peers[i].mac,6))found=true;
   if(!found)owly_peer_drop(&owly_radio_peers[i]);}
  if(!owly_peer_fault)for(unsigned j=0;j<count;j++){
   struct owly_awdl_radio_peer *p=owly_peer_find(observed[j].mac);
   if(p){
    owly_peer_refresh_rates(p,&observed[j].caps,observed[j].action_rssi,ch);
    continue;
   }
   for(unsigned i=0;i<OWLY_RADIO_PEERS;i++)if(!owly_radio_peers[i].sta){
    const int rc=owly_peer_open(&owly_radio_peers[i],observed[j].mac,&observed[j].caps,observed[j].action_rssi);
    if(rc)IOLog("OWLY radio peer create refused: rc=%d\n",rc);
    break;
   }
  }
 }
 /* Drive TX ADDBA (TID 0) negotiation for active peers when turbo is enabled. */
 for (unsigned i = 0; i < OWLY_RADIO_PEERS; ++i) {
  struct owly_awdl_radio_peer *p = &owly_radio_peers[i];
  if (!p->sta || p->blocked || p->state < IEEE80211_STA_ASSOC) continue;
  if (!owly_peer_turbo) {
   if (p->tx_ba_state != OWLY_TX_BA_NONE) owly_peer_tx_ba_stop(p, false);
   continue;
  }
  if (p->tx_ba_state == OWLY_TX_BA_WAIT && now >= p->tx_ba_sent_us &&
      now - p->tx_ba_sent_us >= OWLY_TX_BA_TIMEOUT_US) {
   p->tx_ba_state = p->tx_ba_attempts < OWLY_TX_BA_MAX_TRIES ? OWLY_TX_BA_NONE : OWLY_TX_BA_DECLINED;
  }
  if (p->tx_ba_needed && p->tx_ba_state == OWLY_TX_BA_NONE &&
      p->sta->deflink.ht_cap.ht_supported && p->tx_ba_attempts < OWLY_TX_BA_MAX_TRIES) {
   spin_lock(&owly_link_lock);
   const uint64_t check_us = glue_now_us();
   const bool ready = owly_peer_known_locked(p->mac, check_us) &&
       owly_link_unicast_reply_tx_ready(&owly_link_state, p->mac, check_us);
   spin_unlock(&owly_link_lock);
   if (ready) {
    uint8_t req[33];
    if (!++owly_tx_ba_dialog) owly_tx_ba_dialog = 1;
    p->tx_ba_token = owly_tx_ba_dialog;
    owly_ba_request_frame(p->mac, own, p->tx_ba_token, 0u, OWLY_BA_WINDOW,
                          p->tx_qos_sn, owly_control_sequence++, req);
    const int tx = owly_sync_submit(ch, req, sizeof(req), false);
    if (!tx) {
     p->tx_ba_state = OWLY_TX_BA_WAIT;
     p->tx_ba_sent_us = check_us;
     ++p->tx_ba_attempts;
     IOLog("RTLAirLink AWDL TX ADDBA request: peer=%02x:%02x:%02x:%02x:%02x:%02x token=%u ssn=%u try=%u\n",
           p->mac[0], p->mac[1], p->mac[2], p->mac[3], p->mac[4], p->mac[5],
           (unsigned)p->tx_ba_token, (unsigned)p->tx_qos_sn, (unsigned)p->tx_ba_attempts);
    }
   }
  }
 }
 struct owly_peer_packet_queues q;owly_peer_queues_init(&q);spin_lock(&owly_peer_lock);
 for(unsigned i=0;i<4;i++)for(unsigned t=0;t<16;t++)if(owly_radio_peers[i].ba_map&(1u<<t))owly_ba_timeout(&owly_radio_peers[i].reorder[t],now,owly_peer_good,&q);
 spin_unlock(&owly_peer_lock);owly_peer_queues_finish(&q);
 for(unsigned done=0;done<8;done++){
  struct owly_awdl_control c;bool pending;spin_lock(&owly_peer_lock);pending=owly_control_count!=0;
  if(pending)c=owly_controls[owly_control_head];
  spin_unlock(&owly_peer_lock);if(!pending)break;
  spin_lock(&owly_link_lock);
  const uint64_t check_us=glue_now_us();
  const bool known=owly_peer_known_locked(c.bytes+10,check_us);
  const bool due=known && owly_link_unicast_reply_tx_ready(&owly_link_state,c.bytes+10,check_us);
  spin_unlock(&owly_link_lock);
  const bool stale=c.generation!=generation || check_us<c.received ||
    check_us-c.received>OWLY_LINK_PEER_TTL_US || !known;
  if(!stale && !due)break;
  struct owly_ba_request r;uint8_t response[33];int rc=0;uint16_t status=37;struct owly_awdl_radio_peer *p=owly_peer_find(c.bytes+10);
  if(!stale && owly_ba_parse(c.bytes,c.length,own,&r) && p && !p->blocked){
   if(r.action==0){
    const unsigned tid = r.tid;
    const uint16_t tid_bit = (uint16_t)(1u << tid);
    const struct owly_ba_request *old = &p->requests[tid];
    const struct rtw89_sta_link *sta_link = sta_to_rtwsta(p->sta)->links[p->sta->deflink.link_id];
    const int sta_mac_id = sta_link ? sta_link->mac_id : -1;

    spin_lock(&owly_peer_lock);
    const int rx_mac_id = p->rx_cam_seen ? p->rx_cam_mac_id : -1;
    const bool active = (p->ba_map & tid_bit) != 0;
    const bool matching_cam = rx_mac_id >= 0 && rx_mac_id == sta_mac_id;
    const bool duplicate = active && old->token == r.token && old->ssn == r.ssn &&
        old->window == r.window && old->timeout == r.timeout;
    const bool replace = r.immediate && !(duplicate && matching_cam);
    if (replace) {
     p->ba_map &= (uint16_t)~tid_bit;
     while (p->reorder[tid].active && p->reorder[tid].stored)
      owly_ba_advance(&p->reorder[tid], owly_peer_good, &q);
     owly_ba_clear(&p->reorder[tid], owly_peer_bad, &q);
    }
    spin_unlock(&owly_peer_lock);
    owly_peer_queues_finish(&q);

    if (!r.immediate) {
     status = 38;
    } else if (!replace) {
     status = 0;
    } else {
     if (active)
      rc = owly_peer_ba_firmware(p, tid, false, 0, 0);
     /* A BA CAM keyed to a different MAC ID cannot acknowledge this RX
      * stream. Decline ADDBA so the peer can continue with ordinary ACKs. */
     if (!rc && matching_cam) {
      rc = owly_peer_ba_firmware(p, tid, true, r.ssn, r.window);
      if (!rc) {
       spin_lock(&owly_peer_lock);
       owly_ba_start(&p->reorder[tid], r.ssn, r.window);
       p->requests[tid] = r;
       p->ba_map |= tid_bit;
       spin_unlock(&owly_peer_lock);
       status = 0;
       if (tid == 0u && p->tx_ba_state == OWLY_TX_BA_OPERATIONAL &&
           p->tx_ba_sent_us && check_us > p->tx_ba_sent_us &&
           check_us - p->tx_ba_sent_us > 1000000ull) {
        owly_peer_tx_ba_stop(p, false);
        p->tx_ba_attempts = 0;
       }
      }
     }
    }
    if(!owly_peer_reply_due(&c))break; /* H2C may cross the end of a shared AW. */
    owly_ba_response(c.bytes,own,&r,status,owly_control_sequence++,response);const int tx=owly_sync_submit(ch,response,33,false);
    IOLog("OWLY AWDL BlockAck: tid=%u token=%u ssn=%u window=%u status=%u firmware=%d tx=%d "
          "rx_mac_id=%d sta_mac_id=%d delay_us=%llu\n",
     (unsigned)r.tid,(unsigned)r.token,(unsigned)r.ssn,(unsigned)r.window,(unsigned)status,rc,tx,
     rx_mac_id,sta_mac_id,
     (unsigned long long)(glue_now_us()-c.received));
    if(tx==-ENOMEM || tx==-ENOSPC || tx==-EBUSY)break;
   }else if(r.action==1){
    if(r.tid==0 && p->tx_ba_state==OWLY_TX_BA_WAIT && r.token==p->tx_ba_token){
     if(r.status==0 && r.immediate && r.window>0 && owly_peer_turbo){
      struct ieee80211_hw *hw=glue_hw();
      struct rtw89_m80211_local *local=hw_to_local(hw);
      const uint16_t win=r.window<OWLY_BA_WINDOW ? r.window : OWLY_BA_WINDOW;
      struct ieee80211_ampdu_params a={
       .action=IEEE80211_AMPDU_TX_OPERATIONAL,.sta=p->sta,.tid=0,
       .ssn=p->tx_qos_sn,.buf_size=win,.amsdu=false
      };
      const int ampdu_rc=local->ops->ampdu_action ?
          local->ops->ampdu_action(hw,owly_core04c_vif,&a) : -EOPNOTSUPP;
      if(!ampdu_rc){
       p->tx_ba_state=OWLY_TX_BA_OPERATIONAL;
       p->tx_ba_buf_size=win;
       p->tx_ba_attempts=0;
       IOLog("RTLAirLink AWDL TX BlockAck operational: peer=%02x:%02x:%02x:%02x:%02x:%02x tid=0 ssn=%u window=%u\n",
             p->mac[0],p->mac[1],p->mac[2],p->mac[3],p->mac[4],p->mac[5],
             (unsigned)p->tx_qos_sn,(unsigned)win);
      }else{
       p->tx_ba_state=OWLY_TX_BA_DECLINED;
       IOLog("RTLAirLink AWDL TX BlockAck ampdu_action failed: rc=%d\n",ampdu_rc);
      }
     }else{
      p->tx_ba_state=OWLY_TX_BA_DECLINED;
      IOLog("RTLAirLink AWDL TX BlockAck declined: status=%u imm=%u win=%u\n",
            (unsigned)r.status,(unsigned)r.immediate,(unsigned)r.window);
     }
    }
   }else if(r.action==2 && r.initiator){
    const unsigned tid=r.tid;spin_lock(&owly_peer_lock);const bool active=p->ba_map&(1u<<tid);p->ba_map&=~(1u<<tid);
    while(p->reorder[tid].active && p->reorder[tid].stored)owly_ba_advance(&p->reorder[tid],owly_peer_good,&q);
    owly_ba_clear(&p->reorder[tid],owly_peer_bad,&q);spin_unlock(&owly_peer_lock);owly_peer_queues_finish(&q);
    if(active)(void)owly_peer_ba_firmware(p,tid,false,0,0);
   }else if(r.action==2 && !r.initiator && r.tid==0){
    owly_peer_tx_ba_stop(p,false);
    IOLog("RTLAirLink AWDL TX BlockAck deleted by peer\n");
   }
  }
  spin_lock(&owly_peer_lock);owly_control_head=(owly_control_head+1u)%8;--owly_control_count;spin_unlock(&owly_peer_lock);
 }
}
