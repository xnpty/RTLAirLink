/* SPDX-License-Identifier: GPL-2.0 */
#include "owly_awdl_ba.h"
#include <assert.h>
#include <stdio.h>
static unsigned got[8192],count,drops;
static void emit(void *ctx,void *p){(void)ctx;assert(count<8192);got[count++]=(unsigned)(uintptr_t)p;}
static void drop(void *ctx,void *p){(void)ctx;assert(p);++drops;}
int main(void){
 const uint8_t own[6]={2,1,2,3,4,5},peer[6]={2,6,7,8,9,10};
 uint8_t f[64]={0xd0},out[33];struct owly_ba_request r;
 memcpy(f+4,own,6);memcpy(f+10,peer,6);memcpy(f+16,(uint8_t[]){0,0x25,0,0xff,0x94,0x73},6);
 f[24]=3;f[26]=19;owly_ba_put16(f+27,(uint16_t)(3|(7<<2)|(128<<6)));owly_ba_put16(f+29,911);owly_ba_put16(f+31,4093<<4);
 assert(owly_ba_parse(f,33,own,&r) && r.window==64 && r.tid==7 && r.immediate && r.ssn==4093);
 assert(owly_ba_response(f,own,&r,0,4095,out)==33 && out[26]==19 && !owly_ba_le16(out+27));
 assert(!memcmp(out+4,peer,6) && !memcmp(out+10,own,6));
 assert(owly_ba_le16(out+29)==(2|(7<<2)|(64<<6)) && owly_ba_le16(out+31)==911);
 for(unsigned n=0;n<33;n++)assert(!owly_ba_parse(f,n,own,&r));
 for(unsigned bit=0;bit<8;bit++)if((1u<<bit)&0x47){f[1]=(uint8_t)(1u<<bit);assert(!owly_ba_parse(f,33,own,&r));f[1]=0;}
 f[4]^=2;assert(!owly_ba_parse(f,33,own,&r));f[4]^=2;f[16]^=1;assert(!owly_ba_parse(f,33,own,&r));f[16]^=1;
 f[31]|=1;assert(!owly_ba_parse(f,33,own,&r));f[31]&=0xf0;
 f[25]=2;owly_ba_put16(f+26,(uint16_t)((15u<<12)|0x800));assert(owly_ba_parse(f,30,own,&r) && r.tid==15 && r.initiator);
 struct owly_ba_ring s={0};
 for(unsigned size=1;size<=64;size++){
  memset(&s,0,sizeof(s));count=drops=0;assert(owly_ba_start(&s,4093,size));
  for(unsigned i=0;i<7000;i++)owly_ba_push(&s,(uint16_t)((4093+i)&4095),(void *)(uintptr_t)(i+1),i+1,emit,drop,NULL);
  assert(count==7000 && !s.stored && !drops);for(unsigned i=0;i<7000;i++)assert(got[i]==i+1);
  owly_ba_clear(&s,drop,NULL);assert(!s.active && !drops);
 }
 memset(&s,0,sizeof(s));count=drops=0;assert(owly_ba_start(&s,4093,63));
 owly_ba_push(&s,4095,(void *)3,1,emit,drop,NULL);owly_ba_push(&s,0,(void *)4,2,emit,drop,NULL);
 owly_ba_push(&s,4095,(void *)3,3,emit,drop,NULL);assert(drops==1 && !count);
 owly_ba_push(&s,4093,(void *)1,4,emit,drop,NULL);owly_ba_push(&s,4094,(void *)2,5,emit,drop,NULL);
 assert(count==4);for(unsigned i=0;i<4;i++)assert(got[i]==i+1);
 owly_ba_push(&s,4094,(void *)2,6,emit,drop,NULL);assert(drops==2);
 owly_ba_push(&s,3,(void *)7,7,emit,drop,NULL);owly_ba_timeout(&s,7u+OWLY_BA_HOLE_US-1u,emit,NULL);assert(count==4);
 owly_ba_timeout(&s,7u+OWLY_BA_HOLE_US,emit,NULL);assert(count==5 && got[4]==7);
 owly_ba_push(&s,6,(void *)10,8,emit,drop,NULL);owly_ba_bar(&s,6,emit,NULL);assert(count==6 && got[5]==10);
 owly_ba_push(&s,8,(void *)12,9,emit,drop,NULL);owly_ba_clear(&s,drop,NULL);assert(drops==3 && !s.active);
 assert(!owly_ba_start(&s,4096,64) && !owly_ba_start(&s,0,65) && !owly_ba_start(&s,0,0));
 puts("production BlockAck parser/reorder: 64 window sizes, wrap, duplicates, BAR, holes and cleanup passed");
}
