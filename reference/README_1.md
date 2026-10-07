# Activation spec + real-frame fixtures (reference drop)

Reference-only artifacts to fix scaffold flaws #1/#2 and add decode test coverage.

- `ACTIVATION.md` — post-CONNECT activation sequence the SDK must own (CONNECT →
  keepalive + subscription bursts + stick heartbeat → liveview burst) so the
  drone streams OSD + video. Confidence-tagged ([C]/[O]/[U]).
- `tests/test_realframes.c` + `tests/test_fixtures.h` — validates CRC-8/16 and
  OSD `0x03/0x43` decode against ACTUAL captured frames (the scaffold test does
  not). Build against the SDK: gcc -Iinclude -Isrc src/dji_neo.c src/duml.c
  tests/test_realframes.c -lm. Wire into CTest next to test_sdk.c.

Fixtures are protocol/telemetry bytes only — no credentials. Captures with
Wi-Fi PSKs are NOT included.

## captures/
`gimbalmove-sanitized.pcap` — the DJI Fly active session used to validate
ACTIVATION.md, with ALL `0x07` credential frames (SSID/WPA2 PSK) removed
(148 packets dropped, 0 remaining). Safe to commit. Use with the validate
scripts to confirm the activation sequence, OSD and video.
