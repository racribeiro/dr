# October 9 capture-backed link changes (SDK 0.2.0)

The user identifies `vpn-20261009-113325-696Z.pcap` as a DJI capture. There is
no matched current dji-evasive recording or timestamped action log yet. Original
captures stay private/outside Git; this report contains no device identities,
coordinates, credentials or video content.

SHA-256 of the reviewed new file:
`74359f54bb23d3418b6d92d18a10863edb76014954a53138f33d06d2ba6e8ec0`.

## Observations

- Complete DLT_RAW classic PCAP: 56,690 records, 98.673 s, no truncation.
- Background uplink-only prefix, then one CONNECT/accept pair at 8.102/8.109 s.
- 6,676 uplink type-5 DUML frames: all accepted by the SDK CRC validator.
- SDK receives 905 OSD and 36,176 video packets (51,917,614 payload bytes).
  Video spans 90.463 s; maximum packet gap is 47 ms. Receive invokes no signer
  or sender. This verifies decoding, **not** SDK-generated uplink acceptance.
- The two liveview bursts use opaque payload byte 4 = 0x2f, then 0x30,
  approximately 50 ms apart within each burst. All 55 DUMLs are byte-exact with
  the extended builder using captured timer, token, start edge and sequence.
  Video continues between the bursts; the capture does not establish that a
  stop-liveview command was issued. No automatic increment/negotiation is inferred.
- RC first-word matches the latest type-1 prefix word at offset 16 in
  6,509/6,656 active uplinks (97.8%). Earlier sessions independently show
  95–98% agreement. This supports a peer-ACK interpretation, not a fixed body ID.
- Initial type-5 f45 is the little-endian interpretation of CONNECT's two
  big-endian body-ID bytes plus 8; the sub-counter starts at 1. The first eight
  uplinks follow +8/+1 exactly. Later retransmit/flagged packets can depart from
  this normal rule; they are not evidence that CONNECT/KA consume the counter.
- Normal video wrapper sequence agrees with its body word at offset 2.
  Retransmission flags 2/4 do not obey that equality: their wrapper represents
  a range endpoint shared by multiple fragments/messages, not original sequence.
  Keepalive words 0/2 track receive progress. The latest
  arriving video sequence matches both KA words in 3,699/4,373 samples (84.6%):
  capture ordering, loss/reordering and selective ACKs explain why newest arrival
  cannot simply be acknowledged as contiguous receipt.
- KEEPALIVE is 26 bytes in 4,303/4,376 active packets, with occasional short
  extensions. Body word 18 agrees with the last captured uplink f45 in most
  samples. A type-1 datagram's bytes at/after offset 24 are not a KA template;
  copying them risks echoing DUML length/data.

## Implemented, with explicit limits

The runtime seeds type-5 sequence/counter from CONNECT and uses a bounded,
monotonic peer ACK rather than repeating body ID forever. Video and opaque
type-3 auxiliary UDP receive channels maintain cumulative/selective ACKs with a
1024-packet bounded reorder window and natural 16-bit wrap. Missing packets are
not acknowledged merely because a later packet arrived.

Receive blocks contain four LE16 words: cumulative, announced high, count, tag.
Tag 1 inserts `ceil(count/4)` two-bit status bytes directly after that block:
missing = 0, received = 3, unused padding = 3. Later blocks shift accordingly.
The no-gap KA remains 26 bytes; gaps produce variable-length bodies. Type-1 peer
ACK parsing also follows variable block lengths instead of hard-coded offset 16.
The legacy keepalive template now supplies only opaque bytes 20..25; the SDK
generates all receive blocks and watermarks.

Video body bytes 8..10 carry message ID/count/fragment index. A bounded cache
anchors each message from normal packets and, for wholly lost messages, adjacent
known message boundaries. Flags 2/4 retransmissions then mark the original +8
positions. Unanchored retransmissions can announce a missing range, not receipt.
Opaque metadata upper bits/byte 11 are not interpreted as a whole LE16 index.

Valid video/auxiliary receive keeps the data link alive even without OSD;
telemetry stale for 2 s still revokes actuation. All ACK transmission is pump
driven and independent of signing.

Limits: status 1/timeout abandonment, rare count/span discrepancies, outgoing
type-5 retries, auxiliary retransmission semantics and liveview token negotiation
are unresolved. Persistent unrecovered gaps may still require a fresh session.
The supplied older `gimbalmove-sanitized.pcap` demonstrates this limit: 3,050
video packets fall outside the recovery window and 1,024 positions remain
outstanding at EOF. Its decode tests pass; its loss recovery is **not complete**.

`dji_neo_set_liveview_profile()` and `dji_neo_build_liveview_ex()` accept the
opaque token and cadence explicitly. The legacy 0x1a/66 ms runtime defaults and
finite 30-frame burst remain; stop-liveview and token negotiation are not invented.

RC bytes 4..11 remain an optional host hook. Their meaning/keying is still
unresolved; nonzero values do not prove cryptographic signing, and zero values
on some packets do not prove an entire session is unsigned. No actuation gate
was removed. Internal signers are mocks and must never be installed on hardware.

Migration: outgoing counters differ from SDK 0.1.0, and body IDs must seed an
aligned +8 sequence space (`random_u16 & 0xf8ff`). Signers must preserve SDK-owned
ACK/f45 bytes 0..3 rather than assuming bytes 0..1 are a permanent body ID.
The app currently owns its direct-C link engine: copying only SDK DUML builders
will **not** exercise these runtime ACK/counter changes. Integrate the runtime or
port/test the equivalent state changes explicitly.

## Reproduce and next test

```sh
make test
make java-test
make capture-review CAPTURE=/path/to/vpn-20261009-113325-696Z.pcap
```

The capture reviewer is offline: it never opens sockets, reports only aggregate
data and exercises the actual SDK receive/CRC/liveview-builder paths. Its
latest-state fit counts are observations, not a byte-exact proof of the entire
uplink state machine. ACK codec comparisons reconstruct supported captured
blocks before encoding; they do not assert exact Fly/SDK scheduling equivalence.
For duplicate retransmissions, in-memory payload hashes locate captured normal
originals and independently check recovered sequence positions; hashes/content
are not printed or retained.

| Capture suffix | Video tracked | ACK blocks byte-exact | Recovered sequences checked |
| --- | ---: | ---: | ---: |
| 113325-696Z | 36,176 / 36,176 | 4,761 / 4,761 | 426 / 426 |
| 021329-125Z | 55,277 / 55,277 | 7,504 / 7,504 | 4,511 / 4,511 |
| 100609-348Z | 181,381 / 181,381 | 22,064 / 22,064 | 1,558 / 1,558 |
| 101358-236Z | 52,205 / 52,205 | 6,806 / 6,806 | 740 / 740 |
| 101744-646Z | 3,631 / 3,631 | 815 / 815 | 75 / 75 |

All five finish with zero outstanding positions and no metadata/window/lineage
errors. The 021329 file additionally has 13 unsupported ACK shapes and 251
retransmitted originals absent from the capture: those original positions are
inferred from lineage, not independently payload-matched.

Unit tests independently cover complete compact/extended wire packets, send
retry, variable peer-ACK offsets, fragment recovery, unknown anchors, malformed
metadata, bounds, sequence/message-ID wrap, and video-only liveness with stale
telemetry gate revocation. A privacy-normalized +8.295 s keepalive is checked
byte-for-byte against runtime emission. Reordering/gap cases are synthetic.

Next: integrate SDK 0.2.0 in a props-off, disarmed host session, with DJI Fly
fully stopped. Capture both directions from before CONNECT and log app/firmware
versions, profile settings and video continuity for at least 60 s. Do not open
actuation gates or use mock signing. Then compare the new ACK/counter behavior
and sustained video to this DJI baseline. No on-drone success is claimed yet.
