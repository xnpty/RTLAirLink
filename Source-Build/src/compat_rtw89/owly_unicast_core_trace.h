/* SPDX-License-Identifier: GPL-2.0 */
/* RTLAirLink integration and release changes dated 2026-11-21. */
#ifndef OWLY_UNICAST_CORE_TRACE_H
#define OWLY_UNICAST_CORE_TRACE_H
#include <stddef.h>
#include <stdint.h>

struct rtw89_rx_desc_info;
struct sk_buff;
void rtw89_compat_awdl_rx_note(const struct rtw89_rx_desc_info *,const struct sk_buff *);
void rtw89_awdl_note_rx_cam(const uint8_t source[6], const uint8_t destination[6],
                           uint8_t mac_id);

enum owly_awdl_rx_sample_stage {
    OWLY_AWDL_SAMPLE_DMA,
    OWLY_AWDL_SAMPLE_MAC80211,
    OWLY_AWDL_SAMPLE_DEMUX,
    OWLY_AWDL_SAMPLE_STAGES
};
void rtw89_awdl_rx_sample(enum owly_awdl_rx_sample_stage stage, const uint8_t *frame,
                          size_t len, unsigned flags, unsigned frequency,
                          unsigned detail, unsigned aux);
#endif
