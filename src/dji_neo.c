#include "dji_neo/dji_neo.h"
#include "duml.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { WRAP_HEADER = 8, TYPE_CONNECT = 0, TYPE_TELEMETRY = 1, TYPE_VIDEO = 2, TYPE_KEEPALIVE = 4, TYPE_COMMAND = 5 };
struct dji_neo {
    dji_neo_config_t cfg;
    dji_neo_signer_t signer;
    dji_neo_link_state_t state;
    uint16_t field45;
    uint8_t command_counter;
    uint64_t last_connect_ms, last_keepalive_ms, last_telemetry_ms;
    int session_armed, command_armed, takeover_confirmed, stick_enabled;
};
static uint16_t rd16le(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
static int wrapped(const uint8_t *p, size_t n, uint8_t *type, const uint8_t **body, size_t *body_n) {
    if (!p || n < WRAP_HEADER || !(rd16le(p) & 0x8000) || (rd16le(p) & 0x7fff) != n) return 0;
    uint8_t x = 0; for (size_t i = 0; i < 7; ++i) x ^= p[i]; if (x != p[7]) return 0;
    *type = p[6]; *body = p + WRAP_HEADER; *body_n = n - WRAP_HEADER; return 1;
}
static int send_wrapped(dji_neo_t *neo, uint8_t type, uint16_t f45, const uint8_t *body, size_t body_n) {
    uint8_t packet[WRAP_HEADER + DJI_NEO_MAX_DUML + 12];
    size_t n = WRAP_HEADER + body_n;
    if (!neo->cfg.udp_send || n > sizeof packet || n > 0x7fff) return -1;
    uint16_t len = (uint16_t)(n | 0x8000);
    packet[0] = (uint8_t)len; packet[1] = (uint8_t)(len >> 8);
    packet[2] = (uint8_t)neo->cfg.session_id; packet[3] = (uint8_t)(neo->cfg.session_id >> 8);
    packet[4] = (uint8_t)f45; packet[5] = (uint8_t)(f45 >> 8); packet[6] = type;
    packet[7] = 0; for (size_t i = 0; i < 7; ++i) packet[7] ^= packet[i];
    if (body_n) memcpy(packet + WRAP_HEADER, body, body_n);
    return neo->cfg.udp_send(neo->cfg.udp_user, packet, n);
}
static void set_state(dji_neo_t *neo, dji_neo_link_state_t state) {
    if (neo->state == state) return;
    neo->state = state;
    if (neo->cfg.on_state) neo->cfg.on_state(neo->cfg.callback_user, state);
}
static void revoke_command_gates(dji_neo_t *neo) { neo->command_armed = neo->takeover_confirmed = neo->stick_enabled = 0; }
static double f64le(const uint8_t *p) { uint64_t x = 0; double d; for (int i = 7; i >= 0; --i) x = (x << 8) | p[i]; memcpy(&d, &x, sizeof d); return d; }
static int16_t s16le(const uint8_t *p) { return (int16_t)rd16le(p); }
static void decode_osd(dji_neo_t *neo, const uint8_t *frame, size_t n, uint64_t ms) {
    if (!dji_neo_duml_valid(frame, n) || frame[9] != 3 || frame[10] != 0x43 || n < 43) return;
    const uint8_t *p = frame + 11; size_t pn = n - 13; if (pn < 30) return;
    double lon = f64le(p), lat = f64le(p + 8); if (!isfinite(lon) || !isfinite(lat) || fabs(lon) > 3.141594 || fabs(lat) > 1.570797) return;
    dji_neo_telemetry_t t = {0};
    t.longitude_deg = lon * 57.29577951308232; t.latitude_deg = lat * 57.29577951308232;
    t.gps_valid = lon != 0.0 || lat != 0.0; t.attitude_valid = 1; t.relative_altitude_m = s16le(p + 16) / 10.0;
    t.pitch_deg = s16le(p + 24) / 10.0f; t.roll_deg = s16le(p + 26) / 10.0f; t.yaw_deg = fmodf(s16le(p + 28) / 10.0f + 360.0f, 360.0f);
    t.satellites = pn > 36 ? p[36] : -1; t.monotonic_ms = ms;
    if (neo->cfg.on_telemetry) neo->cfg.on_telemetry(neo->cfg.callback_user, &t);
}
static dji_neo_result_t command(dji_neo_t *neo, const uint8_t *duml, size_t n, int needs_signer) {
    if (!neo || !dji_neo_duml_valid(duml, n) || neo->state != DJI_NEO_LINK_CONNECTED || !neo->session_armed) return DJI_NEO_ESTATE;
    uint8_t b[12 + DJI_NEO_MAX_DUML]; uint16_t f45 = neo->field45;
    b[0] = (uint8_t)(neo->cfg.body_id >> 8); b[1] = (uint8_t)neo->cfg.body_id; b[2] = (uint8_t)f45; b[3] = (uint8_t)(f45 >> 8);
    memset(b + 4, 0, 4); b[8] = neo->command_counter++; b[9] = 1; b[10] = 0x60; b[11] = 0;
    if (needs_signer && (!neo->signer.sign || neo->signer.sign(neo->signer.user, b, duml, n) != 0)) return DJI_NEO_EAUTH;
    memcpy(b + 12, duml, n); neo->field45 = (uint16_t)(neo->field45 + 8);
    return send_wrapped(neo, TYPE_COMMAND, f45, b, n + 12) == 0 ? DJI_NEO_OK : DJI_NEO_EIO;
}
dji_neo_t *dji_neo_create(const dji_neo_config_t *config) { if (!config || !config->udp_send) return NULL; dji_neo_t *n = calloc(1, sizeof *n); if (n) { n->cfg = *config; n->state = DJI_NEO_LINK_IDLE; } return n; }
void dji_neo_destroy(dji_neo_t *neo) { free(neo); }
void dji_neo_set_signer(dji_neo_t *neo, const dji_neo_signer_t *s) { if (!neo) return; neo->signer = s ? *s : (dji_neo_signer_t){0}; if (!s) revoke_command_gates(neo); }
void dji_neo_get_capabilities(const dji_neo_t *neo, dji_neo_capabilities_t *out) { if (out) *out = neo ? (dji_neo_capabilities_t){neo->state == DJI_NEO_LINK_CONNECTED, neo->signer.sign != NULL} : (dji_neo_capabilities_t){0}; }
dji_neo_link_state_t dji_neo_link_state(const dji_neo_t *neo) { return neo ? neo->state : DJI_NEO_LINK_IDLE; }
dji_neo_result_t dji_neo_set_session_armed(dji_neo_t *neo, int armed) { if (!neo) return DJI_NEO_EINVAL; neo->session_armed = !!armed; if (!armed) { revoke_command_gates(neo); set_state(neo, DJI_NEO_LINK_IDLE); } return DJI_NEO_OK; }
dji_neo_result_t dji_neo_poll(dji_neo_t *neo, uint64_t ms) {
    static const uint8_t connect_tail[38] = {0x64,0,0x64,0,0xc0,5,0x14,0,0,0x64,0,0,1,0x90,1,0xc0,5,0x14,0,0,0x64,0,0x14,0,0x64,0,0xc0,5,0x14,0,0,0x64,0,1,1,4,1,2};
    if (!neo) return DJI_NEO_EINVAL;
    if (!neo->session_armed) return DJI_NEO_OK;
    if (neo->state == DJI_NEO_LINK_IDLE || (neo->state == DJI_NEO_LINK_CONNECTING && ms - neo->last_connect_ms >= 1000)) { uint8_t b[40] = {(uint8_t)(neo->cfg.body_id >> 8),(uint8_t)neo->cfg.body_id}; memcpy(b + 2, connect_tail, sizeof connect_tail); if (send_wrapped(neo, TYPE_CONNECT, 0, b, sizeof b)) return DJI_NEO_EIO; set_state(neo, DJI_NEO_LINK_CONNECTING); neo->last_connect_ms = ms; }
    else if (neo->state == DJI_NEO_LINK_CONNECTED && ms - neo->last_telemetry_ms >= 2000) set_state(neo, DJI_NEO_LINK_LOST);
    else if (neo->state == DJI_NEO_LINK_CONNECTED && ms - neo->last_keepalive_ms >= 20) { uint8_t b[26] = {0}; if (send_wrapped(neo, TYPE_KEEPALIVE, 0, b, sizeof b)) return DJI_NEO_EIO; neo->last_keepalive_ms = ms; }
    else if (neo->state == DJI_NEO_LINK_LOST) set_state(neo, DJI_NEO_LINK_IDLE);
    return DJI_NEO_OK;
}
dji_neo_result_t dji_neo_on_datagram(dji_neo_t *neo, const uint8_t *data, size_t n, uint64_t ms) { uint8_t type; const uint8_t *b; size_t bn; if (!neo || !wrapped(data,n,&type,&b,&bn)) return DJI_NEO_EINVAL; if (type == TYPE_CONNECT && rd16le(data+2) == neo->cfg.session_id && bn == 1 && b[0] == 1) { set_state(neo,DJI_NEO_LINK_CONNECTED); neo->last_telemetry_ms=neo->last_keepalive_ms=ms; } else if (type == TYPE_TELEMETRY) { neo->last_telemetry_ms=ms; for(size_t i=0;i+13<=bn;++i) if(b[i]==0x55) { size_t l=(size_t)b[i+1]|((size_t)(b[i+2]&3)<<8); if(l>=13&&i+l<=bn) decode_osd(neo,b+i,l,ms); } } else if(type == TYPE_VIDEO && neo->cfg.on_video) { dji_neo_video_packet_t v={b,bn,ms,1}; neo->cfg.on_video(neo->cfg.callback_user,&v); } return DJI_NEO_OK; }
dji_neo_result_t dji_neo_set_command_armed(dji_neo_t *neo, int armed) { if(!neo) return DJI_NEO_EINVAL; if(!armed){revoke_command_gates(neo);return DJI_NEO_OK;} if(!neo->session_armed || neo->state!=DJI_NEO_LINK_CONNECTED || !neo->signer.sign) return DJI_NEO_ESTATE; neo->command_armed=1; return DJI_NEO_OK; }
dji_neo_result_t dji_neo_confirm_takeover(dji_neo_t *neo, int confirmed) { if(!neo) return DJI_NEO_EINVAL; if(!confirmed){neo->takeover_confirmed=neo->stick_enabled=0;return DJI_NEO_OK;} if(!neo->command_armed) return DJI_NEO_ESTATE; neo->takeover_confirmed=1; return DJI_NEO_OK; }
dji_neo_result_t dji_neo_set_stick_enabled(dji_neo_t *neo, int enabled) { if(!neo) return DJI_NEO_EINVAL; if(!enabled){neo->stick_enabled=0;return DJI_NEO_OK;} if(!neo->command_armed||!neo->takeover_confirmed) return DJI_NEO_ESTATE; neo->stick_enabled=1; return DJI_NEO_OK; }
dji_neo_result_t dji_neo_send_query(dji_neo_t *neo,const uint8_t *d,size_t n) { return command(neo,d,n,0); }
dji_neo_result_t dji_neo_send_actuation(dji_neo_t *neo,const uint8_t *d,size_t n) { if(!neo||!neo->command_armed||!neo->takeover_confirmed||!neo->stick_enabled) return DJI_NEO_ESTATE; return command(neo,d,n,1); }
