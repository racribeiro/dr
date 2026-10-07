package com.racribeiro.djievasion.neo

/**
 * App-facing layer above the intentionally mechanical JNI binding. The JNI API
 * mirrors C; this layer owns listener and coroutine adaptation when wired into
 * dji-evasive. No Zenoh or media-server dependency belongs here.
 */
interface DjiNeoClient {
    fun poll(monotonicMs: Long)
    fun onUdpDatagram(bytes: ByteArray, monotonicMs: Long)
    fun setSessionArmed(armed: Boolean)
}
