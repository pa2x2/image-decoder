package tachiyomi.decoder

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
  val bounds: ImageRegion
    get() = native.bounds

  val isRecycled: Boolean
    get() = native.isRecycled

  /**
   * Decodes [region], divided by [sampleSize] (a power of two), into a target [allocator] provides for the output size.
   * Returns null when the region cannot be decoded or the allocator gives no target.
   */
  fun <T : PixelTarget> decode(
    region: ImageRegion = ImageRegion(0, 0, width, height),
    sampleSize: Int = 1,
    allocator: PixelAllocator<T>,
  ): T? = native.decode(region, sampleSize, allocator)

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
