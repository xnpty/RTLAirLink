/* SPDX-License-Identifier: GPL-2.0 */
#ifndef OWLY_AWDL_HT_H
#define OWLY_AWDL_HT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Legacy4C GET154: version followed by the complete EID45/length26 IE.
 * getHtCapabilityIE copies bytes 4..31, then builds AWDL TLV7 from the
 * capability body (skipping EID/length). Returning just the 26-byte body
 * shifts the MCS map and prevents remote peers from enabling their cache.
 * Legacy4C GET214: version followed by the complete EID191/length12 VHT IE
 * (bytes 4..17, total 18 bytes).
 */
#define OWLY_AWDL_HT_REPLY_BYTES 32u
#define OWLY_AWDL_VHT_REPLY_BYTES 18u
static inline bool owly_awdl_build_ht_full(void *output, size_t capacity,
                                           uint16_t rx_mcs, uint8_t ampdu_factor,
                                           uint8_t ampdu_density, uint16_t cap_info)
{
    if (!output || capacity < OWLY_AWDL_HT_REPLY_BYTES || !(uint8_t)rx_mcs ||
        ampdu_factor > 3u || ampdu_density > 7u) return false;
    uint8_t reply[OWLY_AWDL_HT_REPLY_BYTES] = {0};
    /* Preserve safe HT capabilities supported by both radio and AWDL:
     * LDPC (0x0001), 40 MHz (0x0002), SMPS disabled (0x000c),
     * SGI20 (0x0020), SGI40 (0x0040), TX_STBC (0x0080), RX_STBC_1 (0x0100). */
    const uint16_t safe_cap = (uint16_t)((cap_info & 0x01e3u) | 0x000cu);
    reply[0] = 1;             /* APPLE80211_VERSION, little endian */
    reply[4] = 45; reply[5] = 26;
    reply[6] = (uint8_t)safe_cap;
    reply[7] = (uint8_t)(safe_cap >> 8);
    reply[8] = (uint8_t)((ampdu_factor & 3u) | ((ampdu_density & 7u) << 2));
    reply[9] = (uint8_t)rx_mcs;
    reply[10] = (uint8_t)(rx_mcs >> 8);
    reply[21] = 1;          /* TX MCS set defined and equal to RX */
    memcpy(output, reply, sizeof(reply));
    return true;
}

static inline bool owly_awdl_build_ht(void *output, size_t capacity,
                                     uint8_t rx_mcs0, uint8_t ampdu_density)
{
    return owly_awdl_build_ht_full(output, capacity, rx_mcs0, 0u, ampdu_density, 0x000cu);
}

static inline bool owly_awdl_build_vht(void *output, size_t capacity,
                                       uint32_t vht_cap_info,
                                       uint16_t rx_mcs_map, uint16_t tx_mcs_map)
{
    if (!output || capacity < OWLY_AWDL_VHT_REPLY_BYTES ||
        (rx_mcs_map & 3u) == 3u || (tx_mcs_map & 3u) == 3u)
        return false;
    /* Keep safe 80 MHz VHT capability bits:
     * MAX_MPDU (0x3, capped at 7991=1), RXLDPC (0x10), SGI80 (0x20),
     * TXSTBC (0x80), RXSTBC_1 (0x100), MAX_AMPDU_EXP (0x03800000).
     * Never advertise 160/80+80 MHz or beamforming on AWDL. */
    uint32_t max_mpdu = vht_cap_info & 3u;
    if (max_mpdu > 1u) max_mpdu = 1u; /* 7991 bytes */
    uint32_t safe_cap = max_mpdu |
                        (vht_cap_info & (0x00000010u | 0x00000020u | 0x00000080u | 0x03800000u));
    if (vht_cap_info & 0x00000700u)
        safe_cap |= 0x00000100u; /* RX_STBC_1 */
    uint8_t reply[OWLY_AWDL_VHT_REPLY_BYTES] = {0};
    reply[0] = 1;             /* APPLE80211_VERSION, little endian */
    reply[4] = 191;           /* WLAN_EID_VHT_CAPABILITY (0xbf) */
    reply[5] = 12;            /* IE length = 12 */
    reply[6] = (uint8_t)safe_cap;
    reply[7] = (uint8_t)(safe_cap >> 8);
    reply[8] = (uint8_t)(safe_cap >> 16);
    reply[9] = (uint8_t)(safe_cap >> 24);
    reply[10] = (uint8_t)rx_mcs_map;
    reply[11] = (uint8_t)(rx_mcs_map >> 8);
    /* bytes 12..13: rx_highest = 0 */
    reply[14] = (uint8_t)tx_mcs_map;
    reply[15] = (uint8_t)(tx_mcs_map >> 8);
    /* bytes 16..17: tx_highest = 0 */
    memcpy(output, reply, sizeof(reply));
    return true;
}
#endif
