/* SPDX-License-Identifier: GPL-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include "owly_awdl_ht.h"
#include "owly_awdl_sync.h"
enum { NL80211_BAND_2GHZ, NL80211_BAND_5GHZ };
struct ieee80211_supported_band {
    struct { bool ht_supported; uint16_t cap; uint8_t ampdu_factor, ampdu_density;
             struct { uint8_t rx_mask[10]; } mcs; } ht_cap;
    struct { bool vht_supported; uint32_t cap;
             struct { uint16_t rx_mcs_map, tx_mcs_map; } vht_mcs; } vht_cap;
};
struct wiphy { struct ieee80211_supported_band *bands[2]; bool locked; };
struct rtw89_dev { struct { uint8_t tx_nss, rx_nss; } hal; };
static struct ieee80211_supported_band bands[2];
static struct wiphy phy;
static struct rtw89_dev dev = {{1, 1}};
static struct ieee80211_hw { struct wiphy *wiphy; struct rtw89_dev *priv; } hw={&phy, &dev};
static struct { bool active,probed; int cmd_lock; } glue;
static void mutex_lock(int *p){assert(!*p);*p=1;}
static void mutex_unlock(int *p){assert(*p==1);*p=0;}
static void wiphy_lock(struct wiphy *p){assert(glue.cmd_lock==1 && !p->locked);p->locked=true;}
static void wiphy_unlock(struct wiphy *p){assert(p->locked);p->locked=false;}
static struct ieee80211_hw *glue_hw(void){return &hw;}
#include "production_ht_glue.inc"

static const uint8_t *tlv(const uint8_t *frame,size_t size,unsigned type,size_t *length)
{
    for(size_t at=40;at+3<=size;) {
        size_t n=frame[at+1]+((size_t)frame[at+2]<<8);
        assert(n<=size-at-3);
        if(frame[at]==type){*length=n;return frame+at+3;}
        at+=3+n;
    }
    return NULL;
}
int main(void)
{
    uint8_t guarded[34],before[34];memset(guarded,0xa5,sizeof(guarded));
    uint8_t *reply=guarded+1;memcpy(before,guarded,sizeof(before));
    assert(!owly_awdl_build_ht(NULL,32,255,0));
    assert(!owly_awdl_build_ht(reply,31,255,0));
    assert(!owly_awdl_build_ht(reply,32,0,0));
    assert(!owly_awdl_build_ht(reply,32,255,8));
    assert(!memcmp(guarded,before,sizeof(before)));
#if OWLY_NATIVE_PEER_DATA
    assert(rtw89_glue_awdl_ht_capability(reply,32)==-ENODEV);
    glue.active=glue.probed=true;
    assert(rtw89_glue_awdl_ht_capability(reply,32)==-EOPNOTSUPP);
    for(unsigned i=0;i<2;i++){
        phy.bands[i]=&bands[i];bands[i].ht_cap.ht_supported=true;
        bands[i].ht_cap.mcs.rx_mask[0]=255;
        bands[i].ht_cap.mcs.rx_mask[1]=255;
        bands[i].ht_cap.ampdu_factor=0;
        bands[i].ht_cap.cap=0x000c;
    }
    bands[0].ht_cap.ampdu_density=2;bands[1].ht_cap.ampdu_density=4;
    assert(!rtw89_glue_awdl_ht_capability(reply,32));
    static const uint8_t expected[32]={1,0,0,0,45,26,12,0,16,255,
        0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0};
    assert(!memcmp(reply,expected,32) && guarded[0]==0xa5 && guarded[33]==0xa5);
    /* Verify 2SS + 64 KB A-MPDU + HT40/SGI/LDPC/STBC + VHT 18-byte ABI. */
    dev.hal.tx_nss=2; dev.hal.rx_nss=2;
    bands[0].ht_cap.ampdu_factor=3; bands[1].ht_cap.ampdu_factor=3;
    bands[0].ht_cap.cap=0x01ef; bands[1].ht_cap.cap=0x01ef;
    assert(!rtw89_glue_awdl_ht_capability(reply,32));
    assert(reply[6]==0xef && reply[7]==0x01 && reply[8]==19 && reply[9]==255 && reply[10]==255);
    uint8_t vht_reply[20]; memset(vht_reply, 0xa5, sizeof(vht_reply));
    assert(owly_awdl_build_vht(vht_reply, 18, 0x038001b1u, 0xfffa, 0xfffa));
    assert(vht_reply[0]==1 && vht_reply[4]==191 && vht_reply[5]==12 &&
           vht_reply[10]==0xfa && vht_reply[11]==0xff && vht_reply[18]==0xa5);
    dev.hal.tx_nss=1; dev.hal.rx_nss=1;
    bands[0].ht_cap.ampdu_factor=0; bands[1].ht_cap.ampdu_factor=0;
    bands[0].ht_cap.cap=0x000c; bands[1].ht_cap.cap=0x000c;
    assert(!glue.cmd_lock && !phy.locked);
    bands[1].ht_cap.mcs.rx_mask[0]=15;
    assert(!rtw89_glue_awdl_ht_capability(reply,32) && reply[9]==15);
    bands[1].ht_cap.ht_supported=false;
    memcpy(before,guarded,sizeof(before));
    assert(rtw89_glue_awdl_ht_capability(reply,32)==-EOPNOTSUPP);
    assert(!memcmp(before,guarded,sizeof(before)) && !glue.cmd_lock && !phy.locked);
    bands[1].ht_cap.ht_supported=true;
    assert(!rtw89_glue_awdl_ht_capability(reply,32));

    /* Model only the pinned Apple extraction (0x6b479..0x6b4d3) and
     * buildEnhDataRateTlv (0x40294..0x402ca). The production scheduler
     * must preserve that TLV in both PSF and native MIF. No RF is mocked
     * into a transfer success and no native Apple function is executed.
     */
    unsigned used=0;for(unsigned i=0;i<28;i++)if(reply[4+i])used=i+1;
    assert(used>0 && used<=26);
    uint8_t payload[128]={24,40,0};size_t at=43;
    payload[at++]=7;payload[at++]=(uint8_t)(used+2);payload[at++]=0;
    payload[at++]=0;payload[at++]=0;
    memcpy(payload+at,reply+6,used);at+=used;
    payload[at++]=21;payload[at++]=2;payload[at++]=0;payload[at++]=0x3e;payload[at++]=1;
    const uint8_t local[6]={2,1,2,3,4,5},group[6]={255,255,255,255,255,255};
    struct owly_sync sync;owly_sync_reset(&sync);
    assert(!owly_sync_template(&sync,payload,at,local));
    assert(!owly_sync_mi(&sync,true,group,payload,at,local,6,1000000));
    owly_sync_request(&sync,true);owly_sync_reconcile(&sync,true,6,1000000);
    bool psf=false,mif=false;
    for(unsigned i=0;i<8;i++){
        uint8_t frame[2048];size_t size=0,length=0;
        if(owly_sync_frame(&sync,1000000+i*65536,frame,sizeof(frame),&size)!=1)continue;
        const uint8_t *ht=tlv(frame,size,7,&length);
        assert(ht && length==used+2 && ht[5]==15);
        assert(!memcmp(ht+2,reply+6,used));
        psf |= frame[30]==0;mif |= frame[30]==3;
        owly_sync_result(&sync,0);
    }
    assert(psf && mif);
    puts("HT production getter: real-band intersection, Legacy32 ABI, guards, lifecycle, PSF/MIF TLV7 passed; no RF proof");
#else
    assert(rtw89_glue_awdl_ht_capability(reply,32)==-EOPNOTSUPP);
    assert(!memcmp(before,guarded,sizeof(before)) && !glue.cmd_lock && !phy.locked);
    puts("HT feature off: unsupported, output and locks unchanged");
#endif
    return 0;
}
