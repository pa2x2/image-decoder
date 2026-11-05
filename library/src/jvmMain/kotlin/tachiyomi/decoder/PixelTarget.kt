package tachiyomi.decoder

/**
 * Memory a desktop decode writes into, allocated by the caller through a [PixelAllocator], such as a Skia bitmap's
 * pixels. It must hold [height] rows of at least [width] 8-bit RGBA pixels, [rowBytes] apart, and stay allocated and in
 * place until the decode that asked for it returns. Pixels are written with premultiplied alpha.
 */
interface PixelTarget {
  val width: Int
  val height: Int

  /** Native address of the first row's first pixel. */
  val address: Long

  val rowBytes: Long
}
