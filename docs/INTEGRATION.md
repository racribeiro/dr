# dji-evasive integration

## Link ownership

Bind the app's UDP socket to the Neo AP network and send to `192.168.2.1:9003`
from an ephemeral source port. Pass a synchronous sender in `dji_neo_config_t`;
it returns zero only when the complete datagram is accepted, and nonzero only
when nothing was accepted. Packet pointers are valid only during the call.

Call `dji_neo_poll()` about every 5 ms, and feed complete received UDP payloads
to `dji_neo_on_datagram()`. Use one monotonic millisecond clock for all SDK
operations. SDK event callbacks execute within poll/receive and their data is
borrowed until callback return. All operations belong to one host thread;
callbacks must not reenter or destroy the client. Setters defer state event
notification until the next poll/receive.

BLE provisioning/AP sideband, Android network binding, Zenoh AircraftState
publication, HEVC depayloading, KLV, MPEG-TS, and media-server egress stay in the
app. Video callbacks carry wrapper-stripped stream packets, not access units.
The host should ensure DJI Fly/another controller is not holding the primary
link: a secondary client may get accepts without OSD/video.

## Command path

`commands.h` contains pure capacity-checked builders. Typed client operations
own fresh DUML sequence numbers, the type-5 wrapper, and RC subheader. CONNECT
and keepalive always use wrapper `field45=0`. Type-5 sends advance `field45` by
8 and the sub-counter by 1 only after successful output. Rejected signing or
failed sends leave SDK counters unchanged. Sending is not evidence of a drone
effect; gimbal move commands have no ACK.

Session arm defaults off. Every actuation send rechecks the four gates in
order: session arm, command arm, takeover confirmation, stick enable. Disarm,
signer replacement, readiness loss, or link loss clears downstream control
state and centers sticks. The passive centered-stick heartbeat is separately
constructed from fixed neutral channels and cannot carry host deflections.

`dji_neo_send_query()` currently accepts only the validated empty 00/01
heartbeat. Arbitrary raw DUML cannot bypass the actuation gates through that
function. `dji_neo_send_actuation()` is the gated raw command escape hatch.

After the host has explicitly opened all gates and installed a ready signer:

```c
#include <dji_neo/dji_neo.h>

/* Each call returns dji_neo_result_t; propagate errors to the host. */
dji_neo_result_t start_pitch_control(dji_neo_t *neo, uint64_t now_ms) {
    dji_neo_result_t result = dji_neo_gimbal_start(neo);
    if (result != DJI_NEO_OK) return result;
    return dji_neo_gimbal_set_rate(neo, -36, now_ms);
}
```

`gimbal_start` sends the four 04/10 enable frames, retaining progress on partial
output failure. Poll sends 04/12 control keepalive at 1 Hz and scheduled 04/0c
rate commands at 25 Hz. Refresh the desired rate within 600 ms or poll sends a
zero-rate stop. `gimbal_stop` immediately emits zero and ends control keepalive.
Rates are observed wire units (-200..200); degree/sec scaling, sign-to-direction,
and automatic ramps are not supplied. The host can ramp its requested rate.
Stick deflections (-660..660) are emitted at ~19 Hz and expire to center at
600 ms. Pitch/throttle polarity still needs live validation.

## Optional signer

`dji_neo_signer_t.ready` explicitly reports whether the supplied session/body
IDs have usable signing state. A sign function alone, or readiness returning
zero, never grants `has_actuation`. The SDK supplies session ID, body ID, f45,
sub-counter, DUML, and mutable RC subheader to `sign`. The first four subheader
bytes (body ID/f45) must remain unchanged; the signer may fill bytes 4..11.
The host owns session keys, readiness, and any signer reset/retry policy.

The actual DJI rolling-code algorithm is not implemented. Test signers use a
fixed marker solely to verify propagation and gates. They must not be used to
claim real actuation capability. Passive subscriptions, heartbeat, liveview,
and receive/decode never call `sign`, even when a declining signer is installed.

## Activation and sessions

After CONNECT acceptance, poll emits the 127-frame subscription profile in
batches of at most four per poll and repeats it every 2 s. It sends 00/01
heartbeat every 150 ms, neutral 01/0a every 52 ms, and a finite 30-frame liveview
burst every 66 ms after the first subscription batch completes. The first two
liveview frames set the start edge. `dji_neo_restart_liveview()` explicitly
requests another finite burst if video does not start. Delayed polling never
causes unbounded timer catch-up. `disable_activation` is available for framing
experiments/tests; it also disables periodic sticks.

The default liveview timing follows ACTIVATION.md. The delivered DJI Fly capture
has 25 frames at a median 50 ms, so fresh-session effectiveness remains a live
test item. Subscription payloads preserve opaque nested values from the supplied
capture profile; only the outer DUML sequence/CRC is regenerated. They are not
claimed to be a universal negotiated profile for every firmware/client.

Keepalive is emitted every 20 ms. By default it starts from repeated big-endian
body ID pairs; hosts may supply a 26-byte session seed via config or
`dji_neo_set_keepalive_body()` while disarmed. After type-5 output starts, body
bytes 18..19 echo its most recently emitted f45 in little endian. The rest of
the keepalive update rule remains approximate, as documented in
RECONCILE_AND_COMMANDS.md; this fallback has not been live-validated here.

Use fresh independent session/body IDs on each new session. No telemetry for
2 s transitions to LOST, disarms, and stops output. The host must call
`dji_neo_reset_session()` with fresh IDs, reconfigure any keepalive seed and
signer state, and explicitly arm again. CONNECT retries within one attempt
reuse its IDs at 1 s cadence. A late accept cannot resurrect a lost session.

## Verification boundary

CTest covers emitted datagrams, gating, signer rejection, send failures,
counter wrap, activation, watchdogs, and real OSD fixtures. The capture suite
re-encodes 128 gimbal moves, 4 enable frames, 24 control keepalives, 25 liveview
frames, 361 sticks, and 39 heartbeats byte-for-byte. A golden complete type-5
gimbal datagram checks wrapper length/XOR, endian fields, subheader, and DUML.
The separate UDP loopback test uses the same command checks and reports a skip
when the runtime denies sockets. No live-drone commands were transmitted.

JNI implementation is pending; it should mirror the C API mechanically, with
lifecycle/listener adaptation in pure Java/Kotlin above it.
