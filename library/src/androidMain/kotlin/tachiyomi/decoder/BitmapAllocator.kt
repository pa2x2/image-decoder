package tachiyomi.decoder

import android.graphics.Bitmap

/** The decoders' Android pixels; native code writes premultiplied pixels, which is what these bitmaps hold by default. */
internal object BitmapAllocator : PixelAllocator<Bitmap> {
  override fun allocate(width: Int, height: Int): Bitmap? {
    return try {
      Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
    } catch (e: OutOfMemoryError) {
      null
    }
  }
}
