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
8 and the sub-counter by 1 only after successful output. The initial type-5
f45 is `LE(CONNECT body_id bytes) + 8`, with sub-counter 1, not zero. RC bytes
0..1 carry a LE peer ACK initialized to the same CONNECT seed, then advanced
from the type-1 prefix word at body offset 16. This replaces the old fixed
body-id model. Rejected signing or
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
bytes (SDK-owned peer ACK/f45) must remain unchanged; the signer may fill bytes 4..11.
The host owns signing state, any key establishment, readiness, and signer
reset/retry policy. The wire field's actual algorithm and any keying are not
yet recovered; do not infer working DJI signing from a nonzero test marker.

When `ready` returns 1, **every type-5 uplink** goes through `sign`: subscription
replay, liveview, centered sticks, heartbeat/query, and actuation. Signing passive
activation does not require command arm, takeover confirmation, or stick enable;
those gates still apply to all actuation. CONNECT and type-4 keepalive do not
call the signer. When `ready` is missing/returns 0, passive uplink remains
unsigned, `has_actuation` remains 0, and movement is refused.

A ready signer declining a packet, or changing its protected ACK/f45 bytes,
returns `DJI_NEO_EAUTH` without sending that packet; there is no automatic
unsigned downgrade. Receive/decode and scheduled type-4 keepalives remain
independent. Signing/send failures do not advance the SDK's type-5 counters or
activation position. The host must handle signer retry state: a signature is
computed before `udp_send`, and `EIO` may require signing the same counters
again. A successful send means socket acceptance, not drone acceptance.

The remaining opaque RC fields are not implemented as a recovered algorithm.
Their cryptographic meaning/keying has not been established by these captures.
Test signers use a
fixed marker solely to verify propagation and gates. They must not be used to
claim real actuation capability. Receive/decode never call `sign`, even when a
ready declining signer is installed. Signer-free decoding is a structural API
guarantee, not proof of SDK uplink interoperability. The app agent retracted its
earlier ACK-only observation: its fixed-RC unsigned sessions now elicit full OSD,
but video stops after a short burst. Receive ACK/state is a separate candidate
cause; sustained video cannot currently be attributed to signing alone.

## Activation and sessions

After CONNECT acceptance, poll emits the 51-frame bootstrap profile in
batches of at most four per poll and repeats it every 2 s. It sends 00/01
heartbeat every 150 ms, neutral 01/0a every 52 ms, and a finite 30-frame liveview
burst every 66 ms after the first subscription batch completes. The first two
liveview frames set the start edge. `dji_neo_restart_liveview()` explicitly
requests another finite burst if video does not start. Delayed polling never
causes unbounded timer catch-up. `disable_activation` is available for framing
experiments/tests; it also disables periodic sticks.

`dji_neo_set_liveview_profile(neo, token, interval_ms)` configures opaque payload
byte 4 and cadence without sending/restarting. It is allowed disarmed or connected.
The defaults remain 0x1a/66 ms for compatibility; the new DJI capture uses 0x2f,
then 0x30, with approximately 50 ms cadence. Configure the host's experiment
explicitly and call `dji_neo_restart_liveview()` for the second finite burst.
No stop-liveview command or automatic token negotiation/increment is inferred.
The pure builder `dji_neo_build_liveview_ex()` reproduces each observed token;
the original builder retains the 0x1a default. Java mirrors both additions.

The default liveview timing follows ACTIVATION.md. The delivered DJI Fly capture
has 25 frames at a median 50 ms, so fresh-session effectiveness remains a live
test item. Subscription payloads preserve opaque nested values from the supplied
capture profile; only the outer DUML sequence/CRC is regenerated. They are not
claimed to be a universal negotiated profile for every firmware/client.

The source is the app agent's exact 55-frame `neosub_frames.h` artifact,
versioned as `reference/activation/app-profile-55.txt`. Preserve the supplied
order (00/01 first), but remove 18/47 because liveview has a separate finite
schedule, and remove credential GETs 07/07, 07/0c, 07/0e. Neither cmd_set 01 nor
04 is allowed in the profile: centered sticks and gated gimbal/flight control
are separate code paths. The remaining 51 frames use fresh outer DUML sequence
and CRC values per send. Capture-specific nested payloads are left untouched.

This chooses the exact provided app artifact, not a new finer-key extraction.
Its upstream rule used only `(cmd_set,cmd_id)` over a file-relative 0..9.6 s
window, including a pre-CONNECT prefix. It can collapse distinct receivers,
reply kinds and nested subscription topics. The independently supplied PCAP is
not asserted to regenerate these exact bytes. Additional/finer profiles and
fresh-session negotiation need separate capture-backed verification; counts
alone are not activation evidence. See `reference/activation/README.md`.

Keepalive is emitted every 20 ms. Its no-gap body is 26 bytes, initialized from
CONNECT's BE body-ID bytes interpreted as LE sequence seeds. It has two generated
receive ACK blocks, a compact command block, and a two-byte trailer:

| Block | Value |
| --- | --- |
| Video | Cumulative received sequence, announced high, span count, tag, statuses |
| Auxiliary | Same structure for opaque UDP type-3 |
| Command | Peer uplink ACK, last successfully emitted type-5 f45, opaque fields |

Each receive block begins with four LE16 words: cumulative, high, count, tag.
With no gap, count/tag are zero. Tag 1 inserts `ceil(count/4)` bytes immediately
after the block: each two-bit entry is 0 for missing/request-retransmission or
3 for received; unused padding entries are 3. Later blocks therefore move;
neither telemetry ACK nor command watermark should be read at fixed offsets.

Normal video wrapper f45 agrees with body word 2. Retransmission flags 2/4 use
a range endpoint, **not** each fragment's original sequence, and the body word
can differ. Video metadata at body bytes 8..10 identifies message/count/index:
`id=b[8]`, `count=b[9]&127`, `index=(b[10]&31)*2+(b[9]>>7)`.
Opaque upper bits and byte 11 are ignored. A bounded message cache recovers
original fragment sequences from normal packets or adjacent message anchors;
unanchored retransmissions request a range but never fabricate receipt.
This implementation accepts 1..64 fragments; captures exercised counts up to 14.
Type-3 tracking currently accepts the observed normal, matching wrapper/body
sequence form only; auxiliary retransmission semantics are not established.

A bounded 1024-packet reorder window advances cumulative ACK only through
received contiguous +8 sequences. Duplicates, stale/un-aligned/future peer ACKs
and packets outside the window do not advance state. Persistent unrecovered
gaps may require host session reset. Fly's rare status 1/abandonment and
count/span discrepancies are not generated; timeout abandonment and outgoing
type-5 retransmission are not implemented.
Type-3 payload interpretation remains opaque and has no media/telemetry callback.
It is a UDP protocol channel, not the separate host-owned BLE sideband.

The legacy 26-byte experimental template is accepted while disarmed, but only
bytes 20..25 supply the compact command block's opaque fields and trailer.
Bytes 0..19 are ignored: SDK owns receive descriptors and watermarks. Never
copy a whole type-1 datagram: it can contain DUML length/data, not ACK state.
ACK updates require the active matching session; unarmed receive still
decodes telemetry/video without modifying a future session's link state.

Use fresh independent session/body IDs on each new session. Body-ID high-byte
low three bits must be zero (`random_u16 & 0xf8ff`) to align the LE sequence
seed; create/reset reject other values. No valid telemetry/video/auxiliary receive
for 2 s transitions to LOST, disarms, and stops output. Video-only reception can
keep the data link alive, but telemetry stale for 2 s still revokes actuation.
The host must call
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

The activation-profile suite validates all 55 original DUML lengths/CRCs and
compares all 51 retained emitted frames against their original bytes after
only outer resequencing/CRC changes. Both unsigned and ready-signed replay,
periodic rebroadcast, separate liveview, neutral sticks, mid-batch send-failure
retry, and closed movement gates are covered. If Python is available, CTest
also checks the generated table against its source fixture; normal SDK builds
do not need Python.

The signing suite verifies all mutable subheader bytes on emitted activation
packets, signed queries, no gate bypass, signer rejection, corrupted identity,
send failure/counter retry, unsigned degradation, readiness-loss revocation,
and OSD/video delivery with a ready declining signer.

The optional JNI implementation mirrors the C API mechanically. `NativeNeo`
returns C result codes unchanged, with owner-thread/reentry/lifecycle checks in
Java. `Commands` mirrors the pure builders. `NeoClient` is a separate pure-Java
adapter for structured telemetry/listeners. JNI uses the calling thread's
`JNIEnv` only during an API entry; it creates no threads and does not attach to
the JVM. Packet callbacks receive owned copies. See
[Java/JNI integration](../bindings/java/README.md).

The desktop `java_jni` test runs under `-Xcheck:jni` and verifies the captured
gimbal builder and complete golden command datagram through Java, real OSD
decoding, signer isolation/readiness/rejection, four gates, activation, callback
exception propagation, array validation, reentry, wrong-thread/use-after-close
rejection and repeated signer replacement/cleanup. The test signer is still a
mock. Android cross-compilation/device use and genuine on-drone effects remain
unverified.
