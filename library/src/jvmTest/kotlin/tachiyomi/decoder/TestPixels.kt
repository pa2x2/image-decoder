package tachiyomi.decoder

import sun.misc.Unsafe

/**
 * Native memory a decode writes into, with [padding] unused bytes after every row so a decode that ignores
 * [rowBytes] lands pixels in the wrong place.
 */
class TestPixels(
  override val width: Int,
  override val height: Int,
  padding: Int = 12,
) : PixelTarget, AutoCloseable {

  override val rowBytes: Long = width * 4L + padding

  override val address: Long = unsafe.allocateMemory(rowBytes * height).also {
    unsafe.setMemory(it, rowBytes * height, 0)
  }

  /** The premultiplied RGBA bytes of pixel ([x], [y]). */
  fun rgba(x: Int, y: Int): List<Int> {
    val pixel = address + y * rowBytes + x * 4L
    return (0 until 4).map { unsafe.getByte(pixel + it).toInt() and 0xff }
  }

  override fun close() {
    unsafe.freeMemory(address)
  }

  companion object {
    val allocator = PixelAllocator { width, height -> TestPixels(width, height) }

    private val unsafe: Unsafe = Unsafe::class.java.getDeclaredField("theUnsafe")
      .apply { isAccessible = true }
      .get(null) as Unsafe
  }
}
