package tachiyomi.decoder

/** Loads the native library once per process, before any native call. */
internal expect fun loadNativeLibrary()
