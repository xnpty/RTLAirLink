// SPDX-License-Identifier: MIT
// Owly AWDL Core-01: allocation-free, read-only AWDL Action/TLV decoder.
// Input: 802.11 MAC frame, with NO radiotap and NO FCS.
// No peer authorization, synchronization or transmission is performed.
#ifndef RTW89_AWDL_ACTION_DECODE_H
#define RTW89_AWDL_ACTION_DECODE_H
#include <stddef.h>

#define OWLY_ACTION_MAX_FRAME 4096u
#define OWLY_ACTION_MAX_TLVS 64u

/* -1: not an AWDL Action signature, 0: malformed, 1: decoded. */
enum owly_action_result {
    OWLY_ACTION_NOT_MATCH = -1,
    OWLY_ACTION_MALFORMED = 0,
    OWLY_ACTION_DECODED = 1
};

struct owly_action_info {
    unsigned char subtype;      /* 0=PSF, 3=MIF */
    unsigned char header_version;
    unsigned int phy_tx_us;
    unsigned int target_tx_us;
    unsigned char has_sync;
    unsigned char has_chanseq;
    unsigned char has_version;
    unsigned char has_ht_rx_mcs;
    unsigned short ht_rx_mcs;
    unsigned short ht_cap_info;
    unsigned char ht_ampdu_params;
    unsigned char has_vht_cap;
    unsigned int vht_cap_info;
    unsigned short vht_rx_mcs_map;
    unsigned short vht_tx_mcs_map;
    unsigned char complete;     /* presence of key TLVs, NOT valid peer */
    unsigned char next_aw_channel;
    unsigned char master_channel;
    unsigned short tx_down_tu;
    unsigned short aw_period_tu;
    unsigned short next_aw_seq;
    unsigned char awdl_version;
    unsigned char device_class;
    unsigned char channel_encoding;
    unsigned char channels[16];
    unsigned char channel_widths[16]; /* 20, 40, or 80 MHz per slot */
    unsigned char social_mask; /* bit0=6, bit1=44, bit2=149 */
    unsigned char tlv_count;
    unsigned char unknown_tlv_count;
};

static inline unsigned short owly_le16(const unsigned char *p)
{
    return (unsigned short)((unsigned short)p[0] | ((unsigned short)p[1] << 8));
}
static inline unsigned int owly_le32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}
static inline int owly_equal(const unsigned char *a, const unsigned char *b,
                             size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/* Do not assume that a syntactically decodable frame is from a trusted peer. */
static inline enum owly_action_result
owly_decode_action(const unsigned char *f, size_t len,
                   struct owly_action_info *out)
{
    static const unsigned char bssid[6] = {0, 0x25, 0, 0xff, 0x94, 0x73};
    static const unsigned char oui[3] = {0, 0x17, 0xf2};
    struct owly_action_info d = {0};
    unsigned short fc;
    size_t pos;
    unsigned int processed = 0, ht_elements = 0;

    if (!out) return OWLY_ACTION_MALFORMED;
    *out = d;
    if (!f || len < 29u || len > OWLY_ACTION_MAX_FRAME)
        return OWLY_ACTION_NOT_MATCH;
    fc = owly_le16(f);
    /* 802.11 management Action, not protected, ToDS/FromDS both 0. */
    if ((fc & 0x00fcu) != 0x00d0u || (fc & (0x0100u | 0x0200u | 0x4000u)))
        return OWLY_ACTION_NOT_MATCH;
    if (!owly_equal(f + 16, bssid, 6) || f[24] != 127 ||
        !owly_equal(f + 25, oui, 3) || f[28] != 8)
        return OWLY_ACTION_NOT_MATCH;
    if (len < 40u) return OWLY_ACTION_MALFORMED;
    d.header_version = f[29];
    d.subtype = f[30];
    if ((d.subtype != 0 && d.subtype != 3) || f[31] != 0)
        return OWLY_ACTION_MALFORMED;
    d.phy_tx_us = owly_le32(f + 32);
    d.target_tx_us = owly_le32(f + 36);
    pos = 40u;
    while (pos < len) {
        unsigned int ty;
        size_t value_len;
        const unsigned char *v;
        unsigned int i;
        unsigned int stride;
        if (len - pos < 3u) {
            /* Allow one/two ZERO trailing pad bytes only. */
            while (pos < len) if (f[pos++] != 0) return OWLY_ACTION_MALFORMED;
            break;
        }
        if (processed >= OWLY_ACTION_MAX_TLVS)
            return OWLY_ACTION_MALFORMED;
        ty = f[pos];
        value_len = (size_t)owly_le16(f + pos + 1);
        pos += 3u;
        if (value_len > len - pos) return OWLY_ACTION_MALFORMED;
        v = f + pos;
        processed++;
        switch (ty) {
        case 4: /* Synchronization Parameters */
            if (d.has_sync || value_len < 31u) return OWLY_ACTION_MALFORMED;
            d.has_sync = 1;
            d.next_aw_channel = v[0];
            d.tx_down_tu = owly_le16(v + 1);
            d.master_channel = v[3];
            d.aw_period_tu = owly_le16(v + 5);
            d.next_aw_seq = owly_le16(v + 29);
            break;
        case 7: /* HT capabilities: compact (8..11 B) or full body (28 B). */
            ++ht_elements;
            if (ht_elements == 1u &&
                ((value_len >= 8u && value_len <= 11u) || value_len == 28u)) {
                const unsigned short cap = owly_le16(v + 2);
                const unsigned char mcs0 = v[5];
                const unsigned char smps = (unsigned char)((cap >> 2) & 3u);
                const unsigned char mcs1 = (value_len >= 9u && smps == 3u) ? v[6] : 0u;
                d.has_ht_rx_mcs = 1u;
                d.ht_cap_info = cap;
                d.ht_ampdu_params = v[4];
                d.ht_rx_mcs = (unsigned short)(mcs0 | ((unsigned short)mcs1 << 8));
            } else {
                d.has_ht_rx_mcs = 0u;
                d.ht_cap_info = 0u;
                d.ht_ampdu_params = 0u;
                d.ht_rx_mcs = 0u;
            }
            break;
        case 17: /* VHT capabilities IE (EID 191, length 12 -> 14 bytes total). */
            if (!d.has_vht_cap && value_len == 14u && v[0] == 0xbfu && v[1] == 0x0cu) {
                d.has_vht_cap = 1u;
                d.vht_cap_info = owly_le32(v + 2);
                d.vht_rx_mcs_map = owly_le16(v + 6);
                d.vht_tx_mcs_map = owly_le16(v + 10);
            }
            break;
        case 18: /* Channel Sequence, 16 slots */
            if (d.has_chanseq || value_len < 6u || v[0] != 15u ||
                v[2] != 0u || v[3] != 3u || owly_le16(v + 4) != 0xffffu)
                return OWLY_ACTION_MALFORMED;
            stride = (v[1] == 0) ? 1u : ((v[1] == 1 || v[1] == 3) ? 2u : 0u);
            if (stride == 0u || value_len < 6u + 16u * stride)
                return OWLY_ACTION_MALFORMED;
            d.has_chanseq = 1;
            d.channel_encoding = v[1];
            for (i = 0; i < 16u; i++) {
                const unsigned char *slot = v + 6u + i * stride;
                unsigned char ch = slot[v[1] == 1 ? 1u : 0u];
                unsigned char bw = 20u;
                /* Apple's legacy 5 GHz entries use the 40 MHz center for
                 * these social channels. The SCC scheduler uses primaries. */
                if (v[1] == 1 && slot[0] == 0x1du) {
                    if (ch == 46u) ch = 44u;
                    if (ch == 151u) ch = 149u;
                    bw = 40u;
                } else if (v[1] == 3) {
                    const unsigned char oc = slot[1];
                    if (oc == 128u || oc == 129u || oc == 130u)
                        bw = 80u;
                    else if (oc == 116u || oc == 117u || oc == 126u || oc == 127u)
                        bw = 40u;
                }
                d.channels[i] = ch;
                d.channel_widths[i] = ch ? bw : 0u;
                if (ch == 6) d.social_mask |= 1u;
                if (ch == 44) d.social_mask |= 2u;
                if (ch == 149) d.social_mask |= 4u;
            }
            break;
        case 21: /* Version */
            if (d.has_version || value_len < 2u)
                return OWLY_ACTION_MALFORMED;
            d.has_version = 1;
            d.awdl_version = v[0];
            d.device_class = v[1];
            break;
        default:
            d.unknown_tlv_count++;
            break;
        }
        pos += value_len;
    }
    d.tlv_count = (unsigned char)processed;
    d.complete = (unsigned char)(d.has_sync && d.has_chanseq &&
                                 d.has_version && d.awdl_version != 0 &&
                                 d.device_class != 0);
    *out = d;
    return OWLY_ACTION_DECODED;
}
#endif
