# DJI Neo SDK — reference extracts

**These files are REFERENCE ONLY — not for use, compilation, or vendoring.**
They are validated protocol/decoding/contract source extracted from the working
`dji-evasive` application. Use them as ground truth when designing the SDK; do
not copy them in wholesale. They do not form a buildable tree (host glue such as
logging, config, sockets and the Android layer were deliberately left out).

Captures (`dumps/*.pcap`) and `REVERSE_ENGINEERING.md` are intentionally **not**
included: the captures carry Wi-Fi credentials, and the notes doc was not secret-
scanned. Keep those in the `dji-evasive` repo.

## Files → proposed SDK layers

| Proposed layer | Reference files | What to learn from them |
|---|---|---|
| `dji_neo_protocol` — framing/CRC | `reference/src/duml.{c,h}` | DUML frame build/parse, CRC-8 (header) + CRC-16 (body), `duml_reseq` |
| `dji_neo_protocol` — link wrapper | `reference/src/neoconn.{c,h}` | 8-byte neoconn UDP wrapper: `[len\|0x8000 LE][session][f45][type][xor]`, types 0=connect 1=telemetry 2=video 4=keepalive 5=cmd |
| `dji_neo_protocol` — uplink framing | `reference/src/neocmd.{c,h}` | 12-byte type-5 RC sub-header (`body_id`, `f45==wrapper f45`, ctr, `0160`, flag). **The flag/rolling-code is the open auth problem — treat as a pluggable, optional signer.** |
| `dji_neo_client` — control frames | `reference/src/neostick.{c,h}`, `reference/src/neosub_frames.h` | centered-stick `01/0a` builder (the "RC present" heartbeat); the captured CONNECT/subscription/liveview frame set |
| `dji_neo_client` — telemetry decode | `reference/src/neotlm.{c,h}`, `reference/src/render.h` | OSD `0x03/0x43` decode (lon/lat radians, alt, vel, pitch/roll/yaw, sats); `flightdata_t` = the **telemetry data contract** |
| engine / public surface | `reference/src/neolink.h` | the current engine interface (arm gates, status, callbacks) — a starting point for the public C API shape |
| data-out — telemetry ICD | `reference/src/zpub_msg.{c,h}` | GCS `SystemMessageWrapper`/`AircraftState` encoder (radians, 17-digit `time_enc`) = the **Zenoh wire contract** |
| data-out — video/KLV | `reference/src/neomux.h`, `reference/src/klv.{c,h}` | MPEG-TS/UDP egress interface + ST 0601 KLV encoder = the **media-server data-channel contract** |

## Review notes carried over (see the SDK review)
1. Harvest from these, don't greenfield.
2. The uplink **auth/rolling-code** is the crux: isolate it as an optional signer; the data plane (telemetry + video) must work without it.
3. BLE (provisioning/sideband) and Wi-Fi/UDP (the link) are **not** one transport; there is no USB control path for the Neo.
4. The SDK should **emit** decoded telemetry + codec-ready video; it should **not** own Zenoh / KLV / TS-mux (host concerns — shown here only as the contract).
5. Prefer a host-driven **pump/poll** model over internal-thread callbacks (single-threaded constraints + JNI).
