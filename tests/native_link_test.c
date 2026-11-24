/* SPDX-License-Identifier: GPL-2.0 */
#include "owly_native_link.h"
#include "owly_awdl_sync.h"
#include "owly_awdl_frame.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const uint8_t local[6]={2,1,2,3,4,5},remote[6]={2,6,7,8,9,10};
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"FAIL line %u: %s\n",__LINE__,#x);abort();}}while(0)
static struct owly_native_link link_state;
static uint8_t raw[2048],wire[2048],eth[1600];
static size_t action(uint64_t now,uint16_t *seq) {
    static const uint8_t tlvs[]={6,9,0,0,0,0,0,0,0,0,0,0,21,2,0,0x3e,1};
    struct owly_sync s;size_t n=0;
    owly_sync_reset(&s);CHECK(!owly_sync_template(&s,tlvs,sizeof(tlvs),remote));
    owly_sync_request(&s,true);owly_sync_reconcile(&s,true,149,now);
    CHECK(owly_sync_frame(&s,now,raw,sizeof(raw),&n)==1);
    if(seq) {raw[72]=(uint8_t)*seq;raw[73]=(uint8_t)(*seq>>8);}
    return n;
}
static void start(void) {
    owly_link_reset(&link_state);owly_link_reconcile(&link_state,true,local,149,1000000);
    link_state.sync_enabled=true;
}
static size_t ethernet(bool multicast) {
    memset(eth,0,sizeof(eth));memcpy(eth,multicast ? (uint8_t[]){0x33,0x33,0x80,0,0,0xfb} : remote,6);
    memcpy(eth+6,local,6);eth[12]=0x86;eth[13]=0xdd;
    uint8_t *ip=eth+14;ip[0]=0x60;ip[5]=8;ip[6]=17;ip[7]=255;
    ip[8]=0xfe;ip[9]=0x80;ip[23]=1;
    if(multicast){ip[24]=0xff;ip[25]=2;ip[39]=0xfb;}else{ip[24]=0xfe;ip[25]=0x80;ip[39]=2;}
    ip[45]=8;return 62;
}
static void codec(void) {
    size_t n=ethernet(true),out=0;struct owly_awdl_batch batch;
    CHECK(!owly_link_build_tx(eth,n,local,4095,17,wire,sizeof(wire),&out));
    CHECK(out==24+16+48);CHECK(!memcmp(wire+4,(uint8_t[]){0x33,0x33,0,0,0,0xfb},6));
    CHECK(wire[22]==0xf0 && wire[23]==0xff);
    CHECK(owly_awdl_parse_80211(wire,out,&batch)==OWLY_AWDL_OK && batch.count==1);
    CHECK(batch.parts[0].packet_len==48 && !memcmp(batch.parts[0].packet,eth+14,48) && batch.parts[0].awdl_seq==17);
    for (unsigned seq=0; seq<4096; ++seq) {
        CHECK(!owly_link_build_tx(eth,n,local,seq,17,wire,sizeof(wire),&out));
        CHECK(wire[22]==(uint8_t)(seq<<4) && wire[23]==(uint8_t)(seq>>4));
    }
    /* Preserve Apple's payload and framing while assigning our wire sequence. */
    uint8_t wrapped[128];memcpy(wrapped,eth,12);size_t payload=out-24;
    wrapped[12]=(uint8_t)(payload>>8);wrapped[13]=(uint8_t)payload;memcpy(wrapped+14,wire+24,payload);
    uint8_t expected[128];memcpy(expected,wrapped+14,payload);
    expected[10]=999&255;expected[11]=999>>8;
    CHECK(!owly_link_build_tx(wrapped,14+payload,local,3,999,wire,sizeof(wire),&out));
    CHECK(out==24+payload && !memcmp(wire+24,expected,payload));
    CHECK(owly_awdl_parse_80211(wire,out,&batch)==OWLY_AWDL_OK);
    wrapped[13]--;CHECK(owly_link_build_tx(wrapped,14+payload,local,0,0,wire,sizeof(wire),&out)==-EINVAL);
    n=ethernet(false);eth[6]^=1;CHECK(owly_link_build_tx(eth,n,local,0,0,wire,sizeof(wire),&out)==-EINVAL);
    n=ethernet(false);eth[13]=0;CHECK(owly_link_build_tx(eth,n,local,0,0,wire,sizeof(wire),&out)==-EOPNOTSUPP);
    n=ethernet(false);CHECK(owly_link_build_tx(eth,n,local,0,0,wire,16,&out)==-EMSGSIZE);
    CHECK(owly_link_build_tx(eth,n,local,0,0,eth,sizeof(eth),&out)==-EINVAL);
    eth[19]=9;CHECK(owly_link_build_tx(eth,n,local,0,0,wire,sizeof(wire),&out)==-EINVAL);
}
static void qos_length_envelope(void)
{
    const uint8_t llc[8]={0xaa,0xaa,3,0,0,0,0,64};
    struct owly_awdl_batch batch;
    uint8_t wrapped[256], aggregate[256]={0};
    size_t n=ethernet(false),out=0;
    CHECK(!owly_awdl_pack_ipv6(eth+14,n-14,local,remote,21,42,wire,sizeof(wire),&out));
    CHECK(out==90);
    memcpy(wrapped,wire,26);
    memcpy(wrapped+26,llc,sizeof(llc));
    memcpy(wrapped+34,wire+26,64);
    CHECK(owly_awdl_parse_80211(wrapped,98,&batch)==OWLY_AWDL_OK && batch.count==1);
    CHECK(batch.parts[0].packet_len==48 && batch.parts[0].awdl_seq==42);
    CHECK(!memcmp(batch.parts[0].packet,eth+14,48));
    CHECK(!memcmp(batch.parts[0].dst_mac,local,6) && !memcmp(batch.parts[0].src_mac,remote,6));
    CHECK(owly_awdl_parse_80211(wrapped,97,&batch)==OWLY_AWDL_MALFORMED && !batch.count);
    wrapped[33]=63;
    CHECK(owly_awdl_parse_80211(wrapped,98,&batch)==OWLY_AWDL_MALFORMED && !batch.count);
    wrapped[33]=64;wrapped[31]=1;
    CHECK(owly_awdl_parse_80211(wrapped,98,&batch)==OWLY_AWDL_MALFORMED && !batch.count);
    wrapped[31]=0;
    memcpy(aggregate,wire,26);
    aggregate[24]=0x80;
    /* 14 + 72, then two bytes of pad, then another 14 + 64. */
    memcpy(aggregate+26,wire+4,12);aggregate[39]=72;
    memcpy(aggregate+40,wrapped+26,72);
    memcpy(aggregate+114,wire+4,12);aggregate[127]=64;
    memcpy(aggregate+128,wire+26,64);
    CHECK(owly_awdl_parse_80211(aggregate,192,&batch)==OWLY_AWDL_OK && batch.count==2);
    CHECK(!memcmp(batch.parts[0].packet,eth+14,48) && !memcmp(batch.parts[1].packet,eth+14,48));
    aggregate[47]=65;
    CHECK(owly_awdl_parse_80211(aggregate,192,&batch)==OWLY_AWDL_MALFORMED && !batch.count);
    aggregate[47]=64;aggregate[112]=1;
    CHECK(owly_awdl_parse_80211(aggregate,192,&batch)==OWLY_AWDL_MALFORMED && !batch.count);
    wrapped[0]=8;
    memmove(wrapped+24,wrapped+26,72);
    CHECK(owly_awdl_parse_80211(wrapped,96,&batch)==OWLY_AWDL_MALFORMED && !batch.count);
}
static void peers(void) {
    start();size_t n=action(1000000,NULL);struct owly_link_window w;
    CHECK(!owly_link_observe(&link_state,raw,n,44,-45,2000000));
    CHECK(!owly_link_observe(&link_state,raw,n,149,0,2000000));
    CHECK(owly_link_observe(&link_state,raw,n,149,-45,2000000));
    CHECK(owly_link_window(&link_state,2000100,&w) && !w.remote);
    for(unsigned i=1;i<=3;i++) {
        n=action(1000000+(uint64_t)i*7*16384,NULL);
        CHECK(owly_link_observe(&link_state,raw,n,149,-45,2000000+(uint64_t)i*7*16384));
    }
    uint64_t now=2000000+3*7*16384;
    CHECK(owly_link_window(&link_state,now,&w) && w.remote);
    const struct owly_link_peer *peer=&link_state.peers[0];
    CHECK(owly_link_peer_action_rssi(peer,now)==-45);
    CHECK(owly_link_peer_action_rssi(peer,now-1)==0);
    CHECK(owly_link_peer_action_rssi(peer,now+OWLY_LINK_PEER_TTL_US)==-45);
    struct owly_link_peer data_only=*peer;
    data_only.last_data_rx_us=now+OWLY_LINK_PEER_TTL_US+1;
    CHECK(owly_link_peer_alive(&data_only,data_only.last_data_rx_us));
    CHECK(owly_link_peer_action_rssi(&data_only,data_only.last_data_rx_us)==0);
    CHECK(!memcmp(w.master,remote,6));
    CHECK(w.sequence==(uint16_t)((1000000+3*7*16384)/16384));
    uint8_t source[6];memcpy(source,remote,6);size_t e=ethernet(false);
    CHECK(owly_link_admit_ipv6(&link_state,source,local,eth+14,e-14,now)==OWLY_LINK_IPV6_ACCEPTED);
    source[5]^=1;
    CHECK(owly_link_admit_ipv6(&link_state,source,local,eth+14,e-14,now)==OWLY_LINK_IPV6_NO_PEER);
    memcpy(source,remote,6);
    CHECK(owly_link_admit_ipv6(&link_state,source,local,eth+14,e-15,now)==OWLY_LINK_IPV6_INVALID);
    CHECK(owly_link_admit_ipv6(&link_state,source,remote,eth+14,e-14,now)==OWLY_LINK_IPV6_BAD_DEST);
    e=ethernet(true);
    const uint8_t mdns[]={0x33,0x33,0,0,0,0xfb};
    CHECK(owly_link_admit_ipv6(&link_state,source,mdns,eth+14,e-14,now)==OWLY_LINK_IPV6_ACCEPTED);
    CHECK(owly_link_admit_ipv6(&link_state,source,local,eth+14,e-14,now+OWLY_LINK_PEER_TTL_US+1)==OWLY_LINK_IPV6_NO_PEER);
    CHECK(owly_link_window(&link_state,now+OWLY_LINK_PEER_TTL_US+1,&w) && !w.remote && !w.peers);
    owly_link_prefer(&link_state,remote);CHECK(!owly_link_window(&link_state,now+OWLY_LINK_PEER_TTL_US+2,&w) && !w.active);
    /* Replayed RX time never replaces a newer timing sample. */
    start();n=action(1000000,NULL);CHECK(owly_link_observe(&link_state,raw,n,149,-40,2000000));
    uint64_t prior=link_state.peers[0].last_rx_us;
    CHECK(!owly_link_observe(&link_state,raw,n,149,-40,2000000));CHECK(link_state.peers[0].last_rx_us==prior);
}
static void queue(void) {
    size_t n,out=0;unsigned offset;uint64_t generation;start();n=ethernet(true);
    CHECK(!owly_link_enqueue(&link_state,eth,n,1000100));
    CHECK(owly_link_next_tx(&link_state,1000200,true,wire,sizeof(wire),&out,&offset,&generation)==1);
    CHECK(out==88 && link_state.tx_handoffs==0 && link_state.tx_radio_acked==0);
    owly_link_tx_result(&link_state,offset,generation,-ENOSPC);
    CHECK(link_state.count==1 && !link_state.tx_refused && !link_state.wifi_sequence);
    CHECK(owly_link_next_tx(&link_state,1000201,true,wire,sizeof(wire),&out,&offset,&generation)==1);
    CHECK(!wire[22] && !wire[23] && !wire[34] && !wire[35]);
    owly_link_tx_result(&link_state,offset,generation,0);CHECK(link_state.tx_handoffs==1 && !link_state.tx_radio_acked);
    CHECK(owly_link_next_tx(&link_state,1000300,true,wire,sizeof(wire),&out,&offset,&generation)==0 && out==0);
    for(unsigned i=0;i<OWLY_LINK_QUEUE;i++)CHECK(!owly_link_enqueue(&link_state,eth,n,1000400+i));
    CHECK(owly_link_enqueue(&link_state,eth,n,1000400+OWLY_LINK_QUEUE)==-ENOBUFS);
    owly_link_reconcile(&link_state,false,NULL,0,1000400+OWLY_LINK_QUEUE+100);CHECK(link_state.count==0 && link_state.tx_expired==OWLY_LINK_QUEUE);
    CHECK(owly_link_enqueue(&link_state,eth,n,1000400+OWLY_LINK_QUEUE+200)==-ENETDOWN);
    start();CHECK(!owly_link_enqueue(&link_state,eth,n,1000100));
    CHECK(owly_link_next_tx(&link_state,2100101,true,wire,sizeof(wire),&out,&offset,&generation)==0 && link_state.tx_expired==1);
    start();CHECK(!owly_link_enqueue(&link_state,eth,n,1000100));
    CHECK(owly_link_next_tx(&link_state,999999,true,wire,sizeof(wire),&out,&offset,&generation)==0 && link_state.count==0);
    start();n=ethernet(false);CHECK(owly_link_enqueue(&link_state,eth,n,1000100)==-EHOSTUNREACH);
    link_state.sync_enabled=false;n=ethernet(true);CHECK(owly_link_enqueue(&link_state,eth,n,1000200)==-ENETDOWN);

    start();CHECK(!owly_link_enqueue(&link_state,eth,n,1000100));
    CHECK(owly_link_next_tx(&link_state,1000200,true,wire,sizeof(wire),&out,&offset,&generation)==1);
    owly_link_reconcile(&link_state,true,local,44,1000250);
    CHECK(!owly_link_enqueue(&link_state,eth,n,1000300));
    owly_link_tx_result(&link_state,offset,generation,0);
    CHECK(link_state.count==1 && !link_state.tx_handoffs && link_state.tx_expired==1);
}
static void malformed(void) {
    start();size_t n=action(1000000,NULL);uint8_t copy[2048];memcpy(copy,raw,n);
    for(size_t size=0;size<n;size++) {
        struct owly_native_link *s=calloc(1,sizeof(*s));CHECK(s!=NULL);
        owly_link_reconcile(s,true,local,149,2000000);
        (void)owly_link_observe(s,raw,size,149,-50,2000100);free(s);
    }
    memcpy(raw+10,local,6);CHECK(!owly_link_observe(&link_state,raw,n,149,-50,2000100));
    memcpy(raw,copy,n);raw[58]=17;CHECK(!owly_link_observe(&link_state,raw,n,149,-50,2000100));
    memcpy(raw,copy,n);raw[1]|=4;CHECK(!owly_link_observe(&link_state,raw,n,149,-50,2000100));
    memcpy(raw,copy,n);raw[22]|=1;CHECK(!owly_link_observe(&link_state,raw,n,149,-50,2000100));
}
static void native_controls(void) {
    struct owly_native_config c={0};struct owly_cfg_u32 channel={0,44};
    struct owly_cfg_sync sync={0};struct owly_cfg_sequence slots;
    CHECK(!owly_config_request(&c,true,132,&channel,local,149));
    channel.value=0;CHECK(!owly_config_request(&c,false,132,&channel,local,149) && channel.value==44);
    sync.state=1;memcpy(sync.master,remote,6);
    CHECK(!owly_config_request(&c,true,134,&sync,local,149));
    CHECK(!memcmp(c.sync.master,remote,6));
    owly_config_sequence(&slots,149);CHECK(!owly_config_request(&c,true,129,&slots,local,149));
    CHECK(c.master_channel==44);slots.channels[0].channel=44;
    CHECK(owly_config_request(&c,true,129,&slots,local,149)==-EOPNOTSUPP);
    CHECK(c.master_channel==44);
}
static void observed_header00(void) {
    /* The LOQ reports a 03 00 native wrapper. This IPv6 payload is synthetic. */
    static const uint8_t mac[6]={0x82,0x74,0x02,0x58,0x56,0x51};
    static const uint8_t snap[8]={0xaa,0xaa,3,0,0x17,0xf2,8,0};
    uint8_t input[763]={0},copy[749];size_t out=0;struct owly_awdl_batch batch;
    memcpy(input,(uint8_t[]){0x33,0x33,0x80,0,0,0xfb},6);memcpy(input+6,mac,6);
    input[12]=749>>8;input[13]=749&255;memcpy(input+14,snap,8);
    input[22]=3;input[24]=0x34;input[25]=0x12;input[28]=0x86;input[29]=0xdd;
    uint8_t *ip=input+30;ip[0]=0x60;ip[4]=693>>8;ip[5]=693&255;ip[6]=17;ip[7]=255;
    ip[8]=0xfe;ip[9]=0x80;ip[23]=1;ip[24]=0xff;ip[25]=2;ip[39]=0xfb;
    memcpy(copy,input+14,sizeof(copy));
    copy[9]=4; /* On-air data header seen in the public iOS 26 captures. */
    copy[10]=999&255;copy[11]=999>>8;
    CHECK(!owly_link_build_tx(input,sizeof(input),mac,3,999,wire,sizeof(wire),&out));
    CHECK(out==773 && !memcmp(wire+24,copy,sizeof(copy)));
    CHECK(!memcmp(wire+4,(uint8_t[]){0x33,0x33,0,0,0,0xfb},6));
    CHECK(owly_awdl_parse_80211(wire,out,&batch)==OWLY_AWDL_OK && batch.count==1);
    CHECK(batch.parts[0].packet_len==733 && !memcmp(batch.parts[0].packet,ip,733));
    CHECK(batch.parts[0].awdl_seq==999);
    for(unsigned variant=0;variant<256;variant++) {
        input[23]=(uint8_t)variant;
        int rc=owly_link_build_tx(input,sizeof(input),mac,3,999,wire,sizeof(wire),&out);
        CHECK((variant==0 || variant==4) ? rc==0 : rc==-EINVAL);
        /* Exercise RX separately even if TX refused this variant. */
        input[23]=0;CHECK(!owly_link_build_tx(input,sizeof(input),mac,3,999,wire,sizeof(wire),&out));
        wire[33]=(uint8_t)variant;
        CHECK((owly_awdl_parse_80211(wire,out,&batch)==OWLY_AWDL_OK)==(variant==0 || variant==4));
    }
    input[26]=1;CHECK(owly_link_build_tx(input,sizeof(input),mac,0,0,wire,sizeof(wire),&out)==-EINVAL);input[26]=0;
    input[13]--;CHECK(owly_link_build_tx(input,sizeof(input),mac,0,0,wire,sizeof(wire),&out)==-EINVAL);input[13]++;
    ip[5]--;CHECK(owly_link_build_tx(input,sizeof(input),mac,0,0,wire,sizeof(wire),&out)==-EINVAL);ip[5]++;
    input[6]^=1;CHECK(owly_link_build_tx(input,sizeof(input),mac,0,0,wire,sizeof(wire),&out)==-EINVAL);input[6]^=1;
    owly_link_reset(&link_state);owly_link_reconcile(&link_state,true,mac,149,1000000);link_state.sync_enabled=true;
    unsigned offset;uint64_t generation;
    copy[10]=copy[11]=0; /* First real queued packet receives node sequence0. */
    CHECK(!owly_link_enqueue(&link_state,input,sizeof(input),1000100));
    CHECK(owly_link_next_tx(&link_state,1000200,true,wire,sizeof(wire),&out,&offset,&generation)==1);
    CHECK(out==773 && !memcmp(wire+24,copy,sizeof(copy)));
    CHECK(link_state.tx_enqueued==1 && !link_state.tx_handoffs && !link_state.tx_radio_acked);
    owly_link_tx_result(&link_state,offset,generation,0);CHECK(link_state.tx_handoffs==1 && !link_state.tx_radio_acked);
}
static void scan_activity_lifetime(void)
{
    start();CHECK(!owly_link_data_busy(&link_state,1000000));
    size_t n=action(1000000,NULL);
    CHECK(owly_link_observe(&link_state,raw,n,149,-45,2000000));
    n=ethernet(true);CHECK(!owly_link_enqueue(&link_state,eth,n,2000100));
    CHECK(!owly_link_data_busy(&link_state,2000100)); // mDNS alone cannot block scans
    const uint8_t mdns[6]={0x33,0x33,0,0,0,0xfb};
    CHECK(owly_link_admit_ipv6(&link_state,remote,mdns,eth+14,n-14,2000200)==OWLY_LINK_IPV6_ACCEPTED);
    CHECK(!owly_link_data_busy(&link_state,2000200));
    n=ethernet(false);CHECK(!owly_link_enqueue(&link_state,eth,n,2000300));
    CHECK(owly_link_data_busy(&link_state,2000300));
    CHECK(!owly_link_data_busy(&link_state,2000300+4000000)); // stale queue bounded too
    link_state.sync_enabled=false;CHECK(!owly_link_data_busy(&link_state,2000300));
    link_state.sync_enabled=true;
    owly_link_reconcile(&link_state,true,local,44,2000400);
    CHECK(!owly_link_data_busy(&link_state,2000400) && !link_state.last_unicast_activity_us);
    start();n=action(1000000,NULL);CHECK(owly_link_observe(&link_state,raw,n,149,-45,2000000));
    n=ethernet(false);
    CHECK(owly_link_admit_ipv6(&link_state,remote,local,eth+14,n-14,2000100)==OWLY_LINK_IPV6_ACCEPTED);
    CHECK(owly_link_data_busy(&link_state,2000100));
    CHECK(owly_link_data_busy(&link_state,4000100));
    CHECK(!owly_link_data_busy(&link_state,4000101));
    owly_link_reconcile(&link_state,true,local,149,999999); // real clock rollback
    CHECK(!owly_link_data_busy(&link_state,1000000) && !link_state.last_unicast_activity_us);
}
static void control_reply_slot(void)
{
    start();
    struct owly_link_peer *p=&link_state.peers[0];
    p->used=p->clock_valid=true;memcpy(p->mac,remote,6);memcpy(p->master,remote,6);
    p->channel=p->master_channel=149;p->local_slots=1;p->rssi=-40;
    p->last_rx_us=p->clock_rx_us=p->anchor_us=2000000;p->next_seq=1;
    for(unsigned i=0;i<16;i++)p->channels[i]=6;
    p->channels[0]=149;
    /* After 4ms the proactive query wake interval has ended, but the
     * peer's confirmed channel slot still permits a BlockAck reply. */
    CHECK(!owly_link_unicast_action_tx_ready(&link_state,remote,2006000));
    CHECK(owly_link_unicast_reply_tx_ready(&link_state,remote,2006000));
    CHECK(!owly_link_unicast_reply_tx_ready(&link_state,remote,2000000+2*16384+15360));
    CHECK(!owly_link_unicast_reply_tx_ready(&link_state,remote,2000000+3*16384+6000));
    /* In a '.' (0) slot, recent RX data on our channel permits immediate
     * unicast replies/ACKs, while explicit other channels (ch 6) still gate. */
    const uint64_t slot1=2000000+3*16384+6000;
    p->channels[1]=0;
    CHECK(!owly_link_unicast_reply_tx_ready(&link_state,remote,slot1));
    p->last_data_rx_us=slot1-1000;
    CHECK(owly_link_unicast_reply_tx_ready(&link_state,remote,slot1));
    CHECK(!owly_link_unicast_action_tx_ready(&link_state,remote,slot1));
    p->last_data_rx_us=slot1-OWLY_LINK_AW_US-1;
    CHECK(!owly_link_unicast_reply_tx_ready(&link_state,remote,slot1));
    p->channels[1]=6;p->last_data_rx_us=slot1-1000;
    CHECK(!owly_link_unicast_reply_tx_ready(&link_state,remote,slot1));
    p->last_data_rx_us=0;
    uint8_t other[6];memcpy(other,remote,6);other[5]^=2;
    CHECK(!owly_link_unicast_reply_tx_ready(&link_state,other,2006000));
    link_state.tx_paused=true;
    CHECK(!owly_link_unicast_reply_tx_ready(&link_state,remote,2006000));
    link_state.tx_paused=false;link_state.sync_enabled=false;
    CHECK(!owly_link_unicast_reply_tx_ready(&link_state,remote,2006000));
    link_state.sync_enabled=true;
    CHECK(!owly_link_unicast_reply_tx_ready(&link_state,remote,4000001));
    link_state.active=false;
    CHECK(!owly_link_unicast_reply_tx_ready(&link_state,remote,2006000));
}
static void ht_receive_rates(void)
{
    static const uint8_t ht[]={7,8,0,0,0,0x0c,0,0,0xf1,0,0};
    const uint64_t now=2000000;
    start();
    size_t n=action(1000000,NULL);
    CHECK(owly_link_observe(&link_state,raw,n,149,-42,now));
    struct owly_link_peer *peer=&link_state.peers[0];
    CHECK(peer->used && !peer->ht_rx_mcs);

    memcpy(raw+n,ht,sizeof(ht));
    CHECK(!owly_link_observe(&link_state,raw,n+sizeof(ht)-1,149,-42,now+1));
    CHECK(!peer->ht_rx_mcs);
    CHECK(owly_link_observe(&link_state,raw,n+sizeof(ht),149,-42,now+2));
    CHECK(peer->ht_rx_mcs==0xf1);

    /* A PSF without the optional element leaves the known mask intact. */
    CHECK(owly_link_observe(&link_state,raw,n,149,-42,now+3));
    CHECK(peer->ht_rx_mcs==0xf1);
    memcpy(raw+n+sizeof(ht),ht,sizeof(ht));
    raw[n+sizeof(ht)+8]=0;
    CHECK(owly_link_observe(&link_state,raw,n+2*sizeof(ht),149,-42,now+4));
    CHECK(peer->ht_rx_mcs==0xf1);
    raw[n+1]=7;
    CHECK(owly_link_observe(&link_state,raw,n+sizeof(ht)-1,149,-42,now+5));
    CHECK(peer->ht_rx_mcs==0xf1);
    memcpy(raw+n,ht,sizeof(ht));
    raw[n+8]=0;
    CHECK(owly_link_observe(&link_state,raw,n+sizeof(ht),149,-42,now+6));
    CHECK(!peer->ht_rx_mcs);

    /* VHT TLV17 (14 bytes: EID 191, len 12, cap + MCS maps) and 2SS HT TLV7. */
    static const uint8_t ht2[]={7,9,0,0,0,0xef,0x01,0x17,0xff,0xff,0,0};
    static const uint8_t vht[]={17,14,0,0xbf,0x0c,0xb1,0x01,0x80,0x03,0xfa,0xff,0,0,0xfa,0xff,0,0};
    memcpy(raw+n,ht2,sizeof(ht2));
    memcpy(raw+n+sizeof(ht2),vht,sizeof(vht));
    CHECK(owly_link_observe(&link_state,raw,n+sizeof(ht2)+sizeof(vht),149,-42,now+7));
    CHECK(peer->ht_rx_mcs==0xffff && peer->ht_cap_info==0x01ef && peer->ht_ampdu_params==0x17);
    CHECK(peer->has_vht_cap && peer->vht_cap_info==0x038001b1u && peer->vht_rx_mcs_map==0xfffa);

    /* Promote built unicast Non-QoS frame to QoS Data with exact sequence. */
    size_t eth_len=ethernet(false), wlen=0;
    CHECK(!owly_link_build_tx(eth,eth_len,local,7,9,wire,sizeof(wire),&wlen));
    CHECK(wire[0]==0x08 && wlen==eth_len-14+40);
    uint8_t snap_copy[16]; memcpy(snap_copy,wire+24,16);
    CHECK(owly_link_promote_qos(wire,&wlen,sizeof(wire),0x345,0));
    CHECK(wire[0]==0x88 && wire[22]==0x50 && wire[23]==0x34 && wire[24]==0 && wire[25]==0);
    CHECK(!memcmp(wire+26,snap_copy,16) && wlen==eth_len-14+42);
}

int main(int argc,char **argv)
{
    if (argc==2 && !strcmp(argv[1],"header")) observed_header00();
    else {ht_receive_rates();codec();qos_length_envelope();peers();queue();scan_activity_lifetime();control_reply_slot();malformed();native_controls();observed_header00();}
    printf("native link production: %u checks passed; no RF proof\n",checks);
    return 0;
}
