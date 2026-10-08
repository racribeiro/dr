package io.github.racribeiro.djineo;

/** Mechanical mirror of commands.h. Builders return length or a negative C
 * error code; they do not grant permission to transmit. Output arrays are
 * caller-owned and their length is the C capacity argument.
 */
public final class Commands {
    static { System.loadLibrary("dji_neo_jni"); }
    private Commands() {}
    public static native int dumlBuild(byte[] out, int source, int destination,
            int sequence, int commandType, int commandSet, int commandId, byte[] payload);
    public static native boolean dumlValid(byte[] frame);
    public static native int dumlReseq(byte[] frame, int sequence);
    public static native int buildHeartbeat(byte[] out, int sequence);
    public static native int buildLiveview(byte[] out, int sequence, int timerMs, boolean startEdge);
    public static native int buildStick(byte[] out, int sequence, int[] channels, long timerMs);
    public static native int buildGimbalRate(byte[] out, int sequence, int rate);
    public static native int buildGimbalEnable(byte[] out, int sequence, int stage);
    public static native int buildGimbalKeepalive(byte[] out, int sequence);
}
