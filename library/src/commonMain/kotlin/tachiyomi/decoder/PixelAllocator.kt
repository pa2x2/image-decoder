package tachiyomi.decoder

/**
 * Provides the object a decode writes its pixels into, sized to the decoded
 * output: an `android.graphics.Bitmap` on Android, a [PixelTarget] on the JVM.
 * Pixels are 8-bit RGBA with premultiplied alpha.
 *
 * Called from native code. Returning null fails that decode.
 */
fun interface PixelAllocator<out T : Any> {
  fun allocate(width: Int, height: Int): T?
}
