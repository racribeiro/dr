#ifndef DJI_NEO_DJI_NEO_H
#define DJI_NEO_DJI_NEO_H

/*
 * DJI Neo SDK public C API.
 *
 * The SDK owns DJI wire encoding, activation and decoding. Hosts own the UDP socket, BLE
 * provisioning, thread/event-loop choice, persistence, Zenoh, KLV and media
 * muxing. All callbacks are made synchronously by dji_neo_poll() or
 * dji_neo_on_datagram(); the SDK never creates a thread.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DJI_NEO_PEER_HOST "192.168.2.1"
#define DJI_NEO_PEER_PORT 9003u
#define DJI_NEO_MAX_DUML 1023u /* ten-bit DUML length field */

typedef struct dji_neo dji_neo_t;

typedef enum {
    DJI_NEO_OK = 0,
    DJI_NEO_EINVAL = -1,
    DJI_NEO_ESTATE = -2,
    DJI_NEO_EAUTH = -3,
    DJI_NEO_EIO = -4,
    DJI_NEO_ENOSPACE = -5
} dji_neo_result_t;

typedef enum {
    DJI_NEO_LINK_IDLE,
    DJI_NEO_LINK_CONNECTING,
    DJI_NEO_LINK_CONNECTED,
    DJI_NEO_LINK_LOST
} dji_neo_link_state_t;

typedef struct {
    int has_link;       /* a Neo session has been accepted */
    int has_actuation;  /* connected session AND signer reports ready */
} dji_neo_capabilities_t;

/* This is deliberately an SDK data contract, not a Zenoh message. Angles are
 * degrees because that is the decoded OSD representation; hosts convert at
 * their egress boundary if their ICD requires radians. */
typedef struct {
    int gps_valid;
    int attitude_valid;
    double latitude_deg;
    double longitude_deg;
    double relative_altitude_m;
    float roll_deg;
    float pitch_deg;
    float yaw_deg;
    int satellites; /* -1 when not carried by this packet */
    uint64_t monotonic_ms;
} dji_neo_telemetry_t;

typedef struct {
    const uint8_t *data;
    size_t size;
    uint64_t monotonic_ms;
    /* Wrapper type 2 carries H.265 stream material. Access-unit boundaries are
     * not claimed until the depayloader is brought into this SDK. */
    int codec_h265;
} dji_neo_video_packet_t;

typedef void (*dji_neo_telemetry_cb)(void *user, const dji_neo_telemetry_t *telemetry);
typedef void (*dji_neo_video_cb)(void *user, const dji_neo_video_packet_t *video);
typedef void (*dji_neo_state_cb)(void *user, dji_neo_link_state_t state);

/* Send one complete UDP datagram to the already-provisioned Neo Wi-Fi AP.
 * BLE is intentionally absent: it is a provisioning/sideband concern, not a
 * protocol-link substitute. Return 0 only when all bytes were accepted. */
typedef int (*dji_neo_udp_send_fn)(void *user, const uint8_t *data, size_t size);

typedef struct {
    dji_neo_udp_send_fn udp_send;
    void *udp_user;
    dji_neo_telemetry_cb on_telemetry;
    dji_neo_video_cb on_video;
    dji_neo_state_cb on_state;
    void *callback_user;
    /* Fresh, independently chosen identifiers for this connection attempt.
     * Do not reuse either after a reconnect. The SDK currently does not own an
     * entropy source, so the host must provide them. body_id high-byte low
     * three bits must be zero (e.g. random_u16 & 0xf8ff): its wire bytes seed
     * the +8 sequence space; low sequence bits are protocol flags. Invalid
     * alignment makes create return NULL / reset_session return EINVAL. */
    uint16_t session_id;
    uint16_t body_id;
    /* Legacy 26-byte experimental template: only bytes 20..25 supply the
     * compact third channel's opaque fields and trailer. Bytes 0..19 are
     * ignored: SDK generates all watermarks and variable receive ACK blocks.
     * Zero is the default. Do not copy telemetry/DUML into this template. */
    uint8_t keepalive_body[26];
    /* Activation on CONNECTED is automatic unless explicitly disabled. */
    int disable_activation;
} dji_neo_config_t;

/* Complete state supplied to the optional type-5 signer. `rc_subheader` is
 * mutable: the signer writes the rolling-code/flag and any future fields. The
 * session id, bootstrap body id, f45 and counter identify this uplink.
 * rc_subheader[0..1] is a LE peer acknowledgement, initialized from the BE
 * CONNECT body-id bytes and advanced by validated type-1 receive state. It
 * is NOT a fixed body-id field after connection. */
typedef struct {
    uint16_t session_id;
    uint16_t body_id;
    uint16_t field45;
    uint8_t counter;
    uint8_t rc_subheader[12];
    const uint8_t *duml;
    size_t duml_size;
} dji_neo_sign_request_t;

/* Return 0 to authorise the uplink, nonzero to decline it. When ready() returns
 * 1, this hook is used for EVERY type-5 packet, including neutral activation,
 * liveview, subscriptions, heartbeat, queries and gated actuation. Signer may
 * modify subheader bytes 4..11; it must preserve SDK-owned ACK/f45 bytes 0..3. No signer means
 * has_actuation == 0. This hook is never invoked by receive, telemetry or
 * video data-plane paths. */
typedef int (*dji_neo_sign_fn)(void *user, dji_neo_sign_request_t *request);
/* Explicit session capability: return 1 only when session signing state is ready.
 * A no-op or unavailable signer returns 0. Presence of sign() alone never
 * enables actuation. Unready/missing signing state skips the hook for passive
 * uplink only; it cannot bypass actuation gates. A ready signer's rejection
 * blocks that packet with EAUTH (no unsigned fallback). The host owns any key
 * establishment and signer state; the rolling-code algorithm is not recovered. */
typedef int (*dji_neo_signer_ready_fn)(void *user, uint16_t session_id, uint16_t body_id);

typedef struct {
    dji_neo_sign_fn sign;
    void *user;
    dji_neo_signer_ready_fn ready;
} dji_neo_signer_t;

dji_neo_t *dji_neo_create(const dji_neo_config_t *config);
void dji_neo_destroy(dji_neo_t *neo);

void dji_neo_set_signer(dji_neo_t *neo, const dji_neo_signer_t *signer);
void dji_neo_get_capabilities(const dji_neo_t *neo, dji_neo_capabilities_t *out);
dji_neo_link_state_t dji_neo_link_state(const dji_neo_t *neo);
/* Explicit fresh session after loss/disarm; resets counters, activation and
 * all arms. Refused while session-armed. Install freshly keyed signer state
 * before rearming commands. There is no automatic reuse of lost session IDs. */
dji_neo_result_t dji_neo_reset_session(dji_neo_t *neo, uint16_t session_id,
                                       uint16_t body_id);
/* Optional experimental keepalive template (only bytes 20..25 are used);
 * NULL clears it. Configure while disarmed, including after reset_session(). */
dji_neo_result_t dji_neo_set_keepalive_body(dji_neo_t *neo, const uint8_t body[26]);
/* Explicitly request another finite liveview start burst on a connected link.
 * Useful if no video arrives. No implicit endless retry/start traffic. */
dji_neo_result_t dji_neo_restart_liveview(dji_neo_t *neo);
/* Configure opaque byte 4 and cadence (20..1000 ms; defaults 0x1a/66 ms).
 * Allowed disarmed or connected; does not restart/send. Call restart_liveview
 * separately for a new finite burst. Profile persists across session reset. */
dji_neo_result_t dji_neo_set_liveview_profile(dji_neo_t *neo, uint8_t token,
                                             uint16_t interval_ms);

/* Session arm permits connect/keepalive and passive activation transmission. It is independent of
 * actuation signing and defaults off. Disarming clears every downstream gate. */
dji_neo_result_t dji_neo_set_session_armed(dji_neo_t *neo, int armed);

/* Host-driven link pump. Call regularly (about every 5 ms) with a monotonic
 * clock. Callbacks run synchronously. The API is single-threaded and callbacks
 * must not reenter or destroy the client. Backward clock values are rejected.
 * Valid telemetry/video/type-3 receive keeps the link alive; stale telemetry
 * still revokes actuation. LINK_LOST stops traffic until fresh reset/rearm. */
dji_neo_result_t dji_neo_poll(dji_neo_t *neo, uint64_t monotonic_ms);

/* Feed an incoming complete Wi-Fi/UDP payload. Data-plane decoding works
 * regardless of signer presence or arm state. Callbacks run synchronously. */
dji_neo_result_t dji_neo_on_datagram(dji_neo_t *neo, const uint8_t *data,
                                     size_t size, uint64_t monotonic_ms);

/* Actuation state machine: session arm -> command arm -> takeover confirmation
 * -> stick enable. Every later gate is revoked automatically by a prior one. */
dji_neo_result_t dji_neo_set_command_armed(dji_neo_t *neo, int armed);
dji_neo_result_t dji_neo_confirm_takeover(dji_neo_t *neo, int confirmed);
dji_neo_result_t dji_neo_set_stick_enabled(dji_neo_t *neo, int enabled);

/* Conservative passive allowlist: currently only empty 00/01 heartbeat from
 * source 02 to destination 0e with command type 40. All other raw frames are
 * rejected, preventing a command from bypassing actuation gates via this API.
 * Works unsigned when signing is unavailable; uses a ready optional signer.
 * Use typed APIs as more queries are validated. */
dji_neo_result_t dji_neo_send_query(dji_neo_t *neo, const uint8_t *duml, size_t size);

/* Sends an actuation command only after every safety gate and signer approval.
 * The caller supplies a valid DUML frame; command semantics remain explicit in
 * the host until each command is validated on owned test hardware. */
dji_neo_result_t dji_neo_send_actuation(dji_neo_t *neo, const uint8_t *duml,
                                         size_t size);

/* Typed command APIs own DUML sequence numbers and RC wrapper counters.
 * All gimbal operations require every actuation gate and signer readiness.
 * start sends the four enable frames; poll sends control keepalive at 1 Hz.
 * set rate schedules 25 Hz output, returning to zero after 600 ms without a
 * refresh. No degree/sec or up/down interpretation is asserted. stop sends a
 * zero-rate command and closes the gimbal control session. */
dji_neo_result_t dji_neo_gimbal_start(dji_neo_t *neo);
dji_neo_result_t dji_neo_gimbal_set_rate(dji_neo_t *neo, int rate, uint64_t monotonic_ms);
dji_neo_result_t dji_neo_gimbal_stop(dji_neo_t *neo);
/* Deflections -660..660. poll emits signed non-neutral sticks at ~19 Hz and
 * returns to center after 600 ms without refresh. Passive centered heartbeat
 * is independent of actuation gates; it uses a ready optional signer. */
dji_neo_result_t dji_neo_set_stick(dji_neo_t *neo, int roll, int pitch,
                                  int throttle, int yaw, uint64_t monotonic_ms);

#ifdef __cplusplus
}
#endif
#endif
