#ifndef DJI_NEO_DJI_NEO_H
#define DJI_NEO_DJI_NEO_H

/*
 * DJI Neo SDK public C API.
 *
 * The SDK owns DJI wire decoding only. Hosts own the UDP socket, BLE
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
#define DJI_NEO_MAX_DUML 1024u

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
    int has_actuation;  /* installed signer has authorised command framing */
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
    uint16_t session_id;
    uint16_t body_id;
} dji_neo_config_t;

/* Optional signer for type-5 uplink. It may alter the twelve-byte RC subheader
 * in place, including its final rolling-code/flag byte. Return 0 to authorise
 * the command, nonzero to decline it. No signer means has_actuation == 0.
 * This hook is never invoked by receive, telemetry or video data-plane paths. */
typedef int (*dji_neo_sign_fn)(void *user, uint8_t rc_subheader[12],
                               const uint8_t *duml, size_t duml_size);

typedef struct {
    dji_neo_sign_fn sign;
    void *user;
} dji_neo_signer_t;

dji_neo_t *dji_neo_create(const dji_neo_config_t *config);
void dji_neo_destroy(dji_neo_t *neo);

void dji_neo_set_signer(dji_neo_t *neo, const dji_neo_signer_t *signer);
void dji_neo_get_capabilities(const dji_neo_t *neo, dji_neo_capabilities_t *out);
dji_neo_link_state_t dji_neo_link_state(const dji_neo_t *neo);

/* Session arm permits connect/keepalive transmission. It is independent of
 * actuation signing and defaults off. Disarming clears every downstream gate. */
dji_neo_result_t dji_neo_set_session_armed(dji_neo_t *neo, int armed);

/* Host-driven link pump. Call regularly (about every 5 ms) with a monotonic
 * clock. State callbacks, if any, run synchronously in this function. */
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

/* Sends a read-only/light query. It needs a connected, session-armed link but
 * does not require a signer or command/takeover/stick gate. */
dji_neo_result_t dji_neo_send_query(dji_neo_t *neo, const uint8_t *duml, size_t size);

/* Sends an actuation command only after every safety gate and signer approval.
 * The caller supplies a valid DUML frame; command semantics remain explicit in
 * the host until each command is validated on owned test hardware. */
dji_neo_result_t dji_neo_send_actuation(dji_neo_t *neo, const uint8_t *duml,
                                         size_t size);

#ifdef __cplusplus
}
#endif
#endif
