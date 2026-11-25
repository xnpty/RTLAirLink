// SPDX-License-Identifier: GPL-2.0
/*
 * rtlairlink-rtgd: automatic AWDL Real-Time Group (RTG) daemon for RTLAirLink.
 *
 * When iPhone Mirroring establishes an active AWDL session (IPv6 link-local
 * socket on awdl0), macOS's ScreenSharingKit does not always forward an RTG
 * traffic registration to IO80211FamilyLegacy on Hackintosh setups, allowing
 * iOS to drop its AWDL schedule to 0x8e87 (196-262 ms off-channel gaps) during
 * online video playback.
 *
 * This daemon monitors for active iPhone Mirroring awdl0 sessions using pure
 * libproc + sysctl + IOKit APIs (zero subprocesses), registers RTG
 * (MPRemoteCamera, flags=0x40) once per session so iPhone locks to 0xffff
 * (16/16 slots on the shared social channel), and cleanly clears RTG when the
 * Mirroring session ends so AirDrop and idle battery stay untouched.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <libproc.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet6/nd6.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/proc_info.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>

#define SIOCSA80211 0x802869c8UL
#define APPLE80211_IOC_AWDL_PEER_TRAFFIC_REGISTRATION 164

#define RTGD_SERVICE_NAME "MPRemoteCamera"
#define RTGD_RTG_FLAGS    0x40u
#define RTGD_MISSING_GRACE_TICKS 4   /* 4 * 500ms = 2.0s before CLEAR */
#define RTGD_RETRY_BACKOFF_MS    2000ULL

struct apple80211req {
    char     req_if_name[16];
    int32_t  req_type;
    int32_t  req_val;
    uint32_t req_len;
    uint32_t _pad;
    void    *req_data;
};

struct apple80211_awdl_peer_traffic_registration {
    uint32_t version;
    uint8_t  addr[6];
    uint8_t  pad0[2];
    uint32_t name_len;
    char     name[128];
    uint32_t generation;
    uint32_t service_flags;
    uint8_t  reserved[16];
    uint32_t active;
    uint32_t flags;
    uint16_t pref_chan;
    uint16_t sec_pref_chan;
    uint32_t pad1;
};

_Static_assert(sizeof(struct apple80211req) == 40, "apple80211req must be 40B");
_Static_assert(sizeof(struct apple80211_awdl_peer_traffic_registration) == 0xb8,
               "traffic_reg must be 184B");
_Static_assert(offsetof(struct tcp_sockinfo, tcpsi_ini) == 0,
               "tcp_sockinfo must begin with in_sockinfo");

static volatile sig_atomic_t g_stop = 0;

static void on_signal(int sig) {
    (void)sig;
    g_stop = 1;
}

static uint64_t mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000L);
}

static void log_msg(const char *fmt, ...) {
    time_t now = time(NULL);
    struct tm tm;
    char tbuf[32];
    localtime_r(&now, &tm);
    strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &tm);
    fprintf(stderr, "%s [rtlairlink-rtgd] ", tbuf);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

static bool is_social_channel(uint32_t ch) {
    return ch == 6 || ch == 44 || ch == 149;
}

static bool mac_is_valid_unicast(const uint8_t mac[6]) {
    if (mac[0] & 1u) return false;
    return (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) != 0;
}

static bool path_has_suffix(const char *path, const char *suffix) {
    size_t plen = strlen(path), slen = strlen(suffix);
    return plen >= slen && strcmp(path + plen - slen, suffix) == 0;
}

static bool is_fe80_on_if(const uint8_t addr[16], unsigned int ifidx) {
    if (addr[0] != 0xfe || (addr[1] & 0xc0u) != 0x80u) return false;
    unsigned int embedded = ((unsigned int)addr[2] << 8) | (unsigned int)addr[3];
    return embedded == ifidx;
}

static bool eui64_to_mac(const uint8_t addr[16], uint8_t out[6]) {
    if (addr[11] != 0xff || addr[12] != 0xfe) return false;
    out[0] = addr[8] ^ 0x02u;
    out[1] = addr[9];
    out[2] = addr[10];
    out[3] = addr[13];
    out[4] = addr[14];
    out[5] = addr[15];
    return mac_is_valid_unicast(out);
}

static bool get_awdl_local_mac(uint8_t out[6]) {
    struct ifaddrs *ifap = NULL;
    if (getifaddrs(&ifap) != 0) return false;
    bool found = false;
    for (struct ifaddrs *ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_name || strcmp(ifa->ifa_name, "awdl0") != 0) continue;
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_LINK) continue;
        const struct sockaddr_dl *sdl = (const struct sockaddr_dl *)ifa->ifa_addr;
        if (sdl->sdl_alen == 6) {
            memcpy(out, LLADDR(sdl), 6);
            found = true;
            break;
        }
    }
    freeifaddrs(ifap);
    return found;
}

/* Pure sysctl lookup in the kernel IPv6 NDP table (identical source to `ndp -an`). */
static bool ndp_lookup_mac(unsigned int awdl_idx, const uint8_t target_ip6[16], uint8_t out_mac[6]) {
    int mib[6] = { CTL_NET, PF_ROUTE, 0, AF_INET6, NET_RT_FLAGS, RTF_LLINFO };
    size_t needed = 0;
    if (sysctl(mib, 6, NULL, &needed, NULL, 0) != 0 || needed == 0 || needed > (4u * 1024u * 1024u))
        return false;
    uint8_t *buf = malloc(needed);
    if (!buf) return false;
    if (sysctl(mib, 6, buf, &needed, NULL, 0) != 0) {
        free(buf);
        return false;
    }

    bool found = false;
    uint8_t *end = buf + needed;
    for (uint8_t *p = buf; p + sizeof(struct rt_msghdr) <= end; ) {
        const struct rt_msghdr *rtm = (const struct rt_msghdr *)p;
        if (rtm->rtm_msglen == 0 || p + rtm->rtm_msglen > end) break;
        const uint8_t *sa_ptr = p + sizeof(struct rt_msghdr);
        const uint8_t *msg_end = p + rtm->rtm_msglen;
        if (sa_ptr + sizeof(struct sockaddr) <= msg_end) {
            const struct sockaddr *dst = (const struct sockaddr *)sa_ptr;
            size_t dst_len = dst->sa_len ? ((dst->sa_len + 3u) & ~3u) : sizeof(uint32_t);
            const uint8_t *gw_ptr = sa_ptr + dst_len;
            if (dst->sa_family == AF_INET6 &&
                dst->sa_len >= sizeof(struct sockaddr_in6) &&
                gw_ptr + sizeof(struct sockaddr_dl) <= msg_end) {
                const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)dst;
                const struct sockaddr_dl *sdl = (const struct sockaddr_dl *)gw_ptr;
                if (sdl->sdl_family == AF_LINK && sdl->sdl_index == awdl_idx &&
                    sdl->sdl_alen == 6 &&
                    memcmp(sin6->sin6_addr.s6_addr, target_ip6, 16) == 0) {
                    memcpy(out_mac, LLADDR(sdl), 6);
                    if (mac_is_valid_unicast(out_mac)) {
                        found = true;
                        break;
                    }
                }
            }
        }
        p += rtm->rtm_msglen;
    }
    free(buf);
    return found;
}

static bool inspect_pid_awdl_peer(pid_t pid, unsigned int awdl_idx, const uint8_t local_mac[6],
                                  uint8_t out_mac[6], int *out_proto, uint16_t *out_fport) {
    int buf_sz = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, NULL, 0);
    if (buf_sz <= 0) return false;
    buf_sz += 64 * (int)sizeof(struct proc_fdinfo);
    struct proc_fdinfo *fds = malloc((size_t)buf_sz);
    if (!fds) return false;
    int actual = proc_pidinfo(pid, PROC_PIDLISTFDS, 0, fds, buf_sz);
    if (actual <= 0) {
        free(fds);
        return false;
    }
    int count = actual / (int)sizeof(struct proc_fdinfo);
    bool found = false;

    for (int i = 0; i < count; i++) {
        if (fds[i].proc_fdtype != PROX_FDTYPE_SOCKET) continue;
        struct socket_fdinfo si;
        if (proc_pidfdinfo(pid, fds[i].proc_fd, PROC_PIDFDSOCKETINFO, &si, (int)sizeof(si)) != (int)sizeof(si))
            continue;
        if (si.psi.soi_family != AF_INET6) continue;

        bool active_conn = false;
        if (si.psi.soi_kind == SOCKINFO_TCP) {
            if (si.psi.soi_proto.pri_tcp.tcpsi_state == TSI_S_ESTABLISHED)
                active_conn = true;
        } else if (si.psi.soi_kind == SOCKINFO_IN) {
            if (si.psi.soi_proto.pri_in.insi_fport != 0)
                active_conn = true;
        }
        if (!active_conn) continue;

        const uint8_t *faddr = si.psi.soi_proto.pri_in.insi_faddr.ina_6.s6_addr;
        const uint8_t *laddr = si.psi.soi_proto.pri_in.insi_laddr.ina_6.s6_addr;
        if (memcmp(faddr, laddr, 16) == 0) continue;

        if (!is_fe80_on_if(faddr, awdl_idx) && !is_fe80_on_if(laddr, awdl_idx))
            continue;
        if (faddr[0] != 0xfe || (faddr[1] & 0xc0u) != 0x80u)
            continue;

        uint8_t scoped_faddr[16];
        memcpy(scoped_faddr, faddr, 16);
        scoped_faddr[2] = (uint8_t)((awdl_idx >> 8) & 0xffu);
        scoped_faddr[3] = (uint8_t)(awdl_idx & 0xffu);

        uint8_t mac[6] = {0};
        if (!ndp_lookup_mac(awdl_idx, scoped_faddr, mac) && !eui64_to_mac(faddr, mac))
            continue;
        if (memcmp(mac, local_mac, 6) == 0)
            continue;

        memcpy(out_mac, mac, 6);
        if (out_proto) *out_proto = si.psi.soi_kind;
        if (out_fport) *out_fport = ntohs((uint16_t)si.psi.soi_proto.pri_in.insi_fport);
        found = true;
        break;
    }

    free(fds);
    return found;
}

struct detected_session {
    bool    app_running;
    bool    connected;
    pid_t   owner_pid;
    int     proto_kind;
    uint16_t fport;
    uint8_t peer_mac[6];
};

static void detect_mirroring_session(struct detected_session *out) {
    memset(out, 0, sizeof(*out));
    int needed = proc_listallpids(NULL, 0);
    if (needed <= 0) return;
    int cap = needed + 128;
    pid_t *pids = malloc((size_t)cap * sizeof(pid_t));
    if (!pids) return;
    int n = proc_listallpids(pids, cap * (int)sizeof(pid_t));
    if (n <= 0) {
        free(pids);
        return;
    }

    pid_t mirror_pids[16];
    int n_mirror = 0;
    pid_t avc_pids[16];
    int n_avc = 0;

    for (int i = 0; i < n; i++) {
        if (pids[i] <= 0) continue;
        char path[PROC_PIDPATHINFO_MAXSIZE];
        if (proc_pidpath(pids[i], path, sizeof(path)) <= 0) continue;
        if (path_has_suffix(path, "/iPhone Mirroring")) {
            if (n_mirror < 16) mirror_pids[n_mirror++] = pids[i];
        } else if (path_has_suffix(path, "/avconferenced")) {
            if (n_avc < 16) avc_pids[n_avc++] = pids[i];
        }
    }
    free(pids);

    if (n_mirror == 0) return;
    out->app_running = true;

    unsigned int awdl_idx = if_nametoindex("awdl0");
    if (awdl_idx == 0) return;
    uint8_t local_mac[6] = {0};
    if (!get_awdl_local_mac(local_mac)) return;

    /* Primary: active IPv6 socket on awdl0 inside iPhone Mirroring itself. */
    for (int i = 0; i < n_mirror; i++) {
        if (inspect_pid_awdl_peer(mirror_pids[i], awdl_idx, local_mac,
                                  out->peer_mac, &out->proto_kind, &out->fport)) {
            out->connected = true;
            out->owner_pid = mirror_pids[i];
            return;
        }
    }
    /* Secondary: while iPhone Mirroring is running, check avconferenced's awdl0 UDP flow. */
    for (int i = 0; i < n_avc; i++) {
        if (inspect_pid_awdl_peer(avc_pids[i], awdl_idx, local_mac,
                                  out->peer_mac, &out->proto_kind, &out->fport)) {
            out->connected = true;
            out->owner_pid = avc_pids[i];
            return;
        }
    }
}

struct front_state {
    bool     present;
    uint32_t bound_channel;
    uint32_t peer_count;
    uint64_t generation;
    uint64_t peer_cache_reported;
    uint64_t umi_reported;
    uint32_t revision;
};

static uint64_t read_cf_u64(io_service_t svc, CFStringRef key) {
    CFTypeRef v = IORegistryEntryCreateCFProperty(svc, key, kCFAllocatorDefault, 0);
    uint64_t out = 0;
    if (v && CFGetTypeID(v) == CFNumberGetTypeID()) {
        int64_t s = 0;
        if (CFNumberGetValue(v, kCFNumberSInt64Type, &s))
            out = (uint64_t)s;
    }
    if (v) CFRelease(v);
    return out;
}

static void read_front_state(struct front_state *st) {
    memset(st, 0, sizeof(*st));
    io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault,
                                                   IOServiceMatching("RTLAirLinkFront"));
    if (!svc) return;
    st->present = true;
    st->bound_channel = (uint32_t)read_cf_u64(svc, CFSTR("OwlyNativeBoundChannel"));
    if (!is_social_channel(st->bound_channel))
        st->bound_channel = (uint32_t)read_cf_u64(svc, CFSTR("OwlyNativeProgrammedChannel"));
    st->peer_count = (uint32_t)read_cf_u64(svc, CFSTR("OwlyNativePeerCount"));
    st->generation = read_cf_u64(svc, CFSTR("OwlyNativeProgrammedGeneration"));
    st->peer_cache_reported = read_cf_u64(svc, CFSTR("OwlyNativePeerCacheReported"));
    st->umi_reported = read_cf_u64(svc, CFSTR("OwlyNativeUMIStatusReported"));
    st->revision = (uint32_t)read_cf_u64(svc, CFSTR("OwlyNativePeerDataRevision"));
    IOObjectRelease(svc);
}

static int call_rtg_ioctl(bool set_active, const uint8_t peer_mac[6], uint16_t pref_ch) {
    struct apple80211_awdl_peer_traffic_registration reg;
    memset(&reg, 0, sizeof(reg));
    reg.version = 1;
    memcpy(reg.addr, peer_mac, 6);
    reg.name_len = (uint32_t)strlen(RTGD_SERVICE_NAME);
    memcpy(reg.name, RTGD_SERVICE_NAME, reg.name_len);
    reg.generation = 1;
    reg.active = set_active ? 1u : 0u;
    reg.flags = RTGD_RTG_FLAGS;
    reg.pref_chan = pref_ch;

    struct apple80211req req;
    memset(&req, 0, sizeof(req));
    strncpy(req.req_if_name, "awdl0", sizeof(req.req_if_name) - 1);
    req.req_type = APPLE80211_IOC_AWDL_PEER_TRAFFIC_REGISTRATION;
    req.req_len = (uint32_t)sizeof(reg);
    req.req_data = &reg;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return -errno;
    int rc = ioctl(fd, SIOCSA80211, &req);
    int err = rc < 0 ? -errno : 0;
    close(fd);
    return err;
}

struct rtg_controller {
    bool     active;
    uint8_t  active_mac[6];
    uint16_t active_ch;
    uint64_t active_gen;
    uint64_t next_retry_ms;
    int      missing_ticks;
};

typedef int (*rtg_ioctl_fn)(bool set_active, const uint8_t peer_mac[6], uint16_t pref_ch);

static void rtg_step(struct rtg_controller *ctl, const struct detected_session *sess,
                     const struct front_state *front, uint64_t now_ms, rtg_ioctl_fn do_ioctl) {
    const uint16_t ch = is_social_channel(front->bound_channel) ? (uint16_t)front->bound_channel : 149u;
    const bool ready = sess->connected && front->present && is_social_channel(front->bound_channel);
    if (ready) {
        ctl->missing_ticks = 0;
        const bool same = ctl->active &&
                          memcmp(ctl->active_mac, sess->peer_mac, 6) == 0 &&
                          ctl->active_ch == ch &&
                          ctl->active_gen == front->generation;
        if (same) return;

        if (ctl->active) {
            if (ctl->active_gen == front->generation) {
                int clr_rc = do_ioctl(false, ctl->active_mac, ctl->active_ch);
                log_msg("RTG CLEAR old peer=%02x:%02x:%02x:%02x:%02x:%02x ch=%u rc=%d",
                        ctl->active_mac[0], ctl->active_mac[1], ctl->active_mac[2],
                        ctl->active_mac[3], ctl->active_mac[4], ctl->active_mac[5],
                        (unsigned)ctl->active_ch, clr_rc);
            }
            ctl->active = false;
        }

        if (now_ms < ctl->next_retry_ms) return;
        int rc = do_ioctl(true, sess->peer_mac, ch);
        if (rc == 0) {
            ctl->active = true;
            memcpy(ctl->active_mac, sess->peer_mac, 6);
            ctl->active_ch = ch;
            ctl->active_gen = front->generation;
            ctl->next_retry_ms = 0;
            log_msg("RTG ON peer=%02x:%02x:%02x:%02x:%02x:%02x ch=%u gen=%llu pid=%d port=%u",
                    sess->peer_mac[0], sess->peer_mac[1], sess->peer_mac[2],
                    sess->peer_mac[3], sess->peer_mac[4], sess->peer_mac[5],
                    (unsigned)ctl->active_ch, (unsigned long long)ctl->active_gen,
                    (int)sess->owner_pid, (unsigned)sess->fport);
        } else {
            ctl->next_retry_ms = now_ms + RTGD_RETRY_BACKOFF_MS;
            log_msg("RTG SET failed peer=%02x:%02x:%02x:%02x:%02x:%02x ch=%u rc=%d (%s); retry in 2s",
                    sess->peer_mac[0], sess->peer_mac[1], sess->peer_mac[2],
                    sess->peer_mac[3], sess->peer_mac[4], sess->peer_mac[5],
                    (unsigned)ch, rc, strerror(-rc));
        }
    } else if (ctl->active) {
        if (++ctl->missing_ticks >= RTGD_MISSING_GRACE_TICKS) {
            int rc = do_ioctl(false, ctl->active_mac, ctl->active_ch);
            log_msg("RTG OFF peer=%02x:%02x:%02x:%02x:%02x:%02x ch=%u rc=%d (%s)",
                    ctl->active_mac[0], ctl->active_mac[1], ctl->active_mac[2],
                    ctl->active_mac[3], ctl->active_mac[4], ctl->active_mac[5],
                    (unsigned)ctl->active_ch, rc, rc ? strerror(-rc) : "ok");
            ctl->active = false;
            ctl->missing_ticks = 0;
            ctl->next_retry_ms = 0;
        }
    }
}

#ifndef RTGD_UNIT_TEST
int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "--status") == 0) {
        struct detected_session sess;
        struct front_state front;
        detect_mirroring_session(&sess);
        read_front_state(&front);
        printf("RTLAirLinkFront: present=%d rev=%u ch=%u peers=%u gen=%llu cache_rep=%llu umi_rep=%llu\n",
               front.present, front.revision, front.bound_channel, front.peer_count,
               (unsigned long long)front.generation,
               (unsigned long long)front.peer_cache_reported,
               (unsigned long long)front.umi_reported);
        printf("iPhone Mirroring: app_running=%d connected=%d pid=%d proto=%d port=%u peer=%02x:%02x:%02x:%02x:%02x:%02x\n",
               sess.app_running, sess.connected, (int)sess.owner_pid, sess.proto_kind,
               (unsigned)sess.fport,
               sess.peer_mac[0], sess.peer_mac[1], sess.peer_mac[2],
               sess.peer_mac[3], sess.peer_mac[4], sess.peer_mac[5]);
        return 0;
    }

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGPIPE, SIG_IGN);

    log_msg("started (pid=%d, service=%s, flags=0x%x)", (int)getpid(), RTGD_SERVICE_NAME, RTGD_RTG_FLAGS);

    struct rtg_controller ctl;
    memset(&ctl, 0, sizeof(ctl));

    while (!g_stop) {
        struct detected_session sess;
        struct front_state front;
        detect_mirroring_session(&sess);
        read_front_state(&front);
        rtg_step(&ctl, &sess, &front, mono_ms(), call_rtg_ioctl);
        usleep((sess.app_running || ctl.active) ? 500000u : 1000000u);
    }

    if (ctl.active) {
        int rc = call_rtg_ioctl(false, ctl.active_mac, ctl.active_ch);
        log_msg("shutdown RTG CLEAR peer=%02x:%02x:%02x:%02x:%02x:%02x rc=%d",
                ctl.active_mac[0], ctl.active_mac[1], ctl.active_mac[2],
                ctl.active_mac[3], ctl.active_mac[4], ctl.active_mac[5], rc);
    }
    log_msg("stopped");
    return 0;
}
#endif
