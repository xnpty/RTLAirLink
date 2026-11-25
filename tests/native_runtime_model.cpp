/* SPDX-License-Identifier: GPL-2.0 */
/* Executes the actual new frontend runtime functions against controlled kernel
 * boundaries. The subclass/vtable is separately compiled against real SDK. */
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <memory>
#include <thread>
#include <vector>
extern "C" {
#include "owly_native_link.h"
#include "owly_awdl_sync.h"
}
#include "owly_abi6_packet.h"
#include "rtw89_awdl_action_decode.h"
#include "owly_native_peer_gate.h"
using UInt=unsigned;using UInt32=uint32_t;using IOReturn=int;using thread_t=void *;
using thread_call_param_t=void *;
constexpr int kIOReturnOutputDropped=1,kIOReturnOutputSuccess=0,kIOReturnNotReady=6,kIOReturnSuccess=0,THREAD_UNINT=0;
constexpr int kIO80211NetworkLinkUp=2;
constexpr uint32_t APPLE80211_VERSION=1,APPLE80211_C_FLAG_20MHZ=0x2,APPLE80211_C_FLAG_2GHZ=0x8,APPLE80211_C_FLAG_5GHZ=0x10;
struct apple80211_channel {uint32_t version,channel,flags;};
struct packet_info_tag;struct IO80211Controller;
static void *const kOSBooleanTrue=reinterpret_cast<void *>(1);
static unsigned modelPeerClassSize=OWLY_NATIVE_PEER_CLASS_BYTES;
struct OSMetaClass {
    const char *name;
    unsigned getClassSize()const{return modelPeerClassSize;}
    const char *getClassName()const{return name;}
};
struct OSObject {
    std::atomic<int> references{1};bool peer=false;
    void retain(){++references;}void release(){assert(references.fetch_sub(1)>0);}
    void *metaCast(const char *name){return peer && !strcmp(name,"IO80211AWDLPeerManager") ? this : nullptr;}
    const OSMetaClass *getMetaClass()const{
        static const OSMetaClass native={"IO80211AWDLPeerManager"},other={"OSObject"};
        return peer ? &native : &other;
    }
};
struct IO80211PeerManager:OSObject {uint8_t nativeStorage[OWLY_NATIVE_PEER_CLASS_BYTES]={};};
static IO80211PeerManager *modelNativePeer=nullptr;
static void modelGate(uint32_t flags,uint32_t sync){
    auto *bytes=reinterpret_cast<uint8_t *>(modelNativePeer);
    memcpy(bytes+OWLY_NATIVE_PEER_FLAGS_OFFSET,&flags,4);
    memcpy(bytes+OWLY_NATIVE_PEER_SYNC_STATE_OFFSET,&sync,4);
    const int32_t floor=-70;memcpy(bytes+OWLY_NATIVE_PEER_RSSI_FLOOR_OFFSET,&floor,4);
}
struct IO80211VirtualInterface:OSObject {
    bool enabled=false;int link=1;
    uint8_t nativeStorage[0x200]={};
    const OSMetaClass *getMetaClass()const{
        static const OSMetaClass value={"OwlyNativeP2PInterface"};return &value;
    }
    void setEnabledBySystem(bool on){enabled=on;}
    void setLinkState(int state,unsigned){link=state;}
};
struct Buffer {std::vector<uint8_t> bytes;size_t dataLength,packetLength;Buffer *next=nullptr,*nextpkt=nullptr;
    explicit Buffer(size_t n):bytes(n),dataLength(n),packetLength(n){}};
using mbuf_t=Buffer *;
static std::atomic<unsigned> allocations{0},frees{0},actionCalls{0},dataCalls{0},enableCalls{0};
static std::atomic<bool> failAllocation{false},fragmentAllocation{false};
static size_t mbuf_len(mbuf_t m){return m->dataLength;}
static size_t mbuf_trailingspace(mbuf_t m){assert(m->dataLength<=m->bytes.size());return m->bytes.size()-m->dataLength;}
static void *mbuf_data(mbuf_t m){return m->bytes.data();}
static size_t mbuf_pkthdr_len(mbuf_t m){return m->packetLength;}
static mbuf_t mbuf_next(mbuf_t m){return m->next;}
static void mbuf_setnextpkt(mbuf_t m,mbuf_t next){m->nextpkt=next;}
static void mbuf_setlen(mbuf_t m,size_t length){assert(length<=m->bytes.size());m->dataLength=length;}
static void mbuf_pkthdr_setlen(mbuf_t m,size_t length){m->packetLength=length;}
static int mbuf_copydata(mbuf_t m,size_t offset,size_t size,void *out){
    if(offset>m->dataLength || size>m->dataLength-offset)return 1;
    memcpy(out,m->bytes.data()+offset,size);return 0;
}
static void mbuf_freem(mbuf_t m){if(m){++frees;delete m;}}
constexpr int MBUF_DONTWAIT=1;
static int mbuf_allocpacket(int how,size_t length,unsigned *chunks,mbuf_t *out){
    assert(how==MBUF_DONTWAIT && chunks && *chunks==1);
    if(failAllocation){*out=nullptr;return 12;}
    ++allocations;*out=new Buffer(fragmentAllocation ? length-1 : length);
    // XNU reserves capacity here; current data and packet lengths are zero.
    (*out)->dataLength=0;(*out)->packetLength=0;*chunks=1;
    return 0;
}
struct IOLock {std::mutex mutex;std::condition_variable condition;};
static void IOLockLock(IOLock *p){p->mutex.lock();}
static void IOLockUnlock(IOLock *p){p->mutex.unlock();}
static int IOLockSleep(IOLock *p,void *,int){std::unique_lock<std::mutex> lock(p->mutex,std::adopt_lock);p->condition.wait(lock);lock.release();return 0;}
static void IOLockWakeup(IOLock *p,void *,bool){p->condition.notify_all();}
static thread_t current_thread(){thread_local unsigned identity;return &identity;}
struct Call {void(*function)(void *,void *);void *context;std::atomic<bool> pending{false};};
using thread_call_t=Call *;
static thread_call_t thread_call_allocate(void(*f)(void *,void *),void *c){return new Call{f,c};}
static bool thread_call_enter_delayed(thread_call_t p,uint64_t){return p->pending.exchange(true);}
static bool thread_call_cancel(thread_call_t p){return p->pending.exchange(false);}
static bool thread_call_free(thread_call_t p){assert(!p->pending);delete p;return true;}
static uint64_t mach_absolute_time(){return 1000000000;}
static void nanoseconds_to_absolutetime(uint64_t in,uint64_t *out){*out=in;}
static uint32_t OSSwapHostToBigInt32(uint32_t n){return __builtin_bswap32(n);}
#define OSSafeReleaseNULL(p) do {if(p){(p)->release();(p)=nullptr;}}while(0)
struct Loop {
    bool gated=false;bool inGate(){return gated;}
    int runAction(int(*action)(OSObject *,void *,void *,void *,void *),OSObject *owner,void *arg) {
        bool prior=gated;gated=true;int result=action(owner,arg,nullptr,nullptr,nullptr);gated=prior;return result;
    }
};
class RTLAirLinkFront:public OSObject {
public:
#include "native_class_declarations.inc"
    void owlyNativeCapture(IO80211VirtualInterface *,IO80211PeerManager *);
    UInt32 owlyNativeOutput(IO80211VirtualInterface *,mbuf_t);
    IOLock *_owlyVifLock=nullptr;Loop *_workLoop=nullptr;
    bool _owlyAWDLStopping=false,_owlyVifMethodBusy=false,_owlyVifBusy=false,_owlyAWDLRequestBusy=false;
    struct {bool enabled=false;void *cookie=nullptr;} _owlyAirlink;
    struct {
        void *ctx=nullptr;
        int(*native_window)(void *,struct owly_link_window *)=nullptr;
        uint32_t(*native_output)(void *,mbuf_t)=nullptr;
    } _back;
    bool connected=true,verified=false,deferredStop=false;
    std::atomic<int> backCalls{0};struct owly_link_window testWindow{};int readyResult=0;
    bool backEnter(){++backCalls;return connected;}void backLeave(){assert(backCalls.fetch_sub(1)>0);}
    void *getProperty(const char *){return verified ? kOSBooleanTrue : nullptr;}
    void setProperty(const char *,uint64_t,unsigned){}
    void setProperty(const char *,bool){}
    mbuf_t allocatePacket(UInt32 n){if(failAllocation)return nullptr;++allocations;return new Buffer(fragmentAllocation ? n-1 : n);}
    void freePacket(mbuf_t m){mbuf_freem(m);}
    void owlyFinishDeferredStop(){if(deferredStop){deferredStop=false;owlyNativeStop();}}
};
#include "production_native_runtime.inc"

extern "C" unsigned char bufout[0x420]={};
static std::mutex actionMutex;static std::condition_variable actionCondition;
static bool blockAction=false,enteredAction=false,releaseAction=false,reenterStop=false;
static RTLAirLinkFront *activeFront=nullptr;
static std::vector<uint8_t> lastBody,lastMetadata,lastData,lastWindow;
static bool rejectNativeAction=false;

struct ModelNativeEntry {OSObject object;void *manager;uint8_t mac[6];};
static std::vector<std::unique_ptr<ModelNativeEntry>> nativeEntries;
static std::recursive_mutex nativePeerMutex;
static thread_local unsigned nativePeerLockDepth=0;
static bool normalNativeCreatesPeer=true,nativeAddFails=false,nativeAddWrongMAC=false,nativeGenericDefault=false;
static unsigned nativeAddCalls=0;
static OSObject genericDefault;
static OSObject *findNative(void *manager,const uint8_t *mac) {
    for(auto &p:nativeEntries)if(p->manager==manager && !memcmp(p->mac,mac,6))return &p->object;
    return nullptr;
}
static OSObject *createNative(void *manager,const uint8_t *mac) {
    if(auto *existing=findNative(manager,mac))return existing;
    auto entry=std::make_unique<ModelNativeEntry>();entry->manager=manager;memcpy(entry->mac,mac,6);
    if(nativeAddWrongMAC)entry->mac[5]^=1;
    OSObject *result=&entry->object;nativeEntries.push_back(std::move(entry));return result;
}
extern "C" void owly_peer_lock(void *){nativePeerMutex.lock();++nativePeerLockDepth;}
extern "C" void owly_peer_unlock(void *){assert(nativePeerLockDepth);--nativePeerLockDepth;nativePeerMutex.unlock();}
extern "C" OSObject *owly_peer_find(void *manager,uint8_t *mac){assert(nativePeerLockDepth);return findNative(manager,mac);}
extern "C" OSObject *owly_peer_find_base(void *manager,uint8_t *mac){assert(nativePeerLockDepth);return nativeGenericDefault ? &genericDefault:findNative(manager,mac);}
extern "C" UInt32 owly_peer_mac_equal(void *manager,OSObject *peer,uint8_t *mac){
    assert(nativePeerLockDepth);if(!peer)return 256;
    for(auto &p:nativeEntries)if(&p->object==peer)return p->manager==manager && !memcmp(p->mac,mac,6) ? 1:0;
    return 0;
}
extern "C" OSObject *owly_peer_add(void *manager,const void *metadata,mbuf_t packet){
    assert(activeFront->_workLoop->inGate() && nativePeerLockDepth);
    assert(packet && mbuf_len(packet)>=16 && mbuf_pkthdr_len(packet)==mbuf_len(packet));
    int32_t signal;memcpy(&signal,static_cast<const uint8_t *>(metadata)+44,4);assert(signal<0);
    ++nativeAddCalls;
    return nativeAddFails ? nullptr:createNative(manager,static_cast<const uint8_t *>(metadata));
}

extern "C" bool model_process_action(void *,mbuf_t,UInt,void *,size_t)
    __asm__("__ZN22IO80211AWDLPeerManager23actionFrameProcessInputEP6__mbufjPvm");
extern "C" bool model_process_action(void *peer,mbuf_t m,UInt dlt,void *metadata,size_t n) {
    assert(dlt==12 && n==72);++actionCalls;
    assert(m->next==nullptr && m->nextpkt==nullptr && mbuf_pkthdr_len(m)==mbuf_len(m));
    lastBody.assign(m->bytes.begin(),m->bytes.begin()+mbuf_len(m));lastMetadata.assign(static_cast<uint8_t *>(metadata),static_cast<uint8_t *>(metadata)+n);
    if(reenterStop){activeFront->_owlyAWDLStopping=true;activeFront->deferredStop=true;}
    std::unique_lock<std::mutex> lock(actionMutex);
    if(blockAction){enteredAction=true;actionCondition.notify_all();actionCondition.wait(lock,[]{return releaseAction;});}
    owly_native_peer_gate gate{};assert(owlyNativeReadPeerGate(static_cast<OSObject *>(peer),&gate));
    if(!rejectNativeAction && owly_native_peer_rx_open(&gate)) {
        auto *count=reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(peer)+OWLY_NATIVE_PEER_RX_AF_OFFSET+8);
        ++*count;
        if(normalNativeCreatesPeer)createNative(peer,static_cast<const uint8_t *>(metadata));
    }
    return false; /* Deliberately false even when native updateAFStats ran. */
}
/* The old Fix41 BPF path dispatches via the VIF's current peer, which can
 * differ from the retained/configured peer in the old frontend. */
extern "C" void model_old_vif_action(IO80211VirtualInterface *,mbuf_t,UInt,void *,size_t)
    __asm__("__ZN23IO80211VirtualInterface11bpfTapInputEP6__mbufjPvm");
extern "C" void model_old_vif_action(IO80211VirtualInterface *vif,mbuf_t m,UInt d,void *h,size_t n){
    void *peer=nullptr;memcpy(&peer,reinterpret_cast<uint8_t *>(vif)+OWLY_NATIVE_VIF_PEER_OFFSET,sizeof(peer));
    if(peer)(void)model_process_action(peer,m,d,h,n);
}
extern "C" void owly_vif_input(IO80211VirtualInterface *,mbuf_t m,packet_info_tag *){mbuf_freem(m);}
extern "C" void owly_vif_push(IO80211VirtualInterface *,mbuf_t m){++dataCalls;lastData.assign(m->bytes.begin(),m->bytes.begin()+mbuf_len(m));mbuf_freem(m);}
/* Independent symbol binding permits the old, wrong IOReturn declaration to
 * link too. Leave the exact observed RAX garbage: old production code then
 * refuses native enable, corrected code never consumes it. */
extern "C" void model_peer_enable(void *,const char *)
    __asm__("__ZN18IO80211PeerManager6enableEPKc");
extern "C" void model_peer_enable(void *,const char *){
    assert(!activeFront->_workLoop->inGate());
    ++enableCalls;
    /* Native configure sets CONFIGURED before later channel ioctls can fail. */
    uint32_t flags,sync;
    auto *bytes=reinterpret_cast<uint8_t *>(modelNativePeer);
    memcpy(&flags,bytes+OWLY_NATIVE_PEER_FLAGS_OFFSET,4);
    memcpy(&sync,bytes+OWLY_NATIVE_PEER_SYNC_STATE_OFFSET,4);
    modelGate(flags|OWLY_NATIVE_PEER_CONFIGURED,sync);
#if defined(__x86_64__)
    __asm__ volatile("movl $0xe37b0064, %%eax" ::: "eax");
#endif
}
extern "C" void owly_peer_changed(void *,unsigned source){assert(source==7);}
static int sequenceResult=0,masterResult=0,awdlResult=0;
static unsigned sequenceCalls=0,masterCalls=0,awdlCalls=0;
static bool resetGenerationOnEnable=false;
static owly_cfg_sequence lastSequence{};static owly_cfg_u32 lastMaster{};
extern "C" int model_peer_sequence(void *,struct owly_cfg_sequence *)
    __asm__("__ZN22IO80211AWDLPeerManager22setAwdlChannelSequenceEP37apple80211_awdl_sync_channel_sequence");
extern "C" int model_peer_sequence(void *,struct owly_cfg_sequence *s){assert(activeFront->_workLoop->inGate());++sequenceCalls;lastSequence=*s;return sequenceResult;}
extern "C" int model_peer_master(void *,struct owly_cfg_u32 *)
    __asm__("__ZN22IO80211AWDLPeerManager20setAwdlMasterChannelEP30apple80211_awdl_master_channel");
extern "C" int model_peer_master(void *,struct owly_cfg_u32 *m){assert(activeFront->_workLoop->inGate());++masterCalls;lastMaster=*m;return masterResult;}
extern "C" int owly_peer_awdl_enable(void *){    assert(!activeFront->_workLoop->inGate());++awdlCalls;
    if(!awdlResult) {
        uint32_t sync=0;memcpy(&sync,reinterpret_cast<uint8_t *>(modelNativePeer)+OWLY_NATIVE_PEER_SYNC_STATE_OFFSET,4);
        modelGate(OWLY_NATIVE_PEER_CONFIGURED|OWLY_NATIVE_PEER_SYNC_ACTIVE,sync);
    }
    if(!awdlResult && resetGenerationOnEnable)++activeFront->testWindow.generation;
    return awdlResult;}
static std::vector<unsigned> eventCodes;
static std::vector<std::vector<uint8_t>> peerCacheEvents,umiEvents;
extern "C" void owly_peer_window(void *,unsigned code,void *data,unsigned long n){
    assert(activeFront->_workLoop->inGate());
    eventCodes.push_back(code);
    if(code==41) {
        assert(n==36);
        lastWindow.assign(static_cast<uint8_t *>(data),static_cast<uint8_t *>(data)+n);
        uint32_t state;memcpy(&state,lastWindow.data()+4,4);
        memcpy(reinterpret_cast<uint8_t *>(modelNativePeer)+OWLY_NATIVE_PEER_SYNC_STATE_OFFSET,&state,4);
    } else if(code==0x33) {
        assert(n==0x1d0);
        peerCacheEvents.emplace_back(static_cast<uint8_t *>(data),static_cast<uint8_t *>(data)+n);
    } else if(code==0x45) {
        assert(n==64);
        umiEvents.emplace_back(static_cast<uint8_t *>(data),static_cast<uint8_t *>(data)+n);
    } else assert(false);
}

static unsigned checks;
#define CHECK(x) do {++checks;assert(x);}while(0)
struct Fixture {
    RTLAirLinkFront front;IO80211VirtualInterface vif;IO80211PeerManager peer;IOLock lock;Loop loop;
    Fixture(){nativeEntries.clear();normalNativeCreatesPeer=true;nativeAddFails=false;nativeAddWrongMAC=false;nativeGenericDefault=false;nativeAddCalls=0;
        front._owlyVifLock=&lock;front._workLoop=&loop;peer.peer=true;activeFront=&front;modelNativePeer=&peer;
        auto *pointer=&peer;memcpy(reinterpret_cast<uint8_t *>(&vif)+OWLY_NATIVE_VIF_PEER_OFFSET,&pointer,sizeof(pointer));
        front.testWindow.active=true;front.testWindow.channel=149;front.testWindow.master_channel=149;
        front.testWindow.sampled_us=1000000;front.testWindow.until_next_tu=16;
        memcpy(front.testWindow.local,(uint8_t[]){2,1,2,3,4,5},6);
        memcpy(reinterpret_cast<uint8_t *>(&peer)+OWLY_NATIVE_PEER_OWN_MAC_OFFSET,front.testWindow.local,6);
        memcpy(front.testWindow.master,front.testWindow.local,6);
        front._back.ctx=&front;
        front._back.native_window=[](void *p,struct owly_link_window *out){auto *f=static_cast<RTLAirLinkFront *>(p);*out=f->testWindow;return f->readyResult;};
        front._back.native_output=[](void *,mbuf_t m)->uint32_t {mbuf_freem(m);return kIOReturnOutputSuccess;};
        CHECK(front.owlyNativeStart());front.owlyNativeCapture(&vif,&peer);
        front._owlyAirlink.cookie=&vif;front._owlyAirlink.enabled=true;
    }
    void tick(){CHECK(front._owlyNativeCall->pending.exchange(false));front._owlyNativeCall->function(&front,nullptr);}
    ~Fixture(){front.owlyNativeStop();CHECK(front.references==1);CHECK(vif.references==1);CHECK(peer.references==1);CHECK(front.backCalls==0);thread_call_free(front._owlyNativeCall);}
};
static owly_abi6_packet actionPacket();
static void zero_length_action_allocation() {
    unsigned chunks=1;mbuf_t raw=nullptr;
    CHECK(mbuf_allocpacket(MBUF_DONTWAIT,300,&chunks,&raw)==0 && raw);
    CHECK(mbuf_len(raw)==0 && mbuf_pkthdr_len(raw)==0);
    CHECK(mbuf_trailingspace(raw)==300 && mbuf_next(raw)==nullptr);
    mbuf_freem(raw);
    const unsigned allocated=allocations,freed=frees;
    mbuf_t packet=owlyNativeAllocAction(300);
    CHECK(packet!=nullptr);
    CHECK(mbuf_len(packet)==300 && mbuf_pkthdr_len(packet)==300);
    CHECK(mbuf_next(packet)==nullptr && packet->nextpkt==nullptr);
    mbuf_freem(packet);
    CHECK(allocations==allocated+1 && frees==freed+1);
    fragmentAllocation=true;
    CHECK(owlyNativeAllocAction(300)==nullptr);
    fragmentAllocation=false;
    failAllocation=true;CHECK(owlyNativeAllocAction(300)==nullptr);failAllocation=false;
}

static void current_vif_peer_rebinding(){
    IO80211PeerManager replacement;replacement.peer=true;
    Fixture f;f.front.verified=true;f.tick();auto packet=actionPacket();
    auto *pointer=&replacement;memcpy(reinterpret_cast<uint8_t *>(&f.vif)+OWLY_NATIVE_VIF_PEER_OFFSET,&pointer,sizeof(pointer));
    modelNativePeer=&replacement;
    CHECK(!f.front.owlyNativeReceive(&packet));
    f.tick();CHECK(f.front._owlyNativePeerRebindings==1 && f.front._owlyNativePeer==&replacement);
    CHECK(f.peer.references==1 && replacement.references==2);
    f.tick();CHECK(f.front._owlyNativeEnabled && f.front.owlyNativeReceive(&packet));
    CHECK(f.front._owlyNativeActionsProcessed==1);
}
static void native_registration_uses_exact_native_lookup() {
    Fixture f;f.front.verified=true;f.tick();auto packet=actionPacket();
    CHECK(f.front.owlyNativeReceive(&packet));
    CHECK(nativeAddCalls==0 && f.front._owlyNativePeerAddAttempts==0);
    CHECK(findNative(&f.peer,packet.src)!=nullptr);
    normalNativeCreatesPeer=false;
    CHECK(f.front.owlyNativeReceive(&packet));CHECK(nativeAddCalls==0);
}
static void native_registration_repairs_only_processed_remote_frames() {
    Fixture f;f.front.verified=true;f.tick();auto packet=actionPacket();normalNativeCreatesPeer=false;
    const unsigned before=actionCalls;
    CHECK(f.front.owlyNativeReceive(&packet));
    CHECK(nativeAddCalls==1 && f.front._owlyNativePeerAddAttempts==1);
    CHECK(f.front._owlyNativePeerAddVerified==1 && f.front._owlyNativePeerAddFailed==0);
    CHECK(f.front._owlyNativePeerResubmissions==1 && actionCalls==before+2);
    CHECK(f.front._owlyNativeActionCalls==2 && f.front._owlyNativeActionsProcessed==1);
    CHECK(findNative(&f.peer,packet.src)!=nullptr);
    CHECK(f.front.owlyNativeReceive(&packet));CHECK(nativeAddCalls==1);
    CHECK(nativePeerLockDepth==0);
}
static void native_registration_failure_and_backoff_are_not_admission() {
    Fixture f;f.front.verified=true;f.tick();auto packet=actionPacket();normalNativeCreatesPeer=false;nativeAddFails=true;
    CHECK(f.front.owlyNativeReceive(&packet));
    CHECK(nativeAddCalls==1 && f.front._owlyNativePeerAddFailed==1 && f.front._owlyNativePeerAddVerified==0);
    CHECK(f.front._owlyNativePeerResubmissions==0 && findNative(&f.peer,packet.src)==nullptr);
    packet.received_us+=499999;CHECK(f.front.owlyNativeReceive(&packet));CHECK(nativeAddCalls==1);
    packet.received_us+=1;nativeAddFails=false;CHECK(f.front.owlyNativeReceive(&packet));
    CHECK(nativeAddCalls==2 && f.front._owlyNativePeerAddVerified==1);
}
static void native_registration_respects_rssi_validation_identity_and_stop() {
    Fixture f;f.front.verified=true;f.tick();auto packet=actionPacket();normalNativeCreatesPeer=false;
    packet.rssi=-80;CHECK(f.front.owlyNativeReceive(&packet));CHECK(nativeAddCalls==0);
    packet.rssi=-45;rejectNativeAction=true;CHECK(!f.front.owlyNativeReceive(&packet));CHECK(nativeAddCalls==0);rejectNativeAction=false;
    nativeGenericDefault=true;CHECK(f.front.owlyNativeReceive(&packet));CHECK(nativeAddCalls==0);nativeGenericDefault=false;
    memcpy(reinterpret_cast<uint8_t *>(&f.peer)+OWLY_NATIVE_PEER_OWN_MAC_OFFSET,packet.src,6);
    CHECK(f.front.owlyNativeReceive(&packet));CHECK(nativeAddCalls==0);
    memcpy(reinterpret_cast<uint8_t *>(&f.peer)+OWLY_NATIVE_PEER_OWN_MAC_OFFSET,f.front.testWindow.local,6);
    nativeAddWrongMAC=true;CHECK(f.front.owlyNativeReceive(&packet));
    CHECK(nativeAddCalls==1 && f.front._owlyNativePeerAddVerified==0 && f.front._owlyNativePeerAddFailed==1);
    CHECK(f.front._owlyNativePeerResubmissions==0);
    packet.received_us+=1000000;reenterStop=true;CHECK(f.front.owlyNativeReceive(&packet));reenterStop=false;
    CHECK(nativeAddCalls==1 && f.front._owlyNativeClosed && nativePeerLockDepth==0);
}

static void native_processing_is_not_call_success(){
    Fixture f;f.front.verified=true;f.tick();auto packet=actionPacket();
    rejectNativeAction=true;const auto before=actionCalls.load();
    CHECK(!f.front.owlyNativeReceive(&packet));CHECK(actionCalls==before+1);
    CHECK(f.front._owlyNativeActionCalls==1 && f.front._owlyNativeActionsProcessed==0 &&
          f.front._owlyNativeActionsUnprocessed==1);
    rejectNativeAction=false;CHECK(f.front.owlyNativeReceive(&packet));
    CHECK(f.front._owlyNativeActionsProcessed==1);
}
static void native_gate_recovery(){
    Fixture f;f.front.verified=true;f.tick();
    CHECK(f.front._owlyNativeEnabled);
    const unsigned before=awdlCalls;const auto generation=f.front._owlyNativeProgrammedGeneration;
    auto pkt=actionPacket();
    /* Apple closes its own flag while our cached bool and radio epoch stay
     * unchanged. A real service/state transition can do exactly this. */
    modelGate(OWLY_NATIVE_PEER_CONFIGURED,0);
    const bool blocked=!f.front.owlyNativeReceive(&pkt);
    f.tick();CHECK(awdlCalls==before+1 && f.front._owlyNativeGateRecoveries==1);
    CHECK(blocked && f.front._owlyNativeGateBlocked==1);
    CHECK(f.front._owlyNativeProgrammedGeneration==generation);
    CHECK(f.front.owlyNativeReceive(&pkt));
    /* AW-start state can be lost without advancing the window number.
     * Replaying the current real window repairs only the native sync word. */
    const auto calls=f.front._owlyNativeWindowCalls;
    modelGate(OWLY_NATIVE_PEER_CONFIGURED|OWLY_NATIVE_PEER_SYNC_ACTIVE,0);
    f.tick();CHECK(f.front._owlyNativeWindowCalls==calls+1);
    CHECK(f.front.owlyNativeReceive(&pkt));
    /* Unknown layout must never be read or trigger a native call. */
    const unsigned actions=actionCalls,enables=awdlCalls;
    modelPeerClassSize=OWLY_NATIVE_PEER_CLASS_BYTES-1;
    CHECK(!f.front.owlyNativeReceive(&pkt));f.tick();
    CHECK(actionCalls==actions && awdlCalls==enables && !f.front._owlyNativeEnabled);
    modelPeerClassSize=OWLY_NATIVE_PEER_CLASS_BYTES;
}
static void native_recovery_backoff(){
    Fixture f;f.front.verified=true;f.tick();
    const unsigned before=awdlCalls;
    modelGate(OWLY_NATIVE_PEER_CONFIGURED,0);awdlResult=102;
    f.tick();CHECK(!f.front._owlyNativeEnabled && awdlCalls==before+1);
    f.tick();CHECK(awdlCalls==before+1);
    f.front.testWindow.sampled_us+=500000;awdlResult=0;
    f.tick();CHECK(f.front._owlyNativeEnabled && awdlCalls==before+2);
}
static owly_abi6_packet actionPacket() {
    const uint8_t remote[6]={2,6,7,8,9,10};const uint8_t tlv[]={21,2,0,0x3e,1};
    owly_sync sync{};uint8_t frame[2048];size_t n=0;
    CHECK(!owly_sync_template(&sync,tlv,sizeof(tlv),remote));owly_sync_request(&sync,true);owly_sync_reconcile(&sync,true,149,1000000);
    CHECK(owly_sync_frame(&sync,1000000,frame,sizeof(frame),&n)==1);
    owly_abi6_packet pkt{};CHECK(owly_abi6_packet_make(&pkt,1,frame,n,remote,frame+4));
    pkt.channel=149;pkt.rssi=-45;pkt.received_us=1000100;return pkt;
}
static void readiness_and_ownership() {
    memset(bufout,0,sizeof(bufout));unsigned before=enableCalls;
    Fixture f;f.tick();CHECK(!f.front._owlyNativeEnabled && enableCalls==before);CHECK(bufout[0x200]==0 && bufout[0x410]==0);
    f.front.verified=true;f.tick();CHECK(f.front._owlyNativeEnabled && enableCalls==before+1);CHECK(bufout[0x200]==0xaa && bufout[0x410]==0xaa);
    auto pkt=actionPacket();unsigned a=allocations,b=frees;
    CHECK(f.front.owlyNativeReceive(&pkt));CHECK(allocations==a+1 && frees==b+1);
    /* A pending SET120 service ioctl waiter must make owlyNativeAcquire yield
     * immediately so wifip2pd never hits ENXIO against 16 ms ticks or RX. */
    f.front._owlyServiceWaiters=1;
    CHECK(!f.front.owlyNativeReceive(&pkt));
    f.front._owlyServiceWaiters=0;
    CHECK(f.front.owlyNativeReceive(&pkt));
    CHECK(lastBody.size()==pkt.length-24 && !memcmp(lastBody.data(),pkt.bytes+24,lastBody.size()));
    int32_t rssi;memcpy(&rssi,lastMetadata.data()+44,4);CHECK(rssi==-45);
    uint32_t ch;memcpy(&ch,lastMetadata.data()+16,4);CHECK(ch==149);
    pkt.kind=2;pkt.length=48;memset(pkt.bytes,0,48);pkt.bytes[0]=0x60;pkt.bytes[5]=8;
    a=allocations;b=frees;CHECK(f.front.owlyNativeReceive(&pkt));CHECK(allocations==a+1 && frees==b+1);
    CHECK(lastData.size()==62 && lastData[12]==0x86 && lastData[13]==0xdd);
    CHECK(!memcmp(lastData.data()+14,pkt.bytes,48));
    failAllocation=true;CHECK(!f.front.owlyNativeReceive(&pkt));failAllocation=false;
    fragmentAllocation=true;a=allocations;b=frees;CHECK(!f.front.owlyNativeReceive(&pkt));CHECK(allocations==a+1 && frees==b+1);fragmentAllocation=false;
    pkt.rssi=0;CHECK(f.front.owlyNativeReceive(&pkt));
    pkt=actionPacket();pkt.rssi=0;CHECK(!f.front.owlyNativeReceive(&pkt));
    auto *m=f.front.allocatePacket(62);CHECK(f.front.owlyNativeOutput(&f.vif,m)==kIOReturnOutputSuccess);
    f.front.connected=false;m=f.front.allocatePacket(62);CHECK(f.front.owlyNativeOutput(&f.vif,m)==kIOReturnOutputDropped);CHECK(f.front.backCalls==0);
}
static void reentrant_stop() {
    Fixture f;f.front.verified=true;f.tick();auto pkt=actionPacket();reenterStop=true;
    CHECK(f.front.owlyNativeReceive(&pkt));reenterStop=false;
    CHECK(f.front._owlyNativeClosed && !f.front._owlyNativePending && !f.front._owlyNativeUsers);
    CHECK(!f.front.owlyNativeReceive(&pkt));
}
static void window_and_corrupt_guard() {
    memset(bufout,0,sizeof(bufout));bufout[0x410]=1;unsigned before=enableCalls;
    {Fixture f;f.front.verified=true;f.tick();CHECK(!f.front._owlyNativeEnabled && enableCalls==before);
        CHECK(bufout[0x200]==0 && bufout[0x410]==1);}
    memset(bufout,0,sizeof(bufout));
    Fixture f;f.front.verified=true;f.front.testWindow.active=true;
    f.front.testWindow.sequence=77;f.front.testWindow.until_next_tu=16;
    memcpy(f.front.testWindow.master,(uint8_t[]){2,1,2,3,4,5},6);
    f.tick();CHECK(f.front._owlyNativeWindowCalls==1 && lastWindow.size()==36);
    uint32_t sequence,sync;memcpy(&sequence,lastWindow.data(),4);memcpy(&sync,lastWindow.data()+4,4);
    CHECK(sequence==77 && sync==1 && !memcmp(lastWindow.data()+14,f.front.testWindow.master,6));
    f.tick();CHECK(f.front._owlyNativeWindowCalls==1);
    f.front.testWindow.sequence=78;f.tick();CHECK(f.front._owlyNativeWindowCalls==2);
}
static uint32_t legacy_window_start_us() {
    const auto *time=lastWindow.data()+20;
    return (uint32_t(time[0])<<24)|(uint32_t(time[1])<<16)|(uint32_t(time[2])<<8)|time[3];
}
static void legacy_window_channel_and_time() {
    Fixture f;f.front.verified=true;
    auto &window=f.front.testWindow;
    window.remote=true;
    window.master_channel=44; // A peer can be master on another social channel.
    memcpy(window.master,(uint8_t[]){2,6,7,8,9,10},6);
    window.sequence=65535;
    window.sampled_us=0xffffe000ULL+4096;
    window.until_next_tu=12;
    f.tick();
    CHECK(lastWindow.size()==36);
    // Legacy reads the channel at byte 28 and flags at offset 32.
    CHECK(lastWindow[28]==149);
    uint32_t version,flags;
    memcpy(&version,lastWindow.data()+24,4);
    memcpy(&flags,lastWindow.data()+32,4);
    CHECK(version==1 && flags==0x12);
    CHECK(!memcmp(lastWindow.data()+8,window.master,6) &&
          !memcmp(lastWindow.data()+14,window.master,6));
    const uint32_t first=legacy_window_start_us();
    CHECK(first==0xffffe000u);

    // The next report is sampled at a different phase. Firmware time is
    // 32-bit; the counter and the timestamp can both wrap at this boundary.
    window.sequence=0;
    window.sampled_us=0x100002000ULL+9*1024;
    window.until_next_tu=7;
    f.tick();
    CHECK(legacy_window_start_us()==0x2000u &&
          legacy_window_start_us()-first==16384u);
}
static void native_programming_results() {
    memset(bufout,0,sizeof(bufout));unsigned e=enableCalls,a=awdlCalls,s=sequenceCalls;
    {
        Fixture f;f.front.verified=true;f.front.testWindow.active=false;
        f.front.testWindow.channel=0;f.tick();
        CHECK(enableCalls==e && !f.front._owlyNativeEnabled);
        f.front.testWindow.channel=149;sequenceResult=102;f.tick();
        CHECK(enableCalls==e+1 && sequenceCalls==s+1 && awdlCalls==a);
        CHECK(!f.front._owlyNativeEnabled && lastMaster.value==149 && lastSequence.length==16);
        for(unsigned i=0;i<16;i++)CHECK(lastSequence.channels[i].channel==149 && lastSequence.channels[i].flags==0x12);
        sequenceResult=0;f.tick();CHECK(f.front._owlyNativeEnabled && enableCalls==e+1 && awdlCalls==a+1);
        CHECK(f.front._owlyNativeWindowCalls==0);
        f.front.testWindow.active=true;f.tick();
        CHECK(f.front._owlyNativeWindowCalls==1 && awdlCalls==a+1);
        f.front.testWindow.generation++;f.tick();
        CHECK(f.front._owlyNativeWindowCalls==2 && awdlCalls==a+2);
        f.front.testWindow.remote=true;f.front.testWindow.master_channel=0;
        f.front.testWindow.channel=6;f.tick();CHECK(lastMaster.value==6 && lastSequence.channels[0].channel==6);
        CHECK(f.front._owlyNativeEnabled && enableCalls==e+2);
    }
    {
        Fixture f;f.front.verified=true;masterResult=102;s=sequenceCalls;a=awdlCalls;f.tick();
        CHECK(!f.front._owlyNativeEnabled && sequenceCalls==s && awdlCalls==a);
        masterResult=0;awdlResult=6;f.tick();CHECK(!f.front._owlyNativeEnabled && awdlCalls==a+1);
        awdlResult=0;f.tick();CHECK(f.front._owlyNativeEnabled);
    }
}
static void session_epoch_commit_and_reenable() {
    Fixture f;f.front.verified=true;
    const unsigned before=enableCalls,aw=awdlCalls;
    resetGenerationOnEnable=true;
    f.tick();
    CHECK(f.front._owlyNativeEnabled && enableCalls==before+1 && awdlCalls==aw+1);
    CHECK(f.front._owlyNativeProgrammedGeneration==f.front.testWindow.generation);
    f.tick();CHECK(enableCalls==before+1 && awdlCalls==aw+1);
    ++f.front.testWindow.generation; // backend stop/start on the retained VIF
    f.tick();CHECK(enableCalls==before+2 && awdlCalls==aw+2);
    CHECK(f.front._owlyNativeProgrammedGeneration==f.front.testWindow.generation);
    f.tick();CHECK(enableCalls==before+2 && awdlCalls==aw+2);
    resetGenerationOnEnable=false;
}
static void drain_before_stop() {
    Fixture f;f.front.verified=true;f.tick();auto pkt=actionPacket();
    {std::lock_guard<std::mutex> lock(actionMutex);blockAction=true;enteredAction=releaseAction=false;}
    std::thread rx([&]{CHECK(f.front.owlyNativeReceive(&pkt));});
    {std::unique_lock<std::mutex> lock(actionMutex);actionCondition.wait(lock,[]{return enteredAction;});}
    /* IPv6 data RX must not be dropped while an Action call or tick holds _owlyVifMethodBusy. */
    auto ipv6=pkt;ipv6.kind=OWLY_ABI6_PACKET_IPV6;ipv6.length=48;memset(ipv6.bytes,0,48);
    ipv6.bytes[0]=0x60;ipv6.bytes[5]=8;
    const unsigned beforeData=dataCalls.load();
    CHECK(f.front.owlyNativeReceive(&ipv6));
    CHECK(dataCalls.load()==beforeData+1 && f.front._owlyNativeDataUsers==0);
    std::atomic<bool> finished{false};std::thread stop([&]{f.front.owlyNativeStop();finished=true;});
    std::this_thread::sleep_for(std::chrono::milliseconds(10));CHECK(!finished);
    {std::lock_guard<std::mutex> lock(actionMutex);releaseAction=true;actionCondition.notify_all();}
    rx.join();stop.join();CHECK(finished && f.front._owlyNativeUsers==0 && f.front._owlyNativeDataUsers==0);
    blockAction=false;
}
static void peer_cache_and_umi_indications() {
    eventCodes.clear();peerCacheEvents.clear();umiEvents.clear();
    Fixture f;f.front.verified=true;
    const uint8_t peerMac[6]={0x42,0x73,0x3d,0xb9,0x2c,0x97};
    const int refsBefore=f.front.references.load();
    /* Tick is already pending from capture(); expediting must not double-retain. */
    f.front.owlyNativeNotePeerCache(peerMac,1u);
    f.front.owlyNativeNotePeerCache(peerMac,3u);
    f.front.owlyNativeNoteUMI(peerMac);
    CHECK(f.front.references.load()==refsBefore);
    f.tick();
    CHECK(f.front._owlyPeerCacheNoted==2 && f.front._owlyPeerCacheReported==2 && f.front._owlyPeerCacheDropped==0);
    CHECK(f.front._owlyUmiNoted==1 && f.front._owlyUmiReported==1 && f.front._owlyUmiDropped==0);
    CHECK(peerCacheEvents.size()==2 && umiEvents.size()==1);
    CHECK(eventCodes.size()==4 && eventCodes[0]==0x33 && eventCodes[1]==0x33 && eventCodes[2]==0x45 && eventCodes[3]==41);
    uint32_t v0=0,op0=0,v1=0,op1=0;
    memcpy(&v0,peerCacheEvents[0].data(),4);memcpy(&op0,peerCacheEvents[0].data()+4,4);
    memcpy(&v1,peerCacheEvents[1].data(),4);memcpy(&op1,peerCacheEvents[1].data()+4,4);
    CHECK(v0==1 && op0==1 && !memcmp(peerCacheEvents[0].data()+8,peerMac,6));
    CHECK(v1==1 && op1==3 && !memcmp(peerCacheEvents[1].data()+8,peerMac,6));
    CHECK(!memcmp(umiEvents[0].data(),peerMac,6) && umiEvents[0][6]==0);
    /* Ring overflow drops safely without out-of-bounds writes. */
    for(unsigned i=0;i<18;i++)f.front.owlyNativeNotePeerCache(peerMac,1u);
    CHECK(f.front._owlyPeerCacheDropped==2 && f.front._owlyPeerCacheCount==16);
}
int main(){native_registration_uses_exact_native_lookup();native_registration_repairs_only_processed_remote_frames();native_registration_failure_and_backoff_are_not_admission();native_registration_respects_rssi_validation_identity_and_stop();zero_length_action_allocation();current_vif_peer_rebinding();native_processing_is_not_call_success();native_gate_recovery();native_recovery_backoff();readiness_and_ownership();reentrant_stop();window_and_corrupt_guard();legacy_window_channel_and_time();native_programming_results();session_epoch_commit_and_reenable();drain_before_stop();peer_cache_and_umi_indications();CHECK(allocations==frees);printf("native frontend production runtime: %u checks passed; mocked kernel boundaries\n",checks);}
