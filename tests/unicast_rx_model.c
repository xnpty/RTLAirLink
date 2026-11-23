#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef uint32_t __le32;
#define BIT(n) (1u << (n))
#define GENMASK(high, low) ((UINT32_MAX << (low)) & (UINT32_MAX >> (31u - (high))))
#define FIELD_PREP(mask, value) (((u32)(value) << __builtin_ctz(mask)) & (mask))
#define FIELD_GET(mask, value) (((u32)(value) & (mask)) >> __builtin_ctz(mask))
#define cpu_to_le32(value) (value)
#define ETH_ALEN 6
#define RTW89_MAC_0 0
#define RTW89_MAC_1 1
#define RTL8852B 1
#define NL80211_IFTYPE_STATION 2
#define NL80211_IFTYPE_P2P_CLIENT 8
#define RTW89_VIF_IDLE_LINK_ID 0
#define RTW89_ROLE_INFO_CHANGE 3
#define IEEE80211_CONF_MONITOR BIT(10)
#define IEEE80211_FTYPE_DATA 0x08
#define IEEE80211_STYPE_QOS_DATA 0x80
#define IEEE80211_TX_INTFL_DONT_ENCRYPT BIT(16)
#define IEEE80211_TX_CTL_REQ_TX_STATUS BIT(0)
#define IEEE80211_TX_CTL_AMPDU BIT(1)
#define IEEE80211_TX_CTL_NO_ACK BIT(2)
#define IEEE80211_TX_CTL_INJECTED BIT(3)
static inline bool test_bit(unsigned bit, const unsigned long *map) { return ((*map >> bit) & 1ul) != 0; }
#define RTW89_CORE_TX_TYPE_DATA 1
#define RTW89_RX_TYPE_DATA 2
#define RTW89_CMAC_SEL 1
#define RTW89_MGNT 0
#define RTW89_DATA 2
#define RTW89_FWD_TO_HOST 1
#define R_AX_PLCP_HDR_FLTR 0xce08
#define B_AX_CCK_CRC_CHK BIT(0)
#define B_AX_CCK_SIG_CHK BIT(1)
#define B_AX_LSIG_PARITY_CHK_EN BIT(2)
#define B_AX_SIGA_CRC_CHK BIT(3)
#define B_AX_VHT_SU_SIGB_CRC_CHK BIT(4)
#define B_AX_VHT_MU_SIGB_CRC_CHK BIT(5)
#define B_AX_HE_SIGB_CRC_CHK BIT(6)
#define IOLog(...) ((void)sizeof(printf(__VA_ARGS__)))

struct wiphy { unsigned interface_modes; };
struct ieee80211_hw {
    void *priv;
    struct wiphy *wiphy;
    struct { unsigned flags; } conf;
};

struct rtw89_mac_gen_def { u32 rx_fltr; };
struct chip_info { unsigned chip_id; const struct rtw89_mac_gen_def *mac_def; };
struct rtw89_dev {
    struct { u32 rx_fltr; bool owly_awdl_scc_rx; } hal;
    struct ieee80211_hw *hw;
    const struct chip_info *chip;
    bool dbcc_en, scanning;
};
struct rtw89_chan { unsigned band_width; };
struct rtw89_bssid_cam_entry { bool valid; u8 bssid_cam_idx, bssid[ETH_ALEN]; };
struct rtw89_vif_link {
    u8 mac_addr[ETH_ALEN], bssid[ETH_ALEN], port, chanctx_idx;
    struct { bool valid; u8 addr_cam_idx; } addr_cam;
    struct rtw89_bssid_cam_entry bssid_cam;
};
struct rtw89_vif { struct rtw89_vif_link *links[1]; };
struct ieee80211_vif {
    struct rtw89_vif priv;
    struct { const u8 *bssid; } bss_conf;
    int type;
    bool p2p;
    u8 addr[ETH_ALEN];
};
struct ieee80211_link_sta {
    struct { u8 ampdu_factor, ampdu_density; } ht_cap;
};
struct ieee80211_sta { struct ieee80211_link_sta deflink; };
struct rtw89_sta {
    bool owly_awdl;
    unsigned long ampdu_map[1];
    struct { u16 agg_num; } ampdu_params[16];
};
struct rtw89_sta_link {
    struct rtw89_sta *rtwsta;
    bool ra_configured;
    struct { unsigned macid, bw_cap; u64 ra_mask; } ra;
};
struct ops {
    void (*configure_filter)(struct ieee80211_hw *, unsigned, unsigned *, u64);
    int (*add_interface)(struct ieee80211_hw *, struct ieee80211_vif *);
    void (*remove_interface)(struct ieee80211_hw *, struct ieee80211_vif *);
};
struct rtw89_m80211_local { struct ops *ops; };
struct sk_buff { const u8 *data; unsigned len; };
struct rtw89_rx_desc_info { unsigned frame_type; };
struct ieee80211_tx_info { u32 flags; };
struct rtw89_tx_desc_info {
    bool use_rate, stbc, ldpc, dis_data_fb, is_bmc;
    bool agg_en, hw_rts_en, rts_en, tx_cnt_lmt_en;
    unsigned data_bw, gi_ltf, data_rate, port, mac_id, hw_seq_mode, hw_ssn_sel;
    unsigned ampdu_num, ampdu_density, tx_cnt_lmt;
};
struct rtw89_core_tx_request {
    struct ieee80211_vif *vif;
    const struct rtw89_vif_link *rtwvif_link;
    const struct rtw89_sta_link *rtwsta_link;
    struct ieee80211_sta *sta;
    const struct sk_buff *skb;
    unsigned tx_type;
    struct rtw89_tx_desc_info desc_info;
};
static const struct rtw89_chan mock_chan = { 2u };
static const struct rtw89_chan *rtw89_chan_get(struct rtw89_dev *p, u8 idx)
{ (void)p; (void)idx; return &mock_chan; }

static struct wiphy wiphy = {BIT(NL80211_IFTYPE_P2P_CLIENT)};
static struct ieee80211_hw hw;
static struct rtw89_dev dev;
static struct ops ops;
static struct rtw89_m80211_local local;
static const struct rtw89_mac_gen_def mac_def = {0xce20};
static const struct chip_info rtl8852b = {RTL8852B, &mac_def}, other_chip = {0, &mac_def};
static struct ieee80211_vif primary, auxiliary;
static struct rtw89_vif_link aux_link;
static struct { struct ieee80211_vif *vif; } glue = {&primary};
static struct ieee80211_vif *owly_core04c_vif;
static bool owly_scc_filter_reported;
static unsigned filter_calls, last_flags, changed_flags, register_writes;
static u32 filter_register[2];
static unsigned cam_writes, removals, frees;
static unsigned filter_readbacks;
static bool published;
static int cam_result;

static struct ieee80211_hw *glue_hw(void) { return &hw; }
static struct rtw89_m80211_local *hw_to_local(struct ieee80211_hw *p)
{
    assert(p == &hw);
    return &local;
}
static void lockdep_assert_wiphy(const void *p) { assert(p == &wiphy); }
static void rtw89_leave_ps_mode(struct rtw89_dev *p) { assert(p == &dev); }
static u32 rtw89_mac_reg_by_idx(struct rtw89_dev *p, u32 reg, u8 index)
{
    assert(p == &dev && index <= RTW89_MAC_1);
    assert(reg == mac_def.rx_fltr || reg == R_AX_PLCP_HDR_FLTR);
    return reg == R_AX_PLCP_HDR_FLTR ? reg + index : index;
}
static u32 rtw89_read32(struct rtw89_dev *p, u32 reg)
{
    if (reg == mac_def.rx_fltr) {
        ++filter_readbacks;
        reg = RTW89_MAC_0;
    }
    assert(p == &dev && reg <= RTW89_MAC_1);
    return filter_register[reg];
}
static void rtw89_write32(struct rtw89_dev *p, u32 reg, u32 bits)
{
    assert(p == &dev && reg <= RTW89_MAC_1);
    filter_register[reg] = bits;
    ++register_writes;
}
static void rtw89_write16(struct rtw89_dev *p, u32 reg, uint16_t bits)
{
    assert(p == &dev && (reg == R_AX_PLCP_HDR_FLTR || reg == R_AX_PLCP_HDR_FLTR + 1));
    assert(bits == 0x7f);
}
static int rtw89_mac_check_mac_en(struct rtw89_dev *p, u8 index, unsigned type)
{
    assert(p == &dev && index <= RTW89_MAC_1 && type == RTW89_CMAC_SEL);
    return 0;
}
static void rtw89_io_pack(struct rtw89_dev *p) { assert(p == &dev); }
static void rtw89_io_unpack(struct rtw89_dev *p) { assert(p == &dev); }
static int rtw89_mac_typ_fltr_opt_ax(struct rtw89_dev *p, int type, int dest, u8 index)
{
    assert(p == &dev && type >= RTW89_MGNT && type <= RTW89_DATA);
    assert(dest == RTW89_FWD_TO_HOST && index <= RTW89_MAC_1);
    return 0;
}
static struct rtw89_vif *vif_to_rtwvif(struct ieee80211_vif *vif) { return &vif->priv; }
static bool ether_addr_equal(const u8 *a, const u8 *b) { return !memcmp(a, b, ETH_ALEN); }
static void ether_addr_copy(u8 *to, const u8 *from) { memcpy(to, from, ETH_ALEN); }

static bool owly_core04c_make_mac(const u8 *base, u8 *mac)
{
    ether_addr_copy(mac, base);
    mac[0] ^= 0x02;
    return true;
}

static struct ieee80211_vif *rtw89_m80211_vif_alloc(struct ieee80211_hw *p, int type, const u8 *mac)
{
    assert(p == &hw && !published);
    memset(&auxiliary, 0, sizeof(auxiliary));
    auxiliary.type = type;
    ether_addr_copy(auxiliary.addr, mac);
    return &auxiliary;
}

static int add_interface(struct ieee80211_hw *p, struct ieee80211_vif *vif)
{
    assert(p == &hw && vif == &auxiliary && vif->p2p);
    memset(&aux_link, 0, sizeof(aux_link));
    ether_addr_copy(aux_link.mac_addr, vif->addr);
    aux_link.port = 1;
    aux_link.addr_cam.valid = aux_link.bssid_cam.valid = true;
    aux_link.addr_cam.addr_cam_idx = aux_link.bssid_cam.bssid_cam_idx = 1;
    vif->priv.links[0] = &aux_link;
    return 0;
}

static void remove_interface(struct ieee80211_hw *p, struct ieee80211_vif *vif)
{
    assert(p == &hw && vif == &auxiliary && !published);
    aux_link.addr_cam.valid = aux_link.bssid_cam.valid = false;
    ++removals;
}

static void rtw89_m80211_vif_free(struct ieee80211_vif *vif)
{
    assert(vif == &auxiliary && !aux_link.addr_cam.valid);
    ++frees;
}

static void rtw89_m80211_vif_set_in_driver(struct ieee80211_vif *vif, bool in_driver)
{
    assert(vif == &auxiliary && !published);
    published = in_driver;
}

static int rtw89_fw_h2c_cam(struct rtw89_dev *p, struct rtw89_vif_link *link,
                           void *sta, const u8 *scan_mac, int mode)
{
    static const u8 bssid[ETH_ALEN] = {0x00, 0x25, 0x00, 0xff, 0x94, 0x73};
    assert(p == &dev && link == &aux_link && !sta && !scan_mac && mode == RTW89_ROLE_INFO_CHANGE);
    assert(link->port == 1 && !published && !owly_core04c_vif);
    assert(ether_addr_equal(link->mac_addr, auxiliary.addr));
    assert(ether_addr_equal(link->bssid_cam.bssid, bssid));
    assert(ether_addr_equal(auxiliary.bss_conf.bssid, bssid));
    ++cam_writes;
    return cam_result;
}

#include "production_unicast_rx.inc"

static void configure(struct ieee80211_hw *p, unsigned changed, unsigned *flags, u64 multicast)
{
    ++filter_calls;
    changed_flags = changed;
    rtw89_ops_configure_filter(p, changed, flags, multicast);
    last_flags = *flags;
}

static void test_filter(void)
{
    const unsigned social_filters = FIF_OTHER_BSS | FIF_ALLMULTI | FIF_BCN_PRBRESP_PROMISC;
    const unsigned ax_filters = social_filters | FIF_PROBE_REQ;
    const u32 blocked = B_AX_A_A1_MATCH | B_AX_A_BC | B_AX_A_MC | B_AX_A_BCN_CHK_EN;
    const u32 cam = B_AX_A_BC_CAM_MATCH | B_AX_A_UC_CAM_MATCH;
    const u32 preserved = BIT(14);
    /* AWDL No-DS traffic must not require an AP/CAM unicast match.
     * Keep receiver-address filtering and the station's original cache. */
    const u32 addressed = preserved | B_AX_A_A1_MATCH;
    const u32 multicast_cam = B_AX_A_MC_LIST_CAM_MATCH;
    const u32 uid = FIELD_PREP(B_AX_UID_FILTER_MASK, 3);
    const u32 max_length = FIELD_PREP(B_AX_RX_MPDU_MAX_LEN_MASK, 32);
    const u32 initial = blocked | cam | multicast_cam | preserved | uid;
    const u32 station_cache = multicast_cam | preserved | uid;
    unsigned flags = social_filters;

    dev.hal.rx_fltr = initial;
    filter_register[RTW89_MAC_0] = max_length;
    rtw89_ops_configure_filter(&hw, social_filters, &flags, 0);
    assert(flags == social_filters);
    assert(filter_register[RTW89_MAC_0] == (cam | station_cache | max_length));

    dev.hal.rx_fltr = initial;
    register_writes = 0;
    owly_scc_filter_set(true, 5745);
    assert(filter_calls == 1 && changed_flags == ax_filters && last_flags == ax_filters);
    assert(owly_scc_filter_reported && register_writes == 1);
    assert(filter_readbacks == 1);
    assert(filter_register[RTW89_MAC_0] == (addressed | max_length));
    assert(filter_register[RTW89_MAC_0] & B_AX_A_A1_MATCH);
    assert(!(filter_register[RTW89_MAC_0] & (B_AX_A_UC_CAM_MATCH | B_AX_A_BC_CAM_MATCH |
                                         B_AX_UID_FILTER_MASK | B_AX_SNIFFER_MODE)));
    assert(!(filter_register[RTW89_MAC_0] & B_AX_A_CRC32_ERR));
    assert(dev.hal.rx_fltr == station_cache);
    assert(!(hw.conf.flags & IEEE80211_CONF_MONITOR));

    /* The scan and ROC writer still clears RX admission gates when its
     * input is the unmodified filter cache. Secondary MAC is unaffected. */
    dev.scanning = true;
    flags = ax_filters;
    rtw89_ops_configure_filter(&hw, ax_filters, &flags, 0);
    assert(filter_register[RTW89_MAC_0] == (preserved | max_length));
    dev.scanning = false;
    rtw89_mac_set_rx_fltr(&dev, RTW89_MAC_0, initial);
    assert(filter_register[RTW89_MAC_0] == (addressed | max_length));
    rtw89_mac_set_rx_fltr(&dev, RTW89_MAC_1, dev.hal.rx_fltr);
    assert(filter_register[RTW89_MAC_1] == station_cache);
    assert(!rx_fltr_init_ax(&dev, RTW89_MAC_0));
    assert(filter_register[RTW89_MAC_0] == addressed);
    filter_register[RTW89_MAC_0] |= max_length; /* The MAC also sets the MPDU length. */
    assert(!rx_fltr_init_ax(&dev, RTW89_MAC_1));
    assert(filter_register[RTW89_MAC_1] == station_cache);
    owly_scc_filter_set(true, 5745);
    assert(filter_calls == 1);

    /* Link notifications can change the AP channel while SCC stays active. */
    owly_scc_filter_set(true, 5220);
    assert(filter_calls == 1 && filter_register[RTW89_MAC_0] == (addressed | max_length));
    owly_scc_filter_set(true, 2437);
    assert(filter_calls == 1 && filter_register[RTW89_MAC_0] == (addressed | max_length));
    owly_scc_filter_set(true, 5745);
    assert(filter_calls == 1 && filter_register[RTW89_MAC_0] == (addressed | max_length));
    owly_scc_filter_set(true, 2412); /* Unsupported AWDL channel restores CAM/UID. */
    assert(filter_calls == 2 && filter_register[RTW89_MAC_0] == (station_cache | max_length));
    owly_scc_filter_set(true, 2437);
    assert(filter_calls == 3 && filter_register[RTW89_MAC_0] == (addressed | max_length));
    owly_scc_filter_set(false, 0);
    assert(!owly_scc_filter_reported && last_flags == 0);
    assert(filter_register[RTW89_MAC_0] == (initial | max_length));

    owly_scc_filter_set(true, 5745);
    dev.scanning = true;
    owly_scc_filter_set(false, 0);
    assert(dev.hal.rx_fltr == initial);
    assert((filter_register[RTW89_MAC_0] & (cam | B_AX_UID_FILTER_MASK)) == (cam | uid));
    assert(!(filter_register[RTW89_MAC_0] & (B_AX_A_A1_MATCH | B_AX_A_BC | B_AX_A_BCN_CHK_EN)));
    dev.scanning = false;
    rtw89_mac_set_rx_fltr(&dev, RTW89_MAC_0, dev.hal.rx_fltr);
    assert(filter_register[RTW89_MAC_0] == (initial | max_length));

    hw.conf.flags |= IEEE80211_CONF_MONITOR;
    rtw89_ops_configure_filter(&hw, 0, &flags, 0);
    assert(filter_register[RTW89_MAC_0] & B_AX_SNIFFER_MODE);
    hw.conf.flags = 0;
    rtw89_ops_configure_filter(&hw, 0, &flags, 0);
    assert(filter_register[RTW89_MAC_0] == (initial | max_length));

    dev.chip = &other_chip;
    owly_scc_filter_set(true, 5745);
    assert(last_flags == social_filters);
    assert(filter_register[RTW89_MAC_0] == (cam | station_cache | max_length));
    owly_scc_filter_set(false, 0);
    assert(filter_register[RTW89_MAC_0] == (initial | max_length));
    dev.chip = &rtl8852b;
}

static void test_aux_cam(void)
{
    const u8 station[ETH_ALEN] = {0x02, 1, 2, 3, 4, 5};
    const u8 awdl[ETH_ALEN] = {0x02, 1, 2, 3, 4, 6};
    ether_addr_copy(primary.addr, station);
    assert(owly_aux_program_cam(&hw, &primary) == -EINVAL);
    assert(cam_writes == 0);

    cam_result = -EIO;
    assert(owly_core04c_vif_register(&hw, awdl) == -EIO);
    assert(cam_writes == 1 && removals == 1 && frees == 1);
    assert(!published && !owly_core04c_vif);

    cam_result = 0;
    assert(owly_core04c_vif_register(&hw, awdl) == 0);
    assert(cam_writes == 2 && removals == 1 && frees == 1);
    assert(published && owly_core04c_vif == &auxiliary);
    assert(ether_addr_equal(primary.addr, station) && !primary.bss_conf.bssid);
}

static void test_unicast_tx_port(void)
{
    const u8 snap[8] = {0xaa,0xaa,3,0,0x17,0xf2,8,0};
    u8 frame[100] = {0x08};
    struct sk_buff skb = {frame, sizeof(frame)};
    struct ieee80211_tx_info info = {
        IEEE80211_TX_INTFL_DONT_ENCRYPT | IEEE80211_TX_CTL_REQ_TX_STATUS
    };
    struct rtw89_core_tx_request tx = {
        .vif = &auxiliary, .rtwvif_link = &aux_link, .skb = &skb,
        .tx_type = RTW89_CORE_TX_TYPE_DATA,
        .desc_info = {.mac_id = 7, .use_rate = true, .data_rate = 4}
    };
    frame[4] = 0x02;
    memcpy(frame + 10, auxiliary.addr, ETH_ALEN);
    memcpy(frame + 16, aux_link.bssid, ETH_ALEN);
    memcpy(frame + 24, snap, sizeof(snap));
    frame[32] = 3;frame[33] = 4;frame[38] = 0x86;frame[39] = 0xdd;frame[40] = 0x60;
    const u32 old_txwd = rtw89_build_txwd_info0(&tx.desc_info);
    rtw89_core_tx_update_awdl_port(&dev, &tx, &info);
    assert(tx.desc_info.port == aux_link.port && tx.desc_info.mac_id == 7);
    assert(rtw89_build_txwd_info0(&tx.desc_info) ==
           (old_txwd | FIELD_PREP(RTW89_TXWD_INFO0_MULTIPORT_ID, aux_link.port)));

    tx.desc_info.port = 0;
    tx.vif = &primary;
    rtw89_core_tx_update_awdl_port(&dev, &tx, &info);
    assert(!tx.desc_info.port);
    tx.vif = &auxiliary;
    tx.desc_info.is_bmc = true;
    rtw89_core_tx_update_awdl_port(&dev, &tx, &info);
    assert(!tx.desc_info.port);
    tx.desc_info.is_bmc = false;
    frame[15] ^= 1;
    rtw89_core_tx_update_awdl_port(&dev, &tx, &info);
    assert(!tx.desc_info.port);
    frame[15] ^= 1;
    skb.len = 24;
    rtw89_core_tx_update_awdl_port(&dev, &tx, &info);
    assert(!tx.desc_info.port);
    skb.len = sizeof(frame);
    info.flags |= IEEE80211_TX_CTL_NO_ACK;
    rtw89_core_tx_update_awdl_port(&dev, &tx, &info);
    assert(rtw89_build_txwd_info0(&tx.desc_info) == old_txwd);
}

static void test_native_management_sequence(void)
{
    u8 f[40]={0xd0};struct sk_buff skb={f,sizeof(f)};
    memcpy(f+10,aux_link.mac_addr,6);memcpy(f+16,aux_link.bssid,6);
    struct ieee80211_tx_info info={IEEE80211_TX_CTL_INJECTED|IEEE80211_TX_INTFL_DONT_ENCRYPT};
    struct rtw89_core_tx_request tx={.vif=&auxiliary,.rtwvif_link=&aux_link,.skb=&skb,
        .desc_info={.hw_seq_mode=1,.hw_ssn_sel=1}};
    rtw89_core_tx_update_awdl_port(&dev,&tx,&info);
    assert(tx.desc_info.hw_seq_mode==0 && tx.desc_info.hw_ssn_sel==0);
    tx.desc_info.hw_seq_mode=tx.desc_info.hw_ssn_sel=1;tx.vif=&primary;
    rtw89_core_tx_update_awdl_port(&dev,&tx,&info);
    assert(tx.desc_info.hw_seq_mode==1 && tx.desc_info.hw_ssn_sel==1);
    tx.vif=&auxiliary;f[15]^=1;
    rtw89_core_tx_update_awdl_port(&dev,&tx,&info);
    assert(tx.desc_info.hw_seq_mode==1 && tx.desc_info.hw_ssn_sel==1);
}

static void test_unicast_ppdu_bypass(void)
{
    u8 frame[90]={0x88};
    struct sk_buff skb={frame,sizeof(frame)};
    struct rtw89_rx_desc_info desc={.frame_type=RTW89_RX_TYPE_DATA};
    memcpy(frame+16,aux_link.bssid,ETH_ALEN);
    assert(rtw89_core_awdl_data_rx_immediate(&dev,&desc,&skb));
    frame[0]=0x08;
    assert(rtw89_core_awdl_data_rx_immediate(&dev,&desc,&skb));
    frame[1]=0x40;
    assert(!rtw89_core_awdl_data_rx_immediate(&dev,&desc,&skb));
    frame[1]=0;
    frame[4]=0x33;
    assert(!rtw89_core_awdl_data_rx_immediate(&dev,&desc,&skb));
    frame[4]=0x02;
    dev.scanning=true;
    assert(!rtw89_core_awdl_data_rx_immediate(&dev,&desc,&skb));
    dev.scanning=false;
    skb.len=23;
    assert(!rtw89_core_awdl_data_rx_immediate(&dev,&desc,&skb));
    skb.len=sizeof(frame);
    frame[21]^=1;
    assert(!rtw89_core_awdl_data_rx_immediate(&dev,&desc,&skb));
    frame[21]^=1;
    frame[0]=0xd0;
    assert(!rtw89_core_awdl_data_rx_immediate(&dev,&desc,&skb));
}

int main(void)
{
    hw.priv = &dev;
    hw.wiphy = &wiphy;
    dev.hw = &hw;
    dev.chip = &rtl8852b;
    local.ops = &ops;
    ops.configure_filter = configure;
    ops.add_interface = add_interface;
    ops.remove_interface = remove_interface;
    test_filter();
    test_aux_cam();
    test_unicast_tx_port();
    test_native_management_sequence();
    test_unicast_ppdu_bypass();
    puts("AWDL AX active RX filter, CAM rollback, directed data RX and RTL8852B TX descriptor port passed");
    return 0;
}
