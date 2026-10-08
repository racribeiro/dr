#include "dji_neo/dji_neo.h"
#include "dji_neo/commands.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { HEADER = 8, CONNECT = 0, TELEMETRY = 1, VIDEO = 2, KEEPALIVE = 4, COMMAND = 5 };
enum { KA_MS = 20, CONNECT_MS = 1000, LOST_MS = 2000, HEARTBEAT_MS = 150,
       STICK_MS = 52, HOLD_MS = 600, SUB_MS = 2000, VIEW_MS = 66, VIEW_COUNT = 30 };
typedef struct {
    uint8_t source, destination, kind, set, id;
    uint16_t payload_size;
    uint8_t payload[132];
} activation_template_t;
#include "activation_frames.inc"

struct dji_neo {
    dji_neo_config_t cfg;
    dji_neo_signer_t signer;
    dji_neo_link_state_t state, notified_state;
    uint16_t field45, last_field45, sequence;
    uint8_t counter;
    int session_armed, session_used, command_armed, takeover, stick_enabled;
    int clock_set, connect_sent, has_type5;
    uint64_t now, connect_at, last_telemetry, ka_at;
    uint64_t heartbeat_at, stick_at, sub_at, view_at;
    size_t sub_index;
    unsigned view_count;
    int sub_running, first_sub_done;
    uint16_t sticks[4];
    uint64_t stick_set_at;
    unsigned gimbal_stage;
    int gimbal_active, gimbal_rate, gimbal_pending;
    uint64_t gimbal_at, gimbal_ka_at, gimbal_set_at;
};

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static void put16(uint8_t *p, uint16_t value) { p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8); }
static void center(dji_neo_t *n) { for (unsigned i = 0; i < 4; ++i) n->sticks[i] = 1024; }
static void revoke(dji_neo_t *n) {
    n->command_armed = n->takeover = n->stick_enabled = 0;
    n->gimbal_active = n->gimbal_pending = n->gimbal_rate = 0;
    n->gimbal_stage = 0;
    center(n);
}
static void notify_state(dji_neo_t *n) {
    if (n->notified_state == n->state) return;
    n->notified_state = n->state;
    if (n->cfg.on_state) n->cfg.on_state(n->cfg.callback_user, n->state);
}
static int ready(const dji_neo_t *n) {
    return n->signer.sign && n->signer.ready &&
        n->signer.ready(n->signer.user, n->cfg.session_id, n->cfg.body_id) == 1;
}
static int link_open(const dji_neo_t *n) {
    return n->session_armed && n->state == DJI_NEO_LINK_CONNECTED;
}
static dji_neo_result_t actuation_gate(dji_neo_t *n) {
    if (!n) return DJI_NEO_EINVAL;
    if (!link_open(n) || !n->command_armed || !n->takeover || !n->stick_enabled)
        return DJI_NEO_ESTATE;
    if (!ready(n)) { revoke(n); return DJI_NEO_EAUTH; }
    return DJI_NEO_OK;
}
static dji_neo_result_t clock_update(dji_neo_t *n, uint64_t ms) {
    if (n->clock_set && ms < n->now) return DJI_NEO_EINVAL;
    n->now = ms; n->clock_set = 1;
    return DJI_NEO_OK;
}
static int unwrap(const uint8_t *p, size_t size, uint8_t *type,
                  const uint8_t **body, size_t *body_size) {
    if (!p || size < HEADER || size > 65507 || !(le16(p) & 0x8000)) return 0;
    uint8_t x = 0;
    for (unsigned i = 0; i < 7; ++i) x ^= p[i];
    if (x != p[7]) return 0;
    /* Embedded length is advisory for receive. DUML lengths/CRCs remain strict. */
    *type = p[6]; *body = p + HEADER; *body_size = size - HEADER;
    return 1;
}
static dji_neo_result_t emit(dji_neo_t *n, uint8_t type, uint16_t f45,
                            const uint8_t *body, size_t body_size) {
    uint8_t packet[HEADER + 12 + DJI_NEO_MAX_DUML];
    size_t size = HEADER + body_size;
    if (size > sizeof packet) return DJI_NEO_ENOSPACE;
    put16(packet, (uint16_t)(size | 0x8000));
    put16(packet + 2, n->cfg.session_id); put16(packet + 4, f45);
    packet[6] = type; packet[7] = 0;
    for (unsigned i = 0; i < 7; ++i) packet[7] ^= packet[i];
    memcpy(packet + HEADER, body, body_size);
    return n->cfg.udp_send(n->cfg.udp_user, packet, size) == 0 ? DJI_NEO_OK : DJI_NEO_EIO;
}
static dji_neo_result_t send_command(dji_neo_t *n, const uint8_t *frame,
                                    size_t size, int actuating) {
    if (!n || !dji_neo_duml_valid(frame, size)) return DJI_NEO_EINVAL;
    if (!link_open(n)) return DJI_NEO_ESTATE;
    if (actuating) {
        dji_neo_result_t gate = actuation_gate(n);
        if (gate != DJI_NEO_OK) return gate;
    }
    dji_neo_sign_request_t r = {0};
    r.session_id = n->cfg.session_id; r.body_id = n->cfg.body_id;
    r.field45 = n->field45; r.counter = n->counter;
    r.duml = frame; r.duml_size = size;
    r.rc_subheader[0] = (uint8_t)(r.body_id >> 8);
    r.rc_subheader[1] = (uint8_t)r.body_id;
    put16(r.rc_subheader + 2, r.field45);
    r.rc_subheader[8] = r.counter; r.rc_subheader[9] = 1; r.rc_subheader[10] = 0x60;
    uint8_t identity[4]; memcpy(identity, r.rc_subheader, sizeof identity);
    /* Receive/decode never depends on this optional uplink hook. Unavailable
     * signing state leaves neutral activation/queries on their unsigned path;
     * a ready signer may sign every type-5, not just gated actuation. Never
     * silently downgrade a ready signer's rejection to an unsigned packet. */
    if ((actuating || ready(n)) && n->signer.sign(n->signer.user, &r) != 0)
        return DJI_NEO_EAUTH;
    if (memcmp(identity, r.rc_subheader, sizeof identity) != 0) return DJI_NEO_EAUTH;
    uint8_t body[12 + DJI_NEO_MAX_DUML];
    memcpy(body, r.rc_subheader, 12); memcpy(body + 12, frame, size);
    dji_neo_result_t result = emit(n, COMMAND, n->field45, body, size + 12);
    if (result == DJI_NEO_OK) {
        n->last_field45 = n->field45; n->has_type5 = 1;
        n->field45 = (uint16_t)(n->field45 + 8); ++n->counter;
    }
    return result;
}
static dji_neo_result_t send_built(dji_neo_t *n, const uint8_t *frame,
                                  int size, int actuating) {
    if (size < 0) return (dji_neo_result_t)size;
    dji_neo_result_t result = send_command(n, frame, (size_t)size, actuating);
    if (result == DJI_NEO_OK) ++n->sequence;
    return result;
}
static void activation_reset(dji_neo_t *n) {
    n->sub_running = n->first_sub_done = 0;
    n->sub_index = n->view_count = 0;
    n->heartbeat_at = n->stick_at = n->sub_at = n->view_at = n->now;
}
static dji_neo_result_t activation_tick(dji_neo_t *n) {
    uint8_t frame[DJI_NEO_MAX_DUML];
    dji_neo_result_t r;
    if (n->now >= n->heartbeat_at) {
        r = send_built(n, frame, dji_neo_build_heartbeat(frame, sizeof frame, n->sequence), 0);
        if (r != DJI_NEO_OK) return r;
        n->heartbeat_at = n->now + HEARTBEAT_MS;
    }
    if (n->now >= n->stick_at) {
        if (!n->stick_enabled || n->now - n->stick_set_at >= HOLD_MS) center(n);
        int nonneutral = 0;
        for (unsigned i = 0; i < 4; ++i) nonneutral |= n->sticks[i] != 1024;
        r = send_built(n, frame, dji_neo_build_stick(frame, sizeof frame, n->sequence,
                                                   n->sticks, (uint32_t)n->now), nonneutral);
        if (r != DJI_NEO_OK) return r;
        n->stick_at = n->now + STICK_MS;
    }
    if (!n->sub_running && n->now >= n->sub_at) {
        n->sub_running = 1; n->sub_index = 0; n->sub_at = n->now + SUB_MS;
    }
    /* Bound replay work per poll; preserve heartbeat and keepalive service. */
    for (unsigned budget = 0; n->sub_running && budget < 4; ++budget) {
        const activation_template_t *t = &activation_templates[n->sub_index];
        int size = dji_neo_duml_build(frame, sizeof frame, t->source, t->destination,
                                      n->sequence, t->kind, t->set, t->id,
                                      t->payload, t->payload_size);
        r = send_built(n, frame, size, 0);
        if (r != DJI_NEO_OK) return r;
        if (++n->sub_index == sizeof activation_templates / sizeof activation_templates[0]) {
            n->sub_running = 0; n->first_sub_done = 1;
        }
    }
    if (n->first_sub_done && n->view_count < VIEW_COUNT && n->now >= n->view_at) {
        r = send_built(n, frame, dji_neo_build_liveview(frame, sizeof frame, n->sequence,
                       (uint16_t)n->now, n->view_count < 2), 0);
        if (r != DJI_NEO_OK) return r;
        ++n->view_count; n->view_at = n->now + VIEW_MS;
    }
    return DJI_NEO_OK;
}
static dji_neo_result_t gimbal_tick(dji_neo_t *n) {
    if (!n->gimbal_active) return DJI_NEO_OK;
    dji_neo_result_t r = actuation_gate(n);
    if (r != DJI_NEO_OK) return r;
    uint8_t frame[32];
    if (n->now >= n->gimbal_ka_at) {
        r = send_built(n, frame, dji_neo_build_gimbal_keepalive(frame, sizeof frame, n->sequence), 1);
        if (r != DJI_NEO_OK) return r;
        n->gimbal_ka_at = n->now + 1000;
    }
    if (n->gimbal_pending && n->now >= n->gimbal_at) {
        if (n->now - n->gimbal_set_at >= HOLD_MS) n->gimbal_rate = 0;
        r = send_built(n, frame, dji_neo_build_gimbal_rate(frame, sizeof frame, n->sequence, n->gimbal_rate), 1);
        if (r != DJI_NEO_OK) return r;
        n->gimbal_at = n->now + 40;
        if (!n->gimbal_rate) n->gimbal_pending = 0;
    }
    return DJI_NEO_OK;
}

dji_neo_t *dji_neo_create(const dji_neo_config_t *cfg) {
    if (!cfg || !cfg->udp_send) return NULL;
    dji_neo_t *n = calloc(1, sizeof *n);
    if (n) { n->cfg = *cfg; center(n); }
    return n;
}
void dji_neo_destroy(dji_neo_t *n) { free(n); }
void dji_neo_set_signer(dji_neo_t *n, const dji_neo_signer_t *s) {
    if (!n) return;
    revoke(n); n->signer = s ? *s : (dji_neo_signer_t){0};
}
void dji_neo_get_capabilities(const dji_neo_t *n, dji_neo_capabilities_t *out) {
    if (out) *out = n ? (dji_neo_capabilities_t){link_open(n), link_open(n) && ready(n)} : (dji_neo_capabilities_t){0};
}
dji_neo_link_state_t dji_neo_link_state(const dji_neo_t *n) { return n ? n->state : DJI_NEO_LINK_IDLE; }
dji_neo_result_t dji_neo_reset_session(dji_neo_t *n, uint16_t session, uint16_t body) {
    if (!n) return DJI_NEO_EINVAL;
    if (n->session_armed) return DJI_NEO_ESTATE;
    if (n->session_used && session == n->cfg.session_id && body == n->cfg.body_id) return DJI_NEO_EINVAL;
    n->cfg.session_id = session; n->cfg.body_id = body;
    n->field45 = n->last_field45 = n->sequence = n->counter = 0;
    n->connect_sent = n->has_type5 = n->session_used = 0;
    memset(n->cfg.keepalive_body, 0, sizeof n->cfg.keepalive_body);
    revoke(n); activation_reset(n); n->state = DJI_NEO_LINK_IDLE;
    return DJI_NEO_OK;
}
dji_neo_result_t dji_neo_set_session_armed(dji_neo_t *n, int armed) {
    if (!n) return DJI_NEO_EINVAL;
    if (armed && !n->session_armed && n->session_used) return DJI_NEO_ESTATE;
    n->session_armed = !!armed;
    if (!armed) { revoke(n); n->state = DJI_NEO_LINK_IDLE; }
    return DJI_NEO_OK;
}
dji_neo_result_t dji_neo_set_keepalive_body(dji_neo_t *n, const uint8_t body[26]) {
    if (!n) return DJI_NEO_EINVAL;
    if (n->session_armed) return DJI_NEO_ESTATE;
    if (body) memcpy(n->cfg.keepalive_body, body, sizeof n->cfg.keepalive_body);
    else memset(n->cfg.keepalive_body, 0, sizeof n->cfg.keepalive_body);
    return DJI_NEO_OK;
}
dji_neo_result_t dji_neo_restart_liveview(dji_neo_t *n) {
    if (!n) return DJI_NEO_EINVAL;
    if (!link_open(n) || n->cfg.disable_activation) return DJI_NEO_ESTATE;
    n->view_count = 0; n->view_at = n->now;
    return DJI_NEO_OK;
}
dji_neo_result_t dji_neo_set_command_armed(dji_neo_t *n, int armed) {
    if (!n) return DJI_NEO_EINVAL;
    if (!armed) { revoke(n); return DJI_NEO_OK; }
    if (!link_open(n)) return DJI_NEO_ESTATE;
    if (!ready(n)) return DJI_NEO_EAUTH;
    n->command_armed = 1;
    return DJI_NEO_OK;
}
dji_neo_result_t dji_neo_confirm_takeover(dji_neo_t *n, int confirmed) {
    if (!n) return DJI_NEO_EINVAL;
    if (!confirmed) { revoke(n); return DJI_NEO_OK; }
    if (!link_open(n) || !n->command_armed || !ready(n)) return DJI_NEO_ESTATE;
    n->takeover = 1;
    return DJI_NEO_OK;
}
dji_neo_result_t dji_neo_set_stick_enabled(dji_neo_t *n, int enabled) {
    if (!n) return DJI_NEO_EINVAL;
    if (!enabled) { n->stick_enabled = 0; center(n); n->gimbal_active = n->gimbal_pending = 0; n->gimbal_stage = 0; return DJI_NEO_OK; }
    if (!link_open(n) || !n->command_armed || !n->takeover || !ready(n)) return DJI_NEO_ESTATE;
    n->stick_enabled = 1;
    return DJI_NEO_OK;
}
dji_neo_result_t dji_neo_send_query(dji_neo_t *n, const uint8_t *f, size_t size) {
    if (!n || !dji_neo_duml_valid(f, size)) return DJI_NEO_EINVAL;
    if (size != 13 || f[4] != 2 || f[5] != 0x0e || f[8] != 0x40 || f[9] != 0 || f[10] != 1)
        return DJI_NEO_EINVAL;
    return send_command(n, f, size, 0);
}
dji_neo_result_t dji_neo_send_actuation(dji_neo_t *n, const uint8_t *f, size_t size) {
    return send_command(n, f, size, 1);
}
dji_neo_result_t dji_neo_gimbal_start(dji_neo_t *n) {
    dji_neo_result_t r = actuation_gate(n);
    if (r != DJI_NEO_OK) return r;
    if (n->gimbal_active) return DJI_NEO_OK;
    uint8_t frame[32];
    while (n->gimbal_stage < 4) {
        r = send_built(n, frame, dji_neo_build_gimbal_enable(frame, sizeof frame,
                       n->sequence, n->gimbal_stage), 1);
        if (r != DJI_NEO_OK) return r;
        ++n->gimbal_stage;
    }
    n->gimbal_active = 1; n->gimbal_ka_at = n->now + 1000; n->gimbal_at = n->now;
    return DJI_NEO_OK;
}
dji_neo_result_t dji_neo_gimbal_set_rate(dji_neo_t *n, int rate, uint64_t ms) {
    if (rate < -200 || rate > 200) return DJI_NEO_EINVAL;
    dji_neo_result_t r = actuation_gate(n);
    if (r != DJI_NEO_OK) return r;
    if (!n->gimbal_active) return DJI_NEO_ESTATE;
    if (clock_update(n, ms) != DJI_NEO_OK) return DJI_NEO_EINVAL;
    n->gimbal_rate = rate; n->gimbal_pending = 1; n->gimbal_set_at = ms;
    return DJI_NEO_OK;
}
dji_neo_result_t dji_neo_gimbal_stop(dji_neo_t *n) {
    dji_neo_result_t r = actuation_gate(n);
    if (r != DJI_NEO_OK) return r;
    if (!n->gimbal_active) return DJI_NEO_ESTATE;
    uint8_t frame[32];
    r = send_built(n, frame, dji_neo_build_gimbal_rate(frame, sizeof frame, n->sequence, 0), 1);
    if (r == DJI_NEO_OK) { n->gimbal_active = n->gimbal_pending = 0; n->gimbal_stage = 0; }
    return r;
}
dji_neo_result_t dji_neo_set_stick(dji_neo_t *n, int roll, int pitch, int throttle,
                                  int yaw, uint64_t ms) {
    int values[4] = {roll, pitch, throttle, yaw};
    for (unsigned i = 0; i < 4; ++i) if (values[i] < -660 || values[i] > 660) return DJI_NEO_EINVAL;
    dji_neo_result_t r = actuation_gate(n);
    if (r != DJI_NEO_OK) return r;
    if (n->cfg.disable_activation) return DJI_NEO_ESTATE;
    if (clock_update(n, ms) != DJI_NEO_OK) return DJI_NEO_EINVAL;
    for (unsigned i = 0; i < 4; ++i) n->sticks[i] = (uint16_t)(1024 + values[i]);
    n->stick_set_at = ms;
    return DJI_NEO_OK;
}
dji_neo_result_t dji_neo_poll(dji_neo_t *n, uint64_t ms) {
    static const uint8_t tail[38] = {0x64,0,0x64,0,0xc0,5,0x14,0,0,0x64,0,0,1,0x90,1,0xc0,5,0x14,0,0,0x64,0,0x14,0,0x64,0,0xc0,5,0x14,0,0,0x64,0,1,1,4,1,2};
    if (!n || clock_update(n, ms) != DJI_NEO_OK) return DJI_NEO_EINVAL;
    notify_state(n);
    if (!n->session_armed) return DJI_NEO_OK;
    if (n->state == DJI_NEO_LINK_IDLE || n->state == DJI_NEO_LINK_CONNECTING) {
        if (!n->connect_sent || ms - n->connect_at >= CONNECT_MS) {
            uint8_t body[40] = {(uint8_t)(n->cfg.body_id >> 8), (uint8_t)n->cfg.body_id};
            memcpy(body + 2, tail, sizeof tail);
            dji_neo_result_t r = emit(n, CONNECT, 0, body, sizeof body);
            if (r != DJI_NEO_OK) return r;
            n->connect_sent = n->session_used = 1; n->connect_at = ms;
            n->state = DJI_NEO_LINK_CONNECTING; notify_state(n);
        }
        return DJI_NEO_OK;
    }
    if (ms - n->last_telemetry >= LOST_MS) {
        revoke(n); n->state = DJI_NEO_LINK_LOST; n->session_armed = 0;
        notify_state(n); return DJI_NEO_OK;
    }
    if (n->command_armed && !ready(n)) revoke(n);
    if (ms >= n->ka_at) {
        uint8_t body[26]; memcpy(body, n->cfg.keepalive_body, sizeof body);
        int seeded = 0;
        for (unsigned i = 0; i < sizeof body; ++i) seeded |= body[i];
        if (!seeded) {
            for (unsigned i = 0; i < 3; ++i) {
                body[8*i] = body[8*i+2] = (uint8_t)(n->cfg.body_id >> 8);
                body[8*i+1] = body[8*i+3] = (uint8_t)n->cfg.body_id;
            }
        }
        if (n->has_type5) put16(body + 18, n->last_field45);
        dji_neo_result_t r = emit(n, KEEPALIVE, 0, body, sizeof body);
        if (r != DJI_NEO_OK) return r;
        n->ka_at = ms + KA_MS;
    }
    if (!n->cfg.disable_activation) {
        dji_neo_result_t r = activation_tick(n);
        if (r != DJI_NEO_OK) return r;
    }
    return gimbal_tick(n);
}

static double f64le(const uint8_t *p) {
    uint64_t bits = 0; double value;
    for (int i = 7; i >= 0; --i) bits = (bits << 8) | p[i];
    memcpy(&value, &bits, sizeof value); return value;
}
static void decode_osd(dji_neo_t *n, const uint8_t *f, size_t size) {
    if (size < 43 || f[9] != 3 || f[10] != 0x43) return;
    const uint8_t *p = f + 11;
    double lon = f64le(p), lat = f64le(p + 8);
    if (!isfinite(lon) || !isfinite(lat) || fabs(lon) > 3.141594 || fabs(lat) > 1.570797) return;
    dji_neo_telemetry_t t = {0};
    t.longitude_deg = lon * 57.29577951308232; t.latitude_deg = lat * 57.29577951308232;
    t.gps_valid = lon != 0 || lat != 0; t.attitude_valid = 1;
    t.relative_altitude_m = (int16_t)le16(p + 16) / 10.0;
    t.pitch_deg = (int16_t)le16(p + 24) / 10.0f; t.roll_deg = (int16_t)le16(p + 26) / 10.0f;
    t.yaw_deg = fmodf((int16_t)le16(p + 28) / 10.0f, 360.0f);
    if (t.yaw_deg < 0) t.yaw_deg += 360.0f;
    t.satellites = size - 13 > 36 ? p[36] : -1; t.monotonic_ms = n->now;
    if (n->cfg.on_telemetry) n->cfg.on_telemetry(n->cfg.callback_user, &t);
}
dji_neo_result_t dji_neo_on_datagram(dji_neo_t *n, const uint8_t *data,
                                     size_t size, uint64_t ms) {
    uint8_t type; const uint8_t *body; size_t body_size;
    if (!n || !unwrap(data, size, &type, &body, &body_size)) return DJI_NEO_EINVAL;
    if (n->session_armed && le16(data + 2) != n->cfg.session_id) return DJI_NEO_EINVAL;
    if (type != CONNECT && type != TELEMETRY && type != VIDEO) return DJI_NEO_EINVAL;
    if (clock_update(n, ms) != DJI_NEO_OK) return DJI_NEO_EINVAL;
    if (type == CONNECT) {
        if (n->state != DJI_NEO_LINK_CONNECTING || !n->session_armed ||
            le16(data + 2) != n->cfg.session_id || body_size != 1 || body[0] != 1)
            return DJI_NEO_ESTATE;
        n->state = DJI_NEO_LINK_CONNECTED; n->last_telemetry = ms;
        n->ka_at = ms + KA_MS; activation_reset(n);
    } else if (type == TELEMETRY) {
        if (link_open(n)) n->last_telemetry = ms;
        for (size_t i = 0; i + 13 <= body_size; ++i) {
            if (body[i] != 0x55) continue;
            size_t length = (size_t)body[i + 1] | ((size_t)(body[i + 2] & 3) << 8);
            if (length >= 13 && length <= body_size - i && dji_neo_duml_valid(body + i, length)) {
                decode_osd(n, body + i, length); i += length - 1;
            }
        }
    } else if (n->cfg.on_video) {
        dji_neo_video_packet_t v = {body, body_size, ms, 1};
        n->cfg.on_video(n->cfg.callback_user, &v);
    }
    notify_state(n); return DJI_NEO_OK;
}
