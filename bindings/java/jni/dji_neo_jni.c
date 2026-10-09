/* Mechanical C API bridge. The Java peer owns thread confinement, reentry and
 * lifetime. env is borrowed ONLY for the current Java entry; no native thread
 * exists and no AttachCurrentThread/retained JNIEnv is needed. Input arrays are
 * copied, never pinned across callbacks. All global refs are explicitly freed.
 */
#include <jni.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "dji_neo/commands.h"

#define N(name) Java_io_github_racribeiro_djineo_NativeNeo_##name
#define B(name) Java_io_github_racribeiro_djineo_Commands_##name
typedef struct {
    dji_neo_t *neo;
    JNIEnv *env;
    jobject callbacks, signer;
    jmethodID send, telemetry, video, state, sign, ready;
} peer_t;

static peer_t *peer(jlong handle) { return (peer_t *)(intptr_t)handle; }
static void exception(JNIEnv *env, const char *type, const char *message) {
    jclass cls = (*env)->FindClass(env, type);
    if (cls) { (*env)->ThrowNew(env, cls, message); (*env)->DeleteLocalRef(env, cls); }
}
static jbyteArray bytes(JNIEnv *env, const uint8_t *data, size_t size) {
    jbyteArray a = (*env)->NewByteArray(env, (jsize)size);
    if (a && size) (*env)->SetByteArrayRegion(env, a, 0, (jsize)size, (const jbyte *)data);
    return a;
}
static int failed(peer_t *p) { return (*p->env)->ExceptionCheck(p->env); }
static int udp_send(void *user, const uint8_t *data, size_t size) {
    peer_t *p = user; JNIEnv *e = p->env;
    if (failed(p)) return -1;
    jbyteArray a = bytes(e, data, size);
    if (!a) return -1;
    jint result = (*e)->CallIntMethod(e, p->callbacks, p->send, a);
    (*e)->DeleteLocalRef(e, a);
    return failed(p) ? -1 : result;
}
static void telemetry(void *user, const dji_neo_telemetry_t *t) {
    peer_t *p = user; JNIEnv *e = p->env;
    if (failed(p)) return;
    (*e)->CallVoidMethod(e, p->callbacks, p->telemetry,
        (jboolean)(t->gps_valid != 0), (jboolean)(t->attitude_valid != 0),
        (jdouble)t->latitude_deg, (jdouble)t->longitude_deg, (jdouble)t->relative_altitude_m,
        (jfloat)t->roll_deg, (jfloat)t->pitch_deg, (jfloat)t->yaw_deg,
        (jint)t->satellites, (jlong)t->monotonic_ms);
}
static void video(void *user, const dji_neo_video_packet_t *v) {
    peer_t *p = user; JNIEnv *e = p->env;
    if (failed(p)) return;
    jbyteArray a = bytes(e, v->data, v->size);
    if (!a) return;
    (*e)->CallVoidMethod(e, p->callbacks, p->video, a, (jlong)v->monotonic_ms,
                         (jboolean)(v->codec_h265 != 0));
    (*e)->DeleteLocalRef(e, a);
}
static void state(void *user, dji_neo_link_state_t value) {
    peer_t *p = user;
    if (!failed(p)) (*p->env)->CallVoidMethod(p->env, p->callbacks, p->state, (jint)value);
}
static int ready(void *user, uint16_t session, uint16_t body) {
    peer_t *p = user;
    if (failed(p)) return 0;
    jboolean result = (*p->env)->CallBooleanMethod(p->env, p->signer, p->ready,
                                                 (jint)session, (jint)body);
    return !failed(p) && result == JNI_TRUE;
}
static int sign_packet(void *user, dji_neo_sign_request_t *r) {
    peer_t *p = user; JNIEnv *e = p->env;
    if (failed(p)) return -1;
    jbyteArray header = bytes(e, r->rc_subheader, 12);
    if (!header) return -1;
    jbyteArray duml = bytes(e, r->duml, r->duml_size);
    if (!duml) { (*e)->DeleteLocalRef(e, header); return -1; }
    jint result = (*e)->CallIntMethod(e, p->signer, p->sign, (jint)r->session_id,
        (jint)r->body_id, (jint)r->field45, (jint)r->counter, header, duml);
    if (!failed(p) && result == 0)
        (*e)->GetByteArrayRegion(e, header, 0, 12, (jbyte *)r->rc_subheader);
    (*e)->DeleteLocalRef(e, header); (*e)->DeleteLocalRef(e, duml);
    return failed(p) ? -1 : result;
}

JNIEXPORT jlong JNICALL N(nCreate)(JNIEnv *e, jclass cls, jint session, jint body,
                                   jboolean disabled, jobject callbacks) {
    (void)cls;
    peer_t *p = calloc(1, sizeof *p);
    if (!p) { exception(e, "java/lang/OutOfMemoryError", "Allocating JNI peer"); return 0; }
    p->callbacks = (*e)->NewGlobalRef(e, callbacks);
    if (!p->callbacks) { free(p); return 0; }
    jclass type = (*e)->GetObjectClass(e, callbacks);
    if (!type) { (*e)->DeleteGlobalRef(e, p->callbacks); free(p); return 0; }
    p->send = (*e)->GetMethodID(e, type, "udpSend", "([B)I");
    if (!(*e)->ExceptionCheck(e)) p->telemetry = (*e)->GetMethodID(e, type, "onTelemetry", "(ZZDDDFFFIJ)V");
    if (!(*e)->ExceptionCheck(e)) p->video = (*e)->GetMethodID(e, type, "onVideo", "([BJZ)V");
    if (!(*e)->ExceptionCheck(e)) p->state = (*e)->GetMethodID(e, type, "onState", "(I)V");
    (*e)->DeleteLocalRef(e, type);
    if ((*e)->ExceptionCheck(e)) { (*e)->DeleteGlobalRef(e, p->callbacks); free(p); return 0; }
    dji_neo_config_t config = {0};
    config.session_id = (uint16_t)session; config.body_id = (uint16_t)body;
    config.disable_activation = disabled;
    config.udp_send = udp_send; config.udp_user = p;
    config.on_telemetry = telemetry; config.on_video = video; config.on_state = state;
    config.callback_user = p;
    p->neo = dji_neo_create(&config);
    if (!p->neo) {
        (*e)->DeleteGlobalRef(e, p->callbacks); free(p);
        exception(e, "java/lang/OutOfMemoryError", "Allocating C peer"); return 0;
    }
    return (jlong)(intptr_t)p;
}
JNIEXPORT void JNICALL N(nDestroy)(JNIEnv *e, jclass cls, jlong h) {
    (void)cls; peer_t *p = peer(h);
    dji_neo_destroy(p->neo);
    if (p->signer) (*e)->DeleteGlobalRef(e, p->signer);
    (*e)->DeleteGlobalRef(e, p->callbacks); free(p);
}
JNIEXPORT void JNICALL N(nSetSigner)(JNIEnv *e, jclass cls, jlong h, jobject signer) {
    (void)cls; peer_t *p = peer(h);
    jobject ref = NULL; jmethodID sign_id = NULL, ready_id = NULL;
    if (signer) {
        ref = (*e)->NewGlobalRef(e, signer);
        if (!ref) return;
        jclass type = (*e)->GetObjectClass(e, signer);
        if (!type) { (*e)->DeleteGlobalRef(e, ref); return; }
        sign_id = (*e)->GetMethodID(e, type, "sign", "(IIII[B[B)I");
        if (!(*e)->ExceptionCheck(e)) ready_id = (*e)->GetMethodID(e, type, "ready", "(II)Z");
        (*e)->DeleteLocalRef(e, type);
        if ((*e)->ExceptionCheck(e)) { (*e)->DeleteGlobalRef(e, ref); return; }
    }
    if (p->signer) (*e)->DeleteGlobalRef(e, p->signer);
    p->signer = ref; p->sign = sign_id; p->ready = ready_id;
    dji_neo_signer_t s = {.sign=sign_packet, .user=p, .ready=ready};
    dji_neo_set_signer(p->neo, ref ? &s : NULL);
}

#define ENTER() (void)cls; peer_t *p = peer(h); p->env = e
#define RETURN(call) do { jint jni_result = (call); p->env = NULL; return jni_result; } while (0)
JNIEXPORT jint JNICALL N(nGetCapabilities)(JNIEnv *e, jclass cls, jlong h) {
    ENTER(); dji_neo_capabilities_t caps;
    dji_neo_get_capabilities(p->neo, &caps);
    RETURN((caps.has_link ? 1 : 0) | (caps.has_actuation ? 2 : 0));
}
JNIEXPORT jint JNICALL N(nLinkState)(JNIEnv *e, jclass cls, jlong h) {
    ENTER(); RETURN(dji_neo_link_state(p->neo));
}
JNIEXPORT jint JNICALL N(nResetSession)(JNIEnv *e, jclass cls, jlong h, jint session, jint body) {
    ENTER(); RETURN(dji_neo_reset_session(p->neo, (uint16_t)session, (uint16_t)body));
}
JNIEXPORT jint JNICALL N(nSetKeepaliveBody)(JNIEnv *e, jclass cls, jlong h, jbyteArray body) {
    ENTER(); uint8_t seed[26];
    if (body) {
        if ((*e)->GetArrayLength(e, body) != 26) RETURN(DJI_NEO_EINVAL);
        (*e)->GetByteArrayRegion(e, body, 0, 26, (jbyte *)seed);
        if ((*e)->ExceptionCheck(e)) RETURN(DJI_NEO_EINVAL);
    }
    RETURN(dji_neo_set_keepalive_body(p->neo, body ? seed : NULL));
}
#define SIMPLE(name, function) \
JNIEXPORT jint JNICALL N(name)(JNIEnv *e, jclass cls, jlong h) { ENTER(); RETURN(function(p->neo)); }
#define GATE(name, function) \
JNIEXPORT jint JNICALL N(name)(JNIEnv *e, jclass cls, jlong h, jboolean flag) { \
    ENTER(); RETURN(function(p->neo, flag)); }
SIMPLE(nRestartLiveview, dji_neo_restart_liveview)
JNIEXPORT jint JNICALL N(nSetLiveviewProfile)(JNIEnv *e, jclass cls, jlong h,
                                            jint token, jint interval) {
    ENTER();
    if (token < 0 || token > 255 || interval < 20 || interval > 1000) RETURN(DJI_NEO_EINVAL);
    RETURN(dji_neo_set_liveview_profile(p->neo, (uint8_t)token, (uint16_t)interval));
}
SIMPLE(nGimbalStart, dji_neo_gimbal_start)
SIMPLE(nGimbalStop, dji_neo_gimbal_stop)
GATE(nSetSessionArmed, dji_neo_set_session_armed)
GATE(nSetCommandArmed, dji_neo_set_command_armed)
GATE(nConfirmTakeover, dji_neo_confirm_takeover)
GATE(nSetStickEnabled, dji_neo_set_stick_enabled)
JNIEXPORT jint JNICALL N(nPoll)(JNIEnv *e, jclass cls, jlong h, jlong ms) {
    ENTER(); RETURN(dji_neo_poll(p->neo, (uint64_t)ms));
}
JNIEXPORT jint JNICALL N(nGimbalSetRate)(JNIEnv *e, jclass cls, jlong h, jint rate, jlong ms) {
    ENTER(); RETURN(dji_neo_gimbal_set_rate(p->neo, rate, (uint64_t)ms));
}
JNIEXPORT jint JNICALL N(nSetStick)(JNIEnv *e, jclass cls, jlong h, jint roll, jint pitch,
                                   jint throttle, jint yaw, jlong ms) {
    ENTER(); RETURN(dji_neo_set_stick(p->neo, roll, pitch, throttle, yaw, (uint64_t)ms));
}
/* Copy receive input before invoking the SDK: Java callbacks may run GC and
 * may retain or modify their own arrays. Never hold a pinned Java buffer. */
static uint8_t *input(JNIEnv *e, jbyteArray a, size_t maximum, size_t *size) {
    if (!a) return NULL;
    *size = (size_t)(*e)->GetArrayLength(e, a);
    if (*size > maximum) return NULL;
    uint8_t *data = malloc(*size ? *size : 1);
    if (!data) { exception(e, "java/lang/OutOfMemoryError", "Copying packet"); return NULL; }
    if (*size) (*e)->GetByteArrayRegion(e, a, 0, (jsize)*size, (jbyte *)data);
    if ((*e)->ExceptionCheck(e)) { free(data); return NULL; }
    return data;
}
JNIEXPORT jint JNICALL N(nOnDatagram)(JNIEnv *e, jclass cls, jlong h, jbyteArray a, jlong ms) {
    ENTER(); size_t size = 0; uint8_t *data = input(e, a, 65507, &size);
    if (!data) RETURN(DJI_NEO_EINVAL);
    jint result = dji_neo_on_datagram(p->neo, data, size, (uint64_t)ms);
    free(data); RETURN(result);
}
#define SEND(name, function) \
JNIEXPORT jint JNICALL N(name)(JNIEnv *e, jclass cls, jlong h, jbyteArray a) { \
    ENTER(); size_t size = 0; uint8_t *data = input(e, a, DJI_NEO_MAX_DUML, &size); \
    if (!data) RETURN(DJI_NEO_EINVAL); \
    jint result = function(p->neo, data, size); free(data); RETURN(result); }
SEND(nSendQuery, dji_neo_send_query)
SEND(nSendActuation, dji_neo_send_actuation)

static int u8(jint v) { return v >= 0 && v <= 255; }
static int u16(jint v) { return v >= 0 && v <= 65535; }
/* Builder capacity is limited by the DUML protocol, not by Java array size.
 * Invalid inputs return the same negative codes as the C encoders. */
static size_t capacity(JNIEnv *e, jbyteArray out) {
    size_t size = (size_t)(*e)->GetArrayLength(e, out);
    return size > DJI_NEO_MAX_DUML ? DJI_NEO_MAX_DUML : size;
}
static jint output(JNIEnv *e, jbyteArray out, const uint8_t *data, int result) {
    if (result > 0) (*e)->SetByteArrayRegion(e, out, 0, result, (const jbyte *)data);
    return result;
}
JNIEXPORT jint JNICALL B(dumlBuild)(JNIEnv *e, jclass cls, jbyteArray out, jint source,
    jint destination, jint sequence, jint type, jint set, jint id, jbyteArray payload) {
    (void)cls; uint8_t frame[DJI_NEO_MAX_DUML]; size_t size = 0;
    if (!out || !u8(source) || !u8(destination) || !u16(sequence) || !u8(type) || !u8(set) || !u8(id))
        return DJI_NEO_EINVAL;
    uint8_t *data = payload ? input(e, payload, DJI_NEO_MAX_DUML - 13, &size) : NULL;
    if (payload && !data) return DJI_NEO_EINVAL;
    int result = dji_neo_duml_build(frame, capacity(e, out), (uint8_t)source, (uint8_t)destination,
        (uint16_t)sequence, (uint8_t)type, (uint8_t)set, (uint8_t)id, data, size);
    free(data); return output(e, out, frame, result);
}
JNIEXPORT jboolean JNICALL B(dumlValid)(JNIEnv *e, jclass cls, jbyteArray a) {
    (void)cls; size_t size = 0; uint8_t *data = input(e, a, DJI_NEO_MAX_DUML, &size);
    if (!data) return JNI_FALSE;
    int valid = dji_neo_duml_valid(data, size); free(data);
    return valid ? JNI_TRUE : JNI_FALSE;
}
JNIEXPORT jint JNICALL B(dumlReseq)(JNIEnv *e, jclass cls, jbyteArray a, jint sequence) {
    (void)cls; size_t size = 0;
    if (!u16(sequence)) return DJI_NEO_EINVAL;
    uint8_t *data = input(e, a, DJI_NEO_MAX_DUML, &size);
    if (!data) return DJI_NEO_EINVAL;
    int result = dji_neo_duml_reseq(data, size, (uint16_t)sequence);
    if (!result) (*e)->SetByteArrayRegion(e, a, 0, (jsize)size, (const jbyte *)data);
    free(data); return result;
}
#define BUILD_BEGIN() (void)cls; uint8_t frame[DJI_NEO_MAX_DUML]; \
    if (!out || !u16(sequence)) { return DJI_NEO_EINVAL; } \
    size_t cap = capacity(e, out)
#define BUILD_RETURN(call) return output(e, out, frame, (call))
JNIEXPORT jint JNICALL B(buildHeartbeat)(JNIEnv *e, jclass cls, jbyteArray out, jint sequence) {
    BUILD_BEGIN(); BUILD_RETURN(dji_neo_build_heartbeat(frame, cap, (uint16_t)sequence));
}
JNIEXPORT jint JNICALL B(buildLiveview)(JNIEnv *e, jclass cls, jbyteArray out, jint sequence,
                                       jint timer, jboolean edge) {
    BUILD_BEGIN(); if (!u16(timer)) return DJI_NEO_EINVAL;
    BUILD_RETURN(dji_neo_build_liveview(frame, cap, (uint16_t)sequence, (uint16_t)timer, edge));
}
JNIEXPORT jint JNICALL B(buildLiveviewEx)(JNIEnv *e, jclass cls, jbyteArray out, jint sequence,
                                         jint timer, jint token, jboolean edge) {
    BUILD_BEGIN(); if (!u16(timer) || token < 0 || token > 255) return DJI_NEO_EINVAL;
    BUILD_RETURN(dji_neo_build_liveview_ex(frame, cap, (uint16_t)sequence,
                                          (uint16_t)timer, (uint8_t)token, edge));
}
JNIEXPORT jint JNICALL B(buildStick)(JNIEnv *e, jclass cls, jbyteArray out, jint sequence,
                                    jintArray channels, jlong timer) {
    BUILD_BEGIN();
    if (!channels || (*e)->GetArrayLength(e, channels) != 4 || timer < 0 || (uint64_t)timer > UINT32_MAX)
        return DJI_NEO_EINVAL;
    jint java_channels[4]; uint16_t native_channels[4];
    (*e)->GetIntArrayRegion(e, channels, 0, 4, java_channels);
    if ((*e)->ExceptionCheck(e)) return DJI_NEO_EINVAL;
    for (unsigned i = 0; i < 4; ++i) {
        if (!u16(java_channels[i])) return DJI_NEO_EINVAL;
        native_channels[i] = (uint16_t)java_channels[i];
    }
    BUILD_RETURN(dji_neo_build_stick(frame, cap, (uint16_t)sequence, native_channels, (uint32_t)timer));
}
JNIEXPORT jint JNICALL B(buildGimbalRate)(JNIEnv *e, jclass cls, jbyteArray out, jint sequence, jint rate) {
    BUILD_BEGIN(); BUILD_RETURN(dji_neo_build_gimbal_rate(frame, cap, (uint16_t)sequence, rate));
}
JNIEXPORT jint JNICALL B(buildGimbalEnable)(JNIEnv *e, jclass cls, jbyteArray out, jint sequence, jint stage) {
    BUILD_BEGIN(); if (stage < 0) return DJI_NEO_EINVAL;
    BUILD_RETURN(dji_neo_build_gimbal_enable(frame, cap, (uint16_t)sequence, (unsigned)stage));
}
JNIEXPORT jint JNICALL B(buildGimbalKeepalive)(JNIEnv *e, jclass cls, jbyteArray out, jint sequence) {
    BUILD_BEGIN(); BUILD_RETURN(dji_neo_build_gimbal_keepalive(frame, cap, (uint16_t)sequence));
}
