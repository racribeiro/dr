# Java / JNI binding

Three small layers, with no networking or internal threads:

- `NativeNeo`: low-level session API, mirroring `dji_neo.h` operations. Private
  native entries marshal inputs/outputs; Java owns lifetime/thread checks.
- `Commands`: capacity-checked builders and CRC/resequencing, mirroring
  `commands.h`. Builders return a length or negative C error code.
- `NeoClient`: optional pure-Java listener/structured-telemetry adapter above
  `NativeNeo`. It exposes `api()` for the unchanged low-level operations.

No genuine DJI signing algorithm is supplied. A `Signer` is a host function
implementing `ready(sessionId, bodyId)` and `sign(sessionId, bodyId, field45,
counter, rcSubheader, duml)`. Only RC bytes 4..11 are writable; modifying DUML's
Java copy has no effect. Missing/unready signing state allows unsigned passive
uplink and receive/decode, never actuation. A ready signer declining returns
`EAUTH`, with no unsigned fallback. All four C safety gates still apply.

## Desktop build and tests

```sh
make java-test
# Or:
cmake -S . -B build/java -DDJI_NEO_BUILD_JAVA=ON
cmake --build build/java --parallel
ctest --test-dir build/java --output-on-failure
```

Requires a native C toolchain and JDK 11+ to build; classes target Java 8. The
artifacts are `build/java/bindings/java/dji-neo.jar` and the platform's
`dji_neo_jni` shared library in that directory. At runtime, put the jar on the
classpath and the native directory on `java.library.path` (or install the
library where the platform loader searches). Shared C builds also require
the matching `dji_neo` shared library; static C linkage is the default.

`cmake --install build/java --prefix /your/sdk/prefix` installs the native
library under `lib` (platform dependent), the jar under `share/dji_neo`, and
Java sources under `share/dji_neo/java`.

The JVM integration test uses `-Xcheck:jni`, treats JNI warnings as failures,
and checks real captured DUML/OSD fixtures and a complete golden emitted
gimbal datagram. It tests error propagation, safety gates, unsigned activation,
ready optional signing, owned video copies, lifecycle and thread confinement.
These are offline packet tests, not on-drone authentication/acceptance tests.

## Lifecycle and host loop

Create, use, and close a peer on the same host thread. `poll()` runs about every
5 ms using nonnegative monotonic milliseconds; feed whole UDP payloads through
`onDatagram()`. Java maps C `uint64_t` time to nonnegative `long` (0..Long.MAX_VALUE).
Callbacks run synchronously on this thread, including sender/signer callbacks
inside command calls. They must not reenter **the same peer** or close it.
The Java layer rejects these cases before native entry.

```java
// hostSend returns 0 only when the complete UDP datagram was accepted.
// Choose fresh, independent session/body IDs on every connection attempt.
try (NativeNeo neo = new NativeNeo(sessionId, bodyId, false,
        new NativeNeo.Callbacks() {
            @Override public int udpSend(byte[] packet) { return hostSend(packet); }
            @Override public void onVideo(byte[] packet, long ms, boolean h265) {
                // Forward raw stream material to the host depayloader.
            }
        })) {
    int result = neo.setSessionArmed(true);
    // Host event loop: check result, neo.poll(nowMs), neo.onDatagram(packet, nowMs).
    // Do not open command/takeover/stick gates without genuine session signing
    // state and explicit operator approval. No mock signer on real hardware.
}
```

All native packet input/output arrays are copied. Video/UDP callback arrays and
`NeoClient.Telemetry` objects can be retained after return. No Java buffers are
pinned across callbacks. A callback exception propagates to the calling Java
operation; later callbacks in that operation are suppressed, not silently
swallowed. Already-completed C state updates/sends are **not rolled back**.
Handle the exception explicitly before choosing whether/how to retry.

Always close explicitly (`try`-with-resources is preferred). JNI holds callback
and signer global references until close or signer replacement; there is no
finalizer or background cleanup thread. Close is idempotent on the owner thread.
Using a closed peer or another thread throws `IllegalStateException`. C protocol
errors remain result codes, not automatically converted to exceptions. Invalid
Java arguments (null required arrays, negative time, out-of-range session IDs,
wrong keepalive size) are rejected before entering the client API. Pure builders
return `EINVAL`/`ENOSPACE` instead, preserving their C convention.

## Android source integration

Add the SDK via `add_subdirectory(...)` in the host's external native CMake
project, with these options set before that call:

```cmake
set(DJI_NEO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(DJI_NEO_BUILD_JAVA ON CACHE BOOL "" FORCE)
add_subdirectory(path/to/dji-reverse neo-sdk)
# Build/package target dji_neo_jni for each app ABI.
```

With an Android toolchain, JNI headers come from the NDK; no desktop JDK probe
or jar/test compilation runs inside CMake. Add `bindings/java/src` to the app's
Java source directories and `bindings/java/consumer-rules.pro` to its R8/ProGuard
rules. The Java source uses Java 8 interfaces; configure the app's normal Java 8
compilation/desugaring. The app must package `dji_neo_jni` for its supported ABIs.
The Android toolchain/device path has **not** been tested in this workspace.

The app owns Neo AP network binding, UDP, BLE provisioning/sideband, UI/operator
gates, genuine signer state, Zenoh, HEVC depayloading, KLV/MPEG-TS, and media-server
egress. A Kotlin lifecycle/coroutine adapter can sit above the Java API; no
coroutines or dispatcher adaptation belong in JNI. The C singleton-thread
contract is unchanged and fits a host-owned single-threaded event loop.
