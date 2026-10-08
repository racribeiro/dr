# JNI exports and GetMethodID use these exact class/method names. Keep the
# callbacks' implementations too, including anonymous Java/Kotlin adapters.
-keep class io.github.racribeiro.djineo.NativeNeo { *; }
-keep class io.github.racribeiro.djineo.Commands { *; }
-keep interface io.github.racribeiro.djineo.NativeNeo$Callbacks { *; }
-keep interface io.github.racribeiro.djineo.NativeNeo$Signer { *; }
-keepclassmembers class * implements io.github.racribeiro.djineo.NativeNeo$Callbacks {
    public <methods>;
}
-keepclassmembers class * implements io.github.racribeiro.djineo.NativeNeo$Signer {
    public <methods>;
}
