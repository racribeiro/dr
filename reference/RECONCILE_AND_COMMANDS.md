# Packet 3 — reconciliations + command semantics

Answers the two open points from the SDK review and adds the gimbal command
API. Derived from the delivered capture (`gimbalmove-sanitized.pcap`). Confidence
tags: **[C]** confirmed in capture, **[O]** observed/partial, **[U]** unverified.

## 1. `field45` rule — RESOLVED  **[C]**
Measured over 3,236 uplink frames of one session:

| wrapper type | n | f45 |
|---|---|---|
| 0 CONNECT | 1 | **always 0** |
| 4 KEEPALIVE | 997 | **always 0** |
| 5 COMMAND (sub/stick/liveview/gimbal) | 2,238 | rolls, **+8 per type‑5 frame** (2,216/2,237 deltas = +8) |

**Rule: `f45` is a single counter incremented by +8 on each type‑5 uplink only.
Type‑0 CONNECT and type‑4 KEEPALIVE always send `f45 = 0`.** The type‑5 counter
is unaffected by interleaved keepalives (observed: `84e8, 84f0, 84f8, 8500,
8508…` across type‑5 while keepalives between them stay 0).

➡️ The earlier change to increment `f45` for keepalives is **wrong — revert it.**
The SDK's `send_wrapped` already uses `f45=0` for CONNECT/KEEPALIVE; keep that,
and advance the +8 counter only in the type‑5 path (the scaffold already does this
in `command()`).

## 2. Keepalive (type‑4) body — PARTIALLY RESOLVED  **[O]**
- **Lengths:** 26 B in 988/997 frames; rare 27/28/29 B (9 frames total) — likely
  an extra subsystem entry. Treat 26 B as the norm.
- **One field is derivable:** body offset **[18:20] (LE) echoes the current
  type‑5 `f45`** (98% of keepalives match the most-recent type‑5 f45 ±8; exact in
  samples: last_t5_f45 `0x84f0` → ka field `0x84f0`, etc.).
- The body's other 16‑bit fields also **roll** across the session (not the fixed
  constants an earlier reading suggested — ~800 distinct bodies over 988 frames).
  The full per-field update rule is **not fully pinned**.
- **Practical guidance:** the body appears to be a liveness ping whose content the
  drone does **not strictly validate** (earlier standalone sessions streamed
  telemetry with different bodies). So: **never send zeros** (the scaffold bug),
  echo `f45` at [18:20] to match DJI Fly, and a plausible rolling/session body is
  tolerated for the rest. Do not block activation on reproducing it exactly.

## 3. Gimbal command API — encoding + semantics  **[C] encoding, [U] direction**
*(Execution remains gated by the actuation signer, which is unavailable — see §4.
This documents the frame semantics so the builder is complete.)*

**Move (pitch):** DUML `04/0c`, `cmd_type=0x00`, src `0x02` → dst `0x04`.
Payload (8 B): `00 00 00 00 <rate_i16 LE> 80 00`.
- `rate` is a signed pitch rate; magnitude observed up to **±200**
  (`0xff38 = -200`, `0x00c8 = +200`).
- DJI Fly **ramps** rate over ~5 frames at start (`-36,-88,-160,-200`), holds the
  target, then ramps back to `0` to stop. Cadence ~**40 ms (25 Hz)** while moving.
- Sign→direction (up vs down) **[U]** — not provable from this capture without a
  gimbal-pitch telemetry readback; DJI Fly's sequence went negative first, then
  positive.

**Control-session enable (prereq for moves):** `cmd_type=0x40`, dst `0x04`:
- `04/10` payload `06 07 26 08 09 27 29 2a 2b 1a 1b 28` (once), then `04/10`
  payload `0a` ×3.
- `04/12` keepalive payload `e6 01 48`, ~**1 Hz**, for the duration of control.

**Expected response:** none for `04/0c` moves — `cmd_type=0x00` is fire-and-forget
(no ACK). Effect must be observed via gimbal state in telemetry (readback field
not yet decoded) or a video/visual check. Do not treat silence as failure.

## 4. Still blocked / out of scope (unchanged)
- **Actuation signer:** the `(c2,flag)` rolling code in the type‑5 RC sub-header
  is session-keyed and not derivable from captures; recovering it = defeating the
  device's uplink auth, which is out of scope and harness-blocked. The gimbal API
  above is **encoding-complete but non-executable** until an authorized signer
  exists. Keep `has_actuation == 0` by default.
- **Fresh-session, on-drone validation:** replay can't prove a fresh SDK session
  activates streaming; that needs the owner's runnable device + authorized,
  prop-removed tests. Agreed.

## Tools
`tools/reconcile.py`, `tools/ka_corr.py` — reproduce §1 and §2 against the
sanitized capture.
