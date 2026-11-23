/* SPDX-License-Identifier: GPL-2.0 */
/* Independent wire cases from public RTL and iOS capture layouts. */
#include "owly_native_link.h"
#include "owly_awdl_sync.h"
#include "rtw89_awdl_action_decode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const uint8_t local[6]={2,1,2,3,4,5},root[6]={2,6,7,8,9,10};
static const uint8_t bssid[6]={0,0x25,0,0xff,0x94,0x73};
static void put16(uint8_t *p,unsigned v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static size_t fixture(uint8_t *f,unsigned hop,unsigned remaining,unsigned seq,bool relay)
{
    memset(f,0,256);f[0]=0xd0;memset(f+4,255,6);memcpy(f+10,root,6);memcpy(f+16,bssid,6);
    if(relay)f[15]++;
    f[24]=127;f[26]=0x17;f[27]=0xf2;f[28]=8;f[29]=0x10;
    f[40]=4;put16(f+41,31);uint8_t *v=f+43;
    v[0]=149;put16(v+1,hop);v[3]=149;put16(v+5,16);put16(v+15,remaining);memcpy(v+21,root,6);v[27]=4;put16(v+29,seq);
    f[74]=18;put16(f+75,22);v=f+77;v[0]=15;v[3]=3;v[4]=v[5]=255;memset(v+6,149,16);
    f[99]=21;put16(f+100,2);f[102]=0x3e;f[103]=1;
    return 104;
}
static void start(struct owly_native_link *s)
{owly_link_reset(s);owly_link_reconcile(s,true,local,149,1000000);s->sync_enabled=true;}
static int send_next(struct owly_native_link *link,uint64_t now,uint8_t *out,
                     size_t capacity,size_t *written)
{
    unsigned offset;
    uint64_t generation;
    const int result=owly_link_next_tx(link,now,true,out,capacity,written,&offset,&generation);
    if(result==1)owly_link_tx_result(link,offset,generation,0);
    return result;
}
static void advertising(void)
{
    static const uint8_t tlv[]={6,9,0,0,0,0,0,0,0,0,0,0,21,2,0,0x3e,1};
    struct owly_sync s;uint8_t f[2048];size_t n=0;struct owly_action_info d;
    owly_sync_reset(&s);assert(!owly_sync_template(&s,tlv,sizeof(tlv),local));
    owly_sync_request(&s,true);owly_sync_reconcile(&s,true,149,1000000);
    assert(owly_sync_frame(&s,1000000,f,sizeof(f),&n)==1);
    assert(!memcmp(f+4,(uint8_t[]){255,255,255,255,255,255},6));
    assert(!memcmp(f+10,local,6)&&!memcmp(f+16,bssid,6));
    assert(owly_decode_action(f,n,&d)==OWLY_ACTION_DECODED&&d.complete&&d.subtype==0);
    assert(owly_le16(f+44)==48 && owly_le16(f+52)==0x1800);
    assert(owly_le16(f+58)==0 && owly_le16(f+72)==61);
    assert(owly_sync_frame(&s,1001000,f,sizeof(f),&n)==1&&f[30]==3);
    assert(s.due_us==1110000 && s.mif_due_us==1081896);
    assert(!owly_sync_frame(&s,s.mif_due_us-1,f,sizeof(f),&n));
    assert(owly_sync_frame(&s,s.mif_due_us,f,sizeof(f),&n)==1 && f[30]==3);
    assert(owly_sync_frame(&s,1110000,f,sizeof(f),&n)==1 && f[30]==0);
    /* A measured root phase must be advertised in the AW-length field. */
    s.peer_clock.active=true;s.peer_clock.remote=true;s.peer_clock.channel=149;s.peer_clock.master_channel=149;
    s.peer_clock.sampled_us=1220000;s.peer_clock.until_next_tu=7;s.peer_clock.sequence=44;memcpy(s.peer_clock.master,root,6);
    s.have_master_election=true;memcpy(s.master_election,root,6);memcpy(s.master_election+6,root,6);
    assert(owly_sync_frame(&s,1220000,f,sizeof(f),&n)==1);
    assert(owly_le16(f+44)==55&&owly_le16(f+58)==7&&owly_le16(f+72)==44);
    assert(!memcmp(f+64,root,6));
    assert(f[30]==3 && s.mif_due_us==1220000u+55u*1024u);
}
static void clock_fields(bool warmup)
{
    struct owly_native_link s;struct owly_link_window w;uint8_t raw[256];start(&s);
    if(warmup)owly_link_prefer(&s,local);
    for(unsigned i=0;i<3;i++) {
        uint64_t now=2000000+(uint64_t)i*7*OWLY_LINK_AW_US;
        size_t n=fixture(raw,warmup?9:(i==1?0:48),9,100+i*7,false);
        assert(owly_link_observe(&s,raw,n,149,-40,now));
        assert(s.peers[0].anchor_us==now+9*1024); /* +15, never +1 */
        assert(owly_link_window(&s,now,&w));
        if(i==2)assert(w.remote&&!memcmp(w.master,root,6)&&w.sequence==114);
    }
    uint64_t last=2000000+14*OWLY_LINK_AW_US;
    assert(owly_link_window(&s,last+OWLY_LINK_PEER_TTL_US+1,&w)&&!w.remote);
    /* Wait for a newly selected root. It can return before the timeout. */
    owly_link_prefer(&s,root);assert(!owly_link_window(&s,last+OWLY_LINK_PEER_TTL_US+2,&w));
    assert(w.channel==149 && !w.active);
    const uint64_t recover=last+2u*OWLY_LINK_PEER_TTL_US+2;
    owly_link_prefer(&s,root); /* repeated state updates don't renew an absent root */
    assert(owly_link_window(&s,recover,&w) && w.active && !w.remote);
    for(unsigned i=0;i<3;i++) {
        const uint64_t now=recover+1u+i*OWLY_LINK_AW_US;
        size_t n=fixture(raw,0,9,i,false);
        assert(owly_link_observe(&s,raw,n,149,-42,now));
    }
    assert(owly_link_window(&s,recover+1u+2u*OWLY_LINK_AW_US,&w) && w.remote);
}
static void relay_and_bounds(void)
{
    struct owly_native_link s;struct owly_link_window w;uint8_t raw[256];start(&s);
    for(unsigned i=0;i<3;i++) {
        size_t n=fixture(raw,48,9,100+i*7,true);
        uint64_t now=2000000+(uint64_t)i*7*OWLY_LINK_AW_US;
        assert(owly_link_observe(&s,raw,n,149,-40,now));
        assert(owly_link_window(&s,now,&w)&&!w.remote);
    }
    size_t n=fixture(raw,0,17,123,false);
    assert(!owly_link_observe(&s,raw,n,149,-40,2400000));
    n=fixture(raw,48,9,123,false);
    assert(!owly_link_observe(&s,raw,n,44,-40,2400000));
}

static void apple_channel_layout(void)
{
    static const uint8_t version[]={21,2,0,0x3e,1};
    static const uint8_t channels[]={6,44,149};
    uint8_t frame[OWLY_SYNC_FRAME_MAX];
    struct owly_action_info decoded;
    struct owly_sync sync;
    size_t n;

    for (unsigned i=0;i<sizeof(channels);++i) {
        const uint8_t ch=channels[i];
        owly_sync_reset(&sync);
        assert(owly_sync_template(&sync,version,sizeof(version),local)==0);
        owly_sync_request(&sync,true);
        owly_sync_reconcile(&sync,true,ch,1000000);
        assert(owly_sync_frame(&sync,1000000,frame,sizeof(frame),&n)==1);
        assert(frame[40]==4 && owly_le16(frame+41)==73 && frame[77]==1);
        assert(frame[116]==18 && owly_le16(frame+117)==41 && frame[120]==3);
        assert(frame[157]==0 && frame[158]==0 && frame[159]==0 && frame[160]==21);
        assert(owly_decode_action(frame,n,&decoded)==OWLY_ACTION_DECODED && decoded.complete);
        for (unsigned slot=0;slot<16;++slot) {
            assert(frame[82+2*slot]==(ch==6 ? 0x2b : 0x1d));
            assert(frame[83+2*slot]==(ch==6 ? 6 : ch+2));
            assert(frame[125+2*slot]==ch && frame[126+2*slot]==(ch==6 ? 0x51 : 0x80));
            assert(decoded.channels[slot]==ch);
        }
    }
}

static void legacy_peer_channels(void)
{
    struct owly_native_link link;
    uint8_t raw[256];
    size_t n=fixture(raw,0,9,100,false);

    /* Independent legacy wire example: 1d 2e means primary 44 and
     * 1d 97 means primary 149. An explicit 2b 06 remains channel 6. */
    memmove(raw+115,raw+99,n-99);
    put16(raw+75,38);raw[78]=1;
    for (unsigned i=0;i<16;++i) {
        raw[83+2*i]=0x1d;
        raw[84+2*i]=151;
    }
    raw[84]=46;raw[85]=0x2b;raw[86]=6;
    start(&link);
    assert(owly_link_observe(&link,raw,n+16,149,-35,2000000));
    assert(link.peers[0].channels[0]==44 && link.peers[0].channels[1]==6 &&
           link.peers[0].channels[2]==149);
    raw[83]=0x2b; /* another band/flags combination must not map to 44 */
    assert(owly_link_observe(&link,raw,n+16,149,-35,2001000));
    assert(link.peers[0].channels[0]==0 && link.peers[0].channels[2]==149);
}

static void rx_after_timer_advance(void)
{
    struct owly_native_link link;
    struct owly_link_window window;
    uint8_t raw[256], eth[55]={0};
    const uint64_t heard=2000000;
    const uint64_t later=heard+OWLY_LINK_AW_US;

    memcpy(eth,root,6);memcpy(eth+6,local,6);
    eth[12]=0x86;eth[13]=0xdd;eth[14]=0x60;eth[19]=1;
    start(&link);
    size_t n=fixture(raw,0,9,100,false);
    assert(owly_link_observe(&link,raw,n,149,-35,heard));
    assert(owly_link_enqueue(&link,eth,sizeof(eth),heard+100)==0);
    assert(owly_link_window(&link,later+1000,&window) && window.peers==1);
    assert(owly_link_admit_ipv6(&link,root,local,eth+14,sizeof(eth)-14,later+400)==OWLY_LINK_IPV6_ACCEPTED);
    assert(link.count==1 && link.last_now_us==later+1000 && link.data_rx_admitted==1);
    n=fixture(raw,0,9,101,false);
    assert(owly_link_observe(&link,raw,n,149,-35,later));
    assert(link.count==1 && link.peers[0].anchor_us==later+9*1024u);
    assert(!owly_link_observe(&link,raw,n,149,-35,later));
    assert(owly_link_admit_ipv6(&link,root,local,eth+14,sizeof(eth)-14,1)==OWLY_LINK_IPV6_INACTIVE);
}

static void iphone_slot_clock(void)
{
    /* Public Owly discovery-ios26.x-mt7925e-ch44-1.pcap. Relative
     * times and TLV4 fields from the iPhone, with synthetic addresses. */
    static const struct {
        unsigned elapsed_us, down, sequence;
    } samples[] = {
        {0,6,2515}, {118084,19,2522}, {236437,32,2530},
        {294700,38,2533}, {351051,48,2537}, {359338,40,2537},
        {512531,17,2546}, {614090,46,2553}
    };
    struct owly_native_link link;
    struct owly_link_window window;
    uint8_t raw[256];

    owly_link_reset(&link);
    owly_link_reconcile(&link,true,local,44,1000000);
    link.sync_enabled=true;
    for (unsigned i=0; i<sizeof(samples)/sizeof(samples[0]); ++i) {
        const uint64_t now=2000000+samples[i].elapsed_us;
        size_t n=fixture(raw,samples[i].down,0,samples[i].sequence,false);
        raw[43]=raw[46]=44;
        memset(raw+83,44,16);
        assert(owly_link_observe(&link,raw,n,44,-38,now));
        assert(owly_link_window(&link,now,&window));
        if (i>=2) assert(window.remote && window.sequence==samples[i].sequence);
    }
}

static void iphone_mixed_channels(void)
{
    /* The MtkWireless iOS 26 fixture names 11 and 149 in its schedule.
     * A same-channel action establishes a real peer on 149. */
    struct owly_native_link link;
    uint8_t raw[256], ipv6[41]={0x60,0,0,0,0,1};
    size_t n=fixture(raw,29,0,2514,false);
    raw[83]=6;
    raw[84]=11;
    raw[85]=255;
    start(&link);
    assert(owly_link_observe(&link,raw,n,149,-38,2000000));
    assert(link.peers[0].channels[0]==6 && !link.peers[0].channels[1] &&
           !link.peers[0].channels[2] && link.peers[0].channels[3]==149);
    assert(owly_link_admit_ipv6(&link,root,local,ipv6,sizeof(ipv6),2000200)==OWLY_LINK_IPV6_ACCEPTED);
}

static void multicast_queue(bool initial_clock)
{
    struct owly_native_link link;
    uint8_t raw[256], eth[55]={0}, wire[OWLY_LINK_FRAME_MAX];
    size_t written;
    const uint64_t heard=2000000;

    eth[0]=eth[1]=0x33;
    eth[5]=0xfb;
    memcpy(eth+6,local,6);
    eth[12]=0x86;eth[13]=0xdd;eth[14]=0x60;eth[19]=1;
    eth[38]=0xff;eth[39]=2;eth[53]=0xfb;
    start(&link);
    for (unsigned i=0; i<3; ++i) {
        size_t n=fixture(raw,0,9,i,false);
        memset(raw+83,44,15);
        assert(owly_link_observe(&link,raw,n,149,-40,heard+i*OWLY_LINK_AW_US));
        if (!initial_clock && i==1)
            assert(!owly_link_enqueue(&link,eth,sizeof(eth),heard+i*OWLY_LINK_AW_US));
    }
    const uint64_t queued=heard+2*OWLY_LINK_AW_US;
    assert(link.peers[0].clock_valid);
    if (initial_clock) assert(!owly_link_enqueue(&link,eth,sizeof(eth),queued));
    assert(!send_next(&link,queued+510000,wire,sizeof(wire),&written));
    assert(link.count==1 && !link.tx_expired);
    const uint64_t slot15=heard+9*1024u+59*OWLY_LINK_AW_US+4096;
    assert(send_next(&link,slot15,wire,sizeof(wire),&written)==1);
    assert(!memcmp(wire+4,eth,6) && written==81 && !link.tx_expired);
}

static void idle_discovery(bool sparse, uint8_t channel)
{
    static const uint8_t question[]={
        0,0,0,0,0,1,0,0,0,0,0,0,
        15,'_','c','o','m','p','a','n','i','o','n','-','l','i','n','k',
        4,'_','t','c','p',5,'l','o','c','a','l',0,0,12,0,1,
    };
    const uint16_t udp_length=8u+sizeof(question);
    uint8_t eth[54+8+sizeof(question)]={0x33,0x33,0,0,0,0xfb};
    uint8_t raw[256], wire[OWLY_LINK_FRAME_MAX];
    const uint64_t heard=2000000;
    struct owly_native_link link;
    size_t written;

    memcpy(eth+6,local,6);
    eth[12]=0x86;eth[13]=0xdd;eth[14]=0x60;eth[19]=(uint8_t)udp_length;
    eth[20]=17;eth[21]=255;
    eth[38]=0xff;eth[39]=2;eth[53]=0xfb;
    eth[54]=eth[56]=0x14;eth[55]=eth[57]=0xe9;
    eth[59]=(uint8_t)udp_length;
    memcpy(eth+62,question,sizeof(question));

    owly_link_reset(&link);
    owly_link_reconcile(&link,true,local,channel,1000000);
    link.sync_enabled=true;
    for(unsigned i=0;i<3;++i) {
        size_t n=fixture(raw,0,9,i,false);
        raw[43]=raw[46]=channel;
        memset(raw+83,channel,16);
        if(sparse) {
            memset(raw+83,channel==44 ? 149 : 44,16);
            raw[84]=channel;
        }
        assert(owly_link_observe(&link,raw,n,channel,-40,heard+i*OWLY_LINK_AW_US));
    }
    assert(link.peers[0].clock_valid);
    const uint64_t start_aw=heard+9u*1024u+(sparse ? 3u : 39u)*OWLY_LINK_AW_US;
    const uint64_t late=start_aw+8000u;
    if(sparse) {
        assert(!owly_link_action_tx_ready(&link,start_aw-1));
        assert(!owly_link_action_tx_ready(&link,start_aw+1023));
        assert(owly_link_action_tx_ready(&link,start_aw+1024));
        assert(owly_link_action_tx_ready(&link,start_aw+3999));
        assert(!owly_link_action_tx_ready(&link,start_aw+4000));
    }
    assert(owly_link_action_tx_ready(&link,late)==!sparse);
    assert(owly_link_unicast_action_tx_ready(&link,root,late)==!sparse);
    assert(!owly_link_enqueue(&link,eth,sizeof(eth),late));
    assert(send_next(&link,late,wire,sizeof(wire),&written)==!sparse);
    if(sparse) {
        assert(link.count==1 && !link.tx_expired);
        /* A pending query must not hold an answer within the shared slot. */
        eth[64]=0x84;
        assert(!owly_link_enqueue(&link,eth,sizeof(eth),late));
        assert(send_next(&link,late,wire,sizeof(wire),&written)==1);
        assert(wire[90]==0x84 && link.count==1 && !owly_le16(wire+34));
        eth[64]=0;
        const uint64_t next=start_aw+OWLY_LINK_AW_US;
        assert(send_next(&link,next,wire,sizeof(wire),&written)==1);
        assert(!memcmp(wire+4,eth,6) && !memcmp(wire+40,eth+14,sizeof(eth)-14));
        assert(!link.count && owly_le16(wire+34)==1u && owly_le16(wire+22)==16u);

        const uint64_t recent=next+5000;
        assert(owly_link_admit_ipv6(&link,root,local,eth+14,sizeof(eth)-15,recent)==OWLY_LINK_IPV6_INVALID);
        assert(!owly_link_action_tx_ready(&link,recent));
        assert(owly_link_admit_ipv6(&link,root,local,eth+14,sizeof(eth)-14,recent)==OWLY_LINK_IPV6_ACCEPTED);
        assert(owly_link_action_tx_ready(&link,recent));
        assert(owly_link_unicast_action_tx_ready(&link,root,recent));
        assert(!owly_link_enqueue(&link,eth,sizeof(eth),recent));
        assert(send_next(&link,recent,wire,sizeof(wire),&written)==1);

        /* The following shared slot is late enough for the peer to idle. */
        const uint64_t next_cycle=start_aw+64u*OWLY_LINK_AW_US;
        assert(!owly_link_action_tx_ready(&link,next_cycle+8000));
        assert(owly_link_action_tx_ready(&link,next_cycle+1024));
        assert(owly_link_action_tx_ready(&link,heard+3u*OWLY_LINK_PEER_TTL_US));

        /* A fixed-channel VIF can use slot 1 as well as slot 10. */
        owly_link_reset(&link);
        owly_link_reconcile(&link,true,local,channel,1000000);
        link.sync_enabled=true;
        for(unsigned i=0;i<3;++i) {
            size_t n=fixture(raw,0,9,i,false);
            raw[43]=raw[46]=channel;
            memset(raw+83,channel==44 ? 149 : 44,16);
            raw[84]=raw[93]=channel;
            assert(owly_link_observe(&link,raw,n,channel,-40,heard+i*OWLY_LINK_AW_US));
        }
        assert(!owly_link_enqueue(&link,eth,sizeof(eth),start_aw));
        assert(!send_next(&link,start_aw+1023,wire,sizeof(wire),&written));
        eth[64]=0x84;
        assert(!owly_link_enqueue(&link,eth,sizeof(eth),start_aw+1023));
        assert(send_next(&link,start_aw+1024,wire,sizeof(wire),&written)==1);
        assert(wire[90]==0 && link.count==1 && !owly_le16(wire+34));
        assert(!send_next(&link,start_aw+1024,wire,sizeof(wire),&written));
        assert(send_next(&link,start_aw+4096,wire,sizeof(wire),&written)==1);
        assert(wire[90]==0x84 && owly_le16(wire+34)==1);
        assert(!memcmp(wire+40,eth+14,sizeof(eth)-14) && !link.count && !link.queue_used[0] && !link.queue_used[1]);
    }
}

static void transport_startup(void)
{
    const uint8_t multicast[6]={0x33,0x33,0,0,0,0xfb};
    struct owly_native_link link;
    uint8_t raw[256], eth[74]={0}, wire[OWLY_LINK_FRAME_MAX];
    uint8_t ipv6[60]={0x60,0,0,0,0,20,6,64,0xfe,0x80};
    const uint64_t heard=2000000;
    const uint64_t aw=heard+9u*1024u+3u*OWLY_LINK_AW_US;
    const uint64_t late=aw+8000u;
    size_t written;

    start(&link);
    /* A dense schedule allows the first SYN in any settled shared AW. */
    for(unsigned i=0;i<3;++i) {
        size_t n=fixture(raw,0,9,i,false);
        assert(owly_link_observe(&link,raw,n,149,-43,heard+i*OWLY_LINK_AW_US));
    }
    assert(link.peers[0].clock_valid && link.peers[0].local_slots==16);
    memcpy(eth,root,6);
    memcpy(eth+6,local,6);
    eth[12]=0x86;eth[13]=0xdd;
    ipv6[24]=0xfe;ipv6[25]=0x80;ipv6[39]=10;
    ipv6[52]=0x50;ipv6[53]=2; /* TCP SYN */
    memcpy(eth+14,ipv6,sizeof(ipv6));
    assert(!owly_link_enqueue(&link,eth,sizeof(eth),late));
    assert(send_next(&link,late,wire,sizeof(wire),&written)==1);
    assert(written==100 && !memcmp(wire+4,root,6) && wire[93]==2);
    ipv6[24]=0xff;ipv6[25]=2;ipv6[39]=0xfb;
    assert(owly_link_admit_ipv6(&link,root,multicast,ipv6,sizeof(ipv6),late+1)==OWLY_LINK_IPV6_ACCEPTED);

    const uint64_t next_aw=aw+OWLY_LINK_AW_US;
    const uint64_t active=next_aw+8000;
    ipv6[24]=0xfe;ipv6[25]=0x80;ipv6[39]=5;ipv6[53]=0x12;
    assert(owly_link_admit_ipv6(&link,root,root,ipv6,sizeof(ipv6),active)==OWLY_LINK_IPV6_BAD_DEST);
    assert(!owly_link_enqueue(&link,eth,sizeof(eth),active));
    assert(send_next(&link,active,wire,sizeof(wire),&written)==1);
    assert(owly_link_admit_ipv6(&link,root,local,ipv6,sizeof(ipv6),active+1)==OWLY_LINK_IPV6_ACCEPTED);
    const uint64_t after_idle=active+7u*OWLY_LINK_AW_US;
    assert(!owly_link_enqueue(&link,eth,sizeof(eth),after_idle));
    assert(send_next(&link,after_idle,wire,sizeof(wire),&written)==1);
}

static void actual_recipient_slot(uint8_t channel)
{
    const uint8_t other=channel==149 ? 6 : 149;
    const uint8_t iphone[6]={2,6,7,8,9,11};
    const uint8_t unrelated[6]={2,6,7,8,9,12};
    const uint64_t heard=2000000;
    const uint64_t slot1=heard+9u*1024u+3u*OWLY_LINK_AW_US;
    const uint64_t late=slot1+OWLY_LINK_AW_US+8000u;
    const uint64_t slot2=slot1+4u*OWLY_LINK_AW_US;
    struct owly_native_link link;
    struct owly_link_window window;
    uint8_t raw[256], eth[74]={0x33,0x33,0,0,0,0xfb}, wire[OWLY_LINK_FRAME_MAX];
    size_t written;

    owly_link_reset(&link);
    owly_link_reconcile(&link,true,local,channel,1000000);
    link.sync_enabled=true;
    for(unsigned i=0;i<3;++i) {
        const uint64_t now=heard+i*OWLY_LINK_AW_US;
        const size_t n=fixture(raw,0,9,i,false);
        raw[43]=channel;
        raw[46]=i ? 0 : other; /* iOS MIF may omit the master's channel. */
        raw[30]=i ? 3 : 0;
        memset(raw+83,other,16);
        raw[85]=channel; /* The root is on this channel in slot 2. */
        assert(owly_link_observe(&link,raw,n,channel,-35,now));
        memcpy(raw+10,iphone,6);
        raw[84]=channel;raw[85]=other;
        assert(owly_link_observe(&link,raw,n,channel,-40,now));
        memcpy(raw+10,unrelated,6);memcpy(raw+64,unrelated,6);
        raw[84]=other;raw[86]=channel;
        assert(owly_link_observe(&link,raw,n,channel,-60,now));
    }
    assert(owly_link_window(&link,slot1,&window) && window.remote);
    assert(window.channel==channel && window.master_channel==other);
    assert(owly_link_shared_slots(&link,slot1)==((1u<<1)|(1u<<2)));
    assert(!owly_link_action_tx_ready(&link,slot1+1023));
    assert(owly_link_action_tx_ready(&link,slot1+1024));
    assert(!owly_link_action_tx_ready(&link,late));

    memcpy(eth+6,local,6);
    eth[12]=0x86;eth[13]=0xdd;eth[14]=0x60;eth[19]=20;
    eth[20]=17;eth[21]=255;eth[38]=0xff;eth[39]=2;eth[53]=0xfb;
    eth[54]=eth[56]=0x14;eth[55]=eth[57]=0xe9;eth[59]=20;eth[66]=1;
    assert(!owly_link_enqueue(&link,eth,sizeof(eth),late));
    memcpy(eth,iphone,6);eth[38]=0xfe;eth[39]=0x80;
    assert(send_next(&link,late,wire,sizeof(wire),&written)==0);
    assert(!owly_link_enqueue(&link,eth,sizeof(eth),late));
    assert(send_next(&link,late,wire,sizeof(wire),&written)==1 && !memcmp(wire+4,iphone,6));
    assert(link.count==1);

    size_t n=fixture(raw,0,8,5,true);
    raw[43]=channel;raw[46]=0;
    memset(raw+83,other,16);raw[84]=channel;
    assert(owly_link_observe(&link,raw,n,channel,-40,late+1)); /* PSF */
    assert(!owly_link_action_tx_ready(&link,late+1));
    assert(!send_next(&link,late+1,wire,sizeof(wire),&written));
    raw[30]=3;
    assert(owly_link_observe(&link,raw,n,channel,-40,late+2)); /* MIF */
    assert(owly_link_action_tx_ready(&link,late+2));
    assert(owly_link_unicast_action_tx_ready(&link,iphone,late+2));
    assert(send_next(&link,late+2,wire,sizeof(wire),&written)==1 && wire[4]==0x33);
    assert(!link.count && !link.tx_expired && !link.tx_refused);
    assert(!owly_link_unicast_action_tx_ready(&link,iphone,slot2+2048));
    assert(owly_link_action_tx_ready(&link,slot2+2048));
    assert(!owly_link_action_tx_ready(&link,slot2+4u*OWLY_LINK_AW_US+2048));
}

static void unicast_independent_peers(void)
{
    const uint8_t relay[6]={2,6,7,8,9,11};
    const uint64_t heard=2000000;
    const uint64_t slot1=heard+9u*1024u+3u*OWLY_LINK_AW_US;
    const uint64_t slot2=slot1+4u*OWLY_LINK_AW_US;
    struct owly_native_link link;
    uint8_t raw[256], eth[55]={0}, wire[OWLY_LINK_FRAME_MAX];
    size_t written;

    start(&link);
    for(unsigned i=0;i<3;++i) {
        const uint64_t now=heard+i*OWLY_LINK_AW_US;
        const size_t n=fixture(raw,0,9,i,false);
        memset(raw+83,44,16);
        raw[85]=149; /* root in slot 2 */
        assert(owly_link_observe(&link,raw,n,149,-40,now));
        memcpy(raw+10,relay,6);
        raw[84]=149;raw[85]=44; /* relay in slot 1, clocked by root */
        assert(owly_link_observe(&link,raw,n,149,-39,now));
    }
    memcpy(eth,root,6);memcpy(eth+6,local,6);
    eth[12]=0x86;eth[13]=0xdd;eth[14]=0x60;eth[19]=1;
    eth[38]=0xfe;eth[39]=0x80;eth[54]=1;
    assert(!owly_link_enqueue(&link,eth,sizeof(eth),slot1));
    eth[54]=2;
    assert(!owly_link_enqueue(&link,eth,sizeof(eth),slot1));
    memcpy(eth,relay,6);eth[54]=3;
    assert(!owly_link_enqueue(&link,eth,sizeof(eth),slot1));
    assert(!send_next(&link,slot1+1023,wire,sizeof(wire),&written));
    assert(send_next(&link,slot1+4096,wire,sizeof(wire),&written)==1);
    assert(!memcmp(wire+4,relay,6) && wire[80]==3 && link.count==2);
    assert(send_next(&link,slot2+4096,wire,sizeof(wire),&written)==1);
    assert(!memcmp(wire+4,root,6) && wire[80]==1);
    assert(send_next(&link,slot2+4096,wire,sizeof(wire),&written)==1);
    assert(!memcmp(wire+4,root,6) && wire[80]==2 && !link.count);
}

static void local_master_schedule(void)
{
    const uint64_t slot1=33u*4u*OWLY_LINK_AW_US;
    const uint64_t heard=slot1-20000u;
    struct owly_native_link link;
    uint8_t raw[256], eth[55]={0}, wire[OWLY_LINK_FRAME_MAX];
    size_t written;

    start(&link);
    const size_t n=fixture(raw,0,9,0,false);
    memcpy(raw+64,local,6);
    memset(raw+83,44,16);
    raw[84]=149;
    assert(owly_link_observe(&link,raw,n,149,-40,heard));
    assert(!link.peers[0].clock_valid);
    memcpy(eth,root,6);memcpy(eth+6,local,6);
    eth[12]=0x86;eth[13]=0xdd;eth[14]=0x60;eth[19]=1;
    eth[38]=0xfe;eth[39]=0x80;
    assert(!owly_link_enqueue(&link,eth,sizeof(eth),heard));
    assert(!send_next(&link,heard+1,wire,sizeof(wire),&written));
    assert(!send_next(&link,slot1+1023,wire,sizeof(wire),&written));
    assert(send_next(&link,slot1+4096,wire,sizeof(wire),&written)==1);
    assert(!memcmp(wire+4,root,6) && !link.count);
}

static void sparse_first_reply(void)
{
    const uint64_t heard=2000000;
    const uint64_t slot1=heard+9u*1024u+3u*OWLY_LINK_AW_US;
    const uint64_t seen=slot1+8192u;
    struct owly_native_link link;
    uint8_t raw[256], eth[55]={0}, wire[OWLY_LINK_FRAME_MAX];
    uint8_t ip[41]={0x60,0,0,0,0,1};
    const uint8_t multicast[6]={0x33,0x33,0,0,0,0xfb};
    size_t written;

    start(&link);
    for(unsigned i=0;i<3;++i) {
        const size_t n=fixture(raw,0,9,i,false);
        memset(raw+83,44,16);raw[84]=149;
        assert(owly_link_observe(&link,raw,n,149,-40,heard+i*OWLY_LINK_AW_US));
    }
    memcpy(eth,root,6);memcpy(eth+6,local,6);
    eth[12]=0x86;eth[13]=0xdd;eth[14]=0x60;eth[19]=1;
    eth[38]=0xfe;eth[39]=0x80;
    ip[24]=0xff;ip[25]=2;ip[39]=0xfb;
    assert(owly_link_admit_ipv6(&link,root,multicast,ip,sizeof(ip),seen)==OWLY_LINK_IPV6_ACCEPTED);
    assert(!owly_link_enqueue(&link,eth,sizeof(eth),seen+1));
    assert(send_next(&link,seen+1,wire,sizeof(wire),&written)==1);
    assert(!memcmp(wire+4,root,6) && !link.count);
    const uint64_t late=slot1+OWLY_LINK_AW_US+8000u;
    assert(!owly_link_enqueue(&link,eth,sizeof(eth),late));
    assert(send_next(&link,late,wire,sizeof(wire),&written)==1);
}

static void shared_slot_advertising(void)
{
    static const uint8_t version[]={21,2,0,0x3e,1};
    struct owly_native_link link;
    struct owly_sync sync;
    uint8_t raw[256], frame[OWLY_SYNC_FRAME_MAX];
    size_t written;
    const uint64_t heard=2000000;
    start(&link);
    assert(owly_link_action_tx_ready(&link,heard));
    for(unsigned i=0;i<3;++i) {
        size_t n=fixture(raw,0,9,i,false);
        memset(raw+83,44,16);
        raw[84]=149; /* this peer only visits 149 in slot 1 */
        assert(owly_link_observe(&link,raw,n,149,-40,heard+i*OWLY_LINK_AW_US));
    }
    const uint64_t due=heard+2u*OWLY_LINK_AW_US;
    const uint64_t shared=heard+9u*1024u+3u*OWLY_LINK_AW_US+2048u;
    assert(!owly_link_action_tx_ready(&link,due));
    assert(owly_link_action_tx_ready(&link,shared));
    owly_sync_reset(&sync);
    assert(!owly_sync_template(&sync,version,sizeof(version),local));
    owly_sync_request(&sync,true);
    owly_sync_reconcile(&sync,true,149,due);
    sync.hold_for_peer=!owly_link_action_tx_ready(&link,due);
    assert(!owly_sync_frame(&sync,due,frame,sizeof(frame),&written) && !written);
    assert(sync.due_us==due);
    sync.hold_for_peer=!owly_link_action_tx_ready(&link,shared);
    assert(owly_sync_frame(&sync,shared,frame,sizeof(frame),&written)==1);
    assert(owly_le32(frame+32)==(uint32_t)shared && owly_le32(frame+36)==(uint32_t)shared);
    const uint64_t next=sync.due_us;
    sync.hold_for_peer=true;
    const uint64_t full_cycle=64u*OWLY_SYNC_AW_US;
    assert(!owly_sync_frame(&sync,due+full_cycle-1,frame,sizeof(frame),&written));
    assert(owly_sync_frame(&sync,due+full_cycle,frame,sizeof(frame),&written)==1);
    assert(frame[30]==3 && sync.due_us==next);

    /* A unicast MI may be ready for a different peer while the periodic
     * broadcast is held. The due PSF stays scheduled. */
    sync.pending_mi.length=sizeof(version);
    memcpy(sync.pending_mi.bytes,version,sizeof(version));
    memcpy(sync.pending_mi_destination,root,6);
    sync.pending_mi_channel=149;
    sync.pending_mi_us=due+full_cycle;
    sync.pending_mi_ready=true;
    const uint64_t psf=sync.due_us;
    assert(owly_sync_frame(&sync,due+full_cycle+1,frame,sizeof(frame),&written)==1);
    assert(frame[30]==3 && !memcmp(frame+4,root,6) && sync.due_us==psf);
    owly_sync_result(&sync,0);
    assert(owly_sync_frame(&sync,psf+full_cycle,frame,sizeof(frame),&written)==1);
    assert(frame[30]==0 && sync.due_us>psf);
}

static void unicast_window(bool clock_known)
{
    struct owly_native_link s;
    uint8_t raw[256], eth[55]={0}, wire[OWLY_LINK_FRAME_MAX];
    size_t written;
    const uint64_t heard=2000000;

    memcpy(eth,root,6);memcpy(eth+6,local,6);
    eth[12]=0x86;eth[13]=0xdd;eth[14]=0x60;eth[19]=1;
    eth[22]=0xfe;eth[23]=0x80;eth[38]=0xfe;eth[39]=0x80;

    start(&s);
    if(!clock_known) {
        size_t n=fixture(raw,0,9,0,false);
        assert(owly_link_observe(&s,raw,n,149,-40,heard));
        assert(!s.peers[0].clock_valid);
        assert(owly_link_enqueue(&s,eth,sizeof(eth),heard+100)==0);
        assert(send_next(&s,heard+4097,wire,sizeof(wire),&written)==0);
        const uint64_t inbound=heard+8000u;
        assert(owly_link_admit_ipv6(&s,root,local,eth+14,sizeof(eth)-14,inbound)==OWLY_LINK_IPV6_ACCEPTED);
        assert(send_next(&s,inbound+1u,wire,sizeof(wire),&written)==1);
        assert(written==81 && !memcmp(wire+4,root,6));
        assert(owly_link_enqueue(&s,eth,sizeof(eth),inbound+2u)==0);
        assert(!send_next(&s,inbound+4097u,wire,sizeof(wire),&written));
        n=fixture(raw,0,9,7,false);
        const uint64_t heard_again=heard+7*OWLY_LINK_AW_US;
        assert(owly_link_observe(&s,raw,n,149,-40,heard_again));
        assert(!s.peers[0].clock_valid);
        assert(send_next(&s,heard_again+1000,wire,sizeof(wire),&written)==1);
        assert(written==81 && !memcmp(wire+4,root,6));
        return;
    }

    /* Valid phase with only slot 15 on our channel. A queued frame must
     * wait past the old 500 ms deadline and leave only during that slot. */
    for(unsigned i=0;i<3;i++) {
        size_t n=fixture(raw,0,9,i,false);
        memset(raw+83,44,15);
        assert(owly_link_observe(&s,raw,n,149,-40,heard+i*OWLY_LINK_AW_US));
    }
    const uint64_t queued=heard+2*OWLY_LINK_AW_US;
    assert(s.peers[0].clock_valid);
    assert(owly_link_enqueue(&s,eth,sizeof(eth),queued)==0);
    assert(send_next(&s,queued+1000,wire,sizeof(wire),&written)==0);
    assert(send_next(&s,queued+510000,wire,sizeof(wire),&written)==0 && s.count==1);
    const uint64_t slot15=heard+9*1024u+59*OWLY_LINK_AW_US+4096;
    assert(send_next(&s,slot15,wire,sizeof(wire),&written)==1);
    assert(!memcmp(wire+4,root,6));
    assert(owly_link_enqueue(&s,eth,sizeof(eth),slot15)==0);
    assert(send_next(&s,slot15+1100000u,wire,sizeof(wire),&written)==0 && s.count==0);
}
int main(int argc,char **argv)
{
    const char *mode=argc>1?argv[1]:"all";
    if(!strcmp(mode,"all")||!strcmp(mode,"advertising"))advertising();
    if(!strcmp(mode,"all")||!strcmp(mode,"clock"))clock_fields(false);
    if(!strcmp(mode,"all")||!strcmp(mode,"warmup"))clock_fields(true);
    if(!strcmp(mode,"all")||!strcmp(mode,"guards"))relay_and_bounds();
    if(!strcmp(mode,"all")||!strcmp(mode,"unicast-recent"))unicast_window(false);
    if(!strcmp(mode,"all")||!strcmp(mode,"unicast-slot"))unicast_window(true);
    if(!strcmp(mode,"all")||!strcmp(mode,"apple-channel-layout"))apple_channel_layout();
    if(!strcmp(mode,"all")||!strcmp(mode,"legacy-peer"))legacy_peer_channels();
    if(!strcmp(mode,"all")||!strcmp(mode,"rx-order"))rx_after_timer_advance();
    if(!strcmp(mode,"all")||!strcmp(mode,"iphone-clock"))iphone_slot_clock();
    if(!strcmp(mode,"all")||!strcmp(mode,"iphone-channels"))iphone_mixed_channels();
    if(!strcmp(mode,"all")||!strcmp(mode,"multicast-slot"))multicast_queue(true);
    if(!strcmp(mode,"all")||!strcmp(mode,"multicast-clock-change"))multicast_queue(false);
    if(!strcmp(mode,"all")||!strcmp(mode,"idle-discovery")) {
        idle_discovery(true,6);
        idle_discovery(true,44);
        idle_discovery(true,149);
    }
    if(!strcmp(mode,"all")||!strcmp(mode,"dense-discovery"))idle_discovery(false,149);
    if(!strcmp(mode,"all")||!strcmp(mode,"transport-startup"))transport_startup();
    if(!strcmp(mode,"all")||!strcmp(mode,"actual-recipient-slot")) {
        actual_recipient_slot(6);
        actual_recipient_slot(44);
        actual_recipient_slot(149);
    }
    if(!strcmp(mode,"all")||!strcmp(mode,"unicast-independent-peers"))unicast_independent_peers();
    if(!strcmp(mode,"all")||!strcmp(mode,"local-master-schedule"))local_master_schedule();
    if(!strcmp(mode,"all")||!strcmp(mode,"sparse-first-reply"))sparse_first_reply();
    if(!strcmp(mode,"all")||!strcmp(mode,"shared-slot-advertising"))shared_slot_advertising();
    puts("native sync regression: iOS phase, recipient slots on 6/44/149, Bonjour and per-peer unicast passed");
    return 0;
}
