# DJI Neo bootstrap profile

`app-profile-55.txt` contains the exact 55 DUML frames (1,761 bytes) supplied by
the `dji-evasive` app agent on 2026-10-08, copied from its `app/src/neosub_frames.h`.
These are protocol fixture bytes, not executable signer code. They contain no
UDP wrapper or 12-byte RC subheader. All source DUML lengths and CRCs are valid.

## Runtime profile

The SDK retains 51 frames in the supplied order, with 00/01 first. It omits:

- Original index 54, 18/47: liveview is generated separately as a finite burst.
- Original indices 38, 40, 41, respectively 07/0e, 07/0c, 07/07: credential queries.

All of command sets 01 (stick/flight control) and 04 (gimbal control) are forbidden
in bootstrap. The input fixture already excludes those sets. The SDK generates
centered sticks independently; non-neutral sticks and gimbal commands require
all four movement gates and a ready signer. Every type-5 packet may use the
ready optional signer, including bootstrap. Receive/decode never uses signing.

Outer DUML headers/sequences/CRCs are generated per send. Everything inside the
payload, including nested DUML, GUIDs, opaque nonce/session values and nested
sequences, remains unchanged. This is an app-provided capture-specific bootstrap,
not proof of universal firmware compatibility or negotiated fresh-session state.

## Provenance and limits

The app reported extracting its original 55 kinds with `(cmd_set,cmd_id)`-only
deduplication, first occurrence in order, excluding sets 01/04, with 00/01 moved
to the front. Its reported window is the first captured Neo packet through
9.6 seconds, not 9.6 seconds after CONNECT. That rule can collapse distinct
destinations/reply kinds and many nested subscription topics.

The SDK uses the exact supplied artifact so the app and SDK can agree on bytes.
It does not claim that rerunning the rule on the separately bundled sanitized
PCAP reproduces it. The source capture includes a pre-CONNECT prefix and
sanitation removes credential frames; it is also a separate fixture with some
different payload values. A finer-key profile or additional subscription-topic
coverage needs another explicitly versioned artifact and acceptance tests.

Packet tests verify encoding/replay, not that a fresh Neo honors these commands.
The actual DJI rolling-code signer is unavailable, and no on-drone acceptance
is asserted. `docs/INTEGRATION.md` documents timers and the receive boundary.

## Reproduce and test

From the repository root:

```sh
make activation-check
python3 reference/tools/generate_activation.py
make test
```

The generator validates labels, ordering, lengths, CRCs, exclusions and payload
capacity before printing `src/activation_frames.inc` content. `--check` compares
without writing. There is no Python dependency in the built SDK or normal build.
The C fixture test independently validates all 55 originals, checks emitted
51-frame batches byte-for-byte after outer resequencing, and covers signed and
unsigned output, rebroadcast and a mid-batch send failure without skipped frames.
