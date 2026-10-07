/*
 * neoconn — active DJI Neo connect + UDP session keepalive (CARD-021, core).
 *
 * Pure C, no sockets: a wrapper codec, frame builders and a connection state
 * machine driven by an injected monotonic clock and an emit callback.
 *
 * Wrapper (8 bytes, little-endian, REVERSE_ENGINEERING.md §12.1):
 *   0-1 u16  low 15 bits = total packet length, bit15 = 1
 *   2-3 u16  session id
 *   4-5 u16  "field45" -- HYPOTHESIS/UNRESOLVED (seq? counter?). It is 0000 in every
 *            captured frame (the card's "0459" is type=04,xor=59). Opaque, caller-supplied.
 *   6   u8   type (0 connect/handshake, 1 telemetry, 2 video, 4 keepalive, 5 cmd)
 *   7   u8   XOR of bytes 0..6
 */
#ifndef NEOCONN_H
#define NEOCONN_H

#include <stddef.h>
#include <stdint.h>

#define NEOCONN_T_CONNECT   0
#define NEOCONN_T_TELEMETRY 1
#define NEOCONN_T_VIDEO     2
#define NEOCONN_T_KEEPALIVE 4
#define NEOCONN_T_CMD       5

#define NEOCONN_HDR_LEN        8
#define NEOCONN_CONNECT_LEN    48
#define NEOCONN_KEEPALIVE_LEN  34
#define NEOCONN_MAX_FRAME      256

/* Tunable timings (ms). */
#define NEOCONN_KEEPALIVE_MS     20     /* 50 Hz, measured */
#define NEOCONN_CONNECT_RETRY_MS 1000   /* no accept -> re-send CONNECT */
#define NEOCONN_TLM_TIMEOUT_MS   2000   /* no telemetry -> LOST */

int neoconn_wrap(uint8_t *out, size_t cap, uint16_t session, uint16_t field45,
                 uint8_t type, const uint8_t *body, size_t body_len);
int neoconn_unwrap(const uint8_t *pkt, size_t len, uint16_t *session,
                   uint8_t *type, const uint8_t **body, size_t *body_len);

/* body_id: the 40-byte body's leading u16 (body[0:2], BIG-ENDIAN -- high byte
 * first, unlike the wrapper's LE session field). CONFIRMED from
 * captures to be an identity INDEPENDENT of the wrapper session id (see
 * REVERSE_ENGINEERING.md §12.13): e.g. wrapper 0x4fb0 pairs with body
 * 0x707d, wrapper 0x709b with body 0xd084 -- never equal, no arithmetic
 * relation found. Caller must supply both; neoconn_build_connect no longer
 * hardcodes body_id. Whether body_id can be freely invented (like the
 * wrapper session) or must be reused from an observed DJI-Fly connect is
 * UNRESOLVED pending an on-device test (Step 3, operator-gated). */
int neoconn_build_connect(uint8_t *out, size_t cap, uint16_t session,
                          uint16_t body_id);
/* tlm_body NULL -> stored idle body; else mirror the latest telemetry body. */
int neoconn_build_keepalive(uint8_t *out, size_t cap, uint16_t session,
                            uint16_t field45, const uint8_t *tlm_body,
                            size_t tlm_len);
int neoconn_parse_accept(const uint8_t *pkt, size_t len, uint16_t session);

typedef enum {
    NEOCONN_IDLE, NEOCONN_CONNECTING, NEOCONN_CONNECTED, NEOCONN_LOST
} neoconn_state_t;

typedef void (*neoconn_emit_fn)(const uint8_t *pkt, size_t len, void *ctx);

typedef struct {
    neoconn_state_t state;
    uint16_t session;
    uint16_t body_id;             /* CONNECT body[0:2] (big-endian); see neoconn_build_connect */
    uint16_t field45;            /* keepalive wrapper bytes 4-5 (opaque) */
    uint64_t last_tlm_ms;
    uint64_t last_ka_ms;
    uint64_t last_connect_ms;
    int armed;
    uint8_t tlm_body[NEOCONN_MAX_FRAME];
    size_t tlm_len;              /* 0 -> use idle body */
    neoconn_emit_fn emit;
    void *ctx;
} neoconn_t;

void neoconn_init(neoconn_t *c, uint16_t session, uint16_t body_id,
                  neoconn_emit_fn emit, void *ctx);
void neoconn_arm(neoconn_t *c, int armed);
void neoconn_on_rx(neoconn_t *c, const uint8_t *pkt, size_t len, uint64_t now_ms);
void neoconn_tick(neoconn_t *c, uint64_t now_ms);

/* GLUE (follow-up slice): the Android layer owns the UDP socket to the Neo,
 * feeds every received datagram to neoconn_on_rx(), calls neoconn_tick() from
 * a ~5 ms timer with CLOCK_MONOTONIC, and sends frames from the emit callback. */

#endif
