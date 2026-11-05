package tachiyomi.decoder

import android.graphics.Bitmap
import android.graphics.Rect
import java.io.InputStream

class ImageDecoder private constructor(private val native: NativeImageDecoder) {

  /** Width of the whole encoded image, before any border cropping. */
  val sourceWidth: Int
    get() = native.sourceWidth

  /** Height of the whole encoded image, before any border cropping. */
  val sourceHeight: Int
    get() = native.sourceHeight

  val width: Int
    get() = native.width

  val height: Int
    get() = native.height

  /**
   * The part of the encoded image this decoder serves, in source pixels: the whole image, or what is left once borders
   * are cropped. Regions passed to [decode] are relative to its top-left corner.
   */
  val bounds: Rect
    get() = native.bounds.run { Rect(left, top, right, bottom) }

  val isRecycled: Boolean
    get() = native.isRecycled

  fun decode(
    region: Rect = Rect(0, 0, width, height),
    sampleSize: Int = 1,
  ): Bitmap? = native.decode(
    ImageRegion(region.left, region.top, region.right, region.bottom),
    sampleSize,
    BitmapAllocator,
  )

  fun recycle() {
    native.recycle()
  }

  companion object {
    fun newInstance(
      stream: InputStream,
      cropBorders: Boolean = false,
      displayProfile: ByteArray? = null,
    ): ImageDecoder? {
      return NativeImageDecoder.newInstance(stream, cropBorders, displayProfile)?.let(::ImageDecoder)
    }

    fun findType(bytes: ByteArray): ImageType? {
      return NativeImageDecoder.findType(bytes)
    }
  }
}
