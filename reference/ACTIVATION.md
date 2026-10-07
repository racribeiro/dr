# Post-CONNECT activation specification (DJI Neo, Wi-Fi/UDP)

Purpose: make the drone **stream rich OSD telemetry and video**. Without this
sequence the Neo accepts CONNECT and ACKs subscriptions but stays in *idle*
telemetry — no `0x03/0x43` OSD, no type‑2 video. This is the fix for scaffold
flaw #1 (poll() sends only CONNECT + KEEPALIVE).

Confidence tags: **[C]** confirmed vs capture/on-device, **[O]** observed,
**[U]** unverified/needs on-device confirmation.

## Preconditions
- **[C] Be the primary client.** If DJI Fly's background service (or an RC) holds
  the link, the drone gives us *secondary* service: CONNECT accepted, `0x51`
  ACK'd, but **no OSD and no video**. DJI Fly must be force-stopped.
- **[C]** Socket bound to the Neo AP network; peer `192.168.2.1:9003`.
- **[C]** `session_id` and `body_id` should be fresh per session (randomize).

## Wire framing recap
- 8-byte wrapper: `[len|0x8000 LE][session LE][f45 LE][type][xor(bytes0..6)]`.
  Types: 0=connect, 1=telemetry, 2=video, 4=keepalive, 5=command.
- **[C] `f45` increments +8 per *every* uplink frame** (all types), and the
  type‑5 RC sub-header's f45 field must equal the wrapper f45 of the same frame.
- Type‑5 body = **12-byte RC sub-header** + DUML:
  `body_id(BE,2) | f45(LE,2 == wrapper f45) | 00 00 00 00 | ctr(1,+1/frame) | 01 60 | flag(1)`.
  For passive frames (subscription, liveview, stick-heartbeat, queries) `flag=00`
  is accepted. **Actuation** requires a non-constant rolling `flag`/`(c2,flag)`
  that is session-keyed and not derivable from captures — leave to the signer.

## Sequence

### 1. CONNECT — type 0, f45=0  **[C]**
Body (40B) = `body_id` (BE) at [0:1] + fixed tail:
```
64 00 64 00 c0 05 14 00 00 64 00 00 01 90 01 c0 05 14 00 00
64 00 14 00 64 00 c0 05 14 00 00 64 00 01 01 04 01 02
```
Retry every **1000 ms** while CONNECTING.
**Accept [C]:** type‑0 datagram from the drone, session matches, body = `01` →
state CONNECTED.

### 2. KEEPALIVE — type 4  **[C] session-dependent body**
26-byte body = three 16-bit pairs + trailer, each pair followed by `00000000`:
`AA AA 00000000 BB BB 00000000 CC DD 00000000 0000`.
**The values are SESSION-DEPENDENT, not a constant** — validated across two
captures:
- session A (reference): `707d 707d .. 707d 707d .. 707d 707d .. 0000`
- session B (gimbal cap): `b8c0 b8c0 .. a066 a066 .. d084 e084 .. 0000`
They look like per-session component/body ids (note B's third pair differs:
`d084`/`e084`). **Do NOT send zeros** (scaffold flaw #2) and **do NOT hardcode**
`707d`. The drone appears not to strictly validate the content (it's a liveness
ping), but to match DJI Fly derive the pairs from the session's ids. Cadence
~**20–40 ms** while CONNECTED ([O]; ~997 keepalives over the captured session).

### 3. SUBSCRIPTION SET — type 5, `0x51/0x01` (+ the post-CONNECT burst)  **[C]**
Replay the validated **127-frame** post-CONNECT command/subscription set
(`reference/src/neosub_frames.h`): `0x00/0x01` heartbeat, `0x51/0x01` data
subscriptions, `0x18/0x36`,`0x18/0x37`,`0x18/0x3c`, `0x03/0x20`, `0x11/0x4a`,
etc. **Re-send the whole set every ~2000 ms**, each frame **re-seq'd**
(`duml_reseq`, fresh DUML seq) so the drone doesn't dedupe. A `0x00/0x01`
heartbeat flows every **~150 ms** between bursts. The drone only switches to
rich telemetry after ~2.6 s of continuous heartbeat + subscription.

### 4. LIVEVIEW — type 5, `0x18/0x47` (src 0x02 → dst 0xe9)  **[C] bytes, [U] effect standalone**
Payload (10B): `00 08 <ctr16 LE> 1a 00 <edge> 00 00 00`
- **byte[4] = `0x1a`** (ground truth from the DJI Fly gimbal-move capture; an
  earlier `0x3f` was wrong — scaffold/engine flaw).
- `ctr` is a rolling ~ms counter; `edge = 1` for the first 2 frames of a start,
  then `0`.
- Send as a **START BURST**: ~30 frames @ ~66 ms, then STOP (video sustains on
  its own; continuous re-send re-triggers "start" and kills the stream).
- Gates BOTH type‑2 video AND `0x03/0x43` OSD.

### 5. STICK HEARTBEAT — type 5, `0x01/0x0a` (src 0x02 → dst 0xa9)  **[C]**
Centered "an RC is present" stream, **~19 Hz continuous** whenever CONNECTED
(passive/safe — neutral input, no arm, no takeoff). The Neo only streams rich
OSD *and* only honors commands while it receives this. Centered DUML payload:
```
01 0d 00  00 04 20 00 01 08  40 00 02 00 00 06 55 01 04 56 08  <ms u32 LE>  <pad u32>
```

## Loss / retry
- **[O]** No telemetry for ~**2000 ms** → LINK_LOST → back to CONNECTING.
- CONNECT retry 1 s; subscription re-burst 2 s; liveview one-shot burst per
  CONNECTED transition (re-arm the burst if video never starts).

## Decoded OSD contract (`0x03/0x43`)  **[C]**
Payload offsets (payload starts at DUML byte 11): `lon f64@0`, `lat f64@8`
(RADIANS), `height s16@16` (0.1 m), vel @18/20/22, `pitch s16@24`, `roll s16@26`,
`yaw s16@28` (/10 deg), `sats u8@36`. `gps_valid` = lon|lat != 0.

## Order summary
CONNECT → (ACCEPT) → start KEEPALIVE + STICK heartbeat + SUBSCRIPTION bursts →
LIVEVIEW burst → OSD + video begin ~10 ms–2.6 s later.

## Validation evidence
Validated against a full DJI Fly active session (`vpn-20261006-102438`, pulled
from the device). Uplink: CONNECT ×1, type‑4 keepalive ×997, type‑5 ×2238.
Downlink after the sequence: **1,980 telemetry + 6,862 type‑2 video** datagrams,
**202 OSD `0x03/0x43`** frames. Liveview `0x18/0x47` byte[4] = `0x1a` confirmed.
OSD decode cross-checked by `tests/test_realframes.c` (yaw/roll/sats sane).
