/* neoconn — see neoconn.h. Pure logic; no I/O. */
#include "neoconn.h"
#include <string.h>

/* CONNECT body (40 bytes) from the verified capture, MINUS body[0:2] (the
 * body_id, written in by neoconn_build_connect -- see REVERSE_ENGINEERING.md
 * §12.13 for the field map). Bytes 2..39 are replayed unchanged for now;
 * HYPOTHESIS: a repeated param block (version/caps/stream-request), not yet
 * individually field-mapped beyond what §12.13 documents. */
static const uint8_t k_connect_body[40] = {
    0x00,0x00,0x64,0x00,0x64,0x00,0xc0,0x05,0x14,0x00,0x00,0x64,0x00,0x00,0x01,0x90,
    0x01,0xc0,0x05,0x14,0x00,0x00,0x64,0x00,0x14,0x00,0x64,0x00,0xc0,0x05,0x14,0x00,
    0x00,0x64,0x00,0x01,0x01,0x04,0x01,0x02
};

/* Idle keepalive body (26 bytes): mirrors idle telemetry. */
static const uint8_t k_idle_ka_body[26] = {
    0x70,0x7d,0x70,0x7d,0x00,0x00,0x00,0x00,0x70,0x7d,0x70,0x7d,0x00,0x00,0x00,0x00,
    0x70,0x7d,0x70,0x7d,0x00,0x00,0x00,0x00,0x00,0x00
};

_Static_assert(sizeof k_connect_body + NEOCONN_HDR_LEN == NEOCONN_CONNECT_LEN, "connect len");
_Static_assert(sizeof k_idle_ka_body + NEOCONN_HDR_LEN == NEOCONN_KEEPALIVE_LEN, "ka len");

int neoconn_wrap(uint8_t *out, size_t cap, uint16_t session, uint16_t field45,
                 uint8_t type, const uint8_t *body, size_t body_len)
{
    size_t total = NEOCONN_HDR_LEN + body_len;
    if (!out || total > 0x7fff || total > cap || (body_len && !body)) return -1;
    uint16_t lf = (uint16_t)(total | 0x8000);
    out[0] = (uint8_t)lf; out[1] = (uint8_t)(lf >> 8);
    out[2] = (uint8_t)session; out[3] = (uint8_t)(session >> 8);
    out[4] = (uint8_t)field45; out[5] = (uint8_t)(field45 >> 8);
    out[6] = type;
    uint8_t x = 0;
    for (int i = 0; i < 7; i++) x ^= out[i];
    out[7] = x;
    if (body_len) memcpy(out + NEOCONN_HDR_LEN, body, body_len);
    return (int)total;
}

int neoconn_unwrap(const uint8_t *pkt, size_t len, uint16_t *session,
                   uint8_t *type, const uint8_t **body, size_t *body_len)
{
    if (!pkt || len < NEOCONN_HDR_LEN) return 0;
    uint16_t lf = (uint16_t)(pkt[0] | (pkt[1] << 8));
    if (!(lf & 0x8000) || (size_t)(lf & 0x7fff) != len) return 0;
    uint8_t x = 0;
    for (int i = 0; i < 7; i++) x ^= pkt[i];
    if (x != pkt[7]) return 0;
    if (session) *session = (uint16_t)(pkt[2] | (pkt[3] << 8));
    if (type) *type = pkt[6];
    if (body) *body = pkt + NEOCONN_HDR_LEN;
    if (body_len) *body_len = len - NEOCONN_HDR_LEN;
    return 1;
}

int neoconn_build_connect(uint8_t *out, size_t cap, uint16_t session,
                          uint16_t body_id)
{
    uint8_t body[sizeof k_connect_body];
    memcpy(body, k_connect_body, sizeof body);
    /* NOTE: body[0:2] is stored BIG-ENDIAN (high byte first), unlike the
     * wrapper's little-endian session field -- confirmed empirically: the
     * captured body for wrapper 0x4fb0 begins with bytes 70,7d (body_id
     * 0x707d written high-byte-first), not 7d,70 (which LE would give). */
    body[0] = (uint8_t)(body_id >> 8);
    body[1] = (uint8_t)body_id;
    return neoconn_wrap(out, cap, session, 0x0000, NEOCONN_T_CONNECT,
                        body, sizeof body);
}

int neoconn_build_keepalive(uint8_t *out, size_t cap, uint16_t session,
                            uint16_t field45, const uint8_t *tlm_body,
                            size_t tlm_len)
{
    if (!tlm_body)
        return neoconn_wrap(out, cap, session, field45, NEOCONN_T_KEEPALIVE,
                            k_idle_ka_body, sizeof k_idle_ka_body);
    return neoconn_wrap(out, cap, session, field45, NEOCONN_T_KEEPALIVE,
                        tlm_body, tlm_len);
}

int neoconn_parse_accept(const uint8_t *pkt, size_t len, uint16_t session)
{
    uint16_t s; uint8_t t; const uint8_t *b; size_t bl;
    if (!neoconn_unwrap(pkt, len, &s, &t, &b, &bl)) return 0;
    return s == session && t == NEOCONN_T_CONNECT && bl == 1 && b[0] == 0x01;
}

void neoconn_init(neoconn_t *c, uint16_t session, uint16_t body_id,
                  neoconn_emit_fn emit, void *ctx)
{
    memset(c, 0, sizeof *c);
    c->state = NEOCONN_IDLE;
    c->session = session;
    c->body_id = body_id;
    c->field45 = 0x0000;   /* observed 0 in every fixture frame (see header); semantics unknown */
    c->emit = emit;
    c->ctx = ctx;
}

void neoconn_arm(neoconn_t *c, int armed) { c->armed = armed ? 1 : 0; }

static void do_emit(neoconn_t *c, const uint8_t *f, int n)
{
    if (c->armed && c->emit && n > 0) c->emit(f, (size_t)n, c->ctx);
}

void neoconn_on_rx(neoconn_t *c, const uint8_t *pkt, size_t len, uint64_t now_ms)
{
    uint16_t s; uint8_t t; const uint8_t *b; size_t bl;
    if (c->state == NEOCONN_CONNECTING && neoconn_parse_accept(pkt, len, c->session)) {
        c->state = NEOCONN_CONNECTED;
        c->last_tlm_ms = now_ms;
        c->last_ka_ms = now_ms;
        return;
    }
    if (neoconn_unwrap(pkt, len, &s, &t, &b, &bl) && s == c->session &&
        t == NEOCONN_T_TELEMETRY) {
        c->last_tlm_ms = now_ms;
        if (bl <= sizeof c->tlm_body) {
            memcpy(c->tlm_body, b, bl);
            c->tlm_len = bl;
        }
    }
}

void neoconn_tick(neoconn_t *c, uint64_t now_ms)
{
    uint8_t f[NEOCONN_MAX_FRAME];
    int n;
    if (!c->armed) { c->state = NEOCONN_IDLE; return; }
    switch (c->state) {
    case NEOCONN_IDLE:
        n = neoconn_build_connect(f, sizeof f, c->session, c->body_id);
        do_emit(c, f, n);
        c->state = NEOCONN_CONNECTING;
        c->last_connect_ms = now_ms;
        break;
    case NEOCONN_CONNECTING:
        if (now_ms - c->last_connect_ms >= NEOCONN_CONNECT_RETRY_MS) {
            n = neoconn_build_connect(f, sizeof f, c->session, c->body_id);
            do_emit(c, f, n);
            c->last_connect_ms = now_ms;
        }
        break;
    case NEOCONN_CONNECTED:
        if (now_ms - c->last_tlm_ms >= NEOCONN_TLM_TIMEOUT_MS) {
            c->state = NEOCONN_LOST;
            break;
        }
        if (now_ms - c->last_ka_ms >= NEOCONN_KEEPALIVE_MS) {
            n = neoconn_build_keepalive(f, sizeof f, c->session, c->field45,
                                        c->tlm_len ? c->tlm_body : NULL, c->tlm_len);
            do_emit(c, f, n);
            c->last_ka_ms = now_ms;
        }
        break;
    case NEOCONN_LOST:
        c->state = NEOCONN_IDLE;   /* re-establish on next tick */
        break;
    }
}
