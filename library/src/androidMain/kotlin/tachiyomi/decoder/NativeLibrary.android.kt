package tachiyomi.decoder

internal actual fun loadNativeLibrary() {
  System.loadLibrary("imagedecoder")
}
