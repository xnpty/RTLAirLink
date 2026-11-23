#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cerrno>

using SInt32 = int32_t;
constexpr unsigned SIOCGA80211 = 1, SIOCSA80211 = 2;
constexpr unsigned APPLE80211_IOC_POWER = 19, APPLE80211_IOC_SUPPORTED_CHANNELS = 27;
constexpr unsigned APPLE80211_IOC_HT_CAPABILITY = 154;
constexpr unsigned APPLE80211_IOC_VHT_CAPABILITY = 214;
constexpr int RTW89_FRONT_ROLE_AIRLINK_OBSERVED = 6;
constexpr int kIOReturnSuccess = 0, kIOReturnUnsupported = -1;
struct apple80211_ht_capability { uint8_t bytes[32]; };
struct Vif { int role; int getInterfaceRole() const { return role; } };
static int calls, vht_calls, result;
static void *last_buffer;
static int rtw89_glue_awdl_ht_capability(void *data, size_t size) {
    assert(size == 32); ++calls; last_buffer = data;
    if (!result) memset(data, 0x5a, size);
    return result;
}
static int rtw89_glue_awdl_vht_capability(void *data, size_t size) {
    assert(size == 18); ++vht_calls; last_buffer = data;
    if (!result) memset(data, 0x6b, size);
    return result;
}
struct Dispatch {
    void *_netif = this;
    unsigned property_result = 0;
    void setProperty(const char *, unsigned long long value, unsigned bits) {
        assert(bits == 32); property_result = value;
    }
    void setProperty(const char *, void *, unsigned size) { assert(size == 28 || size == 14); }
    int backend(unsigned number, bool isSet, void *data) {
        switch (number) {
#include "production_native_ht_case.inc"
        default: return -999;
        }
    }
    int apple80211Request(unsigned type, unsigned number, void *netif, void *data) {
        assert(netif == _netif);
        return backend(number, type == SIOCSA80211, data);
    }
    int front(unsigned type, unsigned number, Vif *vif, void *data) {
#include "production_front_ht_dispatch.inc"
        return -999;
    }
};
int main() {
    Dispatch d; Vif vif{6}; uint8_t reply[32]; memset(reply, 0xa5, sizeof(reply));
    // Configure must reach the real getter before any tracked VIF/session exists.
    assert(d.front(SIOCGA80211, 154, &vif, reply) == 0);
    assert(calls == 1 && last_buffer == reply && reply[0] == 0x5a);
    result = -ENODEV;
    assert(d.front(SIOCGA80211, 154, &vif, reply) == ENODEV);
    assert(d.property_result == ENODEV && calls == 2);
    assert(d.backend(154, true, reply) == kIOReturnUnsupported && calls == 2);
    assert(d.front(SIOCSA80211, 154, &vif, reply) == -999);
    assert(d.front(SIOCGA80211, 154, nullptr, reply) == -999);
    assert(d.front(SIOCGA80211, 154, &vif, nullptr) == -999);
    vif.role = 1;
    assert(d.front(SIOCGA80211, 154, &vif, reply) == -999);
    vif.role = 6; d._netif = nullptr;
    assert(d.front(SIOCGA80211, 154, &vif, reply) == -999 && calls == 2);
    d._netif = &d; result = 0;
    uint8_t vht_reply[20]; memset(vht_reply, 0xa5, sizeof(vht_reply));
    assert(d.front(SIOCGA80211, 214, &vif, vht_reply) == 0);
    assert(vht_calls == 1 && vht_reply[0] == 0x6b && vht_reply[17] == 0x6b && vht_reply[18] == 0xa5);
    assert(d.backend(214, true, vht_reply) == kIOReturnUnsupported && vht_calls == 1);
    puts("HT/VHT production dispatch: early GET154/GET214, original buffer, errno, SET and role guards passed; I/O mocked");
}
