package io.github.racribeiro.djineo;

import java.util.Objects;

/** Low-level C API mirror. No sockets, threads, key establishment or media muxing.
 * All operations and callbacks run on the thread that created this peer.
 * C error codes are returned unchanged; Java misuse throws an exception.
 */
public final class NativeNeo implements AutoCloseable {
    static { System.loadLibrary("dji_neo_jni"); }

    public static final int OK = 0, EINVAL = -1, ESTATE = -2, EAUTH = -3,
            EIO = -4, ENOSPACE = -5;
    public static final int LINK_IDLE = 0, LINK_CONNECTING = 1,
            LINK_CONNECTED = 2, LINK_LOST = 3;
    public static final int CAP_LINK = 1, CAP_ACTUATION = 2;

    public interface Callbacks {
        /** Zero only when the complete datagram was accepted. Arrays are owned copies. */
        int udpSend(byte[] datagram);
        default void onTelemetry(boolean gpsValid, boolean attitudeValid,
                double latitudeDeg, double longitudeDeg, double relativeAltitudeM,
                float rollDeg, float pitchDeg, float yawDeg, int satellites, long monotonicMs) {}
        /** Raw wrapper-stripped H.265 material, NOT an assembled access unit. */
        default void onVideo(byte[] packet, long monotonicMs, boolean codecH265) {}
        default void onState(int state) {}
    }

    public interface Signer {
        /** True only for genuine usable DJI signing state, never just a no-op hook. */
        boolean ready(int sessionId, int bodyId);
        /** Zero accepts; mutate rcSubheader[4..11] only. DUML is a read-only copy.
         * This is called for every type-5 uplink when ready. Signing can be retried
         * with identical counters after a UDP failure. No algorithm is provided here.
         */
        int sign(int sessionId, int bodyId, int field45, int counter,
                byte[] rcSubheader, byte[] duml);
    }

    private final Thread owner = Thread.currentThread();
    private long handle;
    private boolean busy;

    public NativeNeo(int sessionId, int bodyId, boolean disableActivation, Callbacks callbacks) {
        uint16(sessionId); uint16(bodyId);
        handle = nCreate(sessionId, bodyId, disableActivation, Objects.requireNonNull(callbacks));
        if (handle == 0) throw new OutOfMemoryError("Creating DJI Neo peer");
    }

    private long enter() {
        if (Thread.currentThread() != owner) throw new IllegalStateException("Use the owner thread");
        if (busy) throw new IllegalStateException("SDK callbacks must not reenter the peer");
        if (handle == 0) throw new IllegalStateException("Peer is closed");
        busy = true;
        return handle;
    }
    private void leave() { busy = false; }
    private static void uint16(int value) {
        if (value < 0 || value > 65535) throw new IllegalArgumentException("Expected uint16");
    }
    private static void clock(long ms) {
        if (ms < 0) throw new IllegalArgumentException("Expected nonnegative monotonic milliseconds");
    }

    @Override public void close() {
        if (Thread.currentThread() != owner) throw new IllegalStateException("Use the owner thread");
        if (busy) throw new IllegalStateException("SDK callbacks must not close the peer");
        if (handle != 0) { nDestroy(handle); handle = 0; }
    }
    public void setSigner(Signer signer) {
        long h = enter(); try { nSetSigner(h, signer); } finally { leave(); }
    }
    /** CAP_LINK and CAP_ACTUATION bit mask; readiness is rechecked by the C SDK. */
    public int getCapabilities() {
        long h = enter(); try { return nGetCapabilities(h); } finally { leave(); }
    }
    public int linkState() {
        long h = enter(); try { return nLinkState(h); } finally { leave(); }
    }
    public int resetSession(int sessionId, int bodyId) {
        uint16(sessionId); uint16(bodyId);
        long h = enter(); try { return nResetSession(h, sessionId, bodyId); } finally { leave(); }
    }
    /** Null selects the C SDK's body-ID fallback. Otherwise exactly 26 bytes. */
    public int setKeepaliveBody(byte[] body) {
        if (body != null && body.length != 26) throw new IllegalArgumentException("Expected 26 bytes");
        long h = enter(); try { return nSetKeepaliveBody(h, body); } finally { leave(); }
    }
    public int restartLiveview() {
        long h = enter(); try { return nRestartLiveview(h); } finally { leave(); }
    }
    public int setSessionArmed(boolean armed) {
        long h = enter(); try { return nSetSessionArmed(h, armed); } finally { leave(); }
    }
    public int poll(long monotonicMs) {
        clock(monotonicMs);
        long h = enter(); try { return nPoll(h, monotonicMs); } finally { leave(); }
    }
    public int onDatagram(byte[] data, long monotonicMs) {
        clock(monotonicMs); Objects.requireNonNull(data);
        long h = enter(); try { return nOnDatagram(h, data, monotonicMs); } finally { leave(); }
    }
    public int setCommandArmed(boolean armed) {
        long h = enter(); try { return nSetCommandArmed(h, armed); } finally { leave(); }
    }
    public int confirmTakeover(boolean confirmed) {
        long h = enter(); try { return nConfirmTakeover(h, confirmed); } finally { leave(); }
    }
    public int setStickEnabled(boolean enabled) {
        long h = enter(); try { return nSetStickEnabled(h, enabled); } finally { leave(); }
    }
    public int sendQuery(byte[] duml) {
        Objects.requireNonNull(duml);
        long h = enter(); try { return nSendQuery(h, duml); } finally { leave(); }
    }
    public int sendActuation(byte[] duml) {
        Objects.requireNonNull(duml);
        long h = enter(); try { return nSendActuation(h, duml); } finally { leave(); }
    }
    public int gimbalStart() {
        long h = enter(); try { return nGimbalStart(h); } finally { leave(); }
    }
    public int gimbalSetRate(int rate, long monotonicMs) {
        clock(monotonicMs);
        long h = enter(); try { return nGimbalSetRate(h, rate, monotonicMs); } finally { leave(); }
    }
    public int gimbalStop() {
        long h = enter(); try { return nGimbalStop(h); } finally { leave(); }
    }
    public int setStick(int roll, int pitch, int throttle, int yaw, long monotonicMs) {
        clock(monotonicMs);
        long h = enter(); try { return nSetStick(h, roll, pitch, throttle, yaw, monotonicMs); }
        finally { leave(); }
    }

    private static native long nCreate(int sessionId, int bodyId, boolean disableActivation, Callbacks callbacks);
    private static native void nDestroy(long handle);
    private static native void nSetSigner(long handle, Signer signer);
    private static native int nGetCapabilities(long handle);
    private static native int nLinkState(long handle);
    private static native int nResetSession(long handle, int sessionId, int bodyId);
    private static native int nSetKeepaliveBody(long handle, byte[] body);
    private static native int nRestartLiveview(long handle);
    private static native int nSetSessionArmed(long handle, boolean armed);
    private static native int nPoll(long handle, long monotonicMs);
    private static native int nOnDatagram(long handle, byte[] data, long monotonicMs);
    private static native int nSetCommandArmed(long handle, boolean armed);
    private static native int nConfirmTakeover(long handle, boolean confirmed);
    private static native int nSetStickEnabled(long handle, boolean enabled);
    private static native int nSendQuery(long handle, byte[] duml);
    private static native int nSendActuation(long handle, byte[] duml);
    private static native int nGimbalStart(long handle);
    private static native int nGimbalSetRate(long handle, int rate, long monotonicMs);
    private static native int nGimbalStop(long handle);
    private static native int nSetStick(long handle, int roll, int pitch, int throttle, int yaw, long monotonicMs);
}
