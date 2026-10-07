/*
 * neolink — socket + thread engine that drives the neoconn state machine over
 * a real UDP socket to the DJI Neo (CARD-021 slice 2). Plain POSIX (host
 * compiles it); the Android-only parts (Network binding) arrive through the
 * protect hook registered by Java via JNI.
 *
 * SAFETY: disarmed by default. neolink_start() ALWAYS resets to disarmed and
 * neoconn forces IDLE + transmits nothing while disarmed. Only the operator's
 * token-gated /neolink/arm (or neolink_arm) enables transmission.
 *
 * COMMAND UPLINK (CARD-023) has its OWN, second arm (cmd_armed), default OFF.
 * neolink_send_command() transmits only when state==CONNECTED AND the session
 * is armed AND the command arm is on. Disarming the session, stop and start
 * all clear the command arm.
 */
#ifndef NEOLINK_H
#define NEOLINK_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include "neoconn.h"
#include "render.h"

#define NEOLINK_PEER_IP   "192.168.2.1"
#define NEOLINK_PEER_PORT 9003
/* Source port is EPHEMERAL (OS-assigned), matching DJI Fly which uses a random
 * high port each session; the Neo replies to whoever sent the CONNECT. We do
 * NOT bind a fixed local port. */

typedef int  (*neolink_protect_fn)(int fd, void *ctx);   /* 0 = ok */
typedef void (*neolink_telemetry_fn)(const flightdata_t *fd, void *ctx);

typedef struct {
    pthread_mutex_t lock;
    pthread_t       thread;
    int             running;     /* thread alive (guarded by lock) */
    int             sock;        /* UDP fd, -1 when closed */
    neoconn_t       conn;        /* guarded by lock */
    int             cmd_armed;   /* CARD-023 second gate, default 0 (guarded by lock) */
    int             stick_enabled; /* CARD-029 4th gate, default 0 (guarded by lock) */
    uint16_t        stick[4];    /* setpoint channels roll,pitch,thr,yaw (364..1684) */
    uint64_t        stick_set_ms;  /* when the setpoint was last refreshed */
    uint64_t        stick_tx_ms;   /* last stick frame emit */
    uint16_t        stick_seq;
    int             want;        /* UI request for Java: 0 none, 1 start, 2 stop */
    char            ssid[64];
    char            bssid[32];
    uint32_t        tlm_hz;      /* decoded telemetry frames in last second */
    uint32_t        tlm_acc;
    uint64_t        tlm_win_ms;
    uint64_t        rx_pkts, tx_pkts;
    uint64_t        connected_ms;  /* mono_ms when state entered CONNECTED; 0 if not */
    uint16_t        cmd_f45;     /* type-5 wrapper field45, +8 per uplink (guarded by lock) */
    uint8_t         cmd_ctr;     /* type-5 RC sub-header counter, +1 per uplink (guarded by lock) */
    int             gimbal_en_pending;  /* send the 0x04/0x10 gimbal control-enable burst */
    /* Video+KLV MPEG-TS egress (CARD-VID): forward the Neo H.265 + an ST 0601
     * KLV data channel to a media-server mpeg_ts_udp sink. Config guarded by
     * lock; the neomux handle is owned solely by the engine thread. */
    char            vid_host[80];
    uint16_t        vid_port;
    int             vid_enabled;
    uint64_t        vid_bytes;   /* UDP bytes sent (for status) */
    uint64_t        vid_aus;     /* video access units muxed */
} neolink_t;

typedef struct {
    int         running, armed, cmd_armed, stick_enabled;
    uint16_t    stick[4];        /* current setpoint channels */
    neoconn_state_t state;
    const char *state_name;      /* static string */
    char        ssid[64], bssid[32];
    uint32_t    tlm_hz;
    uint16_t    session;
    uint64_t    rx_pkts, tx_pkts;
    uint32_t    connected_secs;  /* seconds since state entered CONNECTED; 0 if not */
    int         vid_enabled;     /* video egress configured + on */
    char        vid_host[80];
    uint16_t    vid_port;
    uint64_t    vid_bytes, vid_aus;   /* egress counters */
} neolink_status_t;

neolink_t *neolink_global(void);
void neolink_set_protect(neolink_protect_fn fn, void *ctx);
/* Network handle (Android Network.getNetworkHandle()) of the self-joined Neo AP;
 * 0 clears it. When set, the socket is bound via NDK android_setsocknetwork(). */
void neolink_set_neo_network(unsigned long long handle);
/* Called (from the engine thread, outside the lock) per decoded OSD frame. */
void neolink_set_telemetry(neolink_telemetry_fn fn, void *ctx);

/* Configure the video+KLV MPEG-TS/UDP egress. host="" or enabled=0 turns it
 * off. Takes effect on the next engine loop (opens/closes the sink). Safe any
 * time. The H.265 is forwarded verbatim (no re-encode); KLV is an ST 0601 data
 * channel in the same TS — the media-server does any SEI merge, we never touch
 * the video frames. */
void neolink_set_video_egress(neolink_t *c, const char *host, uint16_t port, int enabled);

int  neolink_start(neolink_t *c);     /* 0 ok; always starts DISARMED */
void neolink_stop(neolink_t *c);      /* joins; leaves disarmed */
void neolink_arm(neolink_t *c, int armed);
void neolink_set_ssid(neolink_t *c, const char *ssid, const char *bssid);
void neolink_status(neolink_t *c, neolink_status_t *out);
const char *neolink_state_name(neoconn_state_t s);

/* Command uplink (CARD-023). Return codes of neolink_send_command: */
#define NEOLINK_CMD_SENT     1    /* emitted exactly one type-5 frame */
#define NEOLINK_CMD_DRYRUN   0    /* gate closed: NOTHING emitted */
#define NEOLINK_CMD_EINVAL  -1    /* bad args / frame too large */

/* Arm/disarm the command uplink. Arming is refused (stays 0) unless the engine
 * is running AND the session is armed. Disarm always works. */
void neolink_arm_command(neolink_t *c, int armed);
/* Wrap `duml` as type 5 and emit it ONLY if CONNECTED && session-armed &&
 * command-armed (all re-checked under the lock at send time). */
int  neolink_send_command(neolink_t *c, const uint8_t *duml, size_t len);

/* Experimentation hook: wrap `duml` as type-5 (RC sub-header) and send it when
 * CONNECTED, BYPASSING the command-arm gate. For the raw-command REST endpoint
 * so commands can be tried without rebuilding. Returns NEOLINK_CMD_SENT/DRYRUN. */
int  neolink_send_raw(neolink_t *c, const uint8_t *duml, size_t len);

/* VIRTUAL STICKS (CARD-029). A FOURTH, separate opt-in on top of session arm +
 * command arm. Sticks are transmitted ONLY when CONNECTED && session-armed &&
 * command-armed && stick_enabled (all re-checked under the lock at send time).
 * ch1/ch2 polarity is an UNVERIFIED HYPOTHESIS (see neostick.h).
 * The setpoint is re-sent at NEOLINK_STICK_HZ by the engine thread; it returns
 * to center on release, on stick-disable, on any disarm, on stop/start, and if
 * not refreshed within NEOLINK_STICK_HOLD_MS (UI vanished). */
#define NEOLINK_STICK_PERIOD_MS 52     /* ~19.2 Hz */
#define NEOLINK_STICK_HOLD_MS   600    /* setpoint expires -> center */
/* Takes effect only when command-armed (which implies running + session armed).
 * Disabling re-centers (and emits one neutral frame if the link gate is open). */
void neolink_arm_stick(neolink_t *c, int enabled);
/* Set setpoint from deflections -660..+660 (0 = center). Returns 0 stored, -1 if
 * stick is not enabled (setpoint stays centered). */
int  neolink_set_stick(neolink_t *c, int roll, int pitch, int throttle, int yaw);
/* Engine hook: emit one stick frame if due and all four gates are open.
 * Returns 1 if a frame was emitted. Takes the lock itself. */
int  neolink_stick_tick(neolink_t *c, uint64_t now_ms);

/* Pure helper (host-testable): given a raw UDP payload from the Neo, strip the
 * wrapper (type 1 only) and decode the first OSD DUML frame. 1 if filled. */
int neolink_decode_payload(const uint8_t *pkt, size_t len, flightdata_t *out);

#endif
